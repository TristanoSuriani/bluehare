/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* fel_host.h. The core playing sits behind a lock the audio thread only tries (a block that finds it taken plays
 * silence: a distribution being swapped in), so the host's main thread never waits on the audio, nor the audio on a
 * core being loaded. Events: the block is cut at each event's frame; the frames before it rendered, then the message
 * goes to the core (it plays from the core's next frames: with the resampler, its taps later, the latency reported). */
#include "fel_host.h"
#include "../core/fel_loader.h"
#include "fel_resample.h"
#include <dirent.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#define LIB_EXT ".dylib"
#else
#define LIB_EXT ".so"
#endif
#define CORE_RATE 44100.0

static const char *const KNOWN[][2] = {     /* the chooser's order, and the tags before a core is opened */
    {"idm_native", "IDM NATIVE"}, {"idm", "IDM"}, {"foundry", "FOUNDRY"}, {"warehouse", "WAREHOUSE"}, {"chip", "CHIP"},
};
#define NKNOWN (sizeof KNOWN / sizeof KNOWN[0])

struct fel_host {
    char dir[1024];
    uint32_t ndist, dist;
    char stem[FEL_DIST_MAX][64], tag[FEL_DIST_MAX][64];
    atomic_flag lock;                    /* held by the audio thread while it runs the core, by a swap while it swaps */
    fel_instance_t *in;
    const fel_core_api_t *api;
    double rate;
    int direct;                          /* the host at 44.1 kHz: no resampler */
    fel_rs_t rs;
    uint32_t master;
};

static void lock(fel_host_t *h)
{
    while (atomic_flag_test_and_set_explicit(&h->lock, memory_order_acquire))
        ;
}
static void unlock(fel_host_t *h) { atomic_flag_clear_explicit(&h->lock, memory_order_release); }

static void find_dists(fel_host_t *h)
{
    char found[FEL_DIST_MAX * 2][64];
    uint32_t nf = 0, i, k;
    DIR *d = opendir(h->dir);
    struct dirent *e;
    size_t pl = strlen("fel-core-"), el = strlen(LIB_EXT);
    while (d && (e = readdir(d)) && nf < FEL_DIST_MAX * 2u) {
        size_t n = strlen(e->d_name);
        if (n > pl + el && !strncmp(e->d_name, "fel-core-", pl) && !strcmp(e->d_name + n - el, LIB_EXT) &&
            n - pl - el < 64u) {
            memcpy(found[nf], e->d_name + pl, n - pl - el);
            found[nf++][n - pl - el] = 0;
        }
    }
    if (d)
        closedir(d);
    h->ndist = 0;
    for (k = 0; k < NKNOWN; k++)                     /* the known ones in their order */
        for (i = 0; i < nf; i++)
            if (!strcmp(found[i], KNOWN[k][0]) && h->ndist < FEL_DIST_MAX) {
                snprintf(h->stem[h->ndist], 64, "%s", KNOWN[k][0]);
                snprintf(h->tag[h->ndist++], 64, "%s", KNOWN[k][1]);
                found[i][0] = 0;
            }
    for (i = 0; i < nf; i++)                          /* then any other, by its name */
        if (found[i][0] && h->ndist < FEL_DIST_MAX) {
            char *t = h->tag[h->ndist];
            snprintf(h->stem[h->ndist], 64, "%s", found[i]);
            snprintf(t, 64, "%s", found[i]);
            for (; *t; t++)
                *t = *t == '_' ? ' ' : *t >= 'a' && *t <= 'z' ? (char)(*t - 32) : *t;
            h->ndist++;
        }
}

fel_host_t *fel_host_new(const char *cores_dir, double rate, char *err, unsigned err_n)
{
    fel_host_t *h = calloc(1, sizeof *h);
    if (!h)
        return 0;
    atomic_flag_clear(&h->lock);
    snprintf(h->dir, sizeof h->dir, "%s", cores_dir);
    h->master = 700u;                                 /* (the emulator's) */
    find_dists(h);
    if (!h->ndist) {
        if (err && err_n)
            snprintf(err, err_n, "fel_host: no core in %s", cores_dir);
        free(h);
        return 0;
    }
    if (fel_host_set_rate(h, rate) || fel_host_set_dist(h, 0, err, err_n)) {
        fel_host_free(h);
        return 0;
    }
    return h;
}

void fel_host_free(fel_host_t *h)
{
    if (!h)
        return;
    if (h->in)
        fel_unload(h->in);
    fel_rs_free(&h->rs);
    free(h);
}

uint32_t fel_host_dists(const fel_host_t *h) { return h->ndist; }
const char *fel_host_dist_stem(const fel_host_t *h, uint32_t i) { return i < h->ndist ? h->stem[i] : ""; }
const char *fel_host_dist_tag(const fel_host_t *h, uint32_t i) { return i < h->ndist ? h->tag[i] : ""; }
uint32_t fel_host_dist(const fel_host_t *h) { return h->dist; }

int fel_host_set_dist(fel_host_t *h, uint32_t i, char *err, unsigned err_n)
{
    char path[1200];
    fel_instance_t *in, *old;
    const fel_core_api_t *api;
    if (i >= h->ndist)
        return -1;
    snprintf(path, sizeof path, "%s/fel-core-%s" LIB_EXT, h->dir, h->stem[i]);
    if (!(in = fel_load(path, err, err_n)))
        return -1;
    api = fel_api(in);
    api->nor_erase();
    api->boot();
    api->master(h->master);
    lock(h);
    old = h->in;
    h->in = in;
    h->api = api;
    h->dist = i;
    if (!h->direct)
        fel_rs_reset(&h->rs);
    unlock(h);
    if (old)
        fel_unload(old);
    return 0;
}

int fel_host_set_rate(fel_host_t *h, double rate)
{
    fel_rs_t rs;
    int direct = rate == CORE_RATE;
    if (!direct && fel_rs_init(&rs, CORE_RATE, rate))
        return -1;
    lock(h);
    fel_rs_free(&h->rs);
    if (direct)
        memset(&h->rs, 0, sizeof h->rs);
    else
        h->rs = rs;
    h->rate = rate;
    h->direct = direct;
    unlock(h);
    return 0;
}

uint32_t fel_host_latency(const fel_host_t *h) { return h->direct ? 0u : fel_rs_latency(&h->rs); }

void fel_host_master(fel_host_t *h, uint32_t v)
{
    h->master = v > 1023u ? 1023u : v;
    if (!atomic_flag_test_and_set_explicit(&h->lock, memory_order_acquire)) {
        if (h->api)
            h->api->master(h->master);
        unlock(h);
    }
}

static void pull(void *ctx, float *l, float *r, uint32_t n)
{
    const fel_core_api_t *api = ctx;
    api->render(l, r, n);
}

static void render(fel_host_t *h, float *l, float *r, uint32_t n)
{
    if (!n)
        return;
    if (h->direct)
        h->api->render(l, r, n);
    else
        fel_rs_process(&h->rs, l, r, n, pull, (void *)h->api);
}

void fel_host_run(fel_host_t *h, float *l, float *r, uint32_t n, const fel_event_t *ev, uint32_t nev)
{
    uint32_t at = 0, e;
    if (atomic_flag_test_and_set_explicit(&h->lock, memory_order_acquire)) {
        memset(l, 0, sizeof(float) * n);              /* (a swap under way) */
        memset(r, 0, sizeof(float) * n);
        return;
    }
    for (e = 0; e < nev; e++) {
        uint32_t f = ev[e].frame < n ? ev[e].frame : n;
        if (f > at) {
            render(h, l + at, r + at, f - at);
            at = f;
        }
        if (ev[e].data[0] >= 0x80u && ev[e].data[0] < 0xF0u)
            h->api->midi(ev[e].data[0], ev[e].data[1], ev[e].data[2]);
    }
    render(h, l + at, r + at, n - at);
    unlock(h);
}
