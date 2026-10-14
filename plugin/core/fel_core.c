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
};

__attribute__((visibility("default"))) const fel_core_api_t *fel_core_api(void) { return &CORE; }
