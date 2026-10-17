/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-17) The baseline on its own: the sound (firmware/baseline: engines, voices, modulation, effects, GEN, note
 * input and the layers, MIDI's meaning) built with only its own directory on the include path, nothing of Blue Hare OS
 * (firmware/os) or the FM-1 port (firmware/fm1), and run: a note on every engine, MIDI in through the baseline's ring,
 * a GEN piece loaded and played. What the baseline asks of the instrument around it are the hooks below (Blue Hare
 * OS: keyboard.c's three, ui.c's os_load_sound), here a host without keys that loads at once. A build that fails here means a baseline file reached into a layer above
 *   cc -Ibuild/gen -Ifirmware/baseline tests/baseline/baseline_alone.c -lm   (run_tests.sh) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define __attribute__(x)
#define memset bluehare_memset
#define memcpy bluehare_memcpy
#define memcmp bluehare_memcmp
#include "bluehare_tables.h"
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp
static struct { volatile uint32_t notes, buttons; } fm1_in;   /* (the port's key state: nothing here reads it) */
#include "core.h"
#include "engines.c"
#include "params.c"
#include "mod.c"
#include "voice.c"
#include "fx.c"
#include "seq.c"
/* the instrument's keys (Blue Hare OS keyboard.c): a host without keys */
static void os_keys_block(void) {}
static int os_keys_hold_track(uint32_t t) { (void)t; return 0; }
static int os_keys_hold_note(uint32_t t, uint32_t n) { (void)t; (void)n; return 0; }
/* a sound loaded the host's way (the OS: undo, sync, the IRQ off): here at once (patch.c) */
static void os_load_sound(uint32_t track, uint32_t engine, uint32_t preset)
{
    track_t *t = &trk[track % NTRK];
    if (t->eng_req != engine)
        patch_engine(t, engine);
    patch_preset(t, preset);
}

static int bad;
static void check(const char *what, int ok)
{
    printf("baseline: %-80s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static int32_t buf[2 * CTL];
static int32_t run(uint32_t blocks)                   /* the peak of the mix over n blocks */
{
    int32_t pk = 0;
    uint32_t i;
    while (blocks--) {
        mix_block(buf, CTL);
        for (i = 0; i < 2u * CTL; i++)
            pk = buf[i] > pk ? buf[i] : -buf[i] > pk ? -buf[i] : pk;
    }
    return pk;
}

int main(void)
{
    uint32_t e, i, loud = 0;
    char msg[160];
    memset(&song, 0, sizeof song);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    song.master_q12 = 4096;
    trk_mix_defaults();
    for (e = 0; e < NENGINES; e++) {                    /* a note on each engine, the first preset */
        track_t *t = &trk[0];
        memset(t->v, 0, sizeof t->v);
        for (i = 0; i < P_E0; i++)
            t->p[i] = TP[i].def;
        patch_engine(t, e);                             /* (the engine and its first preset: the baseline's loader) */
        run(64);                                        /* (the engine switch's fade) */
        input_on(t, 60, 100);
        loud += run(200) > 300;
        input_off(t, 60);
        run(400);
    }
    snprintf(msg, sizeof msg, "built with firmware/baseline alone; every engine sounds a note (%u of %u)", loud, NENGINES);
    check(msg, loud == NENGINES);
    midi_enqueue(0x09u | 0x90u << 8 | 64u << 16 | 100u << 24, 1u);   /* MIDI IN: the baseline's ring, channel 1 */
    i = (uint32_t)run(200);
    midi_enqueue(0x08u | 0x80u << 8 | 64u << 16, 1u);
    run(400);
    check("MIDI IN through the baseline's ring (midi_in.c) plays and releases", i > 300u && mi_r == mi_w);
    gen_load_piece(0);                                  /* GARDEN: its sounds through os_load_sound */
    gen.seed = 7u;
    gen.req = 1;
    i = (uint32_t)run(3000);
    check("a GEN piece loaded (its sounds, key, tempo) and played on the baseline alone",
          gen.on && i > 300u && gen.s.played[0] > 0u && song.g[G_BPM] == GEN_PRESETS[0].bpm &&
          trk[1].eng_req == (BLUEHARE_ENG_SH101 ? ENGI_SH101 : trk[1].eng_req));
    printf(bad ? "baseline: %d FAILED\n" : "baseline: all passed\n", bad);
    return bad ? 1 : 0;
}
