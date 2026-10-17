/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
 * The processing is amb-1's, by Charles Vestal (GPL-3.0): docs/excerpt_amb1.c (reverb, echoes, render_chunk) */
/* (Blue Hare, 2026-10) amb-1's sound processing, in float on the FM-1's FPU (-mfprev1; in the host builds the host's),
 * its maths X0X's (fastmath.h). Two uses:
 *  - REVERB TYPE PLATE and DELAY TYPE AMB (2026-10-17, every build: fx.c fx_buses): amb-1's plate and echoes inside
 *    Blue Hare's chain, beside its own types. The plate runs at half the rate, as HALL and CONCERT do (its lines in
 *    CONCERT's memory: one reverb runs at a time); the echoes' tape and Echoplex live in the delay's line (dly_buf).
 *  - The studio's EFFECTS > PROCESSING AMB-1 (BLUEHARE_AMBFX, fx_proc): amb-1's whole chain instead of Blue Hare's
 *    buses and master, at the full rate with its own buffers (~280 KB), to compare the two by ear.
 * The plate: J. Dattorro, "Effect Design, Part 1", JAES 1997: a 30 ms pre-delay, a bandwidth low-pass, four input
 * diffusers, a figure-8 tank of two modulated allpasses, delays, damping low-passes and allpasses, seven taps a side.
 * The echoes: a tape loop of up to 5.6 s at a quarter rate (5.3 s in the delay's line), darker and thinner each pass,
 * wow, two heads that wander across the field (MOTION: GEN's), the second sliding to another part of the loop now and
 * then; and a 160 ms Echoplex, fed back, its second tap to the right.
 * amb-1's chain (render_chunk): the delay sends into the echoes; the reverb sends plus the echoes into the plate; the
 * dry mix with the echoes (x 0.75, the Echoplex x 0.3) and the plate (x 1.3); one tanh a channel after a DC block. */
#include "fastmath.h"

/* ------------------------------------------------------ the plate (amb-1 reverb) --- */
/* the paper's lengths (29761 Hz), each instance's at its rate: ((n * num + 5000) / 10000, num 14818 at 44.1 kHz, 7409
 * at 22.05). The lines, then the excursion, then the output taps */
enum { PT_PRE, PT_IN1, PT_IN2, PT_IN3, PT_IN4, PT_APL, PT_D1L, PT_AP2L, PT_D2L, PT_APR, PT_D1R, PT_AP2R, PT_D2R, PT_EXC,
       PT_L1, PT_L2, PT_L3, PT_L4, PT_L5, PT_L6, PT_L7, PT_R1, PT_R2, PT_R3, PT_R4, PT_R5, PT_R6, PT_R7, PT_N };
#define PT_NL (PT_D2R + 1)
static const uint16_t PT_PAPER[PT_N] = {0, 142, 107, 379, 277, 672, 4453, 1800, 3720, 908, 4217, 2656, 3163, 16,
                                        266, 2974, 1913, 1996, 1990, 187, 1066, 353, 3627, 1228, 2673, 2111, 335, 121};
#define AMB_DSN(n, num) (((n) * (num) + 5000) / 10000)
#define AMB_PL_SIZE(num, pre)                                                                                         \
    ((pre) + AMB_DSN(142, num) + AMB_DSN(107, num) + AMB_DSN(379, num) + AMB_DSN(277, num) + AMB_DSN(672, num) +      \
     AMB_DSN(4453, num) + AMB_DSN(1800, num) + AMB_DSN(3720, num) + AMB_DSN(908, num) + AMB_DSN(4217, num) +            \
     AMB_DSN(2656, num) + AMB_DSN(3163, num) + 2 * (AMB_DSN(16, num) + 2))
#define AMB_NUM_FULL 14818
#define AMB_NUM_HALF 7409
#define AMB_PRE_FULL 1323                           /* 30 ms */
#define AMB_PRE_HALF 662
#define AMB_PL_HALF AMB_PL_SIZE(AMB_NUM_HALF, AMB_PRE_HALF)   /* ~17300 floats: 69 KB (fx.c: in CONCERT's memory) */
typedef struct { float *b; int32_t n, i; } amb_dl_t;
typedef struct {
    amb_dl_t dl[PT_NL];
    int32_t t[PT_N];
    float bw, bwk, damp_l, damp_r, decay, dd2, lfo_s, lfo_c, lfo_k;
} amb_plate_t;

