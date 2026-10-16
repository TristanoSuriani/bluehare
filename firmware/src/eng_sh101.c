/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* SH101 (2026-10, Milestone 1): one oscillator and its mixer into a four-pole low-pass, after the little monosynth of
 * 1982 (Blue Hare's own design: no circuit modelled). The oscillator gives its waves at once, as the original's: a
 * band-limited SAW and PULSE (PW: 50 % down to ~5 %; the SHAPE routings move it, so ENV -> SHAPE or LFO -> SHAPE is
 * the panel's PWM switch), a SUB oscillator locked to it (SUBT: a square an octave down, two octaves down, or a 25 %
 * pulse two octaves down: it flips on the oscillator's own cycles, so it never drifts from it) and white NOISE, each
 * at its level in the mixer. The mix goes into a 24 dB ladder low-pass (four trapezoidal one-poles in a row, the
 * feedback around them solved for the sample: no delay in the loop, so RES tunes with CUT), its input saturating as
 * an OTA's: a louder mix drives it, RES past ~110 sings by itself and the saturation holds it. The input is raised
 * with RES as the passband sinks (half of what a ladder loses: it still thins a little, as the original does). CUT
 * half follows the keyboard; ENV -> FILTER sweeps it (the panel's ENV slider). The oscillator drifts a little, a
 * random walk of ~+-3 cents. The amplitude is the ADSR (the original's one envelope: GATE or ENV).
 * Voice state: ph[0] the oscillator, s[0..3] the ladder's four states, s[4] the oscillator's cycles (the sub's
 * octaves), s[5] the noise, s[6] the drift (1/512 cent). */
static const char *const N_SH_SUBT[] = {"-1 SQ", "-2 SQ", "-2 PU", 0};

static void sh101_note_on(track_t *t, voice_t *v)
{
    v->ph[0] = 0;
    if (t->p[P_E7] > 100)                             /* RES past 100: the ladder pinged, so it rings at once (the */
        v->s[0] = clamp(v->s[0] + (t->p[P_E7] - 100) * 1200, -40000, 40000);   /* resonant kick, the zap) */
    v->s[4] = 0;
    if (!v->s[5])
        v->s[5] = (int32_t)(0x6C8E9CF5u ^ (v->age * 2654435761u));
}

static inline int32_t sh101_pole(int32_t x, int32_t g, int32_t *s)   /* a trapezoidal one-pole low-pass, G Q15 */
{
    int32_t v = ((x - *s) * g) >> 15, y = v + *s;
    *s = clamp(y + v, -40000, 40000);
    return y;
}

static void sh101_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t subt = (uint32_t)p[P_E1] % 3u, ph = v->ph[0], cyc = (uint32_t)v->s[4], inc, sinc, pw32, i;
    int32_t s1 = v->s[0], s2 = v->s[1], s3 = v->s[2], s4 = v->s[3], drift = v->s[6], nz = v->s[5];
    int32_t lsaw = p[P_E2], lpul = p[P_E3], lsub = p[P_E4], lnoi = p[P_E5];
    int32_t pwq, cut, ci, g, g2, g3, a, c1, c2, c3, k, g4, inv, boost;
    pwq = clamp((p[P_E0] << 8) + m->shape - (64 << 8), 0, 127 << 8);   /* PW and the SHAPE routings: PWM */
    pw32 = 0x80000000u - (uint32_t)pwq * 117000u;     /* 50 % .. ~5 % */
    drift += ((int32_t)(noise32(&nz) >> 20) - 2048) >> 3;   /* the drift: a random walk a block, pulled back */
    drift -= drift >> 6;
    inc = cents_inc(m->pitch16, drift >> 9, m->fine);
    sinc = inc >> (subt ? 2u : 1u);
    cut = clamp((p[P_E6] << 8) + m->cutoff + (m->pitch16 - 60 * 16) * 8, 0, 127 << 8);   /* half the keyboard */
    ci = cut >> 8;
    g = LADDER_G[ci];
    if (ci < 127)                                     /* between table points: sweeps without steps */
        g += ((LADDER_G[ci + 1] - g) * (cut & 255)) >> 8;
    g2 = (g * g) >> 15;
    g3 = (g2 * g) >> 15;
    g4 = (g2 * g2) >> 15;
    a = 32768 - g;                                    /* the states' share of the fourth pole's output: */
    c1 = (g3 * a) >> 15;                              /* y4 = G^4 x + c1 s1 + c2 s2 + c3 s3 + (1 - G) s4 */
    c2 = (g2 * a) >> 15;
    c3 = (g * a) >> 15;
    k = p[P_E7] * 18000 / 127;                        /* RES: the feedback, 0 .. ~4.4 (Q12; 4 the edge of singing) */
    inv = (1 << 24) / (4096 + ((k * g4) >> 15));      /* 1 / (1 + k G^4), Q12 */
    boost = 4096 + (k >> 1);                          /* the passband the feedback takes, half given back */
    for (i = 0; i < n; i++) {
        uint32_t o = ph, sph;
        int32_t x, sub, sum, u, y, r;
        ph += inc;
        if (ph < o)
            cyc++;
        sph = subt ? (cyc << 30) | (ph >> 2) : (cyc << 31) | (ph >> 1);   /* (the sub: the oscillator's cycles) */
        sub = osc_pulse(sph, sinc, subt == 2u ? 0x40000000u : 0x80000000u);
        r = (int32_t)(noise32(&nz) >> 16) - 32768;
        x = osc_saw(ph, inc) * lsaw + osc_pulse(ph, inc, pw32) * lpul + sub * lsub + r * lnoi;
        x = (x >> 7) + (r >> 9);                      /* (a wave at 127: +-32k; the four: up to ~4x that. The circuit's
                                                       * hiss, -60 dB: what a filter left singing alone starts from) */
        sum = ((c1 * s1) >> 15) + ((c2 * s2) >> 15) + ((c3 * s3) >> 15) + ((a * s4) >> 15);
        x = ((x >> 2) * boost) >> 10;                 /* (32-bit: x +-2^15 by boost < 2^14) */
        u = (clamp(x - ((k * sum) >> 12), -262143, 262143) * inv) >> 12;
        u = softclip(clamp(u, -65535, 65535));        /* the ladder's input, an OTA's tanh: one wave ~tanh(1) */
        y = sh101_pole(u, g, &s1);
        y = sh101_pole(y, g, &s2);
        y = sh101_pole(y, g, &s3);
        y = sh101_pole(y, g, &s4);
        out[i] += voice_amp(clamp((y * 3) >> 1, -32767, 32767), m, i);   /* (+3.5 dB: the level of the others) */
    }
    v->ph[0] = ph;
    v->s[0] = s1;
    v->s[1] = s2;
    v->s[2] = s3;
    v->s[3] = s4;
    v->s[4] = (int32_t)cyc;
    v->s[5] = nz;
    v->s[6] = drift;
}

/* {PW, SUBT, SAW, PULS, SUB, NOIS, CUT, RES}; SUBT 0 -1 SQ, 1 -2 SQ, 2 -2 PU. RES ~110: the filter sings by itself.
 * fenv: ENV -> FILTER; eshp / lshp: the PWM (ENV, LFO) */
static const preset_t SH101_PRESETS[] = {
    /* basses */
    {"101 BASS", {0, 0, 110, 0, 90, 0, 38, 45}, {0, 45, 40, 15}, 40, 1, FX(5, 0, 0, 5)},
    {"SUB BASS", {0, 1, 60, 0, 127, 0, 42, 10}, {0, 60, 100, 15}, 15, 1, FX(0, 0, 0, 5)},
    {"SQUELCH", {0, 0, 120, 0, 30, 0, 28, 100}, {0, 35, 20, 15}, 55, 1, FX(15, 0, 15, 5)},
    {"PLUCK BASS", {20, 0, 80, 60, 70, 0, 30, 60}, {0, 25, 0, 15}, 50, 1, FX(5, 0, 10, 5)},
    {"DEEP BASS", {0, 2, 0, 110, 100, 0, 45, 20}, {0, 70, 60, 20}, 20, 1, FX(0, 0, 0, 5)},
    {"ACID LINE", {0, 0, 127, 0, 0, 0, 22, 112}, {0, 30, 10, 10}, 60, 1, FX(25, 0, 25, 5)},
    {"PWM BASS", {40, 1, 0, 120, 80, 0, 40, 35}, {0, 50, 60, 15}, 30, 1, FX(5, 10, 0, 5), .lshp = 30, .lrate = 50},
    /* leads */
    {"PWM LEAD", {50, 0, 0, 120, 0, 0, 85, 30}, {0, 40, 110, 30}, 20, 1, FX(0, 20, 30, 25), .lshp = 35, .lrate = 64},
    {"SAW LEAD", {0, 0, 120, 0, 40, 0, 80, 50}, {0, 40, 110, 25}, 25, 1, FX(10, 10, 35, 20)},
    {"SINGING", {0, 0, 90, 0, 0, 0, 60, 118}, {5, 40, 110, 40}, 20, 1, FX(0, 20, 40, 35)},
    {"SYNC-ISH", {90, 0, 60, 110, 0, 0, 70, 70}, {0, 50, 90, 30}, 30, 1, FX(10, 10, 35, 20), .eshp = 50},
    /* keys, plucks, pads */
    {"PLUCK", {10, 0, 100, 40, 30, 0, 45, 50}, {0, 40, 0, 35}, 50, 0, FX(0, 20, 40, 30)},
    {"POLY KEYS", {30, 0, 80, 80, 20, 0, 60, 25}, {2, 60, 60, 40}, 25, 0, FX(0, 40, 25, 35)},
    {"WARM PAD", {40, 1, 90, 70, 50, 10, 55, 20}, {80, 90, 110, 90}, 15, 0, FX(0, 70, 25, 70), .lshp = 30, .lrate = 30},
    {"BRASS", {20, 0, 120, 30, 30, 0, 50, 30}, {25, 60, 90, 30}, 35, 0, FX(0, 30, 20, 35)},
    /* drums, effects */
    {"101 KICK", {0, 0, 0, 0, 0, 0, 30, 127}, {0, 30, 0, 20}, 63, 0, FX(10, 0, 0, 5), .oct = -2},
    {"NOISE HAT", {0, 0, 0, 0, 0, 127, 120, 30}, {0, 12, 0, 8}, 0, 0, FX(0, 0, 10, 10)},
    {"ZAP", {0, 0, 110, 0, 0, 0, 40, 110}, {0, 20, 0, 15}, 63, 0, FX(0, 0, 30, 20)},
    {"WIND", {0, 0, 0, 0, 0, 127, 50, 115}, {90, 0, 127, 100}, 0, 0, FX(0, 40, 30, 70), .lrate = 20},
};

static const engine_t ENG_SH101 = {
    .name = "SH101",
    .id = EID_SH101,
    .cc = 0x78,                  /* CC74 CUT, CC71 RES */
    .cats = "BBBBBBBLLLLUKPKDDFF",
    .page_title = {"VCO", "VCF"},
    .edit = {
        {"PW", F_PCT, 0, 127, 0, 0, 0},
        {"SUBT", F_ENUM, 0, 2, 0, N_SH_SUBT, 0},
        {"SAW", F_PCT, 0, 127, 100, 0, 0},
        {"PULS", F_PCT, 0, 127, 0, 0, 0},
        {"SUB", F_PCT, 0, 127, 40, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 70, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
    },
    .presets = SH101_PRESETS,
    .npresets = NELEM(SH101_PRESETS),
    .note_on = sh101_note_on,
    .render = sh101_render,
    .knob = {P_E6, P_E7, P_E0, P_E4},
    .keep = 0x0F,                /* the ladder (a retrigger, a slide: no click); the noise and the drift go on */
};
