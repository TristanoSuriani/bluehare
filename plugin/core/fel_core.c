/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The plugin's core (fel_core.h): the simulated FM-1 of the browser emulator and the native image
 * (web/emu/felucca_web.c), built FELUCCA_NATIVE, behind one exported table. Only fel_core_api is exported
 * (plugin/build_core.sh: the linker's export list), so two loaded copies never bind to each other's functions.
 *   cc -dynamiclib -DFELUCCA_DIST=<dist> ... plugin/core/fel_core.c  (plugin/build_core.sh) */
#include <stdint.h>
#include <time.h>
#define FELUCCA_NATIVE 1                         /* (as native/felucca_native.c: core.h, fx.c, audio.c) */
static uint32_t native_ticks(void)               /* the computer's clock in the device's TIMER4 ticks (24 MHz) */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 24000000u + (uint64_t)ts.tv_nsec * 24u / 1000u);
}
#include "../../web/emu/felucca_web.c"
#include "fel_core.h"

static void core_render(float *l, float *r, uint32_t n)
{
    uint32_t k, i;
    for (k = 0; k < n; k += 1024u) {
        uint32_t m = n - k < 1024u ? n - k : 1024u;
        web_render(m);
        for (i = 0; i < m; i++) {
            l[k + i] = out_l[i];
            r[k + i] = out_r[i];
        }
    }
}
static void core_render_multi(float *const *out, uint32_t n)
{
    uint32_t k, i, t;
    for (k = 0; k < n; k += 1024u) {
        uint32_t m = n - k < 1024u ? n - k : 1024u, r0 = ring_r;
        web_render(m);                                   /* (ring_r on by m: the frames r0 .. r0 + m) */
        for (i = 0; i < m; i++) {
            out[0][k + i] = out_l[i];
            out[1][k + i] = out_r[i];
        }
        for (t = 0; t < NTRK; t++) {
            float *l = out[2u + 2u * t], *r = out[3u + 2u * t];
            if (!((trk_own >> t) & 1u)) {
                for (i = 0; i < m; i++)
                    l[k + i] = r[k + i] = 0.0f;
                continue;
            }
            for (i = 0; i < m; i++) {
                l[k + i] = tring[t][0][(r0 + i) % RING];
                r[k + i] = tring[t][1][(r0 + i) % RING];
            }
        }
    }
}
static void core_transport(uint32_t playing, double bpm, double beats, uint64_t frame)
{
    host_tr.on = 1;
    host_tr.playing = (uint8_t)(playing != 0u);
    host_tr.bpm = bpm < 20.0 ? 20.0 : bpm > 999.0 ? 999.0 : bpm;
    host_tr.beats0 = beats < 0.0 ? 0.0 : beats;
    host_tr.frame0 = frame;
}
/* the song as a stream of the device's chunks (project.c proj_write, as autosave_write counts and writes it) */
static uint8_t *sg_buf;
static uint32_t sg_cap, sg_n;
static int sg_put(uint32_t chunk, uint32_t len)
{
    (void)chunk;
    if (sg_n + PROJ_HDR + len > sg_cap)
        return 1;
    memcpy(sg_buf + sg_n, proj_wire_u.raw, PROJ_HDR + len);
    sg_n += PROJ_HDR + len;
    return 0;
}
static uint32_t core_song_get(uint8_t *buf, uint32_t cap)
{
    pw_t w;
    uint32_t n, total;
    if (!web_booted)
        return 0;
    memset(&w, 0, sizeof w);
    w.h = 2166136261u;
    proj_write(&w, proj_name);                           /* (counted) */
    if (w.n > PROJ_CHUNKS * PROJ_DATA)
        return 0;
    n = (w.n + PROJ_DATA - 1u) / PROJ_DATA;
    total = w.n + n * PROJ_HDR;
    if (!buf || cap < total)
        return total;
    memset(&w, 0, sizeof w);
    w.h = 2166136261u;
    w.put = sg_put;
    w.count = n;
    w.gen = 1;
    sg_buf = buf;
    sg_cap = cap;
    sg_n = 0;
    proj_write(&w, proj_name);
    return w.err ? 0 : sg_n;
}
static const uint8_t *ss_buf;
static uint32_t ss_off[PROJ_CHUNKS], ss_len[PROJ_CHUNKS];
static int ss_get(uint32_t chunk)                        /* (project_restore: chunk into st_buf) */
{
    if (chunk >= PROJ_CHUNKS || !ss_len[chunk])
        return 1;
    memcpy(st_buf, ss_buf + ss_off[chunk], ss_len[chunk]);
    return 0;
}
static uint32_t core_song_set(const uint8_t *buf, uint32_t n)
{
    uint32_t at = 0, k = 0;
    if (!web_booted)
        return 1;
    memset(ss_len, 0, sizeof ss_len);
    while (at < n) {                                     /* (the chunks: each a header, its length, its data) */
        proj_hdr_t h;
        if (k >= PROJ_CHUNKS || n - at < PROJ_HDR)
            return 1;
        memcpy(&h, buf + at, sizeof h);
        if (h.magic != PROJ_MAGIC || h.len > PROJ_DATA || n - at - PROJ_HDR < h.len)
            return 1;
        ss_off[k] = at;
        ss_len[k] = PROJ_HDR + h.len;
        at += PROJ_HDR + h.len;
        k++;
    }
    ss_buf = buf;
    return k ? (uint32_t)project_restore(ss_get, k) : 1u;
}
static void core_outputs(uint32_t mask) { trk_own = mask & ((1u << NTRK) - 1u); }
static void core_test_ticks(void)
{
    uint32_t i;
    uint8_t hat = 42;
    host_drums(&trk[0]);
    trk[0].p[P_E2] = 0;                                  /* (KIT's DECAY: a quarter; no sends: a short, dry tick) */
    trk[0].p[P_E7] = 0;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    for (i = 0; i < 16u; i++)
        put_step(&trk[0], i, 1, &hat, ST_NOTE, 0);
    tk_pl.mode = TK_LOOP;
    tk_pl.loop_pat = 0;
    ui.force = 1;
}
static const uint16_t *core_screen(void) { return web_fb; }
static uint32_t core_claimed;
static uint32_t core_claim(void) { return core_claimed++ == 0u; }

