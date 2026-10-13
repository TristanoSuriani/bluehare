/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* DARK (1.5, FOUNDRY): sub-bass from sines (Fel-Tracker's own). A sine at the note with SUB1 (an octave down) and SUB2
 * (two down) under it, their sum through a sine folder: FOLD how far it folds (0: clean sines, the chest's weight;
 * up: grit and harmonics over the sub), FENV how much more each note's start folds (a bite, then the round body).
 * PUNCH starts the pitch up to 2 octaves above and lets it fall over PDEC (the 808's thud, a kick from a bass);
 * DRIVE saturates the end, TONE a low-pass over it.
 * Voice state: ph[0] the sine, ph[1] / ph[2] the subs' (at a half and a quarter of its rate), s[0] the fold envelope
 * (Q15), s[1] the punch (Q15), s[2] TONE's filter, s[3] s[4] the DC blocker. */
static void dark_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;               /* (from 0: the subs line up, every note the same thud) */
    v->s[0] = v->s[1] = 32767;
}

static void dark_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t s1 = p[P_E0] * 258, s2 = p[P_E1] * 258, fe = v->s[0], pu = v->s[1], lp = v->s[2], g, drive, kt, x, y, f;
    uint32_t i, ph = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], inc;
    int32_t x1 = v->s[3], y1 = v->s[4];
    fe -= (fe >> 4) + (fe > 0);                       /* the fold's bite: ~25 ms */
    pu -= pu / (1 + p[P_E5] / 3) + (pu > 0);          /* PDEC: the punch falls over ~1 .. ~40 blocks */
    inc = cents_inc(clamp(m->pitch16 + ((p[P_E4] * 3 * pu) >> 13), 0, 2047), 0, m->fine);   /* PUNCH: up to +24 st */
    g = 256 + p[P_E2] * 12 + ((p[P_E3] * 12 * fe) >> 15);   /* the fold's gain, Q8 (1 .. ~13) */
    drive = 256 + p[P_E6] * 10;
    kt = clamp(1200 + p[P_E7] * 250, 1200, 32767);
    for (i = 0; i < n; i++) {
        x = (sine_i(ph) + ((sine_i(ph1) * s1) >> 15) + ((sine_i(ph2) * s2) >> 15)) >> 1;   /* (+-~24k) */
        f = (x * g) >> 8;                             /* the folder: sin(x g), x 1 a quarter turn at full */
        y = sine_i((uint32_t)f << 15);              /* (32768: a quarter turn) */
        y = softclip((y * drive) >> 8);
        lp += ((y - lp) * kt) >> 15;
        y = lp - x1 + y1 - (y1 >> 10);                /* (the DC: the folder's, the drive's) */
        x1 = lp;
        y1 = y;
        out[i] += voice_amp(clamp(y, -32767, 32767), m, i) << 1;
        ph += inc;
        ph1 += inc >> 1;
        ph2 += inc >> 2;
    }
    v->ph[0] = ph;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[3] = x1;
    v->s[4] = y1;
    v->s[0] = fe;
    v->s[1] = pu;
    v->s[2] = lp;
}

/* {SUB1, SUB2, FOLD, FENV, PUNCH, PDEC, DRIVE, TONE} */
static const preset_t DARK_PRESETS[] = {
    {"SUB WEIGHT", {90, 40, 0, 10, 0, 40, 10, 60}, {0, 60, 110, 25}, 0, 1, FX(0, 0, 0, 5)},
    {"CHEST RATTLE", {110, 80, 20, 40, 0, 40, 40, 70}, {0, 60, 110, 25}, 0, 1, FX(10, 0, 0, 5)},
    {"808 THUD", {60, 0, 6, 30, 60, 40, 30, 60}, {0, 90, 60, 50}, 0, 1, FX(10, 0, 0, 5)},
    {"FOLD SUB", {80, 50, 50, 30, 0, 40, 30, 80}, {0, 60, 110, 25}, 0, 1, FX(20, 0, 0, 5)},
    {"GRIT BASS", {70, 30, 90, 50, 10, 30, 70, 90}, {0, 60, 100, 20}, 0, 1, FX(30, 0, 0, 5)},
    {"DEEP DRONE", {127, 127, 10, 0, 0, 40, 20, 40}, {60, 0, 127, 90}, 0, 0, FX(0, 20, 10, 40)},
    {"KICK BASS", {50, 0, 20, 60, 90, 25, 50, 70}, {0, 70, 40, 30}, 0, 1, FX(10, 0, 0, 5)},
    {"BLACK SINE", {0, 0, 0, 0, 0, 40, 0, 50}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 0, 5)},
    {"GROWL SUB", {100, 60, 70, 80, 20, 40, 90, 100}, {0, 50, 90, 20}, 0, 1, FX(40, 0, 0, 5)},
    {"BOOM", {100, 100, 30, 50, 120, 60, 60, 60}, {0, 100, 0, 80}, 0, 1, FX(20, 0, 10, 20)},
};

static const engine_t ENG_DARK = {
    .name = "DARK",
    .id = EID_DARK,
    .cc = 0x37,                  /* CC74 FOLD, CC71 DRIVE */
    .cats = "BBBBBPBBBD",
    .page_title = {"SUB", "PUNCH"},
    .edit = {
        {"SUB1", F_PCT, 0, 127, 90, 0, 0},
        {"SUB2", F_PCT, 0, 127, 40, 0, 0},
        {"FOLD", F_PCT, 0, 127, 0, 0, 0},
        {"FENV", F_PCT, 0, 127, 10, 0, 0},
        {"PUNCH", F_PCT, 0, 127, 0, 0, 0},
        {"PDEC", F_PCT, 0, 127, 40, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 10, 0, 0},
        {"TONE", F_PCT, 0, 127, 60, 0, 0},
    },
    .presets = DARK_PRESETS,
    .npresets = NELEM(DARK_PRESETS),
    .note_on = dark_note_on,
    .render = dark_render,
    .knob = {P_E0, P_E2, P_E4, P_E6},
};
