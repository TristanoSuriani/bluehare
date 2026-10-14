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
static void src_pull(void *ctx, float *const *in, uint32_t nch, uint32_t n)
{
    src_t *s = ctx;
    float *l = in[0], *r = in[1];
    uint32_t i;
    (void)nch;
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
        float *o[2] = {l + at, r + at};
        fel_rs_process(&rs, o, 2, m, src_pull, &s);
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
        float *o[2] = {l + at, r + at};
        fel_host_run(h, o, 2, m, mine, c, 0);
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
        float *o[2] = {l, rr};
        fel_host_run(r->h, o, 2, 256, (r->blocks % 200u) == 0u ? &on : 0, (r->blocks % 200u) == 0u, 0);
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

/* ------------------------------------------------- the host's transport --- */
/* a song of a closed hat on every row (core test_ticks), track 1 to its own pair (dry: no reverb between the hits);
 * the host's blocks of 256, its transport: playing from beat `from` at host frame `start`, at bpm (a jump at
 * frame `jump` to beat `to`: a loop's), stopped at `stop`. The hits' host frames into hits[] (the first sample past
 * -40 dB: onsets) */
/* the hits in x: the first sample past 0.01 after `gap` samples under 0.005 (but its attack's last 64) */
static uint32_t onsets(const float *x, uint32_t n, uint32_t gap, uint32_t *hits, uint32_t maxh)
{
    uint32_t i, j, nh = 0, last = 0;
    for (i = gap; i < n && nh < maxh; i++) {
        float a = fabsf(x[i]), m = 0.0f;
        if (a < 0.01f || (nh && i - last < gap))
            continue;
        for (j = i - gap; j + 64u < i; j++)
            m = fabsf(x[j]) > m ? fabsf(x[j]) : m;
        if (m < 0.005f) {
            hits[nh++] = i;
            last = i;
        }
    }
    return nh;
}

typedef struct { double rate, bpm, from, to; uint32_t start, jump, stop, frames; } play_t;
static uint32_t play(const play_t *pl, uint32_t *hits, uint32_t maxh, fel_host_t **keep)
{
    char err[256];
    fel_host_t *h = fel_host_new(dir, pl->rate, err, sizeof err);
    const fel_core_api_t *api;
    float *buf[FEL_HOST_OUTS], *dry = calloc(pl->frames, sizeof(float));
    uint32_t at, c, nh = 0;
    if (!h)
        return 0;
    api = fel_host_core(h);
    api->test_ticks();
    fel_host_outputs(h, 1u);
    for (c = 0; c < FEL_HOST_OUTS; c++)
        buf[c] = calloc(256, sizeof(float));
    for (at = 0; at < pl->frames; at += 256u) {
        fel_transport_t tr = {1, at >= pl->start && at < pl->stop, pl->bpm, 0.0};
        uint32_t i;
        double from = pl->jump && at >= pl->jump ? pl->to : pl->from, t0 = pl->jump && at >= pl->jump ? pl->jump : pl->start;
        tr.beats = at >= pl->start ? from + (at - t0) / pl->rate * pl->bpm / 60.0 : 0.0;
        fel_host_run(h, buf, FEL_HOST_OUTS, 256, 0, 0, &tr);
        for (i = 0; i < 256u && at + i < pl->frames; i++)
            dry[at + i] = buf[2][i];
    }
    nh = onsets(dry, pl->frames, (uint32_t)(0.01 * pl->rate), hits, maxh);
    for (c = 0; c < FEL_HOST_OUTS; c++)
        free(buf[c]);
    free(dry);
    if (keep)
        *keep = h;
    else
        fel_host_free(h);
    return nh;
}

/* the hits against the grid: 1/16 notes from host frame `first` (+ the latency), the worst error */
static int32_t grid_err(const uint32_t *hits, uint32_t nh, double first, double step, uint32_t *bad_at)
{
    int32_t worst = 0;
    uint32_t i;
    for (i = 0; i < nh; i++) {
        double k = (hits[i] - first) / step;
        int32_t e = (int32_t)(hits[i] - (first + (double)(int64_t)(k + 0.5) * step));
        if ((e < 0 ? -e : e) > (worst < 0 ? -worst : worst)) {
            worst = e;
            *bad_at = hits[i];
        }
    }
    return worst;
}

/* a row plays in the device's block of 32 its start falls in, at the block's start: 0 .. 31 early; the host's start
 * and its jumps reach the device a block or two late (its half buffer ahead: up to 128 frames), the first row then
 * plays at once */
#define EARLY (-34)
#define LATE 4
#define START_LATE 300
static void test_sync(void)
{
    static uint32_t hits[2048];
    char what[240];
    uint32_t nh, bad = 0;
    int32_t e;
    {   /* 44.1 kHz, 120 BPM from beat 0 */
        play_t p = {44100.0, 120.0, 0.0, 0.0, 44100u, 0u, 400000u, 441000u};
        nh = play(&p, hits, 2048, 0);
        e = nh > 1u ? grid_err(hits + 1, nh - 1u, 44100.0, 5512.5, &bad) : 999;
        snprintf(what, sizeof what, "the host's transport, 44.1 kHz 120 BPM: %u hats, the first %u frames after its start, the rest on its 1/16 grid (%+d), none after its stop",
                 nh, nh ? hits[0] - 44100u : 0u, e);
        ck(what, nh >= 64u && hits[0] >= 44100u && hits[0] < 44100u + START_LATE && e >= EARLY && e <= LATE &&
                     hits[nh - 1] < 400000u + 2000u && hits[1] > 44100u + 5000u);
    }
    {   /* 48 kHz, 128 BPM (a beat of 22500 host frames, 20671.875 of the core's): 60 s, no drift; the latency */
        play_t p = {48000.0, 128.0, 0.0, 0.0, 48000u, 0u, 0xFFFFFFFFu, 48000u * 61u};
        nh = play(&p, hits, 2048, 0);
        e = nh > 1u ? grid_err(hits + 1, nh - 1u, 48000.0 + 52.0, 22500.0 / 4.0, &bad) : 999;
        snprintf(what, sizeof what, "48 kHz 128 BPM for 60 s: %u hats on the grid the latency (52) later (worst %+d frames, at %u: no drift)",
                 nh, e, bad);
        ck(what, nh >= 500u && e >= EARLY * 48 / 44 - 3 && e <= LATE + 2 && hits[nh - 1] > 48000u * 59u);
    }
    {   /* started mid-row at beat 2.1; at beat 4.4 the host loops back to beat 0 */
        play_t p = {44100.0, 120.0, 2.1, 0.0, 44100u, 44100u + 50715u, 0xFFFFFFFFu, 300000u};   /* (50715: 2.3 beats) */
        uint32_t i, after;
        int32_t e2 = 999;
        nh = play(&p, hits, 2048, 0);
        e = nh ? (int32_t)hits[0] - (int32_t)(44100.0 + 0.15 * 22050.0) : 999;   /* (beat 2.25: 3307.5 frames on) */
        for (after = 0; after < nh && hits[after] < 44100u + 50715u; after++)
            ;
        if (after + 1u < nh)
            e2 = grid_err(hits + after + 1u, nh - after - 1u, 44100.0 + 50715.0, 5512.5, &bad);
        snprintf(what, sizeof what, "started at beat 2.1: the first hat on beat 2.25 (%+d); looped back to beat 0: a hat at once (%u late), the grid from there (%+d)",
                 e, after < nh ? hits[after] - (44100u + 50715u) : 0u, e2);
        ck(what, e >= EARLY && e <= LATE && after < nh && hits[after] - (44100u + 50715u) < START_LATE && e2 >= EARLY && e2 <= LATE);
        for (i = 0; i < after; i++)
            if (hits[i] < 44100u)
                e = 999;
    }
}

/* ------------------------------------------------------- track outputs --- */
static double energy(const float *x, uint32_t n)
{
    double e = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++)
        e += (double)x[i] * x[i];
    return e;
}