static const fel_core_api_t CORE = {
    .abi = FEL_CORE_ABI,
    .dist = FELUCCA_DIST_TAG[0] ? FELUCCA_DIST_TAG + 1 : "DEFAULT",
    .version = FELUCCA_VERSION_DIST,
    .sample_rate = FS,
    .tracks = NTRK,
    .claim = core_claim,
    .nor = web_nor,
    .nor_size = web_nor_size,
    .nor_erase = web_nor_erase,
    .flash_writes = web_flash_writes_count,
    .boot = web_boot,
    .render = core_render,
    .midi = web_midi,
    .buttons = web_buttons,
    .keys = web_keys,
    .enc = web_enc,
    .master = web_master,
    .screen = core_screen,
    .screen_draws = web_screen_draws,
    .lit_buttons = web_lit_buttons,
    .lit_keys = web_lit_keys,
    .dim_buttons = web_dim_buttons,
    .dim_keys = web_dim_keys,
    .breath_buttons = web_breath_buttons,
    .breath_keys = web_breath_keys,
    .mid_keys = web_mid_keys,
    .now_ms = web_now_ms,
    .playing = web_playing,
    .test_heavy = web_test_heavy,
    .transport = core_transport,
    .outputs = core_outputs,
    .render_multi = core_render_multi,
    .song_get = core_song_get,
    .song_set = core_song_set,
    .test_ticks = core_test_ticks,
};

__attribute__((visibility("default"))) const fel_core_api_t *fel_core_api(void) { return &CORE; }
