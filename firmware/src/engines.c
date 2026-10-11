/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol) and the parts' sounds at
 * power-on. */
#include "dsp.c"
#include "eng_analog.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#if FELUCCA_SAMPLES
#include "eng_sample.c"         /* SAMPLE, and the sample material GRAIN and SLICE play */
#endif
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_wheel.c"
#if FELUCCA_GRAIN
#include "eng_grain.c"
#endif
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#include "eng_drum.c"           /* DRUM: the 8-lane kit (drum_voice.c) */
#include "eng_noise.c"
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0) */
#include "eng_wave.c"           /* WAVE: wavetables (1.5) */
#include "eng_byte.c"           /* BYTE: bytebeat formulas (1.5) */
#include "fm4_convert.c"        /* DIGITAL's tables, and its sounds -> FM6 */
#if FELUCCA_FM4
#include "eng_digital.c"        /* DIGITAL: four-operator FM (retired; FELUCCA_FM4=1 builds it) */
#endif
#if FELUCCA_SLICE
#include "eng_slice.c"
#endif

/* an engine number this build leaves out (core.h: FELUCCA_SAMPLE, FELUCCA_GRAIN, FELUCCA_SLICE): never selectable
 * (eng_ok), no presets, silent; named "-" as DIGITAL's (the editor leaves such names out). A track never keeps one:
 * whatever brings its sound loads another engine's instead (eng_sub, ui.c eng_fix) */
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
#if FELUCCA_FM4
    &ENG_DIGITAL,                /* 1 (ENGI_DIGITAL) */
#else
    &ENG_FM4_GONE,               /* 1: reserved (DIGITAL, retired: its sounds convert to FM6, fm4_convert.c) */
#endif
    &ENG_PHASE,                  /* 2 */
    &ENG_LOFI,                   /* 3 */
#if FELUCCA_SAMPLE
    &ENG_SAMPLE,                 /* 4 (ENGI_SAMPLE) */
#else
    &ENG_NONE,                   /* 4: SAMPLE, not built (FELUCCA_SAMPLE=1 builds it) */
#endif
    &ENG_FORMANT,                /* 5 VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
    &ENG_TRIO,                   /* 6 */
    &ENG_WHEEL,                  /* 7 */
#if FELUCCA_GRAIN
    &ENG_GRAIN,                  /* 8 (ENGI_GRAIN) */
#else
    &ENG_NONE,                   /* 8: GRAIN, not built (FELUCCA_GRAIN=1) */
#endif
    &ENG_PHYS,                   /* 9 (ENGI_PHYS) */
    &ENG_DRUM,                   /* 10 (ENGI_DRUM) */
    &ENG_NOISE,                  /* 11 */
    &ENG_FM6,                    /* 12 (ENGI_FM6) */
#if FELUCCA_SLICE
    &ENG_SLICE,                  /* 13 (ENGI_SLICE) */
#else
    &ENG_NONE,                   /* 13: SLICE, not built (FELUCCA_SLICE=1) */
#endif
    &ENG_WAVE,                   /* 14 (ENGI_WAVE, 1.5) */
    &ENG_BYTE,                   /* 15 (ENGI_BYTE, 1.5) */
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* the sample source a track's sound plays that has no data (SAMPLE's SET, GRAIN's SRC, SLICE's SRC: an empty or
 * invalid user slot, a set or PIANO this build lacks): its name (it plays a sine: eng_sample.c smp_sine), 0 = none;
 * *code a small number for it (1.., per engine and source) so the UI says it once (ui_input.c sample_notice) */
static uint8_t snd_said[NTRK];   /* per track: the code last said, 0 = none (a project load clears them: said again) */
static const char *snd_missing(const track_t *t, uint32_t *code)
{
#if FELUCCA_SAMPLES
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL;
#if FELUCCA_SLICE
    if (e == &ENG_SLICE) {
        si = slc_src_of(t->p);
        *code = 64u + si;
        return slc_get(si) ? 0 : N_SLC_SRC[si];
    }
#endif
#if FELUCCA_SAMPLE
    if (e == &ENG_SAMPLE) {
        *code = 1u + si % 32u;
        return smp_set_missing(si) ? SMP_ALL_NAMES[si] : 0;
    }
#endif
#if FELUCCA_GRAIN
    if (e == &ENG_GRAIN) {
        *code = 32u + si % 32u;
        return smp_set_missing(si) ? SMP_ALL_NAMES[si] : 0;
    }
#endif
#endif
    (void)t; (void)code;
    return 0;
}

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the EDIT layer's keys, the editor's list):
 * engine indices, never DIGITAL's reserved 1 (with FELUCCA_FM4 it follows FM6). The indices stay as they are (the
 * stores and the protocol hold them); only this table orders them */
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {
    0,                           /* ANALOG */
    12,                          /* FM6 */
#if FELUCCA_FM4
    1,                           /* DIGITAL */
#endif
    14,                          /* WAVE */
    2, 3,                        /* PHASE LOFI */
#if FELUCCA_SAMPLE
    4,                           /* SAMPLE */
#endif
    5, 6, 7,                     /* VOICE TRIO WHEEL */
#if FELUCCA_GRAIN
    8,                           /* GRAIN */
#endif
    9,                           /* PHYS */
    11,                          /* NOISE */
    15,                          /* BYTE */
#if FELUCCA_SLICE
    13,                          /* SLICE */
#endif
    10,                          /* DRUM */
};

/* the engines one can pick (engine 1 only with FELUCCA_FM4; 4, 8, 13 only when built), in ENGINE_ORDER: eng_ok(e),
 * the n-th of them eng_vis(n), e's place among them eng_rank(e), the next / previous one eng_step(e, dir) (wraps) */
static int eng_ok(uint32_t e) { return e < NENGINES && ENGINES[e] != &ENG_NONE && (FELUCCA_FM4 || e != ENGI_DIGITAL); }
/* the engine a sound of engine e loads as: e itself when it can be picked; a left-out engine's sounds load as the
 * nearest one built (SAMPLE: FM6's first preset, the electric piano; GRAIN: WAVE, pads; SLICE: DRUM, a kit); DIGITAL
 * stays (its sounds convert to FM6 with a patch of their own: fm4_convert.c) */
static uint32_t eng_sub(uint32_t e)
{
    if (e >= NENGINES || eng_ok(e) || e == ENGI_DIGITAL)
        return e;
    return e == ENGI_SLICE ? ENGI_DRUM : e == ENGI_GRAIN ? ENGI_WAVE : e == ENGI_SAMPLE ? ENGI_FM6 : ENGI_ANALOG;
}
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % NENG_SHOWN]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    if (!eng_ok(e))
        e = ENGI_FM6;                            /* (DIGITAL without FELUCCA_FM4: its sounds play as FM6) */
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
                                                                       * FM6 PAD (was DIGITAL PAD), LOFI PULSE LD, DRUM KIT */
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
