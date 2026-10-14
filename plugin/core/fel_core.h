/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The plugin's core: one distribution of Fel-Tracker, the native image's build of it (FELUCCA_NATIVE: 16 voices a
 * track, the native reverbs, the native engines in a NATIVE distribution), as a shared library that exports one thing,
 * fel_core_api(): this table. The firmware keeps its state in globals, so an instance is a library loaded on its own
 * (plugin/core/fel_loader.c: a private copy each, its globals its own). Every call but the ones marked is the
 * audio thread's: the device runs as the audio asks for it (its clock follows the frames rendered, as in the native
 * image and the browser), so offline renders run faster than real time.
 *   plugin/build_core.sh -> build/plugin/cores/fel-core-<dist>.dylib (.so on Linux) */
#ifndef FEL_CORE_H
#define FEL_CORE_H
#include <stdint.h>

#define FEL_CORE_ABI 3u                  /* raised when the table changes: a core of another ABI is refused */

typedef struct {
    uint32_t abi;                        /* FEL_CORE_ABI */
    const char *dist;                    /* "IDM NATIVE", "FOUNDRY", .. (the distribution's tag) */
    const char *version;                 /* "v0.10.0 IDM NATIVE" */
    uint32_t sample_rate;                /* 44100: the core's own; the plugin resamples */
    uint32_t tracks;                     /* 8 or 4 */
    /* 1 the first time it is called in this image, 0 after: an image already an instance's (the loader's check that
     * the library it opened is a fresh one; the host's main thread) */
    uint32_t (*claim)(void);
    /* the flash (1 MiB NOR: the song, the user presets, the settings). Before boot: erase it, or fill it (a saved
     * state). After: writes counts the saves, so a state can be taken when it changed */
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
    /* the music written to the flash now, as the autosave writes it (project.c autosave_write), if it changed since
     * it was last: what a saved state then holds (the device itself waits until it is quiet). 1 = written */
    uint32_t (*save_now)(void);
    void (*test_ticks)(void);            /* (tests: a short dry closed hat on every row of pattern 0, LOOP) */
} fel_core_api_t;

typedef const fel_core_api_t *(*fel_core_api_fn)(void);
#define FEL_CORE_SYMBOL "fel_core_api"

#endif
