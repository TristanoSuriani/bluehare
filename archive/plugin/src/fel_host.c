/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* fel_host.h. The core playing sits behind a lock the audio thread only tries (a block that finds it taken plays
 * silence: a distribution being swapped in), so the host's main thread never waits on the audio, nor the audio on a
 * core being loaded. Events: the block is cut at each event's frame; the frames before it rendered, then the message
 * goes to the core (it plays from the core's next frames: with the resampler, its taps later, the latency reported).
 * The transport: the core is told, at each block, which beat the host is at for the core's frame that block's first
 * output comes from, less the latency (the host plays this output that much late: the device's beat lands where the
 * host's timeline has it). The song: a state is the device's own song stream (fel_core.h song_get: a few KB, any
 * distribution's), taken by the audio thread at its next block when the main thread asks (or by the main thread with
 * the audio held, when no block comes). */
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
#define BLOB_MAGIC 0x504C4546u          /* "FELP": the song blob, then the stream's size, then it (zlib) */
#define SONG_CAP (1u << 17)              /* the stream at most (16 chunks of 4 KB) */

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
    atomic_uint save_req;                /* 1: the main thread asks the audio thread for the song; 2: it took it */
    uint8_t *song;                       /* the song stream taken (SONG_CAP) */
    uint32_t song_n;
    /* the window: the view (written by the audio thread under view_seq, odd while it writes), the panel's queue */
    fel_view_t view;
    atomic_uint view_seq;
    uint32_t view_draws;
    struct { uint8_t kind; uint32_t a; int32_t b; } uiq[256];   /* kind 0 buttons, 1 keys, 2 an encoder */
    atomic_uint uiq_w, uiq_r;
    uint32_t ui_next;                    /* the device's ms the next button or key change may come */
    atomic_uint runs;                    /* fel_host_run's calls (the idle sees whether the host runs the audio) */
    uint32_t idle_runs, idle_ms;         /* (fel_host_idle: the runs it saw last, and when; when it last ran the device) */
    int idle_on;                         /* the device ran on the idle: the resampler starts anew at the next run */
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
    atomic_init(&h->outs_want, 0u);
    atomic_init(&h->save_req, 0u);
    atomic_init(&h->view_seq, 0u);
    atomic_init(&h->runs, 0u);
    atomic_init(&h->uiq_w, 0u);
    atomic_init(&h->uiq_r, 0u);
    snprintf(h->dir, sizeof h->dir, "%s", cores_dir);
    h->master = 700u;                                 /* (the emulator's) */
    if (!(h->song = malloc(SONG_CAP))) {
        free(h);
        return 0;
    }
    find_dists(h);
    if (!h->ndist) {
        say(err, err_n, "no core in ", cores_dir);
        free(h->song);
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
    free(h->song);
    free(h);
}

uint32_t fel_host_dists(const fel_host_t *h) { return h->ndist; }
const char *fel_host_dist_stem(const fel_host_t *h, uint32_t i) { return i < h->ndist ? h->stem[i] : ""; }
const char *fel_host_dist_tag(const fel_host_t *h, uint32_t i) { return i < h->ndist ? h->tag[i] : ""; }
uint32_t fel_host_dist(const fel_host_t *h) { return h->dist; }
uint32_t fel_host_tracks(const fel_host_t *h) { return h->api ? h->api->tracks : 8u; }

/* distribution i: its core, booted, the song given made its music (0: none), swapped in */
static int start(fel_host_t *h, uint32_t i, const uint8_t *song, uint32_t song_n, char *err, unsigned err_n)
{
    char path[1200];
    fel_instance_t *in, *old;
    const fel_core_api_t *api;
    uint32_t r;
    if (i >= h->ndist)
        return -1;
    snprintf(path, sizeof path, "%s/fel-core-%s" LIB_EXT, h->dir, h->stem[i]);
    if (!(in = fel_load(path, err, err_n)))
        return -1;
    api = fel_api(in);
    api->nor_erase();
    api->boot();
    api->master(h->master);
    if (song && (r = api->song_set(song, song_n)) != 0) {
        say(err, err_n, r == 2u ? "a song of more tracks than this distribution has" : "a damaged song", 0);
        fel_unload(in);
        return -1;
    }
    lock(&h->lock);
    old = h->in;
    h->in = in;
    h->api = api;
    h->dist = i;
    h->outs = 0;                                      /* (the new core has none yet: the next block sets them) */
    h->view_draws = 0xFFFFFFFFu;                      /* (its screen copied at its first block) */
    h->ui_next = 0;
    h->core_frames = 0;
    if (!h->direct)
        fel_rs_reset(&h->rs);
    unlock(&h->lock);
    if (old)
        fel_unload(old);
    return 0;
}

int fel_host_set_dist(fel_host_t *h, uint32_t i, char *err, unsigned err_n) { return start(h, i, 0, 0, err, err_n); }

size_t fel_host_save(fel_host_t *h, uint8_t **out)
{
    uLongf zn;
    uint8_t *b;
    uint32_t k;
    *out = 0;
    /* the song taken by the audio thread at its next block; none running (stopped, or offline): here, the audio held */
    atomic_store(&h->save_req, 1u);
    for (k = 0; k < 300u && atomic_load(&h->save_req) != 2u; k++)
        usleep(1000);
    if (atomic_load(&h->save_req) != 2u) {
        lock(&h->lock);
        if (atomic_load(&h->save_req) != 2u)
            h->song_n = h->api->song_get(h->song, SONG_CAP);
        unlock(&h->lock);
    }
    atomic_store(&h->save_req, 0u);
    if (!h->song_n || h->song_n > SONG_CAP)
        return 0;
    zn = compressBound(h->song_n);
    if (!(b = malloc(zn + 8u)))
        return 0;
    if (compress2(b + 8, &zn, h->song, h->song_n, 6) != Z_OK) {
        free(b);
        return 0;
    }
    memcpy(b, &(uint32_t){BLOB_MAGIC}, 4);
    memcpy(b + 4, &h->song_n, 4);
    *out = b;
    return zn + 8u;
}

int fel_host_load(fel_host_t *h, uint32_t i, const uint8_t *blob, size_t n, char *err, unsigned err_n)
{
    uint32_t magic, size;
    uLongf got;
    uint8_t *song;
    int r;
    if (n < 8u) {
        say(err, err_n, "no song", 0);
        return -1;
    }
    memcpy(&magic, blob, 4);
    memcpy(&size, blob + 4, 4);
    if (magic != BLOB_MAGIC || !size || size > SONG_CAP || !(song = malloc(size))) {
        say(err, err_n, "not a song", 0);
        return -1;
    }
    got = size;
    if (uncompress(song, &got, blob + 8, (uLong)(n - 8u)) != Z_OK || got != size) {
        free(song);
        say(err, err_n, "a damaged song", 0);
        return -1;
    }
    r = start(h, i, song, size, err, err_n);
    free(song);
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

/* the window's queue: one writer (the window), one reader (the audio thread) */
static void ui_push(fel_host_t *h, uint8_t kind, uint32_t a, int32_t b)
{
    uint32_t w = atomic_load(&h->uiq_w);
    if (w - atomic_load(&h->uiq_r) >= 256u)
        return;
    h->uiq[w % 256u].kind = kind;
    h->uiq[w % 256u].a = a;
    h->uiq[w % 256u].b = b;
    atomic_store(&h->uiq_w, w + 1u);
}
void fel_host_ui_buttons(fel_host_t *h, uint32_t labels) { ui_push(h, 0, labels, 0); }
void fel_host_ui_keys(fel_host_t *h, uint32_t mask) { ui_push(h, 1, mask, 0); }
void fel_host_ui_enc(fel_host_t *h, uint32_t role, int32_t n) { ui_push(h, 2, role, n); }

/* (the audio thread, at a block's start) the panel's changes: an encoder at once; a button or a key held at least
 * HOLD_MS of the device's time before the next one, so the panel's scan sees each (the native image's rule) */
#define HOLD_MS 24u
static void ui_apply(fel_host_t *h)
{
    uint32_t r = atomic_load(&h->uiq_r), w = atomic_load(&h->uiq_w), now = h->api->now_ms();
    while (r != w) {
        const uint8_t kind = h->uiq[r % 256u].kind;
        if (kind == 2u) {
            h->api->enc(h->uiq[r % 256u].a, h->uiq[r % 256u].b);
        } else {
            if ((int32_t)(now - h->ui_next) < 0)
                break;
            if (kind)
                h->api->keys(h->uiq[r % 256u].a);
            else
                h->api->buttons(h->uiq[r % 256u].a);
            h->ui_next = now + HOLD_MS;
        }
        r++;
    }
    atomic_store(&h->uiq_r, r);
}

/* (the audio thread, at a block's end) the view: the LEDs always, the screen when the device drew */
static void view_take(fel_host_t *h)
{
    const fel_core_api_t *a = h->api;
    uint32_t d = a->screen_draws(), s = atomic_load(&h->view_seq);
    fel_view_t *v = &h->view;
    uint32_t lb = a->lit_buttons(), lk = a->lit_keys(), db = a->dim_buttons(), dk = a->dim_keys();
    uint32_t bb = a->breath_buttons(), bk = a->breath_keys(), mk = a->mid_keys(), pl = a->playing();
    if (d == h->view_draws && lb == v->lit_buttons && lk == v->lit_keys && db == v->dim_buttons && dk == v->dim_keys &&
        bb == v->breath_buttons && bk == v->breath_keys && mk == v->mid_keys && pl == v->playing)
        return;
    atomic_store(&h->view_seq, s + 1u);               /* (odd: being written) */
    atomic_thread_fence(memory_order_release);
    if (d != h->view_draws)
        memcpy(v->screen, a->screen(), sizeof v->screen);
    v->lit_buttons = lb;
    v->lit_keys = lk;
    v->dim_buttons = db;
    v->dim_keys = dk;
    v->breath_buttons = bb;
    v->breath_keys = bk;
    v->mid_keys = mk;
    v->playing = pl;
    h->view_draws = d;
    atomic_thread_fence(memory_order_release);
    atomic_store(&h->view_seq, s + 2u);
}

#define IDLE_AFTER_MS 150u                       /* no run for this long: the host is not running the audio */
void fel_host_idle(fel_host_t *h, uint32_t now_ms)
{
    static float scratch[2][2048];
    uint32_t r = atomic_load(&h->runs), ms, n;
    if (r != h->idle_runs) {                         /* (the host runs it: nothing to do) */
        h->idle_runs = r;
        h->idle_ms = now_ms;                         /* (idle_on: the run clears it, under the lock) */
        return;
    }
    if ((!h->idle_on && now_ms - h->idle_ms < IDLE_AFTER_MS) || !trylock(&h->lock))
        return;
    ms = h->idle_on ? now_ms - h->idle_ms : 20u;
    ms = ms > 100u ? 100u : ms;                      /* (a stall: no catching up) */
    h->idle_ms = now_ms;
    n = ms * 441u / 10u;
    ui_apply(h);
    while (n) {                                      /* the device on, its sound thrown away (core rate) */
        uint32_t m = n > 2048u ? 2048u : n;
        h->api->render(scratch[0], scratch[1], m);
        h->core_frames += m;
        n -= m;
    }
    h->idle_on = 1;
    view_take(h);
    unlock(&h->lock);
}

int fel_host_view(fel_host_t *h, fel_view_t *v, uint32_t *seq)
{
    uint32_t k;
    for (k = 0; k < 8u; k++) {                        /* (a copy between two writes: a few tries) */
        uint32_t s0 = atomic_load(&h->view_seq), s1;
        if (s0 == *seq)
            return 0;
        if (s0 & 1u)
            continue;
        atomic_thread_fence(memory_order_acquire);
        memcpy(v, &h->view, sizeof *v);
        atomic_thread_fence(memory_order_acquire);
        s1 = atomic_load(&h->view_seq);
        if (s0 == s1) {
            *seq = s0;
            return 1;
        }
    }
    return 0;
}

int fel_host_switch(fel_host_t *h, uint32_t i, char *err, unsigned err_n)
{
    uint8_t *blob = 0;
    size_t n = fel_host_save(h, &blob);
    int r = n ? fel_host_load(h, i, blob, n, err, err_n) : fel_host_set_dist(h, i, err, err_n);
    free(blob);
    return r;
}

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
    uint32_t at = 0, e, c, nch, want;
    if (!trylock(&h->lock)) {
        for (c = 0; c < nout; c++)                    /* (a swap under way) */
            memset(out[c], 0, sizeof(float) * n);
        return;
    }
    atomic_fetch_add(&h->runs, 1u);
    if (h->idle_on) {                                /* (the device ran on the idle: the resampler from here) */
        h->idle_on = 0;
        if (!h->direct)
            fel_rs_reset(&h->rs);
    }
    ui_apply(h);
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
    if (atomic_load(&h->save_req) == 1u) {          /* (a state is being taken: the song) */
        h->song_n = h->api->song_get(h->song, SONG_CAP);
        atomic_store(&h->save_req, 2u);
    }
    view_take(h);
    unlock(&h->lock);
}
