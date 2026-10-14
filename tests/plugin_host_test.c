/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The plugin's own part (plugin/src: fel_resample.c, fel_host.c), with no plugin format around it:
 *   the resampler: 44.1 kHz to 48, 88.2, 96 and 32 kHz; a tone keeps its level (pass band) and comes out clean (no
 *   image, no alias above -70 dB), DC passes whole, an impulse lands at the latency it reports, and the output is the
 *   same whatever the block sizes;
 *   the host: the distributions found in the chooser's order; at 44.1 kHz the core's own output, sample for sample;
 *   at 48 kHz the same whatever the host's blocks, a note played at its frame (two notes 1000 frames apart sound 1000
 *   frames apart); a distribution swapped while the audio thread runs (no stop, no crash, the new one plays).
 *   plugin_host_test CORE_DIR */
#include "../plugin/core/fel_loader.h"
#include "../plugin/src/fel_host.h"
#include "../plugin/src/fel_resample.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#define EXT "dylib"
#else
#define EXT "so"
#endif

static int bad;
static void ck(const char *what, int ok)
{
    printf("plugin host: %-104s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ------------------------------------------------------------ resampler --- */
typedef struct { double f, rate; uint64_t n; int impulse, dc; } src_t;
static void src_pull(void *ctx, float *l, float *r, uint32_t n)
{
    src_t *s = ctx;
    uint32_t i;
    for (i = 0; i < n; i++, s->n++) {
        float v = s->dc ? 0.5f : s->impulse ? (s->n == 1000u ? 1.0f : 0.0f)
                                            : (float)(0.5 * sin(2.0 * M_PI * s->f * (double)s->n / s->rate));
        l[i] = v;
        r[i] = -v;
    }
}

/* the level of frequency f in x (a Hann-windowed single bin), relative to a full-scale 0.5 sine */
static double level(const float *x, uint32_t n, double f, double rate)
{
    double re = 0.0, im = 0.0, w = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double h = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1u));
        re += h * x[i] * cos(2.0 * M_PI * f * i / rate);
        im += h * x[i] * sin(2.0 * M_PI * f * i / rate);
        w += h;
    }
    return sqrt(re * re + im * im) / w / 0.25;
}

/* out rate, a tone: n output frames, blocks of blk (0: one block) */
static float *resample(double out, double f, uint32_t n, uint32_t blk, int impulse, int dc, fel_rs_t *keep)
{
    fel_rs_t rs;
    src_t s = {f, 44100.0, 0, impulse, dc};
    float *l = calloc(n, sizeof *l), *r = calloc(n, sizeof *r);
    uint32_t at = 0, k = 0;
    static const uint32_t ODD[] = {1, 7, 64, 333, 2, 1024, 31, 5000};
    if (fel_rs_init(&rs, 44100.0, out))
        return 0;
    while (at < n) {
        uint32_t m = blk ? blk : n - at;
        if (blk == 1u)
            m = ODD[k++ % 8u];
        if (m > n - at)
            m = n - at;
        fel_rs_process(&rs, l + at, r + at, m, src_pull, &s);
        at += m;
    }
    for (at = 0; at < n; at++)
        if (r[at] != -l[at])
            l[0] = 1e9f;                             /* (both channels alike) */
    free(r);
    if (keep)
        *keep = rs;
    else
        fel_rs_free(&rs);
    return l;
}