static inline float amb_tap(const amb_dl_t *d, int32_t k)
{
    int32_t j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void amb_push(amb_dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float amb_ap(amb_dl_t *d, int32_t len, float x, float g)
{
    float z = amb_tap(d, len), v = x - g * z;
    amb_push(d, fm_flush(v));
    return z + g * v;
}
static inline float amb_ap_mod(amb_dl_t *d, float len, float x, float g)
{
    int32_t k = (int32_t)len;
    float f = len - (float)k, z = amb_tap(d, k) + (amb_tap(d, k + 1) - amb_tap(d, k)) * f, v = x - g * z;
    amb_push(d, fm_flush(v));
    return z + g * v;
}
/* a plate at the rate num gives (half: 22.05 kHz) in mem (cleared here): its lines laid one after the other */
static void amb_plate_init(amb_plate_t *pl, float *mem, int32_t num, int32_t pre)
{
    int32_t k, half = num == AMB_NUM_HALF;
    memset(pl, 0, sizeof *pl);
    for (k = 0; k < PT_N; k++)
        pl->t[k] = AMB_DSN((int32_t)PT_PAPER[k], num);
    pl->t[PT_PRE] = pre;
    for (k = 0; k < PT_NL; k++) {
        int32_t n = pl->t[k] + (k == PT_APL || k == PT_APR ? pl->t[PT_EXC] + 2 : 0);
        pl->dl[k].b = mem;
        pl->dl[k].n = n;
        memset(mem, 0, (uint32_t)n * sizeof(float));
        mem += n;
    }
    pl->lfo_c = 1.0f;
    pl->decay = 0.5f;
    pl->dd2 = 0.5f;
    pl->bwk = half ? 0.84f : 0.6f;                  /* (the bandwidth's ~6.4 kHz at either rate: 1 - 0.4^2) */
    pl->lfo_k = half ? 0.0002f : 0.0001f;           /* (the tank's sweep at amb-1's rate) */
}
static inline void amb_plate(amb_plate_t *pl, float in, float damp, float *ol, float *or_)
{
    amb_dl_t *dl = pl->dl;
    const int32_t *t = pl->t;
    float x, a, b, fb_l = amb_tap(&dl[PT_D2R], t[PT_D2R]), fb_r = amb_tap(&dl[PT_D2L], t[PT_D2L]);
    amb_push(&dl[PT_PRE], in);
    x = amb_tap(&dl[PT_PRE], t[PT_PRE]);
    pl->bw += pl->bwk * (x - pl->bw);
    x = amb_ap(&dl[PT_IN1], t[PT_IN1], pl->bw, 0.75f);
    x = amb_ap(&dl[PT_IN2], t[PT_IN2], x, 0.75f);
    x = amb_ap(&dl[PT_IN3], t[PT_IN3], x, 0.625f);
    x = amb_ap(&dl[PT_IN4], t[PT_IN4], x, 0.625f);
    {
        float s = pl->lfo_s + pl->lfo_k * pl->lfo_c, c = pl->lfo_c - pl->lfo_k * s;
        pl->lfo_s = s;
        pl->lfo_c = c;
    }
    a = amb_ap_mod(&dl[PT_APL], (float)t[PT_APL] + (float)t[PT_EXC] * pl->lfo_s, x + pl->decay * fb_l, -0.7f);
    amb_push(&dl[PT_D1L], a);
    b = amb_tap(&dl[PT_D1L], t[PT_D1L]);
    pl->damp_l += damp * (b - pl->damp_l);
    b = amb_ap(&dl[PT_AP2L], t[PT_AP2L], pl->damp_l * pl->decay, pl->dd2);
    amb_push(&dl[PT_D2L], b);
    a = amb_ap_mod(&dl[PT_APR], (float)t[PT_APR] + (float)t[PT_EXC] * pl->lfo_c, x + pl->decay * fb_r, -0.7f);
    amb_push(&dl[PT_D1R], a);
    b = amb_tap(&dl[PT_D1R], t[PT_D1R]);
    pl->damp_r += damp * (b - pl->damp_r);
    b = amb_ap(&dl[PT_AP2R], t[PT_AP2R], pl->damp_r * pl->decay, pl->dd2);
    amb_push(&dl[PT_D2R], b);
    *ol = 0.6f * (amb_tap(&dl[PT_D1R], t[PT_L1]) + amb_tap(&dl[PT_D1R], t[PT_L2]) - amb_tap(&dl[PT_AP2R], t[PT_L3]) +
                  amb_tap(&dl[PT_D2R], t[PT_L4]) - amb_tap(&dl[PT_D1L], t[PT_L5]) - amb_tap(&dl[PT_AP2L], t[PT_L6]) -
                  amb_tap(&dl[PT_D2L], t[PT_L7]));
    *or_ = 0.6f * (amb_tap(&dl[PT_D1L], t[PT_R1]) + amb_tap(&dl[PT_D1L], t[PT_R2]) - amb_tap(&dl[PT_AP2L], t[PT_R3]) +
                   amb_tap(&dl[PT_D2L], t[PT_R4]) - amb_tap(&dl[PT_D1R], t[PT_R5]) - amb_tap(&dl[PT_AP2R], t[PT_R6]) -
                   amb_tap(&dl[PT_D2R], t[PT_R7]));
}
/* SIZE and DAMP as amb-1's knobs (SIZE 0 .. 127: decay 0.6 .. 0.975; DAMP 60: amb-1's 0.45), once a block */
static inline float amb_plate_knobs(amb_plate_t *pl)
{
    float decay_t = 0.6f + 0.375f * (float)song.g[G_RSIZE] * (1.0f / 127.0f);
    pl->decay += (decay_t - pl->decay) * 0.002f;
    pl->dd2 = fm_clampf(pl->decay + 0.15f, 0.25f, 0.5f);
    return 0.9f - (float)song.g[G_RDAMP] * 0.0075f;
}

/* --------------------------------------------- the echoes (amb-1 tape and Echoplex) --- */
#define AMB_PLEX_N 7100                             /* 160 ms */
#define AMB_PLEX_TAP 2700                           /* the right's tap: ~100 ms */
typedef struct {
    int16_t *tape, *plex;
    int32_t tape_n, tape_w, tape_ph, plex_w;
    float tape_acc, tape_lp, tape_hp, tape_p1, tape_p2, tape_c1, tape_c2, echo_len, echo_len_t, echo_fb;
    float head2, head2_t, head2_v, fb_lean, hg[4];
    uint32_t head_left, rng;
    float mod_v[4], mod_t[4], mod_r[4];
    uint32_t mod_left[4];
    float plex_lp, plex_lp2;
} amb_echo_t;
/* (the tape and the Echoplex cleared here if clear; the device's are, a part a block, before: fx.c fx_buses) */
static void amb_echo_init(amb_echo_t *e, int16_t *tape, int32_t tape_n, int16_t *plex, int clear)
{
    memset(e, 0, sizeof *e);
    if (clear) {
        memset(tape, 0, (uint32_t)tape_n * sizeof(int16_t));
        memset(plex, 0, AMB_PLEX_N * sizeof(int16_t));
    }
    e->tape = tape;
    e->tape_n = tape_n;
    e->plex = plex;
    e->head2 = e->head2_t = 0.75f;
    e->hg[0] = e->hg[3] = 1.0f;
    e->rng = 0x2545F491u;
}
static float amb_frand(amb_echo_t *e)
{
    uint32_t s = e->rng;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    e->rng = s;
    return (float)(s >> 8) * (1.0f / 16777216.0f);
}
static inline float amb_tape_read(const amb_echo_t *e, float d)   /* d quarter-rate samples ago */
{
    int32_t k = (int32_t)d, i0 = e->tape_w - k, i1;
    float f = d - (float)k, a, c;
    i1 = i0 - 1;
    if (i0 < 0)
        i0 += e->tape_n;
    if (i1 < 0)
        i1 += e->tape_n;
    a = (float)e->tape[i0];
    c = (float)e->tape[i1];
    return (a + (c - a) * f) * (1.0f / 12000.0f);
}
static inline int16_t amb_s16(float x)
{
    x *= 12000.0f;
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
}
/* once a block: the echo's length (DLY TIME, down to fit the tape) and feedback (DLY FDBK) */
static void amb_echo_knobs(amb_echo_t *e)
{
    float lt = (float)div_samples((uint32_t)song.g[G_DTIME]) * 0.25f;   /* (the tape runs at a quarter rate) */
    uint32_t div = (uint32_t)song.g[G_DTIME];
    while (lt > (float)(e->tape_n - 8) && div < 10u)                  /* too long: the next shorter value */
        lt = (float)div_samples(div = div ? div - 1u : 0u) * 0.25f;
    e->echo_len_t = fm_clampf(lt, 64.0f, (float)(e->tape_n - 8));
    if (e->echo_len <= 0.0f)
        e->echo_len = e->echo_len_t;
    e->echo_fb = 0.3f + (float)song.g[G_DFDBK] * 0.0045f;            /* (FDBK 60: 0.57; 120: 0.84) */
}
/* once a block: the slow random motion, where the heads sit in the field, the feedback's lean, the second head's slide
 * (amb-1 mods_step, heads_step; its mod 5 the wow, 6 and 7 the heads' places, 8 the lean: here 0, 1, 2, 3) */
static void amb_heads(amb_echo_t *e, uint32_t n, float motion)
{
    static const float HEAD2[5] = {0.75f, 0.6667f, 0.625f, 0.5f, 0.375f};
    float want, vmax;
    int32_t i;
    for (i = 0; i < 4; i++) {
        if (e->mod_left[i] <= n) {
            float secs = 7.0f + 33.0f * amb_frand(e);
            e->mod_t[i] = amb_frand(e) * 2.0f - 1.0f;
            e->mod_left[i] = (uint32_t)(secs * (float)FS);
            e->mod_r[i] = (float)n / (secs * (float)FS * 0.5f);
        } else {
            e->mod_left[i] -= n;
        }
        e->mod_v[i] += (e->mod_t[i] - e->mod_v[i]) * fm_minf(e->mod_r[i], 1.0f);
    }
    for (i = 0; i < 2; i++) {                      /* the heads' pan gains: cos and sin of 0 .. pi/2 (amb-1's) */
        float p = fm_clampf((i ? 0.7f : -0.7f) + 0.9f * motion * e->mod_v[1 + i], -1.0f, 1.0f) * (FM_PI * 0.25f) +
                  FM_PI * 0.25f;
        e->hg[2 * i] = fm_cosf(p);
        e->hg[2 * i + 1] = fm_sinf(p);
    }
    e->fb_lean = 0.25f * motion * (1.0f + e->mod_v[3]);
    if (e->head_left <= n) {
        e->head_left = (uint32_t)((20.0f + 40.0f * amb_frand(e)) * (float)FS);
        if (amb_frand(e) < motion)
            e->head2_t = HEAD2[(int32_t)(amb_frand(e) * 4.999f)];
    } else {
        e->head_left -= n;
    }
    vmax = 0.008f / fm_maxf(e->echo_len, 100.0f);
    want = fm_clampf((e->head2_t - e->head2) * (1.0f / (2.0f * 11025.0f)), -vmax, vmax);
    e->head2_v += (want - e->head2_v) * fm_minf((float)n * (1.0f / (float)FS), 1.0f);
}
static inline void amb_echoes(amb_echo_t *e, float in, float motion, float color, float *el, float *er, float *xl,
                              float *xr)
{
    float y = (float)e->plex[e->plex_w] * (1.0f / 12000.0f);
    int32_t t = e->plex_w - AMB_PLEX_TAP;
    e->plex_lp += 0.35f * (y - e->plex_lp);
    e->plex_lp2 += 0.35f * ((float)e->plex[t < 0 ? t + AMB_PLEX_N : t] * (1.0f / 12000.0f) - e->plex_lp2);
    e->plex[e->plex_w] = amb_s16(in + 0.55f * e->plex_lp);
    if (++e->plex_w >= AMB_PLEX_N)
        e->plex_w = 0;
    *xl = e->plex_lp;
    *xr = e->plex_lp2;
    e->tape_acc += in + 0.3f * e->plex_lp;
    if (++e->tape_ph >= 4) {                        /* the tape, a quarter of the time */
        float d, h1, h2, fb;
        e->tape_ph = 0;
        e->echo_len += (e->echo_len_t - e->echo_len) * 0.001f;
        e->head2 += e->head2_v;
        d = e->echo_len * (1.0f + 0.003f * motion * e->mod_v[0]);   /* wow */
        if (d > (float)(e->tape_n - 4))
            d = (float)(e->tape_n - 4);
        h1 = amb_tape_read(e, d);
        h2 = amb_tape_read(e, d * e->head2);
        e->tape_lp += color * (h1 + e->fb_lean * (h2 - h1) - e->tape_lp);   /* each time round: darker, thinner */
        e->tape_hp += 0.03f * (e->tape_lp - e->tape_hp);
        fb = e->tape_lp - e->tape_hp;
        if (++e->tape_w >= e->tape_n)
            e->tape_w = 0;
        e->tape[e->tape_w] = amb_s16(e->tape_acc * 0.25f + e->echo_fb * fb);
        e->tape_acc = 0.0f;
        e->tape_p1 = e->tape_c1;
        e->tape_p2 = e->tape_c2;
        e->tape_c1 = h1;
        e->tape_c2 = h2;
    }
    {
        float f = (float)e->tape_ph * 0.25f;
        float h1 = e->tape_p1 + (e->tape_c1 - e->tape_p1) * f, h2 = e->tape_p2 + (e->tape_c2 - e->tape_p2) * f;
        *el = e->hg[0] * h1 + e->hg[2] * h2;
        *er = e->hg[1] * h1 + e->hg[3] * h2;
    }
}
static inline float amb_motion(void) { return (float)clamp(song.g[G_GMOT], 0, 127) * (1.0f / 127.0f); }
static inline float amb_color(void) { return 0.2f + (float)song.g[G_DCOLOR] * 0.005f; }   /* (COLR 70: amb-1's 0.55) */

/* ------------------------------------------- REVERB TYPE PLATE, DELAY TYPE AMB (fx.c) --- */
static amb_plate_t amb_dp;                          /* the device's plate (its lines: fx.c rv_u.pl) */
static amb_echo_t amb_de;                           /* the device's echoes (their tape: fx.c dly_buf) */
#define AMB_TAPE_DEV 58000                          /* 5.3 s at 11.025 kHz, and the Echoplex after it: 65100 of
                                                     * the device's 65536 */
#ifndef AMB_PLATE_GAIN
#define AMB_PLATE_GAIN 0.45f                        /* (amb-1 mixes its plate x 1.3: here as loud as HALL, within 1 dB) */
#endif
#ifndef AMB_ECHO_GAIN
#define AMB_ECHO_GAIN 1.0f
#endif
static float amb_pm, amb_ps;                        /* PLATE: the last mid and side out */
/* PLATE: the reverb send (rev_in, two samples averaged: half the rate) through the plate; the mid to out (the wet
 * bus), the side to rev_side (as HALL's: + left, - right; fx.c rev_side_mix), each odd output sample between the
 * plate's last two */
static __attribute__((noinline)) void rev_plate(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    const float sc = 1.0f / 32768.0f, og = AMB_PLATE_GAIN * 1.3f * 0.5f * 32768.0f;
    float d = amb_plate_knobs(&amb_dp), damp = 1.0f - (1.0f - d) * (1.0f - d);   /* (at half the rate: two steps') */
    float pm = amb_pm, ps = amb_ps;
    uint32_t i;
    for (i = 0; i + 1u < n; i += 2u) {
        float l, r, m, s;
        amb_plate(&amb_dp, (float)(rev_in[i] + rev_in[i + 1u]) * (0.5f * 0.8f) * sc, damp, &l, &r);
        m = (l + r) * og;
        s = (l - r) * og;
        out[i] += (int32_t)((pm + m) * 0.5f);
        rev_side[i] = (int32_t)((ps + s) * 0.5f);
        out[i + 1u] += (int32_t)m;
        rev_side[i + 1u] = (int32_t)s;
        pm = m;
        ps = s;
    }
    amb_pm = pm;
    amb_ps = ps;
    hl.side = 1;
}
/* AMB: the delay send (dly_in) through the echoes; their mid to wet, their side to wside, DLY MIX their level (90:
 * amb-1's); a share of them into the reverb's send (rev_in), as amb-1's echoes feed its plate */
static __attribute__((noinline)) void dly_amb(const int32_t *dly_in, int32_t *rev_in, int32_t *wet, uint32_t n)
{
    const float sc = 1.0f / 32768.0f, motion = amb_motion(), color = amb_color();
    const float wg = (float)song.g[G_DMIX] * (1.0f / 90.0f) * AMB_ECHO_GAIN * 0.5f * 32768.0f;
    uint32_t i;
    amb_echo_knobs(&amb_de);
    amb_heads(&amb_de, n, motion);
    for (i = 0; i < n; i++) {
        float el, er, xl, xr, l, r;
        amb_echoes(&amb_de, (float)dly_in[i] * sc, motion, color, &el, &er, &xl, &xr);
        l = 0.75f * el + 0.3f * xl;
        r = 0.75f * er + 0.3f * xr;
        wet[i] += (int32_t)((l + r) * wg);
        wside[i] += (int32_t)((l - r) * wg);
        rev_in[i] += (int32_t)((el + er + xl + xr) * (0.15f * 32768.0f));
    }
}
