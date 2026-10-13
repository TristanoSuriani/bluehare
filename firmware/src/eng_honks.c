/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* HONKS (1.5, FOUNDRY): brass barks and sax stabs (Fel-Tracker's own). A reed: a saw pressed into clipping (BRIGHT:
 * from mellow to a blare), with BRTH (breath noise) in it, through two resonances at formants F1 (~200 Hz .. 1.4 kHz)
 * and F2 (~700 Hz .. 4 kHz), RES how narrow (from a horn to a nasal honk). Each note scoops up into its pitch from
 * SCOOP semitones under it (~60 ms: the player's bend into the note); GROWL flutters it at ~35 Hz (a growled sax).
 * DRIVE at the end. The formants follow the key a quarter.
 * Voice state: ph[0] the reed, ph[1] GROWL's flutter, s[0] s[1] / s[2] s[3] the two formants, s[4] the scoop (Q15),
 * s[5] the noise, s[6] s[7] the DC blocker. */
static void honks_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[4] = 32767;
    if (!v->s[5])
        v->s[5] = (int32_t)(0x2545F491u ^ (v->age * 2654435761u));
}

static void honks_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t i, ph = v->ph[0], gph = v->ph[1], nz = (uint32_t)v->s[5], inc, ginc;
    int32_t sc = v->s[4], a1 = v->s[0], a2 = v->s[1], b1 = v->s[2], b2 = v->s[3], kt = (m->pitch16 - 60 * 16) * 4;
    int32_t bright = 256 + p[P_E6] * 16, brth = p[P_E3] * 64, gr = p[P_E5], drive = 256 + p[P_E7] * 8, k, y, z;
    int32_t x1 = v->s[6], y1 = v->s[7];
    tsvf_t f1, f2;
    sc -= (sc >> 3) + (sc > 0);                       /* SCOOP: ~60 ms up into the note */
    inc = cents_inc(clamp(m->pitch16 - ((p[P_E4] * 16 * sc) >> 15), 0, 2047), 0, m->fine);
    ginc = 3300000u;                                  /* GROWL: ~35 Hz */
    k = 8192 - p[P_E2] * 7800 / 127;                  /* RES: damping 2 .. ~0.1 */
    tsvf_coef_k(&f1, 46 * 256 + p[P_E0] * 213 + kt, k);   /* F1: note ~55 .. ~89 */
    tsvf_coef_k(&f2, 67 * 256 + p[P_E1] * 217 + kt, k);   /* F2: note ~77 .. ~108 */
    for (i = 0; i < n; i++) {
        int32_t x, v1, v2, v3;
        uint32_t pi = ph;
        if (gr)                                       /* GROWL: the reed's phase shaken */
            pi += (uint32_t)(sine_i(gph) * gr) << 4;
        x = softclip((osc_saw(pi, inc) * bright) >> 9);   /* the reed, pressed (BRIGHT) */
        nz ^= nz << 13;
        nz ^= nz >> 17;
        nz ^= nz << 5;
        x += ((int32_t)(int16_t)(nz >> 16) * brth) >> 13;   /* BRTH */
        x >>= 1;
        v3 = x - a2;                                  /* F1: the trapezoidal SVF's band-pass (tsvf) */
        v1 = (f1.a1 * a1 + f1.a2 * v3) >> 13;
        v2 = a2 + ((f1.a2 * a1 + f1.a3 * v3) >> 13);
        a1 = clamp(2 * v1 - a1, -150000, 150000);
        a2 = clamp(2 * v2 - a2, -150000, 150000);
        y = v1;
        v3 = x - b2;                                  /* F2 */
        v1 = (f2.a1 * b1 + f2.a2 * v3) >> 13;
        v2 = b2 + ((f2.a2 * b1 + f2.a3 * v3) >> 13);
        b1 = clamp(2 * v1 - b1, -150000, 150000);
        b2 = clamp(2 * v2 - b2, -150000, 150000);
        y += (v1 * 3) >> 2;
        y = softclip((y * drive) >> 5);             /* (x 8: the formants pass a part of the reed) */
        z = y - x1 + y1 - (y1 >> 10);                 /* (the DC: the drive clips lopsided) */
        x1 = y;
        y1 = z;
        out[i] += voice_amp(clamp(z, -32767, 32767), m, i) << 1;
        ph += inc;
        gph += ginc;
    }
    v->ph[0] = ph;
    v->ph[1] = gph;
    v->s[0] = a1;
    v->s[1] = a2;
    v->s[2] = b1;
    v->s[3] = b2;
    v->s[4] = sc;
    v->s[5] = (int32_t)nz;
    v->s[6] = x1;
    v->s[7] = y1;
}

