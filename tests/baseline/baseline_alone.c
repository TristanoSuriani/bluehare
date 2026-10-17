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
#include "song.c"                                     /* (the song's format: the baseline's, 2026-10-17) */
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

/* the song as one piece in RAM (the format's sink and source: a host's state, a file) */
static uint8_t ram_song[8192];
static uint32_t ram_w, ram_r;
static void ram_sink(pw_t *w, const void *p, uint32_t n)
{
    if (ram_w + n > sizeof ram_song) {
        w->err = 1;
        return;
    }
    memcpy(ram_song + ram_w, p, n);
    ram_w += n;
}
static void ram_src(pr_t *r, void *p, uint32_t n)
{
    if (ram_r + n > ram_w) {
        r->err = 1;
        memset(p, 0, n);
        return;
    }
    memcpy(p, ram_song + ram_r, n);
    ram_r += n;
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
#if BLUEHARE_ENG_WAVE
    {   /* (2026-10-17) WAVE's stepped banks (TBL's second half): 8-bit samples, the nearest frame (POS between two plays
         * the nearer, bit for bit), where the smooth bank crossfades */
        static int32_t o[4][256];
        uint32_t k, bits = 0, stepped_same, smooth_same;
        track_t *t = &trk[0];
        for (k = 0; k < WT_NBANK * WT_NFRAME * WT_FRAME_LEN; k++)
            bits |= (uint32_t)wt_read(WT_DATA + k / 1024u * 1024u, 0, (k % 1024u) << 22, 1) & 0xFFu;
        patch_engine(t, ENGI_WAVE);
        t->p[P_E4] = 127;                               /* (the filter open, no resonance, no drive, no crush) */
        t->p[P_E5] = t->p[P_E6] = t->p[P_E7] = t->p[P_E2] = t->p[P_E3] = 0;
        for (k = 0; k < 4u; k++) {
            voice_t v;
            vmod_t m = {0};
            memset(&v, 0, sizeof v);
            m.inc = 5000000u;                           /* (~56 Hz: the top level) */
            m.pitch16 = v.pitch16 = 33 * 16;
            m.amp0 = m.amp1 = 32767;
            m.cutoff = 0;
            m.shape = 64 << 8;
            t->p[P_E0] = (int16_t)(k < 2u ? WT_NBANK : 0);   /* BASIC8, then BASIC */
            t->p[P_E1] = (int16_t)(k & 1u ? 73 : 70);       /* POS 70 (frame 3.86), 73 (frame 4.02) */
            wave_note_on(t, &v);
            memset(o[k], 0, sizeof o[k]);
            wave_render(t, &v, o[k], 256, &m);
        }
        stepped_same = !memcmp(o[0], o[1], sizeof o[0]);
        smooth_same = !memcmp(o[2], o[3], sizeof o[2]);
        check("WAVE stepped (BASIC8 ..): 8-bit samples, the nearest frame bit for bit; the smooth bank crossfades",
              !bits && stepped_same && !smooth_same && !strcmp(N_WT_BANK[WT_NBANK], "BASIC8") &&
              ENG_WAVE.edit[0].max == 2 * WT_NBANK - 1);
    }
#endif
#if BLUEHARE_ENG_VOSIM && BLUEHARE_ENG_MODAL
    {   /* (2026-10-17) played: VOSIM's SHP raises the formants (brighter); MODAL struck
         * harder rings brighter. Brightness: the first difference's energy over the signal's */
        static int32_t o[2][2048];
        double br[4];
        uint32_t k, j;
        track_t *t = &trk[0];
        for (k = 0; k < 4u; k++) {
            voice_t *v = &t->v[0];
            vmod_t m = {0};
            double e = 0, d = 0;
            memset(t->v, 0, sizeof t->v);
            patch_engine(t, k < 2u ? ENGI_VOSIM : ENGI_MODAL);
            if (k < 2u)
                t->p[P_E7] = 0;                         /* (VOSIM: no breath) */
            m.inc = pitch_inc(48 * 16);
            m.pitch16 = v->pitch16 = 48 * 16;
            m.amp0 = m.amp1 = 32767;
            m.shape = (k == 1u ? 96 : 64) << 8;         /* VOSIM: SHP up half its range */
            v->vel = (uint8_t)(k == 3u ? 127 : 40);     /* MODAL: soft, then hard */
            v->gate = v->active = 1;
            ENGINES[t->eng_req]->note_on(t, v);
            memset(o[k & 1u], 0, sizeof o[0]);
            for (j = 0; j < 2048u; j += CTL)
                ENGINES[t->eng_req]->render(t, v, o[k & 1u] + j, CTL, &m);
            for (j = 1; j < 2048u; j++) {
                e += (double)o[k & 1u][j] * o[k & 1u][j];
                d += (double)(o[k & 1u][j] - o[k & 1u][j - 1u]) * (o[k & 1u][j] - o[k & 1u][j - 1u]);
            }
            br[k] = e > 0 ? d / e : 0;
        }
        check("VOSIM: SHP moves the formants up (brighter); MODAL: a hard strike rings brighter than a soft one",
              br[1] > br[0] * 1.3 && br[3] > br[2] * 1.3);
    }
#endif
    midi_enqueue(0x09u | 0x90u << 8 | 64u << 16 | 100u << 24, 1u);   /* MIDI IN: the baseline's ring, channel 1 */
    i = (uint32_t)run(200);
    midi_enqueue(0x08u | 0x80u << 8 | 64u << 16, 1u);
    run(400);
    check("MIDI IN through the baseline's ring (midi_in.c) plays and releases", i > 300u && mi_r == mi_w);
    check("the player interface (player.c): GEN, its four roles by name, its pieces",
          player() && !strcmp(player()->name, "GEN") && player()->nroles == 4u && player_role("BELL") == 2 &&
          player_role("BASS") == 1 && player()->npieces == GEN_NPRESETS && !strcmp(player()->piece_name(0), "GARDEN"));
    gen_load_piece(0);                                  /* GARDEN: its sounds through os_load_sound */
    gen.seed = 7u;
    gen.req = 1;
    i = (uint32_t)run(3000);
    check("a GEN piece loaded (its sounds, key, tempo) and played on the baseline alone",
          gen.on && i > 300u && gen.s.played[0] > 0u && song.g[G_BPM] == GEN_PRESETS[0].bpm &&
          trk[1].eng_req == ENGI_WAVE);   /* (GARDEN's bass: WAVE's SUB BASS) */
    {   /* the song: written into RAM, the music changed, read back: as it was (song.c, nothing of the OS's store) */
        pw_t w;
        pr_t r;
        char name[PROJ_NAME_LEN + 1u];
        int16_t atk = trk[2].p[P_ATK], bpm;
        uint32_t eng = trk[1].eng_req;
        song.g[G_BPM] = 97;
        lay.on[1] = 1;
        lay.tr[1] = -12;
        bpm = song.g[G_BPM];
        memset(&w, 0, sizeof w);
        w.h = 2166136261u;
        w.sink = ram_sink;
        song_write(&w, "RAM SONG");
        song.g[G_BPM] = 140;
        trk[2].p[P_ATK] = (int16_t)(atk + 7);
        lay.on[1] = 0;
        lay.tr[1] = 0;
        memset(&r, 0, sizeof r);
        r.h = 2166136261u;
        r.src = ram_src;
        ram_r = 0;
        snprintf(msg, sizeof msg, "the song written and read back on the baseline alone (song.c, %u bytes in RAM)", ram_w);
        check(msg, !w.err && song_read(&r, 1, name) == 0 && song.g[G_BPM] == bpm && trk[2].p[P_ATK] == atk &&
              trk[1].eng_req == eng && lay.on[1] == 1 && lay.tr[1] == -12 && !strcmp(name, "RAM SONG"));
    }
    printf(bad ? "baseline: %d FAILED\n" : "baseline: all passed\n", bad);
    return bad ? 1 : 0;
}
