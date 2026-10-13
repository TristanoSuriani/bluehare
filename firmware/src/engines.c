/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md */
/* Engine table (this build's engines in display order: the indices; their ids: core.h) and the parts' sounds at
 * power-on. */
#include "dsp.c"
#include "eng_analog.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_wheel.c"
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#include "eng_drum.c"           /* DRUM: the 8-lane kit (drum_voice.c) */
#include "eng_noise.c"
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0) */
#include "eng_wave.c"           /* WAVE: wavetables (1.5) */
#include "eng_byte.c"           /* BYTE: bytebeat formulas (1.5) */
#include "eng_west.c"           /* WEST: complex oscillator, folder, low-pass gate (1.5) */
#include "eng_hoover.c"         /* HOOVER: PWM saws, sub, bend: the rave lead (1.5) */
#include "eng_speech.c"         /* SPEECH: LPC words (1.5; tools/gen_speech.py) */
#include "eng_fmperc.c"         /* FMPERC: two-operator FM percussion (1.5) */
#include "eng_karplus.c"        /* KARPLUS: a plucked string, Karplus-Strong (1.5) */
#include "eng_vosim.c"          /* VOSIM: formant pulse trains (1.5) */
#include "eng_particle.c"       /* PARTICLE: a cloud of synthesized grains (1.5) */
#include "eng_chaos.c"          /* CHAOS: chaotic systems as oscillators (1.5) */
#include "eng_sid.c"            /* SID: a voice of the C64's sound chip, in its manner (1.5) */
#include "eng_dubsiren.c"       /* DUBSIREN: the sound-system siren, its own tape echo (1.5) */
#include "eng_dubsiren8.c"      /* DUBSIREN8: its 8-bit cousin, every track (1.5) */
#include "eng_dubchord.c"       /* DUBCHORD: the dub-techno chord, a whole chord in one voice (1.5) */
#include "eng_fmkeys.c"         /* FMKEYS: FM electric pianos, bells, mallets (1.5) */
#include "eng_wasp.c"           /* WASP: two oscillators, a gritty multimode filter (1.5) */
#include "eng_modal.c"          /* MODAL: a modal resonator, struck, plucked, bowed (1.5) */
#include "eng_vector.c"         /* VECTOR: four sources, a point moving among them (1.5) */
#include "eng_echoes.c"         /* ECHOES: a note and its repeats, each played anew (1.5) */
#include "eng_glitch.c"         /* GLITCH: a kit of micro-sounds (1.5) */
#include "eng_kit.c"            /* KIT: a drum kit of FMPERC's voices on one part (1.5) */
#include "eng_breaks.c"         /* BREAKS: a synthesized breakbeat player (1.5) */
#include "eng_risers.c"         /* RISERS: build-ups and landings, timed to the song (1.5) */

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

/* this build's engines, in the distribution's order (core.h FELUCCA_DIST_ENGINES: the ENGI_ indices). Only what is
 * listed here is linked */
static const engine_t *const ENGINES[NENGINES] = {
#define X(N) &ENG_##N,
    FELUCCA_DIST_ENGINES(X)
#undef X
};

/* a track's engine as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* ids <-> indices. eng_id(e): engine e's id. eng_of_id(id): the index of the engine with that id, ENG_NO when the build
 * has none (a retired id, an engine left out, an id past the last) */
#define ENG_NO 0xFFu
#define EID_IDX(X) (FELUCCA_ENG_##X ? (uint8_t)ENGI_##X : ENG_NO)
static const uint8_t ENG_OF_ID[EID_COUNT] = {
    [1] = ENG_NO, [4] = ENG_NO, [8] = ENG_NO, [13] = ENG_NO, [27] = ENG_NO,   /* (retired; 27: ACID, Fel-Tracker) */
#define ENGINE(N, id, sub) [EID_##N] = EID_IDX(N),
#include "engine_ids.h"
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
    return n < EID_COUNT ? eng_of_id(id) : 0u;
}

/* (Fel-Tracker) engine e as track t may play it: one on the last track alone (engine_t.last_track: DUBSIREN) is its
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

/* the parts at power-on (engine index, preset): bass, pad, lead, drums (ANALOG ACID, FM6 PAD, LOFI PULSE LD, DRUM
 * KIT; without ANALOG the bass is SID BASS, without FM6 the pad VOSIM's FORMANT PAD, without LOFI the lead SID SYNC
 * LEAD, without DRUM the drums KIT's FM KIT); an engine the build leaves out: its first engine, preset 0 */
#define TRK_PICK(X, p) {FELUCCA_ENG_##X ? (uint8_t)ENGI_##X : 0u, FELUCCA_ENG_##X ? (uint8_t)(p) : 0u}
#define TRK_PICK2(X, p, Y, q)                                                                                          \
    {FELUCCA_ENG_##X ? (uint8_t)ENGI_##X : FELUCCA_ENG_##Y ? (uint8_t)ENGI_##Y : 0u,                                  \
     FELUCCA_ENG_##X ? (uint8_t)(p) : FELUCCA_ENG_##Y ? (uint8_t)(q) : 0u}
static const uint8_t TRK_DEF[NPART][2] = {TRK_PICK2(ANALOG, 4, SID, 0), TRK_PICK2(FM6, 4, VOSIM, 13), TRK_PICK2(LOFI, 0, SID, 2),
                                          TRK_PICK2(DRUM, 0, KIT, 0)
#if NPART == 8                                  /* (Fel-Tracker, TRACKS 8) 5 .. 8: chords, a pluck, a break, the siren */
                                          , TRK_PICK2(DUBCHORD, 0, WAVE, 0), TRK_PICK(WEST, 14), TRK_PICK2(BREAKS, 0, DRUM, 0),
                                          TRK_PICK2(DUBSIREN, 0, HOOVER, 5)   /* (no siren engine: HOOVER's SIREN) */
#endif
};
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
