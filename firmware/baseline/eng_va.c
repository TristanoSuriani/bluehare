/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-23) VA: the virtual analogue the user asked for ("warm, big sound: I'm ready to sacrifice polyphony"), and
 * the core OBIE is a flavour of (eng_obie.c). In float on the FPU, Blue Hare's own (no circuit modelled).
 * The core (va_run): per voice two oscillators and a sub-oscillator, band-limited (polyBLEP saw and pulse; sine;
 * the triangle naive: its images fall at -12 dB an octave), then the input saturating (a rational tanh) into a
 * zero-delay filter, the four-pole ladder (24 dB, its feedback solved for the sample, RES to self-oscillation) or
 * the state-variable low-pass (12 dB, OBIE's), the whole voice run VA_OS times the sample rate (2 on the FM-1, 8 in
 * the native image) and brought down through a fourth-order Butterworth (two biquads at 18 kHz): the saturation and
 * the resonance stay clean up the keyboard. Each voice a little off, as analog voices are: its oscillators and its
 * filter offset as it starts, and a slow drift on top (SLOP: OBIE's knob, VA's fixed and small).
 * VA: W1 W2 the oscillators' waves (SAW PULSE SINE TRI; PULSE's width moved by the SHAPE routings: PWM; or a noise,
 * WHITE PINK BROWN: the user's), DTN them apart (0 .. +-25 cents), SUB the sub-oscillator's level (an octave under
 * osc 1), SUBW its wave (a noise there: the noise's level is SUB: saws and a little pink, say); CUT, RES the
 * ladder (half the keyboard, the velocity, ENV -> FILTER as every engine's); UNI UNISON's voices, 3 or 6. Six
 * voices (POLY 6, UNISON 3 or 6, MONO, LEGATO, GLIDE: the track's VOICE pages). The state each voice keeps beyond
 * voice_t's: va_v (VA and OBIE share it: a track plays one engine) */
#include "fastmath.h"                          /* (the float voice) */
#ifndef VA_OS                                   /* (a build may set it: a measure, -DVA_OS=..) */
#ifdef BLUEHARE_NATIVE
#define VA_OS 8u                                /* (2026-10-23, the user's) the native image: eight times the rate, the
                                                 * resonant ladder and the sync ~10 dB cleaner than 2x (C6 .. C7), ~4.5 %
                                                 * of a laptop's core at full power; its own renders (tests/native) */
#else
#define VA_OS 2u                                /* the device: what the FM-1 has room for */
#endif
#endif
#define VA_POLY 6
enum { VA_LP12, VA_LADDER, VA_BP, VA_HP };      /* (va_cfg_t.ladder) */
enum { VA_SAW, VA_PULSE, VA_SINE, VA_TRI, VA_WHITE, VA_PINK, VA_BROWN,   /* (the noises: the user's, 2026-10-23) */
       VA_SAWP, VA_PWSAW };     /* (OBIE's SAWP: saw + 0.7 pulse; HOOVER's PWSAW: a saw and the same saw PW on) */
static const char *const N_VA_WAVE[] = {"SAW", "PULSE", "SINE", "TRI", "WHITE", "PINK", "BROWN", 0};
static const char *const N_VA_UNI[] = {"3", "6", 0};

#define VA_NOSC 5u                              /* (the oscillators a voice: SANDSTORM's five saws the most) */
typedef struct {
    float f[4];                  /* the filter (ladder: four stages; SVF: two) */
    float d[4];                  /* the decimator's two biquads */
    float prev;                  /* VA_SYNC: the slave's sample not yet out */
    int32_t nz, drift;           /* the voice's noise, its drift (1/512 cent) */
    int8_t o1, o2, of;           /* its offsets: osc 1, osc 2, the filter (-127 .. 127) */
    uint32_t nr;                 /* the noises: white's generator, */
    float pk[3], br;             /* pink's three poles (Paul Kellet's economy filter), brown's integrator */
    uint32_t ph[VA_NOSC + 1u];   /* the oscillators' phases, then the sub's */
} va_voice_t;
static va_voice_t va_v[NPART][NVOICE];
static va_voice_t *va_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < NVOICE ? &va_v[t - trk][i] : 0;
}

static inline float po_blep(float t, float dt)  /* polyBLEP: the step's residual at phase t (cycles), step dt */
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
static inline float po_sat(float x)              /* tanh's shape, rational: x (27 + x^2) / (27 + 9 x^2), |x| <= 3 */
{
    float x2;
    x = fm_clampf(x, -3.0f, 3.0f);
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
static inline float va_osc(uint32_t w, float t, float dt, float pw)
{
    switch (w) {
    case VA_PULSE:
        return po_pulse(t, dt, pw);
    case VA_SAWP:
        return po_saw(t, dt) + 0.7f * po_pulse(t, dt, pw);
    case VA_PWSAW: {                                  /* (PW 0: a saw; towards half a cycle its odd harmonics thin) */
        float t2 = t + pw;
        t2 -= t2 >= 1.0f ? 1.0f : 0.0f;
        return 0.5f * (po_saw(t, dt) + po_saw(t2, dt));
    }
    case VA_SINE:
        return fm_sin_turns(t);
    case VA_TRI:
        return 1.0f - 4.0f * fm_fabsf(t - 0.5f);
    default:
        return po_saw(t, dt);
    }
}

/* a wave (va_osc), or one of the noises: WHITE flat, PINK -3 dB an octave, BROWN -6 dB (at the voice's rate; their
 * level about a saw's) */
static inline float va_wave(va_voice_t *s, uint32_t w, float t, float dt, float pw)
{
    float x;
    if (w < VA_WHITE || w > VA_BROWN)
        return va_osc(w, t, dt, pw);
    s->nr ^= s->nr << 13;
    s->nr ^= s->nr >> 17;
    s->nr ^= s->nr << 5;
    x = (float)(int32_t)s->nr * (1.0f / 2147483648.0f) * 0.577f;
    if (w == VA_PINK) {
        s->pk[0] = 0.99765f * s->pk[0] + x * 0.0990460f;
        s->pk[1] = 0.96300f * s->pk[1] + x * 0.2965164f;
        s->pk[2] = 0.57000f * s->pk[2] + x * 1.0526913f;
        return (s->pk[0] + s->pk[1] + s->pk[2] + x * 0.1848f) * 0.30f;
    }
    if (w == VA_BROWN) {
        s->br = (s->br + 0.02f * x) * (1.0f / 1.02f);
        return s->br * 3.5f;
    }
    return x;
}

/* what a flavour asks of the core for one block of one voice */
typedef struct {
    uint32_t inc[VA_NOSC], incs; /* the oscillators' and the sub's steps at the sample rate (cents_inc) */
    uint8_t w[VA_NOSC], ws;      /* their waves (VA_*) */
    uint8_t nosc;                /* the oscillators sounding (1 .. VA_NOSC) */
    uint8_t sync;                /* 1: osc 2 a saw hard-synced to osc 1 (OBIE's SYNC) */
    uint8_t ladder;              /* 1 the 24 dB ladder, 0 the 12 dB state-variable filter's low-pass; (va_run alone)
                                  * VA_BP its band-pass, VA_HP its high-pass (VAKIT's hats, claps, rims) */
    float g[VA_NOSC], gs;        /* their levels */
    float pw;                    /* PULSE's width (0.5 square .. ~0.05), PWSAW's offset (0 .. 0.5) */
    float cut;                   /* the cutoff, 0 .. 127 (20 Hz .. ~18 kHz) */
    float res;                   /* 0 .. 1 */
    float drive;                 /* into the saturation */
} va_cfg_t;

/* the decimator: two biquads, Butterworth (Q 0.541, 1.307) at 18 kHz, at the voice's rate */
static float va_dq[2][5];
static void va_dq_init(void)
{
    static const float Q[2] = {0.5411961f, 1.3065630f};
    float k = fm_tanf(3.14159265f * 18000.0f / ((float)FS * (float)VA_OS)), k2 = k * k;
    uint32_t j;
    for (j = 0; j < 2u; j++) {
        float nrm = 1.0f / (1.0f + k / Q[j] + k2);
        va_dq[j][0] = k2 * nrm;
        va_dq[j][1] = 2.0f * k2 * nrm;
        va_dq[j][2] = k2 * nrm;
        va_dq[j][3] = 2.0f * (k2 - 1.0f) * nrm;
        va_dq[j][4] = (1.0f - k / Q[j] + k2) * nrm;
    }
}
static inline float va_bq(const float *c, float *z, float x)   /* transposed direct form II */
{
    float y = c[0] * x + z[0];
    z[0] = c[1] * x - c[3] * y + z[1];
    z[1] = c[2] * x - c[4] * y;
    return y;
}

/* the voice's note-on (every flavour's): the phases (osc 2 a quarter cycle on: no flam; the rest of a stack
 * anywhere in their cycles: no click of five at once), the filter and decimator cleared, its offsets; a voice
 * still sounding keeps them all */
static void va_start(track_t *t, voice_t *v)
{
    va_voice_t *s = va_of(t, v);
    int32_t nz;
    uint32_t r, k;
    if (!s)
        return;
    if (v->env) {                                     /* a retrigger, a steal: still sounding (voice.c keeps its level):
                                                       * the phases and the filter go on (resetting them clicks) */
        if (va_dq[0][0] == 0.0f)
            va_dq_init();
        return;
    }
    nz = (int32_t)(0x6C8E9CF5u ^ (v->age * 2654435761u) ^ (uint32_t)(v - t->v) * 40503u);   /* (the voice's own, each note:
                                                      * a render repeats) */
    memset(s, 0, sizeof s->f + sizeof s->d + sizeof s->prev);
    s->drift = 0;
    r = noise32(&nz);
    s->o1 = (int8_t)(r & 255u);
    s->o2 = (int8_t)((r >> 8) & 255u);
    s->of = (int8_t)((r >> 16) & 255u);
    s->nr = noise32(&nz) | 1u;
    s->pk[0] = s->pk[1] = s->pk[2] = s->br = 0.0f;
    s->ph[0] = 0;
    s->ph[1] = 0x40000000u;
    for (k = 2; k < VA_NOSC; k++)
        s->ph[k] = (uint32_t)noise32(&nz);
    s->ph[VA_NOSC] = 0;
    s->nz = nz;
    if (va_dq[0][0] == 0.0f)
        va_dq_init();
}

/* the drift a block (a random walk, pulled back): cents at slop 0 .. 127 */
static int32_t va_drift(va_voice_t *s, int32_t slop)
{
    s->drift += ((int32_t)(noise32(&s->nz) >> 20) - 2048) >> 4;
    s->drift -= s->drift >> 6;
    return (s->drift >> 9) * slop / 127;
}

/* the oscillators' sample at the voice's rate (OBIE's SYNC: osc 2 restarted with osc 1, band-limited between
 * samples), then the sub */
static inline __attribute__((always_inline)) float va_osc_sum(va_voice_t *restrict s, uint32_t *restrict ph, const va_cfg_t *restrict c,
                                                              const uint32_t *ii, const float *dt, uint32_t is, float dts,
                                                              const uint32_t nosc, const int sync)
{
    const float Q32 = 1.0f / 4294967296.0f;
    float x;
    uint32_t k, o0 = ph[0];
    ph[0] += ii[0];
    x = c->g[0] * va_wave(s, c->w[0], (float)ph[0] * Q32, dt[0], c->pw);
    for (k = 1; k < nosc; k++) {
        if (k == 1u && sync) {
            float y = s->prev;
            if (ph[0] < o0) {                        /* the master's restart: the slave's too */
                float d = (float)ph[0] * Q32 / dt[0], hgt = 2.0f * ((float)ph[1] * Q32 + (1.0f - d) * dt[1]), t2;
                hgt -= hgt >= 2.0f ? 2.0f : 0.0f;
                y -= 0.5f * hgt * d * d;                /* (the sample before: lowered by h/2 d^2) */
                ph[1] = (uint32_t)(d * dt[1] * 4294967296.0f);
                t2 = (float)ph[1] * Q32;
                s->prev = 2.0f * t2 - 1.0f + 0.5f * hgt * (1.0f - d) * (1.0f - d);
            } else {
                ph[1] += ii[1];
                s->prev = po_saw((float)ph[1] * Q32, dt[1]);
            }
            x += 0.9f * c->g[1] * y;
            continue;
        }
        ph[k] += ii[k];
        x += c->g[k] * va_wave(s, c->w[k], (float)ph[k] * Q32, dt[k], c->pw);
    }
    if (c->gs > 0.0f) {
        ph[VA_NOSC] += is;
        x += c->gs * va_wave(s, c->ws, (float)ph[VA_NOSC] * Q32, dts, 0.5f);
    }
    return x;
}

/* one block of one voice: the oscillators at VA_OS times the rate, the saturation, the filter, down to the rate:
 * into y (about -1 .. 1), for the flavour to finish (va_out) */
static inline __attribute__((always_inline)) void va_run_k(va_voice_t *restrict s, float *restrict y, uint32_t n, const va_cfg_t *restrict c,
                                                           const uint32_t nosc, const int sync)
{
    const float Q32 = 1.0f / 4294967296.0f, ros = 1.0f / (float)VA_OS;
    uint32_t i, k, ii[VA_NOSC], is = c->incs / VA_OS;
    float dt[VA_NOSC], dts = (float)c->incs * Q32 * ros;
    float s1 = s->f[0], s2 = s->f[1], s3 = s->f[2], s4 = s->f[3], fc, g, o = 0.0f;
    float z0[2] = {s->d[0], s->d[1]}, z1[2] = {s->d[2], s->d[3]};
    uint32_t ph[VA_NOSC + 1u];                        /* (the phases in locals: registers, not the voice's memory) */
    memcpy(ph, s->ph, sizeof ph);
    for (k = 0; k < nosc; k++) {
        ii[k] = c->inc[k] / VA_OS;
        dt[k] = (float)c->inc[k] * Q32 * ros;
    }
    fc = 20.0f * fm_exp2f(fm_clampf(c->cut, 0.0f, 127.0f) * (9.8f / 127.0f));
    fc = fm_minf(fc, 18000.0f);
    g = fm_tanf(3.14159265f * fc / ((float)FS * (float)VA_OS));
    if (c->ladder == VA_LADDER) {
        float G = g / (1.0f + g), G2 = G * G, kk = 4.2f * c->res, inv = 1.0f / (1.0f + kk * G2 * G2), a = 1.0f - G;
        float c1 = G2 * G * a, c2 = G2 * a, c3 = G * a, boost = 1.0f + 0.5f * kk;
        for (i = 0; i < n; i++) {
            for (k = 0; k < VA_OS; k++) {
                float x = va_osc_sum(s, ph, c, ii, dt, is, dts, nosc, sync) * c->drive, sum, u, vv, yy;
                sum = c1 * s1 + c2 * s2 + c3 * s3 + a * s4;
                u = po_sat((x * boost - kk * sum) * inv);
                vv = (u - s1) * G; yy = vv + s1; s1 = yy + vv;
                vv = (yy - s2) * G; yy = vv + s2; s2 = yy + vv;
                vv = (yy - s3) * G; yy = vv + s3; s3 = yy + vv;
                vv = (yy - s4) * G; yy = vv + s4; s4 = yy + vv;
                o = va_bq(va_dq[1], z1, va_bq(va_dq[0], z0, yy));
            }
            y[i] = o;
        }
    } else {
        float kk = 2.0f - 1.95f * c->res, a1 = 1.0f / (1.0f + g * (g + kk)), a2 = g * a1, a3 = g * a2;
        for (i = 0; i < n; i++) {
            for (k = 0; k < VA_OS; k++) {
                float x = po_sat(va_osc_sum(s, ph, c, ii, dt, is, dts, nosc, sync) * c->drive), v1, v2, v3;
                v3 = x - s2;
                v1 = a1 * s1 + a2 * v3;
                v2 = s2 + a2 * s1 + a3 * v3;
                s1 = 2.0f * v1 - s1;
                s2 = 2.0f * v2 - s2;
                v3 = c->ladder == VA_BP ? v1 * (2.0f - 1.95f * c->res) : c->ladder == VA_HP ? x - kk * v1 - v2 : v2;
                o = va_bq(va_dq[1], z1, va_bq(va_dq[0], z0, v3));
            }
            y[i] = o;
        }
    }
    memcpy(s->ph, ph, sizeof ph);
    s->f[0] = fm_flush(s1);
    s->f[1] = fm_flush(s2);
    s->f[2] = fm_flush(s3);
    s->f[3] = fm_flush(s4);
    s->d[0] = fm_flush(z0[0]);
    s->d[1] = fm_flush(z0[1]);
    s->d[2] = fm_flush(z1[0]);
    s->d[3] = fm_flush(z1[1]);
}
/* the core into y, for a flavour that finishes the sound itself (SANDSTORM: its gate's chop, its drive): the
 * oscillators' count known in each loop (unrolled); the two oscillators' flavours use va_run_out below */
static void va_run(va_voice_t *s, float *y, uint32_t n, const va_cfg_t *c)
{
    if (c->sync)
        va_run_k(s, y, n, c, 2u, 1);
    else if (c->nosc == 5u)
        va_run_k(s, y, n, c, 5u, 0);
    else
        va_run_k(s, y, n, c, 2u, 0);
}
/* VA's, OBIE's and HOOVER's two oscillators: the voice's output at once (the committed core's loop, its phases in
 * locals: ~10 % fewer instructions than va_run_k's general one, measured 2026-10-23) */
static void va_run_out(va_voice_t *s, int32_t *out, uint32_t n, const vmod_t *m, const va_cfg_t *c)
{
    const float Q32 = 1.0f / 4294967296.0f, ros = 1.0f / (float)VA_OS;
    uint32_t i, k, ph1 = s->ph[0], ph2 = s->ph[1], phs = s->ph[VA_NOSC];
    uint32_t i1 = c->inc[0] / VA_OS, i2 = c->inc[1] / VA_OS, is = c->incs / VA_OS;
    float dt1 = (float)c->inc[0] * Q32 * ros, dt2 = (float)c->inc[1] * Q32 * ros, dts = (float)c->incs * Q32 * ros;
    float s1 = s->f[0], s2 = s->f[1], s3 = s->f[2], s4 = s->f[3], prev = s->prev, fc, g, y = 0.0f;
    float z0[2] = {s->d[0], s->d[1]}, z1[2] = {s->d[2], s->d[3]};
    const float outg = 36000.0f;
    fc = 20.0f * fm_exp2f(fm_clampf(c->cut, 0.0f, 127.0f) * (9.8f / 127.0f));
    fc = fm_minf(fc, 18000.0f);
    g = fm_tanf(3.14159265f * fc / ((float)FS * (float)VA_OS));
    if (c->ladder) {
        float G = g / (1.0f + g), G2 = G * G, kk = 4.2f * c->res, inv = 1.0f / (1.0f + kk * G2 * G2), a = 1.0f - G;
        float c1 = G2 * G * a, c2 = G2 * a, c3 = G * a, boost = 1.0f + 0.5f * kk;
        for (i = 0; i < n; i++) {
            for (k = 0; k < VA_OS; k++) {
                float x, sum, u, vv, yy;
                uint32_t o = ph1;
                ph1 += i1;
                phs += is;
                if (c->sync) {                 /* the master's restart: the slave's, between samples */
                    x = prev;
                    if (ph1 < o) {
                        float d = (float)ph1 * Q32 / dt1, hgt = 2.0f * ((float)ph2 * Q32 + (1.0f - d) * dt2), t2;
                        hgt -= hgt >= 2.0f ? 2.0f : 0.0f;
                        x -= 0.5f * hgt * d * d;
                        ph2 = (uint32_t)(d * dt2 * 4294967296.0f);
                        t2 = (float)ph2 * Q32;
                        prev = 2.0f * t2 - 1.0f + 0.5f * hgt * (1.0f - d) * (1.0f - d);
                    } else {
                        ph2 += i2;
                        prev = po_saw((float)ph2 * Q32, dt2);
                    }
                    x *= 0.9f * c->g[1];
                    x += c->g[0] * va_wave(s, c->w[0], (float)ph1 * Q32, dt1, c->pw);
                } else {
                    ph2 += i2;
                    x = c->g[0] * va_wave(s, c->w[0], (float)ph1 * Q32, dt1, c->pw) + c->g[1] * va_wave(s, c->w[1], (float)ph2 * Q32, dt2, c->pw);
                }
                if (c->gs > 0.0f)
                    x += c->gs * va_wave(s, c->ws, (float)phs * Q32, dts, 0.5f);
                x *= c->drive;
                sum = c1 * s1 + c2 * s2 + c3 * s3 + a * s4;
                u = po_sat((x * boost - kk * sum) * inv);
                vv = (u - s1) * G; yy = vv + s1; s1 = yy + vv;
                vv = (yy - s2) * G; yy = vv + s2; s2 = yy + vv;
                vv = (yy - s3) * G; yy = vv + s3; s3 = yy + vv;
                vv = (yy - s4) * G; yy = vv + s4; s4 = yy + vv;
                y = va_bq(va_dq[1], z1, va_bq(va_dq[0], z0, yy));
            }
            out[i] += voice_amp((int32_t)fm_clampf(y * outg, -32767.0f, 32767.0f), m, i) << 1;
        }
    } else {
        float kk = 2.0f - 1.95f * c->res, a1 = 1.0f / (1.0f + g * (g + kk)), a2 = g * a1, a3 = g * a2;
        for (i = 0; i < n; i++) {
            for (k = 0; k < VA_OS; k++) {
                float x, v1, v2, v3;
                uint32_t o = ph1;
                ph1 += i1;
                phs += is;
                if (c->sync) {
                    x = prev;
                    if (ph1 < o) {
                        float d = (float)ph1 * Q32 / dt1, hgt = 2.0f * ((float)ph2 * Q32 + (1.0f - d) * dt2), t2;
                        hgt -= hgt >= 2.0f ? 2.0f : 0.0f;
                        x -= 0.5f * hgt * d * d;
                        ph2 = (uint32_t)(d * dt2 * 4294967296.0f);
                        t2 = (float)ph2 * Q32;
                        prev = 2.0f * t2 - 1.0f + 0.5f * hgt * (1.0f - d) * (1.0f - d);
                    } else {
                        ph2 += i2;
                        prev = po_saw((float)ph2 * Q32, dt2);
                    }
                    x *= 0.9f * c->g[1];
                    x += c->g[0] * va_wave(s, c->w[0], (float)ph1 * Q32, dt1, c->pw);
                } else {
                    ph2 += i2;
                    x = c->g[0] * va_wave(s, c->w[0], (float)ph1 * Q32, dt1, c->pw) + c->g[1] * va_wave(s, c->w[1], (float)ph2 * Q32, dt2, c->pw);
                }
                if (c->gs > 0.0f)
                    x += c->gs * va_wave(s, c->ws, (float)phs * Q32, dts, 0.5f);
                x = po_sat(x * c->drive);
                v3 = x - s2;
                v1 = a1 * s1 + a2 * v3;
                v2 = s2 + a2 * s1 + a3 * v3;
                s1 = 2.0f * v1 - s1;
                s2 = 2.0f * v2 - s2;
                y = va_bq(va_dq[1], z1, va_bq(va_dq[0], z0, v2));
            }
            out[i] += voice_amp((int32_t)fm_clampf(y * outg, -32767.0f, 32767.0f), m, i) << 1;
        }
    }
    s->ph[0] = ph1;
    s->ph[1] = ph2;
    s->ph[VA_NOSC] = phs;
    s->f[0] = fm_flush(s1);
    s->f[1] = fm_flush(s2);
    s->f[2] = fm_flush(s3);
    s->f[3] = fm_flush(s4);
    s->d[0] = fm_flush(z0[0]);
    s->d[1] = fm_flush(z0[1]);
    s->d[2] = fm_flush(z1[0]);
    s->d[3] = fm_flush(z1[1]);
    s->prev = prev;
}
/* y into the voice's output (its envelope, its level) */
static void va_out(int32_t *out, uint32_t n, const vmod_t *m, const float *y)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        out[i] += voice_amp((int32_t)fm_clampf(y[i] * 36000.0f, -32767.0f, 32767.0f), m, i) << 1;
}

/* the cutoff a flavour asks for: CUT, the routings, half the keyboard, the velocity, the voice's offset */
static float va_cut(const voice_t *v, const vmod_t *m, int32_t cut, int32_t of, int32_t slop)
{
    return (float)((cut << 8) + m->cutoff + (m->pitch16 - 60 * 16) * 8) * (1.0f / 256.0f) +
           ((float)v->vel - 100.0f) * 0.25f + (float)(of * slop) * (3.0f / 16129.0f);
}

/* ---- VA */
#define VA_SLOP 24                                     /* (its analog's spread: small, fixed) */
static void vae_note_on(track_t *t, voice_t *v) { va_start(t, v); }
static void vae_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    va_voice_t *s = va_of(t, v);
    va_cfg_t c;
    int32_t ct, dtn = p[P_E2] * 25 / 127, sh = m->shape - (64 << 8);
    if (!s)
        return;
    ct = va_drift(s, VA_SLOP);
    c.nosc = 2;
    c.sync = 0;
    c.w[0] = (uint8_t)clamp(p[P_E0], 0, VA_BROWN);
    c.w[1] = (uint8_t)clamp(p[P_E1], 0, VA_BROWN);
    c.ws = (uint8_t)clamp(p[P_E4], 0, VA_BROWN);
    c.inc[0] = cents_inc(m->pitch16, ct + s->o1 * VA_SLOP / 2600 - dtn, m->fine);
    c.inc[1] = cents_inc(m->pitch16, ct + s->o2 * VA_SLOP / 2600 + dtn, m->fine);
    c.incs = cents_inc(m->pitch16 - 12 * 16, ct + s->o1 * VA_SLOP / 2600, m->fine);
    c.g[0] = 0.5f;
    c.g[1] = 0.5f;
    c.gs = (float)p[P_E3] * (0.5f / 127.0f);
    c.pw = 0.5f - 0.45f / (127.0f * 256.0f) * (float)clamp(sh, 0, 127 << 8);
    c.cut = va_cut(v, m, p[P_E5], s->of, VA_SLOP);
    c.res = (float)p[P_E6] * (1.0f / 127.0f);
    c.ladder = 1;
    c.drive = 1.0f + c.gs;                             /* (the sub pushes the ladder a little: the warmth) */
    va_run_out(s, out, n, m, &c);
}
static uint32_t vae_unison(const track_t *t) { return t->p[P_E7] ? 6u : 3u; }

/* {W1, W2, DTN, SUB, SUBW, CUT, RES, UNI}; W 0 SAW 1 PULSE 2 SINE 3 TRI; UNI 0 three 1 six. The common
 * {ATK DEC SUS REL}, ENV -> FILTER, mono; .vmode (V_* + 1) and .det (UNISON's DETUNE + 1) for the supersaws */
#define VA_UNI(d) .vmode = V_UNISON + 1, .det = (d) + 1
static const preset_t VA_PRESETS[] = {
    /* the simple waveforms: each oscillator's wave, nothing else in the way */
    {"SAW", {0, 0, 10, 0, 0, 110, 10, 1}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 15)},
    {"SQUARE", {1, 1, 8, 0, 1, 110, 10, 1}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 15)},
    {"SINE", {2, 2, 4, 0, 2, 127, 0, 1}, {0, 0, 127, 25}, 0, 0, FX(0, 0, 10, 20)},
    {"TRIANGLE", {3, 3, 6, 0, 3, 127, 0, 1}, {0, 0, 127, 25}, 0, 0, FX(0, 0, 10, 20)},
    /* plucks */
    {"SAW PLUCK", {0, 0, 14, 30, 1, 40, 35, 1}, {0, 45, 0, 40}, 70, 0, FX(0, 20, 45, 40)},
    {"GLASS PLUCK", {3, 0, 8, 0, 2, 55, 50, 1}, {0, 35, 0, 50}, 60, 0, FX(0, 30, 50, 55)},
    {"SUB PLUCK", {1, 0, 6, 90, 2, 35, 25, 1}, {0, 40, 0, 30}, 65, 0, FX(0, 10, 35, 30)},
    /* pads */
    {"WARM PAD", {0, 0, 30, 40, 2, 60, 15, 1}, {85, 90, 115, 95}, 15, 0, FX(0, 55, 20, 75)},
    {"PWM PAD", {1, 1, 25, 30, 3, 65, 10, 1}, {80, 90, 115, 95}, 10, 0, FX(0, 50, 20, 75), .lshp = 40, .lrate = 18},
    {"SINE PAD", {2, 3, 18, 50, 2, 90, 5, 1}, {90, 100, 120, 105}, 0, 0, FX(0, 60, 20, 85)},
    /* strings */
    {"STRINGS", {0, 0, 45, 0, 0, 72, 8, 1}, {70, 90, 115, 85}, 5, 0, FX(0, 75, 15, 70), VA_UNI(28)},
    {"SLOW STRINGS", {0, 1, 40, 20, 3, 58, 10, 1}, {105, 100, 120, 100}, 10, 0, FX(0, 80, 15, 80), VA_UNI(30)},
    /* leads */
    {"FAT LEAD", {0, 1, 20, 70, 1, 85, 30, 0}, {0, 70, 110, 30}, 30, 1, FX(10, 0, 35, 25), .glide = 25},
    {"SQUARE LEAD", {1, 1, 12, 50, 1, 92, 20, 0}, {0, 60, 110, 30}, 20, 1, FX(0, 10, 40, 30), .glide = 18},
    {"SINE LEAD", {2, 3, 10, 30, 2, 110, 10, 0}, {0, 60, 115, 35}, 0, 1, FX(0, 15, 45, 35), .glide = 30},
    {"ACID LEAD", {0, 0, 4, 0, 0, 35, 105, 0}, {0, 55, 60, 20}, 80, 1, FX(25, 0, 30, 20), .glide = 12},
    {"MONO BASS", {0, 1, 8, 110, 1, 40, 35, 0}, {0, 55, 80, 15}, 55, 1, FX(10, 0, 0, 5)},
    /* (2026-10-23) the noises: a breath under a pad, a pluck of noise, wind */
    {"BREATH PAD", {0, 2, 25, 35, 5, 70, 10, 1}, {85, 90, 115, 95}, 10, 0, FX(0, 55, 20, 80)},
    {"NOISE HIT", {4, 0, 0, 0, 4, 80, 40, 1}, {0, 20, 0, 25}, 70, 0, FX(0, 0, 40, 35)},
    {"WIND", {6, 5, 0, 100, 6, 96, 55, 1}, {110, 90, 120, 110}, 0, 0, FX(0, 40, 30, 90)},
    /* supersaws */
    {"SUPERSAW", {0, 0, 40, 30, 0, 100, 10, 1}, {5, 70, 115, 50}, 0, 0, FX(0, 40, 35, 55), VA_UNI(70)},
    {"TRANCE SAW", {0, 0, 55, 20, 0, 92, 15, 1}, {0, 75, 100, 45}, 20, 0, FX(0, 40, 50, 60), VA_UNI(90)},
    {"HOOVERISH", {0, 1, 60, 60, 1, 80, 20, 1}, {0, 80, 110, 40}, 25, 1, FX(20, 30, 30, 45), VA_UNI(110), .glide = 40},
    {"SAW STACK", {0, 0, 35, 45, 0, 95, 12, 0}, {0, 70, 110, 40}, 10, 0, FX(0, 30, 30, 50), VA_UNI(60)},
};

static const engine_t ENG_VA = {
    .name = "VA",
    .id = EID_VA,
    .cc = 0x67,                  /* CC74 CUT, CC71 RES */
    .cats = "OOOOUUUPPPPPLLLLBPUFLLLL",
    .page_title = {"OSC", "FILTER"},
    .edit = {
        {"W1", F_ENUM, 0, VA_BROWN, 0, N_VA_WAVE, 0},
        {"W2", F_ENUM, 0, VA_BROWN, 0, N_VA_WAVE, 0},
        {"DTN", F_PCT, 0, 127, 20, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"SUBW", F_ENUM, 0, VA_BROWN, 1, N_VA_WAVE, 0},
        {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},
        {"RES", F_PCT, 0, 127, 15, 0, 0},
        {"UNI", F_ENUM, 0, 1, 1, N_VA_UNI, 0},
    },
    .presets = VA_PRESETS,
    .npresets = NELEM(VA_PRESETS),
    .poly = VA_POLY,
    .note_on = vae_note_on,
    .render = vae_render,
    .knob = {P_E5, P_E6, P_E2, P_E3},
    .plain = {P_E5, P_E6, P_E2, P_E3},
    .unison = vae_unison,
};
