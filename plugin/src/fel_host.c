/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* fel_host.h. The core playing sits behind a lock the audio thread only tries (a block that finds it taken plays
 * silence: a distribution being swapped in), so the host's main thread never waits on the audio, nor the audio on a
 * core being loaded. Events: the block is cut at each event's frame; the frames before it rendered, then the message
 * goes to the core (it plays from the core's next frames: with the resampler, its taps later, the latency reported).
 * The transport: the core is told, at each block, which beat the host is at for the core's frame that block's first
 * output comes from, less the latency (the host plays this output that much late: the device's beat lands where the
 * host's timeline has it). The song: the audio thread copies the flash after a block that wrote it, under a lock the
 * main thread takes to save it (the audio thread never waits: it copies at a later block). */
#include "fel_host.h"
#include "../core/fel_loader.h"
#include "fel_resample.h"
#include <dirent.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#ifdef __APPLE__
#define LIB_EXT ".dylib"
#else
#define LIB_EXT ".so"
#endif
#define CORE_RATE 44100.0
#define BLOB_MAGIC 0x534C4546u          /* "FELS": the song blob, then the flash's size, then it (zlib) */

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
    uint64_t core_frames;                /* the core's frames rendered since its boot (pulled, either way) */
    uint32_t master;
    atomic_uint outs_want;
    uint32_t outs;                       /* the mask the core has */
    atomic_flag snap_lock;
    atomic_uint save_req;                /* 1: the main thread asks the audio thread to save now; 2: it did */
    uint8_t *snap;                       /* the flash after the last block that wrote it */
    uint32_t snap_size, snap_writes;
};

static void lock(atomic_flag *f)
{
    while (atomic_flag_test_and_set_explicit(f, memory_order_acquire))
        ;
}
static int trylock(atomic_flag *f) { return !atomic_flag_test_and_set_explicit(f, memory_order_acquire); }
static void unlock(atomic_flag *f) { atomic_flag_clear_explicit(f, memory_order_release); }
static void say(char *err, unsigned n, const char *a, const char *b)
{
    if (err && n)
        snprintf(err, n, "fel_host: %s%s", a, b ? b : "");
}

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
    atomic_flag_clear(&h->snap_lock);
    atomic_init(&h->outs_want, 0u);
    atomic_init(&h->save_req, 0u);
    snprintf(h->dir, sizeof h->dir, "%s", cores_dir);
    h->master = 700u;                                 /* (the emulator's) */
    find_dists(h);
    if (!h->ndist) {
        say(err, err_n, "no core in ", cores_dir);
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
    free(h->snap);
    free(h);
}

uint32_t fel_host_dists(const fel_host_t *h) { return h->ndist; }
const char *fel_host_dist_stem(const fel_host_t *h, uint32_t i) { return i < h->ndist ? h->stem[i] : ""; }
const char *fel_host_dist_tag(const fel_host_t *h, uint32_t i) { return i < h->ndist ? h->tag[i] : ""; }
uint32_t fel_host_dist(const fel_host_t *h) { return h->dist; }
uint32_t fel_host_tracks(const fel_host_t *h) { return h->api ? h->api->tracks : 8u; }

/* distribution i: its core, booted on the flash given (0: erased), swapped in */
static int start(fel_host_t *h, uint32_t i, const uint8_t *nor, uint32_t nor_n, char *err, unsigned err_n)
{
    char path[1200];
    fel_instance_t *in, *old;
    const fel_core_api_t *api;
    uint8_t *snap, *old_snap;
    if (i >= h->ndist)
        return -1;
    snprintf(path, sizeof path, "%s/fel-core-%s" LIB_EXT, h->dir, h->stem[i]);
    if (!(in = fel_load(path, err, err_n)))
        return -1;
    api = fel_api(in);
    if (nor && nor_n != api->nor_size()) {
        say(err, err_n, "a song of another flash size", 0);
        fel_unload(in);
        return -1;
    }
    if (!(snap = malloc(api->nor_size()))) {
        fel_unload(in);
        return -1;
    }
    api->nor_erase();
    if (nor)
        memcpy(api->nor(), nor, nor_n);
    api->boot();
    api->master(h->master);
    memcpy(snap, api->nor(), api->nor_size());
    lock(&h->lock);
    lock(&h->snap_lock);
    old = h->in;
    old_snap = h->snap;
    h->in = in;
    h->api = api;
    h->dist = i;
    h->snap = snap;
    h->snap_size = api->nor_size();
    h->snap_writes = api->flash_writes();
    h->outs = 0;                                      /* (the new core has none yet: the next block sets them) */
    h->core_frames = 0;
    if (!h->direct)
        fel_rs_reset(&h->rs);
    unlock(&h->snap_lock);
    unlock(&h->lock);
    if (old)
        fel_unload(old);
    free(old_snap);
    return 0;
}

int fel_host_set_dist(fel_host_t *h, uint32_t i, char *err, unsigned err_n) { return start(h, i, 0, 0, err, err_n); }

static void snapshot(fel_host_t *h)              /* (the audio thread's, or the main's with the audio held) */
{
    lock(&h->snap_lock);
    memcpy(h->snap, h->api->nor(), h->snap_size);
    h->snap_writes = h->api->flash_writes();
    unlock(&h->snap_lock);
}

size_t fel_host_save(fel_host_t *h, uint8_t **out)
{
    uLongf zn;
    uint8_t *b;
    uint32_t k;
    *out = 0;
    /* the music saved first: by the audio thread at its next block; none running (stopped, or offline): here */
    atomic_store(&h->save_req, 1u);
    for (k = 0; k < 300u && atomic_load(&h->save_req) != 2u; k++)
        usleep(1000);
    if (atomic_load(&h->save_req) != 2u) {
        lock(&h->lock);
        if (atomic_load(&h->save_req) != 2u) {
            h->api->save_now();
            snapshot(h);
        }
        unlock(&h->lock);
    }
    atomic_store(&h->save_req, 0u);
    lock(&h->snap_lock);
    zn = compressBound(h->snap_size);
    if (!h->snap || !(b = malloc(zn + 8u))) {
        unlock(&h->snap_lock);
        return 0;
    }
    if (compress2(b + 8, &zn, h->snap, h->snap_size, 6) != Z_OK) {
        unlock(&h->snap_lock);
        free(b);
        return 0;
    }
    memcpy(b, &(uint32_t){BLOB_MAGIC}, 4);
    memcpy(b + 4, &h->snap_size, 4);
    unlock(&h->snap_lock);
    *out = b;
    return zn + 8u;
}

int fel_host_load(fel_host_t *h, uint32_t i, const uint8_t *blob, size_t n, char *err, unsigned err_n)
{
    uint32_t magic, size;
    uLongf got;
    uint8_t *nor;
    int r;
    if (n < 8u) {
        say(err, err_n, "no song", 0);
        return -1;
    }
    memcpy(&magic, blob, 4);
    memcpy(&size, blob + 4, 4);
    if (magic != BLOB_MAGIC || size > (16u << 20) || !(nor = malloc(size))) {
        say(err, err_n, "not a song", 0);
        return -1;
    }
    got = size;
    if (uncompress(nor, &got, blob + 8, (uLong)(n - 8u)) != Z_OK || got != size) {
        free(nor);
        say(err, err_n, "a damaged song", 0);
        return -1;
    }
    r = start(h, i, nor, size, err, err_n);
    free(nor);
    return r;
}

int fel_host_set_rate(fel_host_t *h, double rate)
{
    fel_rs_t rs;
    int direct = rate == CORE_RATE;
    if (!direct && fel_rs_init(&rs, CORE_RATE, rate))
        return -1;
    lock(&h->lock);
    fel_rs_free(&h->rs);
    if (direct)
        memset(&h->rs, 0, sizeof h->rs);
    else
        h->rs = rs;
    h->rate = rate;
    h->direct = direct;
    unlock(&h->lock);
    return 0;
}

uint32_t fel_host_latency(const fel_host_t *h) { return h->direct ? 0u : fel_rs_latency(&h->rs); }

void fel_host_master(fel_host_t *h, uint32_t v)
{
    h->master = v > 1023u ? 1023u : v;
    if (trylock(&h->lock)) {
        if (h->api)
            h->api->master(h->master);
        unlock(&h->lock);
    }
}

void fel_host_outputs(fel_host_t *h, uint32_t mask) { atomic_store(&h->outs_want, mask); }
const void *fel_host_core(const fel_host_t *h) { return h->api; }

typedef struct { fel_host_t *h; uint32_t keep; } pull_t;   /* keep: the channels the core fills */
static void pull(void *ctx, float *const *in, uint32_t nch, uint32_t n)
{
    const pull_t *p = ctx;
    uint32_t c;
    p->h->core_frames += n;
    if (nch <= 2u) {
        p->h->api->render(in[0], in[1], n);
        return;
    }
    p->h->api->render_multi(in, n);
    for (c = p->keep; c < nch; c++)
        memset(in[c], 0, sizeof(float) * n);
}

static void render(fel_host_t *h, float *const *out, uint32_t nch, uint32_t at, uint32_t n)
{
    float *o[FEL_HOST_OUTS];
    uint32_t c;
    pull_t p = {h, 2u + 2u * h->api->tracks};
    if (!n)
        return;
    for (c = 0; c < nch; c++)
        o[c] = out[c] + at;
    if (h->direct) {
        pull(&p, o, nch, n);
    } else {
        fel_rs_process(&h->rs, o, nch, n, pull, &p);
    }
}

void fel_host_run(fel_host_t *h, float *const *out, uint32_t nout, uint32_t n, const fel_event_t *ev, uint32_t nev,
                  const fel_transport_t *tr)
{
    uint32_t at = 0, e, c, nch, want, w;
    if (!trylock(&h->lock)) {
        for (c = 0; c < nout; c++)                    /* (a swap under way) */
            memset(out[c], 0, sizeof(float) * n);
        return;
    }
    want = nout >= FEL_HOST_OUTS ? atomic_load(&h->outs_want) & ((1u << h->api->tracks) - 1u) : 0u;
    if (want != h->outs) {
        h->api->outputs(want);
        h->outs = want;
    }
    nch = h->outs ? FEL_HOST_OUTS : 2u;
    if (tr && tr->valid) {
        double frame = h->direct ? (double)h->core_frames
                                 : (double)(h->core_frames - h->rs.pulled) + fel_rs_where(&h->rs);   /* (its base at
                                                                         * the resampler's reset, then its place) */
        double lat = (double)fel_host_latency(h) * tr->bpm / (60.0 * h->rate);
        h->api->transport(tr->playing, tr->bpm, tr->beats - lat, (uint64_t)(frame > 0.0 ? frame + 0.5 : 0.0));
    }
    for (e = 0; e < nev; e++) {
        uint32_t f = ev[e].frame < n ? ev[e].frame : n;
        if (f > at) {
            render(h, out, nch, at, f - at);
            at = f;
        }
        if (ev[e].data[0] >= 0x80u && ev[e].data[0] < 0xF0u)
            h->api->midi(ev[e].data[0], ev[e].data[1], ev[e].data[2]);
    }
    render(h, out, nch, at, n - at);
    for (c = nch; c < nout; c++)
        memset(out[c], 0, sizeof(float) * n);
    if (atomic_load(&h->save_req) == 1u) {          /* (a state is being taken: the music saved, copied) */
        h->api->save_now();
        snapshot(h);
        atomic_store(&h->save_req, 2u);
    }
    w = h->api->flash_writes();
    if (w != h->snap_writes && trylock(&h->snap_lock)) {   /* (the song changed: its copy, for a save) */
        memcpy(h->snap, h->api->nor(), h->snap_size);
        h->snap_writes = w;
        unlock(&h->snap_lock);
    }
    unlock(&h->lock);
}
