/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* GUTS (1.5, FOUNDRY): filthy acid from a folder (Fel-Tracker's own). WAVE (SAW, TRI, SQR, band-limited) through a sine
 * folder (FOLD: from a soft curve to harmonics stacked on harmonics) made lopsided by ASYM (an offset before the fold:
 * the even harmonics, the wet, burping tone; the DC taken out after), then a resonant low-pass (CUT, RES) that every
 * note opens by ENV and lets close over DEC, the acid's squelch, and DRIVE at the end. Velocity 100 and up: the note
 * opens the filter further (an accent).
 * Voice state: ph[0] the oscillator, s[0] s[1] the filter, s[2] its envelope (Q15), s[3] s[4] the DC blocker,
 * s[5] the accent (0 / 1). */
static const char *const N_GUTS_WAVE[] = {"SAW", "TRI", "SQR", 0};

static void guts_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[2] = 32767;
    v->s[5] = v->vel >= 100u;
}

static void guts_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    os2_t *os = os2_of(t, v);                         /* (Fel-Tracker 2026-10: the shaper's 2x, dsp.c) */
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0] % 3u, i, ph = v->ph[0], inc = m->inc;
    int32_t g = 256 + p[P_E1] * 14, bias = p[P_E2] * 120, fe = v->s[2], ic1 = v->s[0], ic2 = v->s[1];
    int32_t x1 = v->s[3], y1 = v->s[4], drive = 256 + p[P_E7] * 9, tau = 2 + p[P_E6] * p[P_E6] / 10, cut;
    tsvf_t flt;
    fe -= fe / tau + (fe > 0);                        /* DEC: ~2 blocks .. ~1.2 s */
    cut = (p[P_E3] << 8) + ((p[P_E5] * (v->s[5] ? 260 : 190) * fe) >> 15) + m->cutoff + (m->pitch16 - 60 * 16) * 4;
    tsvf_coef(&flt, cut, p[P_E4]);
    for (i = 0; i < n; i++) {
        int32_t x = wave == 0u ? osc_saw(ph, inc) : wave == 1u ? osc_tri(ph) : osc_pulse(ph, inc, 0x80000000u), y;
        if (os2_off)
            y = sine_i((uint32_t)((((x >> 1) * g) >> 8) + bias) << 15);   /* the lopsided fold (32768: a quarter turn) */
        else {                                        /* (Fel-Tracker 2026-10: the fold at twice the rate, dsp.c os2) */
            int32_t xa, xb;
            os2_up(os, x, &xa, &xb);
            y = os2_down(os, sine_i((uint32_t)((((xa >> 1) * g) >> 8) + bias) << 15),
                         sine_i((uint32_t)((((xb >> 1) * g) >> 8) + bias) << 15));
        }
        x = y - x1 + y1 - (y1 >> 9);                  /* (the DC: ASYM's) */
        x1 = y;
        y1 = x;
        y = tsvf_lp(&flt, x >> 1, &ic1, &ic2);
        y = softclip((y * drive) >> 7);
        out[i] += voice_amp(y, m, i) << 1;
        ph += inc;
    }
    v->ph[0] = ph;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = fe;
    v->s[3] = x1;
    v->s[4] = y1;
}

