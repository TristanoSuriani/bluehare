/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The plugin (DPF): fel_host.h behind the plugin formats.
 *   Parameters: MASTER (the device's pot); DISTRIBUTION (the chooser; not automatable); TRACK 1 .. 8 OUTPUT, MAIN or
 *   OWN (its own pair: out of the main mix, dry; not automatable).
 *   Outputs: the main pair, then a pair a track, each a bus ("Main", "Track 1" ..).
 *   The host's transport, always: its tempo, play and stop, its place (the quarter notes since its start).
 *   State: "dist" the distribution's stem, "song" the device's flash (compressed, base64): the song, the presets, the
 *   settings, as the device last saved them (it saves as it does on the FM-1: SAVE, and its autosave).
 * The cores live in the bundle's resources (Contents/Resources/cores; FEL_CORES_DIR overrides). A distribution chosen
 * by its parameter is loaded by a thread of the plugin's own, never the audio thread (a host may set a parameter from
 * it); a state, at once (the host's thread). */
#include "DistrhoPlugin.hpp"
#include "DistrhoPluginUtils.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "fel_host.h"
}

START_NAMESPACE_DISTRHO

/* base64 (DPF's String::asBase64 writes a byte past its buffer) */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string to_base64(const uint8_t* d, size_t n)
{
    std::string s;
    s.reserve((n + 2u) / 3u * 4u);
    for (size_t i = 0; i < n; i += 3u) {
        const uint32_t v = (uint32_t)d[i] << 16 | (i + 1u < n ? (uint32_t)d[i + 1u] << 8 : 0u) | (i + 2u < n ? d[i + 2u] : 0u);
        s += B64[v >> 18 & 63u];
        s += B64[v >> 12 & 63u];
        s += i + 1u < n ? B64[v >> 6 & 63u] : '=';
        s += i + 2u < n ? B64[v & 63u] : '=';
    }
    return s;
}
static std::vector<uint8_t> from_base64(const char* s)
{
    std::vector<uint8_t> out;
    uint32_t v = 0, bits = 0;
    for (; *s; s++) {
        const char* p = std::strchr(B64, *s);
        if (*s == '=' || !p)
            continue;                                    /* (padding, line breaks) */
        v = v << 6 | (uint32_t)(p - B64);
        bits += 6u;
        if (bits >= 8u) {
            bits -= 8u;
            out.push_back((uint8_t)(v >> bits));
        }
    }
    return out;
}

enum { P_MASTER, P_DIST, P_OUT1, P_COUNT = P_OUT1 + 8 };
enum { S_DIST, S_SONG, S_COUNT };

class FelTrackerPlugin : public Plugin
{
public:
    FelTrackerPlugin()
        : Plugin(P_COUNT, 0, S_COUNT)
    {
        char err[512] = "";
        std::string dir = coresDir();
        host = fel_host_new(dir.c_str(), getSampleRate(), err, sizeof err);
        if (!host)
            d_stderr2("Fel-Tracker: %s", err);
        else
            setLatency(fel_host_latency(host));
        vst3 = !std::strcmp(getPluginFormatName(), "VST3");
        worker = std::thread([this] { work(); });
    }

