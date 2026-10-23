/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The built plugin, through its CLAP entry, as a host loads it (the formats share one wrapper, DPF's, and one
 * fel_host: the CLAP is the one a test can drive without a DAW): the bundle opens, one plugin, an instrument;
 * activated at 48 kHz it reports the resampler's latency; MIDI notes sound, and silence without them; its parameters
 * (MASTER, DISTRIBUTION with the chooser's names); its state names the distribution, and a DISTRIBUTION set by the
 * host is loaded (the state then names it), a state loaded brings its distribution back; two instances at once, each
 * its own.
 *   plugin_clap_test BUNDLE.clap */
#include "clap/entry.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/latency.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/host.h"
#include "clap/plugin-factory.h"
#include "clap/plugin.h"
#include "clap/process.h"
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int bad;
static void ck(const char *what, int ok)
{
    printf("plugin clap: %-104s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static const void *h_ext(const clap_host_t *h, const char *id) { (void)h; (void)id; return 0; }
static void h_nop(const clap_host_t *h) { (void)h; }
static const clap_host_t HOST = {CLAP_VERSION_INIT, 0, "blue-hare test", "Blue Hare", "", "1", h_ext, h_nop, h_nop, h_nop};

/* events: a list the process reads */
typedef struct { uint32_t n; union { clap_event_header_t h; clap_event_midi_t m; clap_event_param_value_t p; } e[8]; } evlist_t;
static uint32_t ev_size(const clap_input_events_t *l) { return ((const evlist_t *)l->ctx)->n; }
static const clap_event_header_t *ev_get(const clap_input_events_t *l, uint32_t i) { return &((const evlist_t *)l->ctx)->e[i].h; }
static bool out_push(const clap_output_events_t *l, const clap_event_header_t *e) { (void)l; (void)e; return true; }

static void midi(evlist_t *l, uint32_t frame, uint8_t s, uint8_t d1, uint8_t d2)
{
    clap_event_midi_t *m = &l->e[l->n++].m;
    memset(m, 0, sizeof *m);
    m->header.size = sizeof *m;
    m->header.time = frame;
    m->header.type = CLAP_EVENT_MIDI;
    m->data[0] = s;
    m->data[1] = d1;
    m->data[2] = d2;
}
static void param(evlist_t *l, uint32_t frame, clap_id id, double v)
{
    clap_event_param_value_t *p = &l->e[l->n++].p;
    memset(p, 0, sizeof *p);
    p->header.size = sizeof *p;
    p->header.time = frame;
    p->header.type = CLAP_EVENT_PARAM_VALUE;
    p->param_id = id;
    p->note_id = -1;
    p->port_index = -1;
    p->channel = -1;
    p->key = -1;
    p->value = v;
}

/* blocks of 256 frames at 48 kHz, the plugin's 9 stereo buses: the main's energy over the n blocks (the events in
 * the first), track 1's own pair's into *t1 */
static double run2(const clap_plugin_t *p, uint32_t blocks, evlist_t *first, double *t1)
{
    static float bufs[18][256];
    float *ch[18];
    double e = 0.0, et = 0.0;
    uint32_t b, i, k;
    evlist_t none = {0};
    clap_audio_buffer_t out[9];
    clap_input_events_t in = {0, ev_size, ev_get};
    clap_output_events_t oe = {0, out_push};
    clap_process_t pr = {0};
    memset(out, 0, sizeof out);
    for (k = 0; k < 18u; k++)
        ch[k] = bufs[k];
    for (k = 0; k < 9u; k++) {
        out[k].data32 = ch + 2u * k;
        out[k].channel_count = 2;
    }
    pr.frames_count = 256;
    pr.audio_outputs = out;
    pr.audio_outputs_count = 9;
    pr.in_events = &in;
    pr.out_events = &oe;
    for (b = 0; b < blocks; b++) {
        in.ctx = b == 0 && first ? first : &none;
        pr.steady_time = (int64_t)b * 256;
        p->process(p, &pr);
        for (i = 0; i < 256; i++) {
            e += (double)bufs[0][i] * bufs[0][i] + (double)bufs[1][i] * bufs[1][i];
            et += (double)bufs[2][i] * bufs[2][i] + (double)bufs[3][i] * bufs[3][i];
        }
    }
    if (t1)
        *t1 = et;
    return e;
}
static double run(const clap_plugin_t *p, uint32_t blocks, evlist_t *first) { return run2(p, blocks, first, 0); }

typedef struct { char buf[4096]; size_t n, at; } blob_t;
static int64_t os_write(const clap_ostream_t *s, const void *b, uint64_t n)
{
    blob_t *x = s->ctx;
    if (x->n + n > sizeof x->buf)
        return -1;
    memcpy(x->buf + x->n, b, n);
    x->n += n;
    return (int64_t)n;
}
static int64_t is_read(const clap_istream_t *s, void *b, uint64_t n)
{
    blob_t *x = s->ctx;
    if (n > x->n - x->at)
        n = x->n - x->at;
    memcpy(b, x->buf + x->at, n);
    x->at += n;
    return (int64_t)n;
}
static blob_t save(const clap_plugin_t *p)
{
    const clap_plugin_state_t *st = p->get_extension(p, CLAP_EXT_STATE);
    blob_t b;
    clap_ostream_t os = {&b, os_write};
    memset(&b, 0, sizeof b);
    if (st)
        st->save(p, &os);
    return b;
}
static int has(const blob_t *b, const char *s)
{
    size_t k, n = strlen(s);
    for (k = 0; k + n <= b->n; k++)
        if (!memcmp(b->buf + k, s, n))
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
    char bin[1200], what[256];
    void *lib;
    const clap_plugin_entry_t *entry;
    const clap_plugin_factory_t *f;
    const clap_plugin_descriptor_t *d;
    const clap_plugin_t *p, *q;
    const clap_plugin_latency_t *lat;
    const clap_plugin_params_t *par;
    double quiet, loud;
    setvbuf(stdout, 0, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: plugin_clap_test BUNDLE.clap\n");
        return 2;
    }
#ifdef __APPLE__
    snprintf(bin, sizeof bin, "%s/Contents/MacOS/Blue Hare", argv[1]);
#else
    snprintf(bin, sizeof bin, "%s/BlueHare.clap", argv[1]);
#endif
    lib = dlopen(bin, RTLD_NOW | RTLD_LOCAL);
    entry = lib ? dlsym(lib, "clap_entry") : 0;
    ck("the bundle opens, clap_entry found, init with the bundle's path", entry && entry->init(argv[1]));
    if (!entry)
        return 1;
    f = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    d = f && f->get_plugin_count(f) == 1u ? f->get_plugin_descriptor(f, 0) : 0;
    snprintf(what, sizeof what, "one plugin: \"%s\" (%s), an instrument", d ? d->name : "?", d ? d->id : "?");
    ck(what, d && !strcmp(d->name, "Blue Hare") && d->features && d->features[0] && !strcmp(d->features[0], "instrument"));
    if (!d)
        return 1;
    p = f->create_plugin(f, &HOST, d->id);
    ck("created, initialised, activated at 48 kHz", p && p->init(p) && p->activate(p, 48000.0, 1, 4096) &&
                                                      p->start_processing(p));
    if (!p)
        return 1;
    lat = p->get_extension(p, CLAP_EXT_LATENCY);
    snprintf(what, sizeof what, "the latency reported: %u frames (the resampler's taps at 48 kHz)", lat ? lat->get(p) : 0u);
    ck(what, lat && lat->get(p) == 52u);

    {
        const clap_plugin_audio_ports_t *ap = p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
        clap_audio_port_info_t a0, a8;
        int ok = ap && ap->count(p, false) == 9u && ap->get(p, 0, false, &a0) && ap->get(p, 8, false, &a8);
        snprintf(what, sizeof what, "outputs: %u stereo buses, \"%s\" .. \"%s\"", ap ? ap->count(p, false) : 0u, ok ? a0.name : "?",
                 ok ? a8.name : "?");
        ck(what, ok && a0.channel_count == 2u && !strcmp(a0.name, "Main") && !strcmp(a8.name, "Track 8"));
    }
    par = p->get_extension(p, CLAP_EXT_PARAMS);
    {
        clap_param_info_t info[2];
        char text[64] = "";
        int ok = par && par->count(p) == 10u && par->get_info(p, 0, &info[0]) && par->get_info(p, 1, &info[1]);
        if (ok)
            ok = par->value_to_text(p, info[1].id, 2.0, text, sizeof text);
        snprintf(what, sizeof what, "parameters: \"%s\" 0 .. %.0f, \"%s\" 0 .. %.0f (2 = \"%s\")", ok ? info[0].name : "?",
                 ok ? info[0].max_value : 0.0, ok ? info[1].name : "?", ok ? info[1].max_value : 0.0, text);
        ck(what, ok && !strcmp(info[0].name, "Master") && !strcmp(info[1].name, "Distribution") &&
                     info[1].max_value >= 3.0 && !strcmp(text, "FOUNDRY"));
    }

    run(p, 200, 0);                                  /* (the boot: ~0.5 s) */
    quiet = run(p, 100, 0);
    {
        evlist_t on = {0};
        midi(&on, 17, 0x90, 60, 110);
        midi(&on, 18, 0x91, 43, 110);
        loud = run(p, 100, &on);
    }
    snprintf(what, sizeof what, "silent without notes (%.1e), two MIDI notes sound (%.1f)", quiet, loud);
    ck(what, quiet < 1e-6 && loud > 1.0);
    {
        evlist_t e = {0}, on = {0}, off = {0};
        double t1, m1;
        midi(&off, 0, 0x80, 60, 0);
        midi(&off, 1, 0x81, 43, 0);
        run(p, 400, &off);
        param(&e, 0, 2, 1.0);                        /* TRACK 1 OUTPUT: OWN */
        run(p, 1, &e);
        midi(&on, 0, 0x90, 60, 110);
        m1 = run2(p, 100, &on, &t1);
        snprintf(what, sizeof what, "TRACK 1 OUTPUT set to OWN: a note on channel 1 on the Track 1 bus (%.1f), the main has only its FX (%.1f)", t1, m1);
        ck(what, t1 > 1.0 && m1 < 0.5 * t1);
        e.n = 0;
        param(&e, 0, 2, 0.0);
        off.n = 0;
        midi(&off, 0, 0x80, 60, 0);
        run(p, 400, &off);
        run(p, 1, &e);
    }

    {
        blob_t b = save(p), c;
        evlist_t e = {0}, off = {0}, on = {0};
        double e_idm, e_foundry;
        midi(&off, 0, 0x80, 60, 0);
        midi(&off, 1, 0x81, 43, 0);
        run(p, 400, &off);                           /* (the notes above released, their tails gone) */
        midi(&on, 0, 0x90, 48, 110);
        e_idm = run(p, 100, &on);
        param(&e, 0, 1, 2.0);                        /* DISTRIBUTION: FOUNDRY */
        run(p, 1, &e);
        c = save(p);
        ck("state: \"idm_native\" at first, \"foundry\" once the host sets DISTRIBUTION",
           has(&b, "idm_native") && has(&c, "foundry"));
        usleep(500000);                              /* (the plugin's own thread loads it: 20 ms a look) */
        run(p, 200, 0);
        e_foundry = run(p, 100, &on);
        snprintf(what, sizeof what, "the same note on FOUNDRY (%.1f), not as on IDM NATIVE (%.1f): FOUNDRY loaded", e_foundry, e_idm);
        ck(what, e_foundry > 1.0 && fabs(e_foundry - e_idm) > 0.01 * e_idm);
        {
            const clap_plugin_state_t *st = p->get_extension(p, CLAP_EXT_STATE);
            clap_istream_t is = {&b, is_read};
            double again;
            b.at = 0;
            ck("the first state loaded: IDM NATIVE again", st && st->load(p, &is) && (c = save(p), has(&c, "idm_native")));
            run(p, 200, 0);
            again = run(p, 100, &on);
            snprintf(what, sizeof what, "... loaded at once (the host's thread): the note as on IDM NATIVE (%.1f)", again);
            ck(what, fabs(again - e_idm) < 0.01 * e_idm);
            {
                evlist_t o = {0};
                midi(&o, 0, 0x80, 48, 0);
                run(p, 400, &o);                     /* (released, its tail gone) */
            }
        }
    }

    q = f->create_plugin(f, &HOST, d->id);
    if (q && q->init(q) && q->activate(q, 48000.0, 1, 4096) && q->start_processing(q)) {
        evlist_t on = {0};
        double ep, eq;
        run(q, 200, 0);
        run(p, 200, 0);
        midi(&on, 0, 0x90, 60, 110);
        eq = run(q, 100, &on);                       /* a note on the second only */
        ep = run(p, 100, 0);
        snprintf(what, sizeof what, "two instances: a note on the second sounds there (%.1f), not in the first (%.1e)", eq, ep);
        ck(what, eq > 1.0 && ep < 1e-6);
        q->stop_processing(q);
        q->deactivate(q);
        q->destroy(q);
    } else
        ck("a second instance", 0);

    p->stop_processing(p);
    p->deactivate(p);
    p->destroy(p);
    entry->deinit();
    printf(bad ? "plugin clap test FAILED (%d)\n" : "plugin clap test passed\n", bad);
    return bad != 0;
}
