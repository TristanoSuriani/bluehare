/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* Engine table (this build's engines in display order: the indices; their ids: core.h) and the parts' sounds at
 * power-on. */
#include "dsp.c"
#include "shared/drum_map.c"   /* (2026-10-18) the drums' lanes and General MIDI map (KIT's) */
#include "shared/fmp_dsp.c"    /* (2026-10-18) FMPERC's drum voice (KIT's) */
/* (Blue Hare) the engines' own memory of a track (a cavern, a ring, a voice's grains), one block a track that every
 * such engine borrows: a track plays one engine at a time (voice.c: the old one's voices fade and go before the new one
 * takes the track), so the block is as big as the largest of this build's engines needs, not their sum. An engine
 * asks for it with its key (eng_slab, after the engines): the first ask of a key the block was not lent to clears it
 * (as at power-on), so a sound starts on silence. 0: the
 * track is no part, or the engine needs more than the block (a build without it): it plays nothing */
enum { SLAB_NONE, SLAB_MODAL, SLAB_STRINGS, SLAB_SIREN8, SLAB_SANDSTORM };
static void *eng_slab(const track_t *t, uint32_t key, uint32_t bytes);
#include "eng_sid.c"            /* SID: a voice of the C64's sound chip, in its manner (1.5) */
#include "eng_vosim.c"          /* VOSIM: two formants of sin^2 pulse trains (1.5; back from the archive 2026-10-16) */
#include "eng_dubsiren.c"       /* DUBSIREN: the dub siren (back 2026-10-18) */
#include "eng_va.c"             /* VA: the virtual analogue, and the core OBIE, SANDSTORM, HOOVER are flavours of (2026-10-23) */
#include "eng_hoover.c"         /* HOOVER: the rave lead (back on VA's core 2026-10-23) */
#include "eng_sandstorm.c"      /* SANDSTORM: the late-90s trance lead (back 2026-10-18) */
#include "eng_speech.c"         /* SPEECH: LPC words (back 2026-10-18; tools/gen_speech.py) */
#include "eng_onyx.c"           /* ONYX: Kokoro's voices as LPC frames (2026-10-19; tools/gen_onyx_*.py) */
#include "eng_vocoder.c"        /* VOCODER: ONYX's frames shaping a synth (2026-10-19) */
#include "eng_war.c"            /* WAR: an industrial kit (back 2026-10-18) */
#include "eng_linnkit.c"        /* LINNKIT: the 80s drum machine (back 2026-10-18) */
#include "eng_kit.c"            /* KIT: a drum kit (back 2026-10-18) */
#include "eng_rhythm.c"         /* RHYTHM: the 808 and the 909 (2026-10-23) */
#include "eng_vakit.c"          /* VAKIT: synth drums on VA's core (2026-10-23) */
#include "eng_obie.c"           /* OBIE: an analog polysynth after the OB-Xa (2026-10-17) */

/* the block's size: the largest this build's engines ask for (core.h BLUEHARE_ENG_*) */
#define SLAB_MAX(a, b) ((a) > (b) ? (a) : (b))
#define SLAB_IF(on, sz) ((on) ? (uint32_t)(sz) : 0u)
#define ENG_SLAB_BYTES                                                                                                  \
    4u                          /* (MODAL, DUBSIREN8 archived; SANDSTORM on VA's core: 2026-10-23) */
#define ENG_SLAB_WORDS ((ENG_SLAB_BYTES + 3u) / 4u + 1u)
static uint32_t eng_slab_mem[NPART][ENG_SLAB_WORDS] __attribute__((section(".pool")));
static uint8_t eng_slab_key[NPART];
static void *eng_slab(const track_t *t, uint32_t key, uint32_t bytes)
{
    uint32_t k;
    if (t < &trk[0] || t >= &trk[NPART] || bytes > sizeof eng_slab_mem[0])
        return 0;
    k = (uint32_t)(t - trk);
    if (eng_slab_key[k] != key) {
        memset(eng_slab_mem[k], 0, sizeof eng_slab_mem[k]);
        eng_slab_key[k] = (uint8_t)key;
    }
    return eng_slab_mem[k];
}

/* the engine of an id this build has none for (a retired id, or an engine left out): what the editor protocol
 * describes for it (NAMES, DESC: no presets, nothing to edit, named "-"). No track ever plays it */
static void eng_none_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void eng_none_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
#define ED_NONE {"-", F_INT, 0, 0, 0, 0, 0}
static const engine_t ENG_NONE = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {ED_NONE, ED_NONE, ED_NONE, ED_NONE, ED_NONE, ED_NONE, ED_NONE, ED_NONE},
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .knob = {P_ATK, P_DEC, P_SUS, P_REL},
};