    ~FelTrackerPlugin() override
    {
        quit = true;
        worker.join();
        fel_host_free(host);
    }

protected:
    const char* getLabel() const override { return "FelTracker"; }
    const char* getDescription() const override
    {
        return "Fel-Tracker: the FM-1 tracker's distributions as an instrument";
    }
    const char* getMaker() const override { return "Tristano Suriani"; }
    const char* getHomePage() const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense() const override { return "GPL-3.0-only"; }
    uint32_t getVersion() const override { return d_version(0, 10, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        if (input)
            return Plugin::initAudioPort(input, index, port);
        port.groupId = index / 2u;
        if (index < 2u) {
            port.name = index ? "Main R" : "Main L";
            port.symbol = index ? "main_r" : "main_l";
        } else {
            char n[32];
            std::snprintf(n, sizeof n, "Track %u %s", index / 2u, index & 1u ? "R" : "L");
            port.name = n;
            std::snprintf(n, sizeof n, "track%u_%s", index / 2u, index & 1u ? "r" : "l");
            port.symbol = n;
        }
    }

    void initPortGroup(uint32_t id, PortGroup& g) override
    {
        char n[32];
        if (id == 0u) {
            g.name = "Main";
            g.symbol = "main";
            return;
        }
        std::snprintf(n, sizeof n, "Track %u", id);
        g.name = n;
        std::snprintf(n, sizeof n, "track%u", id);
        g.symbol = n;
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        if (index == P_MASTER) {
            p.hints = kParameterIsAutomatable | kParameterIsInteger;
            p.name = "Master";
            p.symbol = "master";
            p.ranges.min = 0.0f;
            p.ranges.max = 1023.0f;
            p.ranges.def = 700.0f;
        } else if (index == P_DIST) {
            const uint32_t n = host ? fel_host_dists(host) : 1u;
            p.hints = kParameterIsInteger;
            p.name = "Distribution";
            p.symbol = "distribution";
            p.ranges.min = 0.0f;
            p.ranges.max = (float)(n - 1u);
            p.ranges.def = 0.0f;
            if (host) {
                ParameterEnumerationValue* v = new ParameterEnumerationValue[n];
                for (uint32_t i = 0; i < n; i++) {
                    v[i].value = (float)i;
                    v[i].label = fel_host_dist_tag(host, i);
                }
                p.enumValues.count = (uint8_t)n;
                p.enumValues.restrictedMode = true;
                p.enumValues.values = v;
            }
        } else if (index >= P_OUT1 && index < P_OUT1 + 8u) {
            char n[32];
            ParameterEnumerationValue* v = new ParameterEnumerationValue[2];
            std::snprintf(n, sizeof n, "Track %u Output", index - P_OUT1 + 1u);
            p.name = n;
            std::snprintf(n, sizeof n, "track%u_output", index - P_OUT1 + 1u);
            p.symbol = n;
            p.hints = kParameterIsInteger | kParameterIsBoolean;
            p.ranges.min = 0.0f;
            p.ranges.max = 1.0f;
            p.ranges.def = 0.0f;
            v[0].value = 0.0f;
            v[0].label = "Main";
            v[1].value = 1.0f;
            v[1].label = "Own";
            p.enumValues.count = 2;
            p.enumValues.restrictedMode = true;
            p.enumValues.values = v;
        }
    }

    void initState(uint32_t index, State& s) override
    {
        if (index == S_DIST) {
            s.key = "dist";
            s.defaultValue = host ? fel_host_dist_stem(host, 0) : "";
            s.hints = kStateIsOnlyForDSP;
        } else if (index == S_SONG) {
            s.key = "song";
            s.defaultValue = "";
            s.hints = kStateIsOnlyForDSP | kStateIsBase64Blob;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index == P_MASTER)
            return (float)master;
        if (index == P_DIST)
            return (float)wanted.load();
        if (index >= P_OUT1 && index < P_OUT1 + 8u)
            return (float)((outs.load() >> (index - P_OUT1)) & 1u);
        return 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index == P_MASTER) {
            master = (uint32_t)(value < 0.0f ? 0.0f : value);
            if (host)
                fel_host_master(host, master);
        } else if (index == P_DIST && host) {
            const int i = (int)(value + 0.5f);
            if (i >= 0 && (uint32_t)i < fel_host_dists(host))
                wanted = (uint32_t)i;
        } else if (index >= P_OUT1 && index < P_OUT1 + 8u) {
            const uint32_t bit = 1u << (index - P_OUT1);
            const uint32_t m = value >= 0.5f ? (outs.load() | bit) : (outs.load() & ~bit);
            outs = m;
            if (host)
                fel_host_outputs(host, m);
        }
    }

