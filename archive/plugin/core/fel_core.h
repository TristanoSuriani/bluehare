/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The plugin's core: one distribution of Blue Hare, the native image's build of it (FELUCCA_NATIVE: 16 voices a
 * track, the native reverbs, the native engines in a NATIVE distribution), as a shared library that exports one thing,
 * fel_core_api(): this table. The firmware keeps its state in globals, so an instance is a library loaded on its own
 * (plugin/core/fel_loader.c: a private copy each, its globals its own). Every call but the ones marked is the
 * audio thread's: the device runs as the audio asks for it (its clock follows the frames rendered, as in the native
 * image and the browser), so offline renders run faster than real time.
 *   plugin/build_core.sh -> build/plugin/cores/fel-core-<dist>.dylib (.so on Linux) */
#ifndef FEL_CORE_H
#define FEL_CORE_H
#include <stdint.h>

#define FEL_CORE_ABI 4u                  /* raised when the table changes: a core of another ABI is refused */

typedef struct {
    uint32_t abi;                        /* FEL_CORE_ABI */
    const char *dist;                    /* "IDM NATIVE", "FOUNDRY", .. (the distribution's tag) */
    const char *version;                 /* "v0.10.0 IDM NATIVE" */
    uint32_t sample_rate;                /* 44100: the core's own; the plugin resamples */
    uint32_t tracks;                     /* 8 or 4 */
    /* 1 the first time it is called in this image, 0 after: an image already an instance's (the loader's check that
     * the library it opened is a fresh one; the host's main thread) */
    uint32_t (*claim)(void);
    /* the flash (1 MiB NOR: the song, the user presets, the settings), the instance's own; erased before boot. (A
     * saved state is the song alone: song_get, song_set) */
    uint8_t *(*nor)(void);
    uint32_t (*nor_size)(void);
    void (*nor_erase)(void);
    uint32_t (*flash_writes)(void);
    void (*boot)(void);
    /* n frames (any number) at sample_rate into l, r */
    void (*render)(float *l, float *r, uint32_t n);
    /* input: a channel message (0: the device's ring was full); the panel's buttons by label (panel.c B_*), the 27
     * keys, an encoder by role (EN_*) n steps, MASTER 0 .. 1023 */
    uint32_t (*midi)(uint32_t status, uint32_t d1, uint32_t d2);
    void (*buttons)(uint32_t labels);
    void (*keys)(uint32_t mask);
    void (*enc)(uint32_t role, int32_t n);
    void (*master)(uint32_t v);
    /* what the device shows (read with the audio thread held, or a copy it made): the 240 x 240 RGB565 screen, high
     * byte first; the LEDs (bit = button label / key) lit, dim, breathing, mid; draws so far */
    const uint16_t *(*screen)(void);
    uint32_t (*screen_draws)(void);
    uint32_t (*lit_buttons)(void), (*lit_keys)(void), (*dim_buttons)(void), (*dim_keys)(void);
    uint32_t (*breath_buttons)(void), (*breath_keys)(void), (*mid_keys)(void);
    uint32_t (*now_ms)(void);            /* the device's clock */
    uint32_t (*playing)(void);
    void (*test_heavy)(void);            /* (tests: the emulator's heavy song into the tracks) */
    /* the host's transport, from the core's frame `frame` on (frames since boot, as render gives them): playing or
     * not, the tempo, the beat it is at that frame. Once called the device follows it, always: its BPM, its CLOCK
     * and PLAY are the host's (seq.c host_block); the song from where PLAY starts it, beat 0 */
    void (*transport)(uint32_t playing, double bpm, double beats, uint64_t frame);
    /* the tracks' own outputs: bit k = track k into a pair of its own, out of the main mix (fx.c trk_own) */
    void (*outputs)(uint32_t mask);
    /* render with them: out[0], out[1] the main, out[2 + 2k], out[3 + 2k] track k (k < tracks; silent if it is not
     * its own), n frames */
    void (*render_multi)(float *const *out, uint32_t n);
    /* the song as the device stores it (project.c proj_write: its chunks, each its header and data, one after the
     * other): into buf if it fits in cap; the bytes it takes (0: none, not booted). With the audio thread held */
    uint32_t (*song_get)(uint8_t *buf, uint32_t cap);
    /* the song song_get gave (any distribution's: a song of more tracks than this one's is refused) made the music,
     * after boot, with the audio thread held. 0 = loaded, 1 = damaged, 2 = more tracks */
    uint32_t (*song_set)(const uint8_t *buf, uint32_t n);
    void (*test_ticks)(void);            /* (tests: a short dry closed hat on every row of pattern 0, LOOP) */
} fel_core_api_t;

typedef const fel_core_api_t *(*fel_core_api_fn)(void);
#define FEL_CORE_SYMBOL "fel_core_api"

#endif
