/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The plugin (DPF): fel_host.h behind the plugin formats. Parameters: MASTER (the device's pot) and DISTRIBUTION
 * (the chooser; not automatable). State: "dist", the distribution's stem. The cores live in the bundle's resources
 * (Contents/Resources/cores; FEL_CORES_DIR overrides). A distribution chosen is loaded by a thread of the plugin's
 * own, never the audio thread (a host may set a parameter from it). */
#include "DistrhoPlugin.hpp"
#include "DistrhoPluginUtils.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

extern "C" {
#include "fel_host.h"
}

START_NAMESPACE_DISTRHO

enum { P_MASTER, P_DIST, P_COUNT };
enum { S_DIST, S_COUNT };

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
        wanted = 0;
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
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
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
        }
    }

    void initState(uint32_t index, State& s) override
    {
        if (index == S_DIST) {
            s.key = "dist";
            s.defaultValue = host ? fel_host_dist_stem(host, 0) : "";
            s.hints = kStateIsOnlyForDSP;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index == P_MASTER)
            return (float)master;
        if (index == P_DIST)
            return (float)wanted.load();
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
        }
    }

    String getState(const char* key) const override
    {
        if (host && !std::strcmp(key, "dist"))
            return String(fel_host_dist_stem(host, wanted.load()));
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (!host || std::strcmp(key, "dist"))
            return;
        for (uint32_t i = 0; i < fel_host_dists(host); i++)
            if (!std::strcmp(value, fel_host_dist_stem(host, i))) {
                wanted = i;
                apply();                          /* (the host's main thread: at once, so the state reads back) */
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
        uint32_t n = 0;
        if (!host) {
            std::memset(outputs[0], 0, sizeof(float) * frames);
            std::memset(outputs[1], 0, sizeof(float) * frames);
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
        fel_host_run(host, outputs[0], outputs[1], frames, ev, n);
    }

private:
    fel_host_t* host = nullptr;
    uint32_t master = 700u;
    std::atomic<uint32_t> wanted{0};
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

    void apply()
    {
        std::lock_guard<std::mutex> g(applying);
        char err[512] = "";
        const uint32_t w = wanted.load();
        if (w != fel_host_dist(host) && fel_host_set_dist(host, w, err, sizeof err))
            d_stderr2("Fel-Tracker: %s", err);
    }

    void work()
    {
        while (!quit) {
            if (host && wanted.load() != fel_host_dist(host))
                apply();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FelTrackerPlugin)
};

Plugin* createPlugin() { return new FelTrackerPlugin(); }

END_NAMESPACE_DISTRHO