/* {F1, F2, RES, BRTH, SCOOP, GROWL, BRIGHT, DRIVE} */
static const preset_t HONKS_PRESETS[] = {
    {"BRASS BARK", {70, 60, 60, 10, 30, 0, 90, 40}, {0, 40, 90, 20}, 0, 1, FX(20, 10, 30, 30)},
    {"SAX STAB", {55, 75, 80, 30, 50, 20, 70, 30}, {0, 35, 60, 20}, 0, 1, FX(10, 10, 35, 30)},
    {"HORN SECT", {65, 55, 50, 10, 20, 0, 80, 20}, {5, 50, 100, 30}, 0, 0, FX(0, 40, 30, 40)},
    {"GROWL SAX", {50, 70, 90, 40, 60, 80, 80, 60}, {0, 40, 100, 25}, 0, 1, FX(30, 0, 30, 30)},
    {"NASAL HONK", {80, 100, 115, 10, 20, 0, 60, 40}, {0, 30, 80, 20}, 0, 1, FX(20, 0, 30, 20)},
    {"TROMBONE", {40, 40, 50, 15, 60, 0, 60, 20}, {10, 40, 110, 30}, 0, 1, FX(0, 20, 30, 40)},
    {"WAR HORN", {35, 45, 70, 20, 90, 30, 110, 90}, {20, 0, 127, 60}, 0, 1, FX(40, 20, 30, 60)},
    {"DIRTY BRASS", {60, 65, 75, 20, 30, 40, 120, 110}, {0, 40, 90, 20}, 0, 1, FX(50, 0, 30, 20)},
    {"FOGHORN", {20, 30, 60, 30, 40, 20, 50, 60}, {40, 0, 127, 100}, 0, 1, FX(20, 30, 30, 80)},
    {"BARITONE", {45, 60, 70, 35, 40, 30, 75, 40}, {0, 40, 100, 30}, 0, 1, FX(10, 10, 30, 30)},
    {"SIREN HORN", {50, 60, 90, 10, 127, 0, 100, 70}, {0, 0, 127, 40}, 0, 1, FX(30, 0, 30, 40)},
    {"BRASS WALL", {60, 55, 55, 15, 10, 10, 90, 50}, {10, 50, 110, 40}, 0, 0, FX(10, 40, 30, 50)},
    {"RAW SAX", {55, 80, 85, 50, 40, 40, 90, 80}, {0, 40, 100, 25}, 0, 1, FX(30, 0, 35, 30)},
    {"TUBA BASS", {25, 35, 60, 20, 30, 10, 60, 40}, {0, 50, 100, 20}, 0, 1, FX(10, 0, 0, 10)},
    {"HONK STAB", {75, 70, 100, 10, 30, 0, 110, 90}, {0, 30, 0, 20}, 0, 0, FX(30, 0, 40, 30)},
    {"SMOKY TENOR", {45, 65, 70, 60, 50, 20, 60, 30}, {5, 40, 110, 30}, 0, 1, FX(0, 10, 35, 40)},
    {"BIG BAND", {65, 60, 55, 15, 25, 0, 85, 30}, {5, 50, 110, 35}, 0, 0, FX(0, 30, 30, 50)},
    {"BARI GROWL", {40, 55, 80, 40, 50, 90, 80, 70}, {0, 40, 100, 25}, 0, 1, FX(20, 0, 30, 30)},
    {"FOG SIGNAL", {15, 25, 70, 25, 60, 10, 60, 80}, {30, 0, 127, 90}, 0, 1, FX(20, 20, 30, 80)},
    {"SQUAWK", {90, 110, 110, 20, 40, 30, 110, 90}, {0, 30, 60, 20}, 0, 1, FX(20, 0, 30, 30)},
    {"MUTED TRUMP", {80, 95, 90, 5, 15, 0, 50, 20}, {0, 40, 90, 25}, 0, 1, FX(0, 10, 35, 35)},
    {"BRASS BASS", {35, 40, 60, 10, 20, 0, 90, 60}, {0, 50, 100, 20}, 0, 1, FX(20, 0, 0, 5)},
    {"HORN SWELL", {55, 60, 60, 20, 10, 0, 70, 30}, {60, 0, 127, 80}, 0, 0, FX(0, 40, 30, 60)},
    {"HONK RIFF", {70, 75, 95, 15, 35, 20, 100, 70}, {0, 30, 80, 20}, 0, 1, FX(20, 0, 30, 30)},
};

static const engine_t ENG_HONKS = {
    .name = "HONKS",
    .id = EID_HONKS,
    .cc = 0x13,                  /* CC74 F1, CC71 RES */
    .cats = "LLKLLLLLPBLKLBKLKLPLLBPK",
    .page_title = {"FORMANT", "REED"},
    .edit = {
        {"F1", F_PCT, 0, 127, 60, 0, 0},
        {"F2", F_PCT, 0, 127, 60, 0, 0},
        {"RES", F_PCT, 0, 127, 60, 0, 0},
        {"BRTH", F_PCT, 0, 127, 10, 0, 0},
        {"SCOOP", F_PCT, 0, 127, 30, 0, 0},
        {"GROWL", F_PCT, 0, 127, 0, 0, 0},
        {"BRIGHT", F_PCT, 0, 127, 80, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 30, 0, 0},
    },
    .presets = HONKS_PRESETS,
    .npresets = NELEM(HONKS_PRESETS),
    .note_on = honks_note_on,
    .render = honks_render,
    .knob = {P_E0, P_E1, P_E2, P_E5},
    .keep = 0x03,                /* F1's states (a retrigger: no click) */
};
