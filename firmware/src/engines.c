/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol) and the parts' sounds at
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

/* a retired engine number (1 DIGITAL, 4 SAMPLE, 8 GRAIN, 13 SLICE: removed in 1.5): never selectable (eng_ok), no
 * presets, silent, named "-" (the editor leaves such names out). A track never keeps one: a project naming one is
 * refused, a user preset of one is not loaded, a song's instrument of one is dropped, an engine change to one plays
 * ANALOG */
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

/* the editor protocol, user presets and projects store these indices: append, never reorder */
static const engine_t *const ENGINES[NENGINES] = {
    &ENG_ANALOG,                 /* 0 */
    &ENG_NONE,                   /* 1: retired (DIGITAL) */
    &ENG_PHASE,                  /* 2 */
    &ENG_LOFI,                   /* 3 */
    &ENG_NONE,                   /* 4: retired (SAMPLE) */
    &ENG_FORMANT,                /* 5 VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
    &ENG_TRIO,                   /* 6 */
    &ENG_WHEEL,                  /* 7 */
    &ENG_NONE,                   /* 8: retired (GRAIN) */
    &ENG_PHYS,                   /* 9 (ENGI_PHYS) */
    &ENG_DRUM,                   /* 10 (ENGI_DRUM) */
    &ENG_NOISE,                  /* 11 */
    &ENG_FM6,                    /* 12 (ENGI_FM6) */
    &ENG_NONE,                   /* 13: retired (SLICE) */
    &ENG_WAVE,                   /* 14 (ENGI_WAVE, 1.5) */
    &ENG_BYTE,                   /* 15 (ENGI_BYTE, 1.5) */
    &ENG_WEST,                   /* 16 (ENGI_WEST, 1.5) */
    &ENG_HOOVER,                 /* 17 (ENGI_HOOVER, 1.5) */
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the EDIT layer's keys, the editor's list):
 * engine indices, never a retired one. The indices stay as they are (the stores and the protocol hold them); only
 * this table orders them */
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {
    0,                           /* ANALOG */
    12,                          /* FM6 */
    14,                          /* WAVE */
    16,                          /* WEST */
    17,                          /* HOOVER */
    2, 3,                        /* PHASE LOFI */
    5, 6, 7,                     /* VOICE TRIO WHEEL */
    9,                           /* PHYS */
    11,                          /* NOISE */
    15,                          /* BYTE */
    10,                          /* DRUM */
};

/* the engines one can pick (all but the retired numbers), in ENGINE_ORDER: eng_ok(e), the n-th of them eng_vis(n),
 * e's place among them eng_rank(e), the next / previous one eng_step(e, dir) (wraps) */
static int eng_ok(uint32_t e) { return e < NENGINES && ENGINES[e] != &ENG_NONE; }
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % NENG_SHOWN]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    if (!eng_ok(e))
        e = ENGI_ANALOG;
    for (n = 0; n < NENG_SHOWN && ENGINE_ORDER[n] != e; n++)
        ;
    return n < NENG_SHOWN ? n : 0u;
}
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    return eng_vis((eng_rank(e % NENGINES) + (dir > 0 ? 1u : NENG_SHOWN - 1u)) % NENG_SHOWN);
}


/* the parts at power-on (engine, preset): bass, pad, lead, drums */
static const uint8_t TRK_DEF[NPART][2] = {{0, 4}, {ENGI_FM6, 4}, {3, 0}, {ENGI_DRUM, 0}}; /* ANALOG ACID,
                                                                       * FM6 PAD, LOFI PULSE LD, DRUM KIT */
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