/* this build's engines, in the distribution's order (core.h BLUEHARE_DIST_ENGINES: the ENGI_ indices). Only what is
 * listed here is linked */
static const engine_t *const ENGINES[NENGINES] = {
#define X(N) &ENG_##N,
    BLUEHARE_DIST_ENGINES(X)
#undef X
};

/* a track's engine as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* ids <-> indices. eng_id(e): engine e's id. eng_of_id(id): the index of the engine with that id, ENG_NO when the build
 * has none (a retired id, an engine left out, an id past the last) */
#define ENG_NO 0xFFu
#define EID_IDX(X) (BLUEHARE_ENG_##X ? (uint8_t)ENGI_##X : ENG_NO)
static const uint8_t ENG_OF_ID[EID_COUNT] = {
    [0] = ENG_NO, [1] = ENG_NO, [2] = ENG_NO, [3] = ENG_NO, [4] = ENG_NO, [5] = ENG_NO, [6] = ENG_NO, [7] = ENG_NO,
    [8] = ENG_NO, [9] = ENG_NO, [10] = ENG_NO, [11] = ENG_NO, [12] = ENG_NO, [13] = ENG_NO, [27] = ENG_NO,   /* (retired) */
#define ENGINE(N, id, sub) [EID_##N] = EID_IDX(N),
#define ARCHIVED(N, id) [id] = ENG_NO,
#include "engine_ids.h"
#undef ARCHIVED
#undef ENGINE
};
static uint32_t eng_id(uint32_t e) { return ENGINES[eng_idx(e)]->id; }
static uint32_t eng_of_id(uint32_t id) { return id < EID_COUNT ? ENG_OF_ID[id] : ENG_NO; }
/* the engine of id, or ENG_NONE: what the protocol describes for an id (NAMES, DESC) */
static const engine_t *eng_by_id(uint32_t id) { return eng_of_id(id) == ENG_NO ? &ENG_NONE : ENGINES[eng_of_id(id)]; }
/* the engine a stored sound of id loads as: its own when the build has it; an engine left out: its substitute (the
 * one engine_ids.h names, down that chain, else the build's first engine), *sub set; ENG_NO for a retired or unknown id (a clean
 * break: the stores refuse such a sound) */
static const uint8_t EID_SUB[EID_COUNT] = {     /* (engine_ids.h: each engine's substitute) */
#define ENGINE(N, id, sub) [EID_##N] = EID_##sub,
#include "engine_ids.h"
#undef ENGINE
};
/* (Blue Hare, 2026-10) a chain that finds none of the build's: the sound loads as the mid track's power-on engine
 * (track 3), not as the build's first engine */
static uint32_t trk_def_engine(uint32_t i);
static uint32_t eng_load_id(uint32_t id, int *sub)
{
    uint32_t n;
    if (sub)
        *sub = 0;
    if (id >= EID_COUNT || EID_RETIRED(id))
        return ENG_NO;
    for (n = 0; n < EID_COUNT && eng_of_id(id) == ENG_NO; n++)
        id = EID_SUB[id];
    if (sub)
        *sub = n != 0u;
    return n < EID_COUNT ? eng_of_id(id) : trk_def_engine(NPART > 2u ? 2u : 0u);
}

/* (2026-10-23) the tracks each engine may play on (the distribution's TRACK lines: gen_dist.py BLUEHARE_TMASK_*):
 * HARE FM1's track 1 the synths, 2 the kits, 3 the voices. A track past the distribution's TRACKS (a test's 8-track
 * build) takes every engine */
static const uint8_t ENG_TMASK[NENGINES] = {
#define X(N) (uint8_t)BLUEHARE_TMASK_##N,
    BLUEHARE_DIST_ENGINES(X)
#undef X
};
static int eng_on(uint32_t e, uint32_t k)
{
    return e < NENGINES && (k >= BLUEHARE_TRACKS || ((ENG_TMASK[e] >> k) & 1u));
}