    String getState(const char* key) const override
    {
        if (!host)
            return String();
        if (!std::strcmp(key, "dist"))
            return String(fel_host_dist_stem(host, wanted.load()));
        if (!std::strcmp(key, "song")) {
            uint8_t* blob = nullptr;
            const size_t n = fel_host_save(host, &blob);
            const std::string b = n ? to_base64(blob, n) : std::string();
            std::free(blob);
            return String(b.c_str());
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (!host)
            return;
        if (!std::strcmp(key, "dist")) {
            for (uint32_t i = 0; i < fel_host_dists(host); i++)
                if (!std::strcmp(value, fel_host_dist_stem(host, i)))
                    wanted = i;                          /* (the song, next, loads it; none: the thread) */
        } else if (!std::strcmp(key, "song") && value[0]) {
            const std::vector<uint8_t> blob = from_base64(value);
            char err[512] = "";
            std::lock_guard<std::mutex> g(applying);
            if (fel_host_load(host, wanted.load(), blob.data(), blob.size(), err, sizeof err))
                d_stderr2("Fel-Tracker: %s", err);
        }
    }

    void sampleRateChanged(double rate) override
    {
        if (host && !fel_host_set_rate(host, rate))
            setLatency(fel_host_latency(host));
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midi, uint32_t nmidi) override
    {
        fel_event_t ev[512];
        fel_transport_t tr;
        const TimePosition& tp = getTimePosition();
        uint32_t n = 0;
        if (!host) {
            for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; c++)
                std::memset(outputs[c], 0, sizeof(float) * frames);
            return;
        }
        for (uint32_t i = 0; i < nmidi && n < 512u; i++) {
            const uint8_t* d = midi[i].size > MidiEvent::kDataSize ? midi[i].dataExt : midi[i].data;
            if (midi[i].size < 1u || midi[i].size > 3u || d[0] < 0x80u || d[0] >= 0xF0u)
                continue;
            ev[n].frame = midi[i].frame;
            ev[n].data[0] = d[0];
            ev[n].data[1] = midi[i].size > 1u ? d[1] : 0u;
            ev[n].data[2] = midi[i].size > 2u ? d[2] : 0u;
            n++;
        }
        tr.valid = 1;
        tr.playing = tp.playing;
        tr.bpm = tp.bbt.beatsPerMinute > 0.0 ? tp.bbt.beatsPerMinute : 120.0;
        if (tp.bbt.valid && tp.bbt.ticksPerBeat > 0.0) {         /* (VST3's beats are the signature's: in quarters) */
            const double q = vst3 && tp.bbt.beatType > 0.0f ? 4.0 / tp.bbt.beatType : 1.0;
            tr.beats = ((tp.bbt.bar - 1) * (double)tp.bbt.beatsPerBar + (tp.bbt.beat - 1) +
                        tp.bbt.tick / tp.bbt.ticksPerBeat) * q;
        } else {
            tr.beats = (double)tp.frame / getSampleRate() * tr.bpm / 60.0;
        }
        fel_host_run(host, outputs, DISTRHO_PLUGIN_NUM_OUTPUTS, frames, ev, n, &tr);
    }

private:
    fel_host_t* host = nullptr;
    uint32_t master = 700u;
    bool vst3 = false;
    std::atomic<uint32_t> wanted{0};
    std::atomic<uint32_t> outs{0};
    std::atomic<bool> quit{false};
    std::thread worker;
    std::mutex applying;

    std::string coresDir() const
    {
        const char* e = std::getenv("FEL_CORES_DIR");
        if (e && *e)
            return e;
        const char* b = getBundlePath();
        const char* r = b ? getResourcePath(b) : nullptr;
        if (r)
            return std::string(r) + "/cores";
        std::string f = getBinaryFilename();                 /* (no bundle: next to the binary) */
        return f.substr(0, f.rfind('/')) + "/cores";
    }

    void work()
    {
        while (!quit) {
            if (host && wanted.load() != fel_host_dist(host)) {
                char err[512] = "";
                std::lock_guard<std::mutex> g(applying);
                const uint32_t w = wanted.load();
                if (w != fel_host_dist(host) && fel_host_set_dist(host, w, err, sizeof err))
                    d_stderr2("Fel-Tracker: %s", err);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FelTrackerPlugin)
};

Plugin* createPlugin() { return new FelTrackerPlugin(); }

END_NAMESPACE_DISTRHO
