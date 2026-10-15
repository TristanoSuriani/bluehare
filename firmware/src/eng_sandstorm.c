/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* SANDSTORM (1.5, WAREHOUSE): the late-90s trance lead, named after Darude's track of 1999 (Fel-Tracker's own: no
 * samples, nothing of the record). Five band-limited saws, SPRD apart (0 .. 50 cents, the outer two the widest),
 * into two resonant low-passes in a row (24 dB, CUT and RES, the key half tracked) that every note opens by ENV and
 * lets close over DEC: the pluck. GATE (OFF, 1/8, 1/16, 1/32 at the song's tempo) plucks it again on every step
 * while the key is held, its level chopped short each step: the riff's machine-gun repeat. DIVE: let go, the pitch
 * falls, up to two octaves over ~400 ms (the drop at a phrase's end: a long release lets it be heard). DRIVE at the
 * end. The ADSR shapes the level.
 * Voice state: the track's block (engines.c eng_slab: the saws' phases, the filters'); s[0] the pluck (Q15), s[1]
 * samples into the gate's step, s[2] the dive (1/16 semitone, Q8), s[3] the step's chop (Q15). */
#define SS_POLY 8
typedef struct {
    uint32_t ph[5];
    int32_t f[4];                /* the two filters' integrators */
} ss_voice_t;
static uint32_t beat_samples(void);                   /* (fx.c) */
static const char *const N_SS_GATE[] = {"OFF", "1/8", "1/16", "1/32", 0};
static const int8_t SS_DET[5] = {-64, -26, 0, 26, 64};   /* the saws' place in the spread (64: all of SPRD) */

static ss_voice_t *ss_voice(track_t *t, voice_t *v)
{
    ss_voice_t *a = eng_slab(t, SLAB_SANDSTORM, sizeof(ss_voice_t) * SS_POLY);
    uint32_t i = (uint32_t)(v - t->v);
    return a && i < SS_POLY ? &a[i] : 0;
}

static void sandstorm_note_on(track_t *t, voice_t *v)
{
    ss_voice_t *s = ss_voice(t, v);
    uint32_t k, r = 0x9E3779B9u ^ (v->age * 2654435761u);
    if (s) {
        for (k = 0; k < 5u; k++) {                    /* (each saw somewhere in its cycle: no flam, no phasing start) */
            r ^= r << 13;
            r ^= r >> 17;
            r ^= r << 5;
            s->ph[k] = r;
        }
        memset(s->f, 0, sizeof s->f);
    }
    v->s[0] = 32767;
    v->s[1] = 0;
    v->s[2] = 0;
    v->s[3] = 32767;
}

static void sandstorm_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    os2_t *os = os2_of(t, v);                         /* (Fel-Tracker 2026-10: the shaper's 2x, dsp.c) */
    const int16_t *p = t->p;
    ss_voice_t *s = ss_voice(t, v);
    int32_t pluck = v->s[0], into = v->s[1], dive = v->s[2], chop = v->s[3], k, cut, drive = p[P_E7];
    uint32_t inc[5], i, step = 0;
    tsvf_t flt;
    if (!s)
        return;
    if (p[P_E5]) {                                    /* GATE: a step of 1/8, 1/16, 1/32 */
        static const uint8_t DEN[4] = {1, 2, 4, 8};
        step = beat_samples() / DEN[p[P_E5] & 3];
        step = step < 64u ? 64u : step;
    }
    if (!v->gate && p[P_E6])                          /* DIVE: let go, the pitch falls (~400 ms to its depth) */
        dive += (p[P_E6] * 24 * 16 * 256 / 127) * (int32_t)n / 17640;
    dive = dive > 24 * 16 * 256 ? 24 * 16 * 256 : dive;
    for (k = 0; k < 5; k++)
        inc[k] = cents_inc(m->pitch16 - (dive >> 8), SS_DET[k] * p[P_E0] * 50 / (64 * 127), m->fine);
    /* the pluck: ENV of the cutoff, closing over DEC (~15 ms .. ~2 s) */
    cut = (p[P_E1] << 8) + m->cutoff + (v->pitch16 - 60 * 16) * 4 + ((pluck >> 7) * p[P_E3] >> 1);
    tsvf_coef(&flt, cut, p[P_E2]);
    pluck -= pluck * (int32_t)n / (64 + p[P_E4] * p[P_E4] * 6);   /* (32767 x 32: no 64 bits) */
    for (i = 0; i < n; i++) {
        int32_t x = 0, y;
        if (step && ++into >= (int32_t)step) {        /* a step of the gate: plucked again, opened again */
            into = 0;
            pluck = 32767;
            chop = 0;
        }
        if (step) {                                   /* the chop: open for the step's first half, closing after */
            int32_t tg = into < (int32_t)step / 2 ? 32767 : 2000;
            chop += (tg - chop) >> 6;
        }
        for (k = 0; k < 5; k++) {
            x += osc_saw(s->ph[k], inc[k]);
            s->ph[k] += inc[k];
        }
        x = (x * 6554) >> 15;                         /* (a fifth each) */
        y = tsvf_lp(&flt, x >> 1, &s->f[0], &s->f[1]);
        y = tsvf_lp(&flt, y, &s->f[2], &s->f[3]);
        if (drive && os2_off)
            y = softclip(y * (64 + drive) >> 6);
        else if (drive) {                             /* (Fel-Tracker 2026-10: the drive at twice the rate, dsp.c os2) */
            int32_t ya, yb;
            os2_up(os, y, &ya, &yb);
            y = os2_down(os, softclip(ya * (64 + drive) >> 6), softclip(yb * (64 + drive) >> 6));
        }
        else
            y = soft_knee(y, 16000);
        if (step)
            y = (y * chop) >> 15;
        out[i] += voice_amp(clamp(y << 1, -32767, 32767), m, i) << 1;
    }
    v->s[0] = pluck;
    v->s[1] = into;
    v->s[2] = dive;
    v->s[3] = chop;
}

/* {SPRD, CUT, RES, ENV, DEC, GATE, DIVE, DRIVE} */
static const preset_t SANDSTORM_PRESETS[] = {
    {"LEAD", {40, 50, 40, 80, 50, 0, 0, 30}, {0, 40, 110, 30}, 0, 0, FX(0, 20, 30, 40)},
    {"STAB", {30, 40, 50, 100, 30, 0, 0, 40}, {0, 55, 0, 30}, 0, 0, FX(0, 20, 25, 45)},
    {"GATE 16", {40, 50, 40, 80, 40, 2, 0, 30}, {0, 40, 120, 30}, 0, 0, FX(0, 20, 30, 40)},
    {"GATE 32", {40, 55, 40, 80, 30, 3, 0, 30}, {0, 40, 120, 30}, 0, 0, FX(0, 20, 30, 40)},
    {"GATE 8", {40, 45, 40, 80, 60, 1, 0, 30}, {0, 40, 120, 30}, 0, 0, FX(0, 20, 30, 40)},
    {"DIVE", {40, 55, 40, 70, 50, 0, 90, 30}, {0, 40, 110, 95}, 0, 0, FX(0, 20, 30, 40)},
    {"ANTHEM", {70, 60, 30, 60, 70, 0, 0, 20}, {10, 60, 110, 50}, 0, 0, FX(0, 30, 30, 60)},
    {"SUPERSAW", {90, 80, 20, 30, 80, 0, 0, 10}, {5, 60, 120, 50}, 0, 0, FX(0, 30, 25, 55)},
    {"DUST STORM", {50, 45, 55, 90, 40, 2, 40, 45}, {0, 40, 115, 45}, 0, 0, FX(0, 20, 35, 45)},
    {"ACID LEAD", {20, 30, 90, 100, 40, 0, 0, 60}, {0, 40, 100, 30}, 0, 0, FX(0, 10, 25, 30)},
    {"PLUCK", {30, 30, 40, 110, 20, 0, 0, 20}, {0, 25, 0, 20}, 0, 0, FX(0, 20, 30, 40)},
    {"TRANCE GATE", {60, 60, 35, 70, 50, 2, 0, 25}, {0, 40, 127, 30}, 0, 0, FX(0, 25, 30, 50)},
    {"HARD LEAD", {50, 60, 50, 70, 50, 0, 0, 90}, {0, 40, 110, 30}, 0, 0, FX(0, 15, 25, 35)},
    {"SOFT LEAD", {40, 35, 20, 40, 60, 0, 0, 0}, {5, 50, 110, 40}, 0, 0, FX(0, 25, 30, 45)},
    {"FALLING", {30, 60, 40, 60, 60, 0, 127, 40}, {0, 40, 110, 105}, 0, 0, FX(0, 20, 35, 50)},
    {"FILTER RISE", {40, 10, 60, 120, 110, 0, 0, 30}, {0, 80, 110, 40}, 0, 0, FX(0, 20, 30, 45)},
    {"STUTTER", {40, 50, 45, 90, 20, 3, 0, 40}, {0, 40, 120, 30}, 0, 0, FX(0, 20, 25, 40)},
    {"BIG ROOM", {80, 70, 25, 50, 70, 1, 0, 35}, {0, 50, 115, 50}, 0, 0, FX(0, 30, 30, 70)},
    {"FAST DIVE", {40, 55, 40, 70, 40, 0, 127, 30}, {0, 40, 110, 80}, 0, 0, FX(0, 20, 30, 40)},
    {"PULSE GATE", {30, 50, 50, 90, 30, 2, 30, 35}, {0, 40, 120, 35}, 0, 0, FX(0, 20, 30, 40)},
    {"DRY LEAD", {40, 50, 40, 80, 50, 0, 0, 30}, {0, 40, 110, 30}, 0, 0, FX(0, 0, 0, 0)},
    {"WIDE LEAD", {100, 55, 35, 70, 60, 0, 0, 25}, {0, 45, 110, 40}, 0, 0, FX(0, 40, 30, 45)},
    {"SCREAMER", {50, 70, 80, 80, 40, 0, 0, 100}, {0, 40, 110, 30}, 0, 0, FX(0, 15, 25, 35)},
    {"ENDLESS", {60, 50, 30, 60, 127, 2, 0, 30}, {0, 60, 127, 80}, 0, 0, FX(0, 25, 35, 55)},
};

static const engine_t ENG_SANDSTORM = {
    .name = "SANDSTRM",          /* (8 characters at most on the device) */
    .id = EID_SANDSTORM,
    .cc = 0x23,                  /* CC74 CUT, CC71 RES */
    .cats = "LLLLLLLLLLPLLLLFLLLLLLLL",
    .page_title = {"SAWS", "STORM"},
    .edit = {
        {"SPRD", F_PCT, 0, 127, 40, 0, 0},
        {"CUT", F_PCT, 0, 127, 50, 0, 0},
        {"RES", F_PCT, 0, 127, 40, 0, 0},
        {"ENV", F_PCT, 0, 127, 80, 0, 0},
        {"DEC", F_PCT, 0, 127, 50, 0, 0},
        {"GATE", F_ENUM, 0, 3, 0, N_SS_GATE, 0},
        {"DIVE", F_PCT, 0, 127, 0, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 30, 0, 0},
    },
    .presets = SANDSTORM_PRESETS,
    .npresets = NELEM(SANDSTORM_PRESETS),
    .poly = SS_POLY,
    .note_on = sandstorm_note_on,
    .render = sandstorm_render,
    .knob = {P_E1, P_E3, P_E5, P_E6},
};