/* (Blue Hare) engine e as track t may play it. One the track may not play (2026-10-23): down its substitutes
 * (engine_ids.h) to one it may, else the track's first engine (a kit's sound on the synth track: its first synth).
 * One on the last track alone (engine_t.last_track: DUBSIREN) is its substitute anywhere else, or itself when the
 * build has none (it then plays as it can: DUBSIREN dry). Every load goes through this: a pick, a preset, a
 * project, an instrument, a user preset */
static uint32_t eng_for_track(const track_t *t, uint32_t e)
{
    uint32_t k = t >= &trk[0] && t < &trk[NPART] ? (uint32_t)(t - trk) : 0u, s, n, id;
    if (e >= NENGINES)
        return e;
    if (ENGINES[e]->last_track && t != &trk[NPART - 1]) {
        s = eng_of_id(EID_SUB[ENGINES[e]->id]);
        e = s != ENG_NO ? s : e;
    }
    for (n = 0, id = ENGINES[e]->id; n < EID_COUNT && !eng_on(e, k); n++) {
        id = EID_SUB[id];
        s = eng_of_id(id);
        e = s != ENG_NO ? s : e;
    }
    for (s = 0; !eng_on(e, k) && s < NENGINES; s++)
        e = s;
    return e;
}

/* the engines one can pick on the selected track (2026-10-23: its own list), in display order: eng_sel_ok(e) (eng_ok: any track's), how many
 * eng_shown(), the n-th of them eng_vis(n), e's place among them eng_rank(e), the next / previous one eng_step(e, dir)
 * (wraps) */
static uint32_t eng_sel_trk(void) { return (uint32_t)song.sel % NPART; }
static int eng_ok(uint32_t e) { return e < NENGINES; }   /* (an engine of the build) */
static int eng_sel_ok(uint32_t e) { return eng_on(e, eng_sel_trk()); }
static uint32_t eng_shown(void)
{
    uint32_t e, n = 0;
    for (e = 0; e < NENGINES; e++)
        n += (uint32_t)eng_sel_ok(e);
    return n ? n : 1u;
}
static uint32_t eng_vis(uint32_t n)
{
    uint32_t e, i = 0;
    n %= eng_shown();
    for (e = 0; e < NENGINES; e++)
        if (eng_sel_ok(e) && i++ == n)
            return e;
    return 0;
}
static uint32_t eng_rank(uint32_t e)
{
    uint32_t i, r = 0;
    if (!eng_sel_ok(e))
        return 0;
    for (i = 0; i < e; i++)
        r += (uint32_t)eng_sel_ok(i);
    return r;
}
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    uint32_t n = eng_shown();
    return eng_vis((eng_rank(e) + (dir > 0 ? 1u : n - 1u)) % n);
}

/* the parts at power-on (engine index, preset). (2026-10-23) HARE FM1's three: the synth track VA's WARM PAD, the
 * kits' KIT, the voices' ONYX (OBEY); 4 or 8 (test builds): then SID's bass, VOSIM, again round. An engine the build
 * leaves out: the second pick, else its first engine, preset 0 */
#define TRK_PICK(X, p) {BLUEHARE_ENG_##X ? (uint8_t)ENGI_##X : 0u, BLUEHARE_ENG_##X ? (uint8_t)(p) : 0u}
#define TRK_PICK2(X, p, Y, q)                                                                                          \
    {BLUEHARE_ENG_##X ? (uint8_t)ENGI_##X : BLUEHARE_ENG_##Y ? (uint8_t)ENGI_##Y : 0u,                                  \
     BLUEHARE_ENG_##X ? (uint8_t)(p) : BLUEHARE_ENG_##Y ? (uint8_t)(q) : 0u}
#define TRK_DEF3 TRK_PICK2(VA, 7, OBIE, 3), TRK_PICK2(KIT, 0, LINNKIT, 0), TRK_PICK2(ONYX, 0, SPEECH, 0)
#if NPART == 8
static const uint8_t TRK_DEF[NPART][2] = {TRK_DEF3, TRK_PICK(SID, 0), TRK_PICK2(VOSIM, 0, SPEECH, 0), TRK_DEF3};
static const int8_t TRK_PAN[NPART] = {0, 0, 0, -24, 24, -16, 16, 8};
#elif NPART == 4
static const uint8_t TRK_DEF[NPART][2] = {TRK_DEF3, TRK_PICK(SID, 0)};
static const int8_t TRK_PAN[NPART] = {0, 0, 0, 0};
#else
static const uint8_t TRK_DEF[NPART][2] = {TRK_DEF3};
static const int8_t TRK_PAN[NPART] = {0, 0, 0};
#endif
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