static void test_resampler(void)
{
    static const double RATES[] = {48000.0, 88200.0, 96000.0, 32000.0};
    uint32_t k;
    for (k = 0; k < 4; k++) {
        double out = RATES[k], top = out < 44100.0 ? out : 44100.0, worst_pass = 0.0, worst_spur = -200.0;
        const double tones[] = {100.0, 1000.0, 5000.0, 12000.0, 0.43 * top};
        uint32_t t, n = (uint32_t)out, i;
        char what[200];
        fel_rs_t rs;
        float *x;
        for (t = 0; t < 5; t++) {
            double f = tones[t], g, spur = 0.0;
            x = resample(out, f, n, 0, 0, 0, 0);
            g = level(x + 4096, n - 4096, f, out);
            if (fabs(20.0 * log10(g)) > worst_pass)
                worst_pass = fabs(20.0 * log10(g));
            /* the images and aliases: at the mirror of f about the two Nyquists, and a few others */
            {
                uint32_t m, sgn;
                for (m = 1; m <= 3u; m++)               /* m * 44.1 kHz -+ f, folded into the output's band */
                    for (sgn = 0; sgn < 2u; sgn++) {
                        double c = fmod(m * 44100.0 + (sgn ? f : -f), out), v;
                        if (c > out / 2.0)
                            c = out - c;
                        if (c < 20.0 || fabs(c - f) < 50.0)
                            continue;
                        v = 20.0 * log10(level(x + 4096, n - 4096, c, out) + 1e-12);
                        if (v > spur || spur == 0.0)
                            spur = v;
                    }
                if (spur != 0.0 && spur > worst_spur)
                    worst_spur = spur;
            }
            free(x);
        }
        snprintf(what, sizeof what, "resampler 44.1 -> %.1f kHz: 100 Hz .. %.1f kHz within %.2f dB, images and aliases %.0f dB",
                 out / 1000.0, 0.43 * top / 1000.0, worst_pass, worst_spur);
        ck(what, worst_pass < 0.1 && worst_spur < -70.0);

        x = resample(out, 0, 8192, 0, 0, 1, 0);
        {
            double e = 0.0;
            for (i = 4096; i < 8192; i++)
                e = fmax(e, fabs(x[i] - 0.5));
            snprintf(what, sizeof what, "resampler 44.1 -> %.1f kHz: DC whole (%.1e)", out / 1000.0, e);
            ck(what, e < 1e-4);
        }
        free(x);

        x = resample(out, 0, 8192, 0, 1, 0, &rs);
        {
            uint32_t peak = 0, want = (uint32_t)lround(1000.0 * out / 44100.0);
            for (i = 0; i < 8192; i++)
                if (fabsf(x[i]) > fabsf(x[peak]))
                    peak = i;
            snprintf(what, sizeof what, "resampler 44.1 -> %.1f kHz: an impulse where it was, not delayed (frame %u, %u)",
                     out / 1000.0, peak, want);
            ck(what, peak + 1u >= want && peak <= want + 1u);
            fel_rs_free(&rs);
        }
        free(x);

        {
            float *a = resample(out, 3000.0, 20000, 0, 0, 0, 0), *b = resample(out, 3000.0, 20000, 1, 0, 0, 0);
            snprintf(what, sizeof what, "resampler 44.1 -> %.1f kHz: blocks of 1 .. 5000 frames, sample for sample one block",
                     out / 1000.0);
            ck(what, a && b && !memcmp(a, b, 20000 * sizeof *a));
            free(a);
            free(b);
        }
    }
}

/* ----------------------------------------------------------------- host --- */
static const char *dir;

static float *host_run(fel_host_t *h, uint32_t n, uint32_t blk, const fel_event_t *ev, uint32_t nev)
{
    float *l = calloc(n, sizeof *l), *r = calloc(n, sizeof *r);
    uint32_t at = 0, e = 0, k = 0;
    static const uint32_t ODD[] = {64, 1, 333, 512, 17, 1024, 100};
    while (at < n) {
        fel_event_t mine[16];
        uint32_t m = blk ? blk : ODD[k++ % 7u], c = 0;
        if (m > n - at)
            m = n - at;
        for (; e < nev && ev[e].frame < at + m; e++) {
            mine[c] = ev[e];
            mine[c++].frame -= at;
        }
        fel_host_run(h, l + at, r + at, m, mine, c);
        at += m;
    }
    free(r);
    return l;
}

static uint32_t onset(const float *x, uint32_t from, uint32_t n)   /* the first frame past -40 dB */
{
    uint32_t i;
    for (i = from; i < n; i++)
        if (fabsf(x[i]) > 0.01f)
            return i;
    return n;
}

typedef struct { fel_host_t *h; volatile int stop; volatile uint32_t blocks; volatile double energy_after; } runner_t;
static void *runner(void *p)
{
    runner_t *r = p;
    float l[256], rr[256];
    fel_event_t on = {0, {0x90, 60, 110}};
    while (!r->stop) {
        uint32_t i;
        fel_host_run(r->h, l, rr, 256, (r->blocks % 200u) == 0u ? &on : 0, (r->blocks % 200u) == 0u);
        for (i = 0; i < 256; i++)
            r->energy_after += (double)l[i] * l[i];
        r->blocks++;
    }
    return 0;
}

