/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* Engine table (this build's engines in display order: the indices; their ids: core.h) and the parts' sounds at
 * power-on. */
#include "dsp.c"
#include "shared/phys_dsp.c"   /* the resonators MODAL plays (PHYS's, DaisySP's: MIT) */
/* (Blue Hare) the engines' own memory of a track (a cavern, a ring, a voice's grains), one block a track that every
 * such engine borrows: a track plays one engine at a time (voice.c: the old one's voices fade and go before the new one
 * takes the track), so the block is as big as the largest of this build's engines needs, not their sum. An engine
 * asks for it with its key (eng_slab, after the engines): the first ask of a key the block was not lent to clears it
 * (as at power-on), so a sound starts on silence. 0: the
 * track is no part, or the engine needs more than the block (a build without it): it plays nothing */
enum { SLAB_NONE, SLAB_MODAL, SLAB_ECHOES };
static void *eng_slab(const track_t *t, uint32_t key, uint32_t bytes);
#include "eng_wave.c"           /* WAVE: wavetables (1.5) */
#include "eng_west.c"           /* WEST: complex oscillator, folder, low-pass gate (1.5) */
#include "eng_sid.c"            /* SID: a voice of the C64's sound chip, in its manner (1.5) */
#include "eng_bliss.c"         /* BLISS: FM electric pianos, bells, mallets (1.5) */
#include "eng_modal.c"          /* MODAL: a modal resonator, struck, plucked, bowed (1.5) */
#include "eng_echoes.c"         /* ECHOES: a note and its repeats, each played anew (1.5) */
#ifdef BLUEHARE_NATIVE           /* (the native image's alone: engine_ids.h, core.h BLUEHARE_NATIVE) */
#include "eng_canyon.c"         /* CANYON: ECHOES' repeats, all of them (native) */
#endif

/* the block's size: the largest this build's engines ask for (core.h BLUEHARE_ENG_*) */
#define SLAB_MAX(a, b) ((a) > (b) ? (a) : (b))
#define SLAB_IF(on, sz) ((on) ? (uint32_t)(sz) : 0u)
#define ENG_SLAB_BYTES                                                                                                  \
    SLAB_MAX(SLAB_IF(BLUEHARE_ENG_MODAL, sizeof(md_voice_t) * MD_POLY), SLAB_IF(BLUEHARE_ENG_ECHOES, sizeof(ec_g_t) * EC_POLY * EC_NG))
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

/* (Blue Hare) engine e as track t may play it: one on the last track alone (engine_t.last_track: DUBSIREN) is its
 * substitute anywhere else (DUBSIREN8: the same controls and preset order, so the sound carries over), or itself
 * when the build has none (it then plays as it can: DUBSIREN dry). Every load goes through this: a pick, a preset, a
 * project, an instrument, a user preset */
static uint32_t eng_for_track(const track_t *t, uint32_t e)
{
    uint32_t s;
    if (e >= NENGINES || !ENGINES[e]->last_track || t == &trk[NPART - 1])
        return e;
    s = eng_of_id(EID_SUB[ENGINES[e]->id]);
    return s != ENG_NO ? s : e;
}

/* the engines one can pick (all of the build's), in display order: eng_ok(e), the n-th of them eng_vis(n), e's place
 * among them eng_rank(e), the next / previous one eng_step(e, dir) (wraps). (The table is in display order: these are
 * the index itself) */
static int eng_ok(uint32_t e) { return e < NENGINES; }
static uint32_t eng_vis(uint32_t n) { return n % NENGINES; }
static uint32_t eng_rank(uint32_t e) { return e < NENGINES ? e : 0u; }
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    return (eng_rank(e) + (dir > 0 ? 1u : NENGINES - 1u)) % NENGINES;
}

/* the parts at power-on (engine index, preset). (Blue Hare, 2026-10-16: Milestone 1) a role a track: the pad (WAVE's
 * WARM PAD) and the bass (SID BASS), centred, the keys (BLISS's E.PIANO, left) and the bells (MODAL's KALIMBA, right);
 * with 8, a drone (WEST), a halo of echoes (ECHOES), a bell (MODAL) and a lead (SID SYNC LEAD) after them. An engine the
 * build leaves out: the second pick, else its first engine, preset 0 */
#define TRK_PICK(X, p) {BLUEHARE_ENG_##X ? (uint8_t)ENGI_##X : 0u, BLUEHARE_ENG_##X ? (uint8_t)(p) : 0u}
#define TRK_PICK2(X, p, Y, q)                                                                                          \
    {BLUEHARE_ENG_##X ? (uint8_t)ENGI_##X : BLUEHARE_ENG_##Y ? (uint8_t)ENGI_##Y : 0u,                                  \
     BLUEHARE_ENG_##X ? (uint8_t)(p) : BLUEHARE_ENG_##Y ? (uint8_t)(q) : 0u}
#if NPART == 8
static const uint8_t TRK_DEF[NPART][2] = {TRK_PICK(WAVE, 8), TRK_PICK(SID, 0), TRK_PICK2(BLISS, 0, WAVE, 0),
                                          TRK_PICK2(MODAL, 10, WEST, 0), TRK_PICK2(WEST, 6, WAVE, 0),
                                          TRK_PICK2(ECHOES, 23, CANYON, 23), TRK_PICK2(MODAL, 2, WEST, 0), TRK_PICK(SID, 2)};
static const int8_t TRK_PAN[NPART] = {0, 0, -24, 24, -16, 16, -8, 8};
#else
static const uint8_t TRK_DEF[NPART][2] = {TRK_PICK(WAVE, 8), TRK_PICK(SID, 0), TRK_PICK2(BLISS, 0, WAVE, 0),
                                          TRK_PICK2(MODAL, 10, WEST, 0)};
static const int8_t TRK_PAN[NPART] = {0, 0, -24, 24};
#endif
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