static void test_outputs(void)
{
    char err[256], what[200];
    double em[2], et[2], dsum = 0.0, esum = 0.0;
    uint32_t mode, at, c, i;
    static float main0[88200 * 2 + 256], main1[88200 * 2 + 256], trk1[88200 * 2 + 256];
    for (mode = 0; mode < 2u; mode++) {          /* track 1 on the main, then on its own pair */
        fel_host_t *h = fel_host_new(dir, 44100.0, err, sizeof err);
        const fel_core_api_t *api = fel_host_core(h);
        float *buf[FEL_HOST_OUTS];
        for (c = 0; c < FEL_HOST_OUTS; c++)
            buf[c] = calloc(256, sizeof(float));
        api->test_ticks();
        fel_host_outputs(h, mode);
        em[mode] = et[mode] = 0.0;
        for (at = 0; at < 88200u * 2u; at += 256u) {
            fel_transport_t tr = {1, at >= 44100u, 120.0, at >= 44100u ? (at - 44100u) / 22050.0 : 0.0};
            fel_host_run(h, buf, FEL_HOST_OUTS, 256, 0, 0, &tr);
            for (i = 0; i < 256u; i++) {
                (mode ? main1 : main0)[at + i] = buf[0][i];
                if (mode)
                    trk1[at + i] = buf[2][i];
            }
            for (c = 4; c < FEL_HOST_OUTS; c++)
                et[mode] += energy(buf[c], 256);         /* (the other tracks' pairs: silent) */
        }
        em[mode] = energy(mode ? main1 : main0, 88200u * 2u);
        for (c = 0; c < FEL_HOST_OUTS; c++)
            free(buf[c]);
        fel_host_free(h);
    }
    for (i = 0; i < 88200u * 2u; i++) {          /* main + own pair against the main alone (the reverb's tail aside) */
        double d = main0[i] - (main1[i] + trk1[i]);
        dsum += d * d;
        esum += (double)main0[i] * main0[i];
    }
    snprintf(what, sizeof what, "track 1 on its own pair: the main keeps %.0f %% of its energy (its FX return), the pair + the main = the mix alone (%.1f dB under it)",
             100.0 * em[1] / em[0], 10.0 * log10(dsum / esum + 1e-12));
    ck(what, em[1] < 0.5 * em[0] && energy(trk1, 88200u * 2u) > 0.3 * em[0] && 10.0 * log10(dsum / esum + 1e-12) < -30.0 &&
                 et[0] == 0.0 && et[1] == 0.0);
}