static void test_host(void)
{
    char err[512] = "", what[200];
    fel_host_t *h = fel_host_new(dir, 44100.0, err, sizeof err);
    uint32_t i, n;
    if (!h) {
        printf("plugin host: %s\n", err);
        ck("the host opens", 0);
        return;
    }
    n = fel_host_dists(h);
    {
        char list[200] = "";
        for (i = 0; i < n; i++)
            snprintf(list + strlen(list), sizeof list - strlen(list), "%s%s", i ? ", " : "", fel_host_dist_tag(h, i));
        snprintf(what, sizeof what, "the chooser's distributions, IDM NATIVE first: %s", list);
        ck(what, n >= 4u && !strcmp(fel_host_dist_stem(h, 0), "idm_native") && fel_host_dist(h) == 0u);
    }
    {   /* 44.1 kHz: the core as it is */
        char p[1100];
        fel_instance_t *in;
        const fel_core_api_t *api;
        fel_event_t ev[2] = {{30000, {0x90, 64, 100}}, {50000, {0x80, 64, 0}}};
        float *a = host_run(h, 70000, 0, ev, 2), *b = calloc(70000, sizeof *b), *br = calloc(70000, sizeof *br);
        snprintf(p, sizeof p, "%s/fel-core-idm_native." EXT, dir);
        in = fel_load(p, err, sizeof err);
        api = fel_api(in);
        api->nor_erase();
        api->boot();
        api->master(700);
        api->render(b, br, 30000);
        api->midi(0x90, 64, 100);
        api->render(b + 30000, br + 30000, 20000);
        api->midi(0x80, 64, 0);
        api->render(b + 50000, br + 50000, 20000);
        ck("44.1 kHz: no resampler, latency 0, the core's own output sample for sample (a note at frame 30000)",
           fel_host_latency(h) == 0u && !memcmp(a, b, 70000 * sizeof *a) && onset(a, 0, 70000) < 70000u);
        fel_unload(in);
        free(a);
        free(b);
        free(br);
    }
    fel_host_free(h);

    h = fel_host_new(dir, 48000.0, err, sizeof err);
    {   /* 48 kHz: the block sizes change nothing; a note lands at its frame */
        fel_event_t ev[2] = {{40000, {0x90, 64, 100}}, {60000, {0x80, 64, 0}}};
        float *a = host_run(h, 80000, 0, ev, 2), *b;
        fel_host_free(h);
        h = fel_host_new(dir, 48000.0, err, sizeof err);
        b = host_run(h, 80000, 64, ev, 2);
        snprintf(what, sizeof what, "48 kHz: latency %u frames; blocks of 1 .. 1024 frames give what blocks of 64 give, sample for sample",
                 fel_host_latency(h));
        ck(what, fel_host_latency(h) == 52u && !memcmp(a, b, 80000 * sizeof *a));
        free(a);
        free(b);
    }
    {
        fel_event_t e1[1] = {{40000, {0x90, 64, 100}}}, e2[1] = {{41000, {0x90, 64, 100}}};
        float *a, *b;
        uint32_t oa, ob;
        fel_host_free(h);
        h = fel_host_new(dir, 48000.0, err, sizeof err);
        a = host_run(h, 60000, 0, e1, 1);
        fel_host_free(h);
        h = fel_host_new(dir, 48000.0, err, sizeof err);
        b = host_run(h, 60000, 0, e2, 1);
        oa = onset(a, 30000, 60000);
        ob = onset(b, 30000, 60000);
        snprintf(what, sizeof what, "48 kHz: a note at frame 40000 sounds at %u, one at 41000 at %u (the device's half buffer: within 140)",
                 oa, ob);
        ck(what, oa >= 40000u && oa < 40000u + 400u && ob > oa && (int)(ob - oa) > 1000 - 140 && (int)(ob - oa) < 1000 + 140);
        free(a);
        free(b);
    }
    {   /* a distribution swapped while the audio runs */
        runner_t r = {h, 0, 0, 0.0};
        pthread_t t;
        uint32_t k, ok = 1, before;
        pthread_create(&t, 0, runner, &r);
        for (k = 1; k < n && ok; k++) {
            before = r.blocks;
            ok = fel_host_set_dist(h, k, err, sizeof err) == 0 && fel_host_dist(h) == k;
            while (r.blocks < before + 400u)
                ;
        }
        r.energy_after = 0.0;
        before = r.blocks;
        while (r.blocks < before + 800u)
            ;
        r.stop = 1;
        pthread_join(t, 0);
        snprintf(what, sizeof what, "every distribution swapped in while the audio thread runs (%u blocks), the last one plays (%s)",
                 r.blocks, fel_host_dist_tag(h, fel_host_dist(h)));
        ck(what, ok && r.energy_after > 0.0);
    }
    fel_host_free(h);
    h = fel_host_new("/nonexistent", 48000.0, err, sizeof err);
    ck("no cores: no host, a reason", !h && strstr(err, "no core"));
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: plugin_host_test CORE_DIR\n");
        return 2;
    }
    dir = argv[1];
    test_resampler();
    test_host();
    printf(bad ? "plugin host test FAILED (%d)\n" : "plugin host test passed\n", bad);
    return bad != 0;
}
