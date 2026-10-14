/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The native image's reverbs (FELUCCA_NATIVE: fx.c includes this; REVERB TYPE 3 .. 5, params.c N_RTYPE). On the FM-1 a
 * song's CONCERT, CAVERN or SHIMMER loads as HALL (project.c clamps the type to the device's last). Floating point, at
 * the full rate, in memory the device does not have (some 0.7 MB):
 *   CONCERT  HALL without the device's economies: 16 lines instead of 8, at 44.1 kHz instead of 22.05 (the air up to
 *            the top), 4 diffusers, every line drifting. SIZE the decay (RT60 0.5 s .. 9 s, ~4 s at 90, as HALL), DAMP
 *            the lines' low-pass (16 kHz .. 1.5 kHz).
 *   CAVERN   a cave: a long gap (70 ms), then the walls' first echoes, six of them, discrete, 60 .. 330 ms, left and
 *            right in turn, then a slow, dark tail from 16 lines three times CONCERT's length (RT60 1.5 s .. 30 s,
 *            ~12 s at 90), its highs gone sooner (DAMP: 8 kHz .. 0.9 kHz).
 *   SHIMMER  a long hall whose tail climbs: an octave up (a two-head pitch shifter in the loop, its window 46 ms),
 *            low-cut at 400 Hz and soft-limited, is added to what goes round (a third), so each pass adds an octave above
 *            (RT60 1 s .. 15 s, ~7 s at 90; DAMP the lines' low-pass, the shimmer's top with it).
 * The model: the send (mono) -> a low cut (~100 Hz) -> a pre-delay -> 4 allpass diffusers (g 0.7) -> 16 delay lines,
 * each read through a slow sine (0.1 .. 0.9 Hz, every line its own; between samples), low-passed, scaled to decay alike
 * (g = 10^(-3 len / (RT60 FS))), then mixed by a 16 x 16 Hadamard matrix (a fast Walsh transform, / 4: lossless) and
 * written back with the send. Out: the even lines' signed sum left, the odd lines' right -> the mid to the wet bus, the
 * side to wside (as ROOM's). The levels: at SIZE 90 and DAMP 60, a burst of noise sends out what HALL does
 * (tests/native_test.c). Silence in ends in silence out (denormals flushed, the output rounded to integers). */
#include <math.h>
#define RN_NL 16u
#define RN_LMAX 8192u                    /* a line's buffer (CAVERN's longest 7237 + the drift) */
#define RN_ER 32768u                     /* the pre-delay and CAVERN's walls behind it (70 + 330 ms: 17640 samples) */
#define RN_SH 4096u                      /* SHIMMER's shifter */
#define RN_SHW 2048.0f                   /* .. its window (samples) */
static const uint16_t RN_LEN[3][RN_NL] = {
    {1153, 1277, 1381, 1499, 1613, 1733, 1867, 1997, 2111, 2239, 2357, 2477, 2591, 2713, 2851, 2969},   /* CONCERT */
    {3001, 3271, 3557, 3833, 4111, 4397, 4673, 4951, 5237, 5519, 5801, 6089, 6373, 6661, 6947, 7237},   /* CAVERN */
    {1499, 1663, 1801, 1949, 2099, 2251, 2423, 2593, 2749, 2909, 3067, 3221, 3371, 3529, 3691, 3853},   /* SHIMMER */
};
static const uint16_t RN_AP[3][4] = {{142, 107, 379, 277}, {229, 173, 613, 449}, {181, 137, 487, 353}};
static const uint16_t RN_PRED[3] = {530, 3087, 880};                 /* pre-delay: 12 ms, 70 ms, 20 ms */
static const uint16_t RN_ERT[6] = {2646, 4630, 6835, 9261, 11907, 14553};   /* CAVERN's walls: 60 .. 330 ms */
static const float RN_ERG[6] = {0.55f, -0.45f, 0.38f, -0.30f, 0.24f, -0.18f};
static const float RN_RT0[3] = {0.5f, 1.5f, 1.0f}, RN_RTK[3] = {18.0f, 20.0f, 15.0f};   /* RT60 = RT0 * RTK^(SIZE/127) */
static const float RN_FC0[3] = {16000.0f, 8000.0f, 14000.0f}, RN_FC1[3] = {1500.0f, 900.0f, 1800.0f};
static const float RN_MOD[3] = {6.0f, 12.0f, 8.0f};                 /* the drift's depth (samples) */
static const float RN_OUT[3] = {0.75f, 0.91f, 0.64f};              /* the outputs' gain (HALL's level: the tests) */
static struct {
    float line[RN_NL][RN_LMAX];
    float ap[4][1024];
    float pre[RN_ER];
    float sh[RN_SH];
    uint32_t w[RN_NL], apw[4], pw, shw;
    float lp[RN_NL], g[RN_NL], kd, hp, shp, shlp, shhp;
    double ph;
    int32_t size, damp, made;            /* what g, kd were made for: SIZE, DAMP, the model + 1 (0: not yet; zero at
                                          * start, so all of this is .bss, nothing to load) */
} rn;

static void rev_native_clear(void)
{
    memset(rn.line, 0, sizeof rn.line);
    memset(rn.ap, 0, sizeof rn.ap);
    memset(rn.pre, 0, sizeof rn.pre);
    memset(rn.sh, 0, sizeof rn.sh);
    memset(rn.lp, 0, sizeof rn.lp);
    rn.hp = rn.shp = rn.shlp = rn.shhp = 0;
    rn.made = 0;
}

static void rn_coefs(uint32_t m)
{
    float rt = RN_RT0[m] * powf(RN_RTK[m], song.g[G_RSIZE] / 127.0f);
    float fc = RN_FC0[m] * powf(RN_FC1[m] / RN_FC0[m], song.g[G_RDAMP] / 127.0f);
    uint32_t k;
    for (k = 0; k < RN_NL; k++)
        rn.g[k] = powf(10.0f, -3.0f * RN_LEN[m][k] / (rt * (float)FS));
    rn.kd = 1.0f - expf(-2.0f * (float)M_PI * fc / (float)FS);
    rn.size = song.g[G_RSIZE];
    rn.damp = song.g[G_RDAMP];
    rn.made = (int32_t)m + 1;
}

static inline float rn_flush(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }

/* the line k's sample d (fractional) behind its write index */
static inline float rn_tap(uint32_t k, float d)
{
    uint32_t len = RN_LMAX, di = (uint32_t)d;
    float f = d - (float)di, *b = rn.line[k];
    uint32_t a = (rn.w[k] + len - di) & (len - 1u), c = (a + len - 1u) & (len - 1u);
    return b[a] + (b[c] - b[a]) * f;
}

/* model rt (3 CONCERT, 4 CAVERN, 5 SHIMMER) on a block of the send: the mid added to out, the side to side */
static __attribute__((noinline)) void rev_native(uint32_t rt, const int32_t *rev_in, int32_t *out, int32_t *side,
                                                 uint32_t n)
{
    uint32_t m = rt - 3u, i, k;
    float mod[RN_NL], kd, gout = RN_OUT[m] * 32768.0f, khp = 1.0f - expf(-2.0f * (float)M_PI * 100.0f / (float)FS);
    float ksh = 1.0f - expf(-2.0f * (float)M_PI * 400.0f / (float)FS);
    if (rn.made != (int32_t)m + 1 || rn.size != song.g[G_RSIZE] || rn.damp != song.g[G_RDAMP])
        rn_coefs(m);
    kd = rn.kd;
    for (k = 0; k < RN_NL; k++) {                     /* the drift, once a block (it moves < 0.02 samples in one) */
        double hz = 0.1 + 0.8 * k / (RN_NL - 1u);
        mod[k] = RN_LEN[m][k] - 2.0f - RN_MOD[m] * (1.0f + (float)sin(rn.ph * hz + k * 1.7));
    }
    rn.ph += 2.0 * M_PI * n / FS;
    if (rn.ph > 2.0 * M_PI * 1000.0)
        rn.ph -= 2.0 * M_PI * 1000.0;
    for (i = 0; i < n; i++) {
        float x = rev_in[i] / 32768.0f * 0.25f, y[RN_NL], l = 0, r = 0, el = 0, er = 0, fb = 0;
        uint32_t h, j;
        rn.hp += (x - rn.hp) * khp;                   /* the low cut */
        x -= rn.hp;
        rn.pre[rn.pw & (RN_ER - 1u)] = x;             /* the pre-delay (and CAVERN's walls) */
        x = rn.pre[(rn.pw - RN_PRED[m]) & (RN_ER - 1u)];
        if (m == 1u)
            for (k = 0; k < 6u; k++) {
                float e = rn.pre[(rn.pw - RN_PRED[m] - RN_ERT[k]) & (RN_ER - 1u)] * RN_ERG[k];
                if (k & 1u)
                    er += e;
                else
                    el += e;
            }
        rn.pw++;
        for (k = 0; k < 4u; k++) {                    /* the diffusers */
            float *b = rn.ap[k], o = b[(rn.apw[k] + 1024u - RN_AP[m][k]) & 1023u], v = x + 0.7f * o;
            b[rn.apw[k] & 1023u] = v;
            x = o - 0.7f * v;
            rn.apw[k]++;
        }
        for (k = 0; k < RN_NL; k++) {                 /* the lines' ends: drifting, low-passed, scaled */
            float o = rn_tap(k, mod[k]);
            rn.lp[k] = rn_flush(rn.lp[k] + (o - rn.lp[k]) * kd);
            y[k] = rn.lp[k] * rn.g[k];
            if (k & 1u)
                r += (k & 2u) ? -y[k] : y[k];
            else
                l += (k & 2u) ? -y[k] : y[k];
            fb += y[k];
        }
        if (m == 2u) {                                /* SHIMMER: an octave up of what goes round, a third of it */
            float p = rn.shp, p2 = p + 0.5f, d1, d2, w1, s;
            uint32_t a1, a2;
            rn.sh[rn.shw & (RN_SH - 1u)] = fb * 0.25f;
            p2 -= p2 >= 1.0f ? 1.0f : 0.0f;
            d1 = (1.0f - p) * RN_SHW + 2.0f, d2 = (1.0f - p2) * RN_SHW + 2.0f;
            a1 = (rn.shw - (uint32_t)d1) & (RN_SH - 1u), a2 = (rn.shw - (uint32_t)d2) & (RN_SH - 1u);
            w1 = sinf((float)M_PI * p);
            w1 *= w1;
            s = rn.sh[a1] * w1 + rn.sh[a2] * (1.0f - w1);
            rn.shp += 1.0f / RN_SHW;                  /* (the heads gain a sample a sample: twice the speed) */
            rn.shp -= rn.shp >= 1.0f ? 1.0f : 0.0f;
            rn.shw++;
            rn.shhp += (s - rn.shhp) * ksh;           /* low cut, the top as the lines' */
            s -= rn.shhp;
            rn.shlp = rn_flush(rn.shlp + (s - rn.shlp) * kd);
            s = tanhf(rn.shlp * 2.0f) * 0.5f;         /* (soft: the climb cannot run away) */
            for (k = 0; k < RN_NL; k++)
                y[k] += s * (k & 1u ? -0.33f : 0.33f) * rn.g[k];   /* (bounded: |s| <= 0.5, the lines' g < 1) */
        }
        for (h = 1; h < RN_NL; h <<= 1)               /* the Hadamard mixing (fast Walsh), / 4 */
            for (k = 0; k < RN_NL; k += h << 1)
                for (j = k; j < k + h; j++) {
                    float a = y[j], b = y[j + h];
                    y[j] = a + b;
                    y[j + h] = a - b;
                }
        for (k = 0; k < RN_NL; k++) {                 /* back into the lines, with the send */
            rn.line[k][rn.w[k] & (RN_LMAX - 1u)] = rn_flush(y[k] * 0.25f + (k & 1u ? -x : x));
            rn.w[k]++;
        }
        l = (l + el) * gout;
        r = (r + er) * gout;
        out[i] += (int32_t)lrintf((l + r) * 0.5f);
        side[i] += (int32_t)lrintf((l - r) * 0.5f);
    }
}