/* ---------------------------------------------------------------- state --- */
static void test_state(void)
{
    char err[256], what[200];
    fel_host_t *a = fel_host_new(dir, 48000.0, err, sizeof err), *b;
    uint8_t *blob = 0, *blob2 = 0;
    size_t n, n2;
    uint32_t hits[64], nh, i, k;
    const fel_core_api_t *api = fel_host_core(a);
    float *buf[2], l[256], r[256];
    buf[0] = l;
    buf[1] = r;
    api->test_ticks();                            /* the song: hats on every row */
    for (k = 0; k < 40u; k++)
        fel_host_run(a, buf, 2, 256, 0, 0, 0);
    n = fel_host_save(a, &blob);                  /* (taken at once: the device has not autosaved yet) */
    snprintf(what, sizeof what, "a state taken: the song in it (the device saved it now), %zu bytes compressed", n);
    ck(what, n > 8u && n < 200000u);
    b = fel_host_new(dir, 48000.0, err, sizeof err);
    ck("loaded into another instance, with IDM (the chooser's second)",
       fel_host_load(b, 1, blob, n, err, sizeof err) == 0 && fel_host_dist(b) == 1u);
    n2 = fel_host_save(b, &blob2);
    for (k = 0; k < 200u; k++)                    /* (its boot, its autosave restored) */
        fel_host_run(b, buf, 2, 256, 0, 0, 0);
    nh = 0;
    {
        uint32_t at;
        static float rec[48000u * 2u + 256u];
        for (at = 0; at < 48000u * 2u; at += 256u) {
            fel_transport_t tr = {1, 1, 120.0, at / 24000.0};
            fel_host_run(b, buf, 2, 256, 0, 0, &tr);
            for (i = 0; i < 256u; i++)
                rec[at + i] = l[i];
        }
        nh = onsets(rec, 48000u * 2u, 480u, hits, 64u);
    }
    snprintf(what, sizeof what, "... and played there: its hats (%u in 2 s at 120 BPM), its state the same song again (%zu bytes)", nh, n2);
    ck(what, nh >= 12u && n2 > 8u);
    ck("a damaged state is refused, the instance plays on as it was", fel_host_load(b, 0, blob, n / 2u, err, sizeof err) != 0 &&
                                                                          fel_host_dist(b) == 1u);
    free(blob);
    free(blob2);
    fel_host_free(a);
    fel_host_free(b);
}

