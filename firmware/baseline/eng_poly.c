/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* POLY (2026-10-17): an eight-voice analog polysynth after the Oberheim OB-Xa of 1981 (Blue Hare's own design, in
 * float on the FPU: no circuit modelled, no code of anyone's). Each voice: two oscillators, band-limited (polyBLEP),
 * WAVE SAW, PULSE (PW: 50 % down to ~5 %; the SHAPE routings move it: PWM) or both; the second INTV semitones away
 * and DTN apart (0 .. ~25 cents: the thickness). WAVE SYNC: the second a saw hard-synced to the first, INTV its
 * pitch over it, the SHAPE routings sweeping it (ENV -> SHAPE: the sync lead), its restart band-limited too. Then the
 * filter, 12 dB (a state-variable low-pass: the open, warm OB sound) or 24 dB (a four-pole ladder, its feedback solved
 * for the sample), both zero-delay, the input saturating (a rational tanh), RES towards self-oscillation; CUT half
 * follows the keyboard, the velocity opens it (-1.5 .. +0.5 octave around 100: the original had none; MODAL showed
 * what a touch does). SLOP: the analog's spread, per voice: each oscillator a little off (up to ~6 cents) and the filter
 * (up to ~3 semitones), set as the voice starts, and a slow drift on top: a chord breathes, no two notes the same.
 * Voice state: ph[0] / ph[1] the oscillators, s[0..3] the filter (float bits), s[4] the voice's offsets (bytes: osc
 * 1, osc 2, filter), s[5] the noise, s[6] the drift (1/512 cent), s[7] the synced oscillator's sample not yet out
 * (float bits: its restart's polyBLEP corrects the sample before it) */
#include "fastmath.h"                          /* (the float voice) */
static const char *const N_PO_WAVE[] = {"SAW", "PULSE", "SAW+P", "SYNC", 0};
static const char *const N_PO_FILT[] = {"12 dB", "24 dB", 0};

static inline float po_f(int32_t x)
{
    union { int32_t i; float f; } u;
    u.i = x;
    return u.f;
}
static inline int32_t po_i(float x)
{
    union { int32_t i; float f; } u;
    u.f = x;
    return u.i;
}
/* polyBLEP: the step's residual at phase t (cycles) for a phase step dt */
static inline float po_blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float po_saw(float t, float dt) { return 2.0f * t - 1.0f - po_blep(t, dt); }
static inline float po_pulse(float t, float dt, float pw)
{
    float t2 = t + pw;
    t2 -= t2 >= 1.0f ? 1.0f : 0.0f;
    return 0.5f * (po_saw(t, dt) - po_saw(t2, dt));
}
static inline float po_sat(float x)                   /* tanh's shape, rational: x (27 + x^2) / (27 + 9 x^2), |x| <= 3 */
{
    float x2;
    x = fm_clampf(x, -3.0f, 3.0f);
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static void poly_note_on(track_t *t, voice_t *v)
{
    int32_t nz = v->s[5] ? v->s[5] : (int32_t)(0x6C8E9CF5u ^ (v->age * 2654435761u)), k;
    uint32_t r;
    (void)t;
    v->ph[0] = 0;
    v->ph[1] = 0x40000000u;                           /* (a quarter cycle apart: no flam at the start) */
    for (k = 0; k < 4; k++)
        v->s[k] = 0;
    r = noise32(&nz);                                 /* SLOP: this voice's own offsets, -127 .. 127 each */
    v->s[4] = (int32_t)(r & 0x00FFFFFFu);
    v->s[5] = nz;
    v->s[7] = 0;
}

static void poly_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0] & 3u, i, ph1 = v->ph[0], ph2 = v->ph[1], inc1, inc2;
    int32_t nz = v->s[5], drift = v->s[6], slop = p[P_E7], o1, o2, of, ct, ivl, sh = m->shape - (64 << 8);
    float s1 = po_f(v->s[0]), s2 = po_f(v->s[1]), s3 = po_f(v->s[2]), s4 = po_f(v->s[3]), prev = po_f(v->s[7]);
    float dt1, dt2, pw, cutn, fc, g, res, mix1, mix2, outg;
    const float Q32 = 1.0f / 4294967296.0f;
    o1 = (int8_t)(v->s[4] & 255);                     /* (the voice's offsets) */
    o2 = (int8_t)((v->s[4] >> 8) & 255);
    of = (int8_t)((v->s[4] >> 16) & 255);
    drift += ((int32_t)(noise32(&nz) >> 20) - 2048) >> 4;   /* the drift: a random walk a block, pulled back */
    drift -= drift >> 6;
    ct = (drift >> 9) * slop / 127;                   /* (up to ~+-4 cents with SLOP at 127) */
    /* the oscillators: osc 1 at the note, osc 2 INTV away, DTN between them (half each way), the voice's slop */
    inc1 = cents_inc(m->pitch16, ct + o1 * slop / 2600 - p[P_E2] / 10, m->fine);
    ivl = p[P_E3] * 100;
    if (wave == 3u)                                   /* SYNC: the SHAPE routings sweep the synced pitch up */
        ivl += (sh > 0 ? sh : 0) * 3600 / (64 << 8);
    inc2 = cents_inc(m->pitch16, ct + ivl + o2 * slop / 2600 + p[P_E2] / 10, m->fine);
    dt1 = (float)inc1 * Q32;
    dt2 = (float)inc2 * Q32;
    pw = 0.5f - 0.45f / (127.0f * 256.0f) * (float)clamp((p[P_E1] << 8) + (wave == 3u ? 0 : sh), 0, 127 << 8);
    mix1 = wave == 1u ? 0.0f : 1.0f;                  /* SAW, SAW+P: the saws; PULSE, SAW+P: the pulses */
    mix2 = wave == 0u ? 0.0f : wave == 2u ? 0.7f : 1.0f;
    /* the filter: CUT, the routings, half the keyboard, the velocity, the voice's slop; 20 Hz .. ~18 kHz */
    cutn = (float)((p[P_E4] << 8) + m->cutoff + (m->pitch16 - 60 * 16) * 8) * (1.0f / 256.0f) +
           ((float)v->vel - 100.0f) * 0.25f + (float)(of * slop) * (3.0f / 16129.0f);   /* (a unit ~0.93 st) */
    fc = 20.0f * fm_exp2f(fm_clampf(cutn, 0.0f, 127.0f) * (9.8f / 127.0f));
    fc = fm_minf(fc, 0.40f * (float)FS);
    g = fm_tanf(3.14159265f * fc / (float)FS);
    res = (float)p[P_E5] * (1.0f / 127.0f);
    outg = 9000.0f;
    if (p[P_E6] & 1) {                                /* 24 dB: the ladder */
        float G = g / (1.0f + g), G2 = G * G, k = 4.2f * res, inv = 1.0f / (1.0f + k * G2 * G2), a = 1.0f - G;
        float c1 = G2 * G * a, c2 = G2 * a, c3 = G * a, boost = 1.0f + 0.5f * k;
        for (i = 0; i < n; i++) {
            float t1 = (float)ph1 * Q32, t2, x, sum, u, vv, y;
            uint32_t o = ph1;
            ph1 += inc1;
            if (wave == 3u) {
                x = prev;
                if (ph1 < o) {                        /* the master's restart: the slave's too, between samples */
                    float d = (float)ph1 * Q32 / dt1, hgt = 2.0f * ((float)ph2 * Q32 + (1.0f - d) * dt2);
                    hgt -= hgt >= 2.0f ? 2.0f : 0.0f;
                    x -= 0.5f * hgt * d * d;          /* (the sample before: lowered by h/2 d^2) */
                    ph2 = (uint32_t)(d * dt2 * 4294967296.0f);
                    t2 = (float)ph2 * Q32;
                    prev = 2.0f * t2 - 1.0f + 0.5f * hgt * (1.0f - d) * (1.0f - d);
                } else {
                    ph2 += inc2;
                    t2 = (float)ph2 * Q32;
                    prev = po_saw(t2, dt2);
                }
                x *= 0.9f;
            } else {
                ph2 += inc2;
                t2 = (float)ph2 * Q32;
                x = 0.5f * (mix1 * (po_saw(t1, dt1) + po_saw(t2, dt2)) + mix2 * (po_pulse(t1, dt1, pw) + po_pulse(t2, dt2, pw)));
            }
            sum = c1 * s1 + c2 * s2 + c3 * s3 + a * s4;
            u = po_sat((x * boost - k * sum) * inv);
            vv = (u - s1) * G; y = vv + s1; s1 = y + vv;
            vv = (y - s2) * G; y = vv + s2; s2 = y + vv;
            vv = (y - s3) * G; y = vv + s3; s3 = y + vv;
            vv = (y - s4) * G; y = vv + s4; s4 = y + vv;
            out[i] += voice_amp((int32_t)fm_clampf(y * outg, -32767.0f, 32767.0f), m, i) << 1;
        }
    } else {                                          /* 12 dB: the state-variable low-pass */
        float k = 2.0f - 1.95f * res, a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        for (i = 0; i < n; i++) {
            float t1 = (float)ph1 * Q32, t2, x, v1, v2, v3;
            uint32_t o = ph1;
            ph1 += inc1;
            if (wave == 3u) {
                x = prev;
                if (ph1 < o) {
                    float d = (float)ph1 * Q32 / dt1, hgt = 2.0f * ((float)ph2 * Q32 + (1.0f - d) * dt2);
                    hgt -= hgt >= 2.0f ? 2.0f : 0.0f;
                    x -= 0.5f * hgt * d * d;
                    ph2 = (uint32_t)(d * dt2 * 4294967296.0f);
                    t2 = (float)ph2 * Q32;
                    prev = 2.0f * t2 - 1.0f + 0.5f * hgt * (1.0f - d) * (1.0f - d);
                } else {
                    ph2 += inc2;
                    t2 = (float)ph2 * Q32;
                    prev = po_saw(t2, dt2);
                }
                x *= 0.9f;
            } else {
                ph2 += inc2;
                t2 = (float)ph2 * Q32;
                x = 0.5f * (mix1 * (po_saw(t1, dt1) + po_saw(t2, dt2)) + mix2 * (po_pulse(t1, dt1, pw) + po_pulse(t2, dt2, pw)));
            }
            x = po_sat(x * 1.2f);
            v3 = x - s2;
            v1 = a1 * s1 + a2 * v3;
            v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            out[i] += voice_amp((int32_t)fm_clampf(v2 * outg, -32767.0f, 32767.0f), m, i) << 1;
        }
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    v->s[0] = po_i(fm_flush(s1));
    v->s[1] = po_i(fm_flush(s2));
    v->s[2] = po_i(fm_flush(s3));
    v->s[3] = po_i(fm_flush(s4));
    v->s[5] = nz;
    v->s[6] = drift;
    v->s[7] = po_i(prev);
}

/* {WAVE, PW, DTN, INTV, CUT, RES, FILT, SLOP}; WAVE 0 SAW 1 PULSE 2 SAW+P 3 SYNC; FILT 0 12 dB 1 24 dB */
static const preset_t POLY_PRESETS[] = {
    {"OB BRASS", {0, 64, 30, 0, 55, 20, 0, 50}, {5, 70, 100, 40}, 40, 0, FX(0, 30, 10, 40)},
    {"BIG STAB", {0, 64, 25, 0, 70, 10, 0, 40}, {0, 60, 90, 30}, 30, 0, FX(0, 30, 15, 45)},
    {"OB STRINGS", {0, 64, 45, 0, 75, 5, 0, 60}, {70, 90, 115, 85}, 0, 0, FX(0, 70, 15, 70)},
    {"PWM PAD", {1, 40, 20, 0, 70, 10, 0, 50}, {80, 90, 115, 95}, 0, 0, FX(0, 50, 20, 75), .lshp = 30, .lrate = 20},
    {"SOFT FIFTHS", {2, 64, 35, 7, 50, 5, 0, 70}, {90, 100, 120, 100}, 0, 0, FX(0, 60, 20, 80)},
    {"BRASS SWELL", {0, 64, 30, 0, 40, 25, 1, 45}, {60, 80, 110, 60}, 50, 0, FX(0, 30, 15, 55)},
    {"SYNC LEAD", {3, 64, 0, 12, 100, 15, 1, 20}, {0, 80, 100, 30}, 0, 1, FX(10, 0, 35, 25), .eshp = 45},
    {"SQUARE LEAD", {1, 0, 10, 12, 85, 20, 1, 25}, {0, 70, 110, 35}, 20, 1, FX(0, 10, 40, 30)},
    {"POLY BASS", {0, 64, 10, -12, 45, 35, 1, 20}, {0, 55, 70, 15}, 45, 1, FX(5, 0, 0, 5)},
    {"RESO SWEEP", {2, 64, 15, 0, 30, 95, 1, 30}, {0, 100, 60, 60}, 55, 0, FX(0, 20, 30, 50)},
    {"OB KEYS", {2, 64, 20, 0, 60, 15, 0, 35}, {0, 60, 40, 30}, 35, 0, FX(0, 30, 15, 40)},
    {"FIFTHS STAB", {0, 64, 15, 7, 65, 20, 0, 30}, {0, 70, 90, 40}, 30, 0, FX(0, 20, 25, 45)},
};

static const engine_t ENG_POLY = {
    .name = "POLY",
    .id = EID_POLY,
    .cc = 0x56,                  /* CC74 CUT, CC71 RES */
    .cats = "LLPPPLLLBFKL",
    .page_title = {"OSC", "FILTER"},
    .edit = {
        {"WAVE", F_ENUM, 0, 3, 0, N_PO_WAVE, 0},
        {"PW", F_PCT, 0, 127, 0, 0, 0},
        {"DTN", F_PCT, 0, 127, 20, 0, 0},
        {"INTV", F_INT, -12, 24, 0, 0, "st"},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"FILT", F_ENUM, 0, 1, 0, N_PO_FILT, 0},
        {"SLOP", F_PCT, 0, 127, 40, 0, 0},
    },
    .presets = POLY_PRESETS,
    .npresets = NELEM(POLY_PRESETS),
    .note_on = poly_note_on,
    .render = poly_render,
    .knob = {P_E4, P_E5, P_E2, P_REL},
    .plain = {P_E4, P_E5, P_E2, P_E7},
};