/* {WAVE, FOLD, ASYM, CUT, RES, ENV, DEC, DRIVE}; WAVE 0 SAW 1 TRI 2 SQR */
static const preset_t GUTS_PRESETS[] = {
    {"GUT ACID", {0, 30, 30, 30, 100, 90, 40, 40}, {0, 0, 127, 10}, 0, 1, FX(20, 0, 25, 10)},
    {"WET BURP", {1, 60, 80, 35, 90, 80, 35, 50}, {0, 0, 127, 10}, 0, 1, FX(20, 0, 20, 10)},
    {"FILTH LINE", {0, 80, 50, 25, 110, 100, 30, 80}, {0, 0, 127, 10}, 0, 1, FX(40, 0, 25, 10)},
    {"BILE", {2, 50, 100, 40, 95, 70, 50, 60}, {0, 0, 127, 15}, 0, 1, FX(30, 0, 20, 10)},
    {"INTESTINE", {1, 100, 20, 30, 80, 110, 25, 90}, {0, 0, 127, 10}, 0, 1, FX(40, 0, 20, 10)},
    {"SLIME BASS", {0, 40, 60, 20, 70, 60, 70, 40}, {0, 60, 80, 15}, 0, 1, FX(20, 0, 0, 5)},
    {"ROTTEN LEAD", {2, 70, 70, 60, 85, 60, 60, 70}, {0, 40, 110, 25}, 0, 1, FX(30, 10, 35, 20)},
    {"SQUELCH GUT", {0, 20, 40, 15, 120, 127, 20, 50}, {0, 0, 127, 10}, 0, 1, FX(20, 0, 30, 10)},
    {"SICK PAD", {1, 50, 60, 50, 60, 30, 100, 30}, {70, 90, 115, 90}, 0, 0, FX(0, 60, 25, 70)},
    {"PUKE STAB", {0, 90, 90, 30, 100, 110, 15, 100}, {0, 30, 0, 20}, 0, 0, FX(30, 0, 40, 25)},
    {"ACID REFLUX", {0, 50, 60, 25, 115, 110, 30, 70}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 25, 10)},
    {"GRISTLE", {2, 90, 40, 35, 90, 80, 40, 100}, {0, 0, 127, 10}, 0, 1, FX(40, 0, 20, 10)},
    {"OFFAL LEAD", {1, 60, 90, 50, 90, 70, 60, 60}, {0, 40, 110, 25}, 0, 1, FX(30, 10, 35, 20)},
    {"GUT PUNCH", {0, 110, 110, 20, 100, 127, 10, 110}, {0, 0, 127, 10}, 0, 1, FX(40, 0, 20, 10)},
    {"BLOATED", {1, 30, 30, 20, 60, 40, 90, 30}, {0, 60, 90, 15}, 0, 1, FX(20, 0, 0, 5)},
    {"VISCERA", {2, 127, 80, 40, 110, 100, 20, 127}, {0, 0, 127, 10}, 0, 1, FX(50, 0, 20, 10)},
    {"STOMACH ACID", {0, 40, 40, 20, 118, 120, 35, 60}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 25, 10)},
    {"CHUNKY", {1, 100, 60, 30, 80, 90, 40, 90}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 25, 10)},
    {"MUCUS", {2, 30, 90, 35, 100, 60, 60, 40}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 25, 10)},
    {"GUT LEAD", {0, 70, 50, 60, 95, 60, 70, 70}, {0, 40, 110, 25}, 0, 1, FX(30, 10, 35, 20)},
    {"PARASITE", {2, 80, 110, 25, 120, 110, 20, 80}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 25, 10)},
    {"SEWAGE", {1, 60, 40, 20, 90, 50, 100, 60}, {0, 60, 90, 15}, 0, 1, FX(30, 0, 25, 10)},
    {"RUPTURE", {0, 127, 127, 30, 110, 127, 15, 127}, {0, 0, 127, 10}, 0, 1, FX(40, 0, 20, 10)},
    {"GUT PAD", {1, 40, 50, 45, 70, 30, 110, 30}, {70, 90, 115, 90}, 0, 0, FX(0, 60, 25, 70)},
};

static const engine_t ENG_GUTS = {
    .name = "GUTS",
    .id = EID_GUTS,
    .cc = 0x45,                  /* CC74 CUT, CC71 RES */
    .cats = "BBBBBBLBPFBBLBBBBBBLBBBP",
    .page_title = {"FOLD", "FILTER"},
    .edit = {
        {"WAVE", F_ENUM, 0, 2, 0, N_GUTS_WAVE, 0},
        {"FOLD", F_PCT, 0, 127, 30, 0, 0},
        {"ASYM", F_PCT, 0, 127, 30, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 30, 0, 0},
        {"RES", F_PCT, 0, 127, 100, 0, 0},
        {"ENV", F_PCT, 0, 127, 90, 0, 0},
        {"DEC", F_PCT, 0, 127, 40, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 40, 0, 0},
    },
    .presets = GUTS_PRESETS,
    .npresets = NELEM(GUTS_PRESETS),
    .note_on = guts_note_on,
    .render = guts_render,
    .knob = {P_E1, P_E3, P_E4, P_E5},
    .keep = 0x03,                /* the filter (a slide: no click) */
};