/* ----------------------------------------------------------- the window --- */
static void blocks(fel_host_t *h, uint32_t n, const fel_transport_t *tr)
{
    float l[256], r[256], *o[2] = {l, r};
    uint32_t k;
    for (k = 0; k < n; k++)
        fel_host_run(h, o, 2, 256, 0, 0, tr);
}

static void test_window(void)
{
    char err[256], what[200];
    fel_host_t *h = fel_host_new(dir, 48000.0, err, sizeof err);
    static fel_view_t v;
    uint32_t seq = 0, seq1, i, diff = 0;
    int got;
    static uint16_t before[240 * 240];
    blocks(h, 300, 0);                                   /* (the boot, HOME drawn) */
    got = fel_host_view(h, &v, &seq);
    seq1 = seq;
    ck("the window's view: the screen and the LEDs (HOME lit after the boot)", got && ((v.lit_buttons >> 6) & 1u));
    ck("... unchanged while the device shows the same: no new view", (blocks(h, 2, 0), !fel_host_view(h, &v, &seq)) && seq == seq1);
    memcpy(before, v.screen, sizeof before);
    fel_host_ui_enc(h, 3, 5);                            /* KNOB 1, 5 steps: HOME's first value moves */
    blocks(h, 60, 0);
    got = fel_host_view(h, &v, &seq);
    for (i = 0; i < 240u * 240u; i++)
        diff += v.screen[i] != before[i];
    snprintf(what, sizeof what, "KNOB 1 turned from the window: the screen redrawn (%u pixels changed)", diff);
    ck(what, got && diff > 100u);
    fel_host_ui_keys(h, 1u << 12);                       /* a key held from the window */
    blocks(h, 20, 0);
    fel_host_view(h, &v, &seq);
    {
        float l[256], r[256], *o[2] = {l, r};
        double e = 0.0;
        uint32_t k, j;
        for (k = 0; k < 20u; k++) {
            fel_host_run(h, o, 2, 256, 0, 0, 0);
            for (j = 0; j < 256u; j++)
                e += (double)l[j] * l[j];
        }
        snprintf(what, sizeof what, "a key pressed in the window sounds (%.2f) and lights (%s)", e, (v.lit_keys >> 12) & 1u ? "lit" : "dark");
        ck(what, e > 0.01 && ((v.lit_keys >> 12) & 1u));
    }
    fel_host_ui_keys(h, 0);
    fel_host_ui_buttons(h, 1u << 10);                    /* PLAY pressed and let go: the host's transport rules, */
    fel_host_ui_buttons(h, 0);                           /* stopped: it does not start */
    {
        fel_transport_t tr = {1, 0, 120.0, 0.0};
        blocks(h, 80, &tr);
    }
    fel_host_view(h, &v, &seq);
    ck("PLAY pressed in the window while the host is stopped: the device stays stopped", !v.playing);
    fel_host_free(h);

    h = fel_host_new(dir, 48000.0, err, sizeof err);
    ((const fel_core_api_t *)fel_host_core(h))->test_ticks();
    blocks(h, 40, 0);
    ck("a distribution switched with the song: IDM NATIVE -> IDM", fel_host_switch(h, 1, err, sizeof err) == 0 && fel_host_dist(h) == 1u);
    {
        static float rec[48000u * 2u + 256u];
        float l[256], r[256], *o[2] = {l, r};
        uint32_t at, hits[64], nh;
        blocks(h, 200, 0);
        for (at = 0; at < 48000u * 2u; at += 256u) {
            fel_transport_t tr = {1, 1, 120.0, at / 24000.0};
            fel_host_run(h, o, 2, 256, 0, 0, &tr);
            for (i = 0; i < 256u; i++)
                rec[at + i] = l[i];
        }
        nh = onsets(rec, 48000u * 2u, 480u, hits, 64u);
        snprintf(what, sizeof what, "... its song plays there (%u hats in 2 s)", nh);
        ck(what, nh >= 12u);
    }
    fel_host_free(h);
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
    test_sync();
    test_outputs();
    test_state();
    test_window();
    printf(bad ? "plugin host test FAILED (%d)\n" : "plugin host test passed\n", bad);
    return bad != 0;
}
