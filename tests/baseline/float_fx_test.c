/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (Blue Hare, 2026-10-17) Host test of the floating-point effects, with the firmware's sources (through hostsim.c):
 *   build/host/float_fx_test            (run_tests.sh builds and runs it)
 *   fastmath   fastmath.h (X0X's, Charles Vestal's) against libm, the bounds its header promises
 *   EQ         fx.c eq_block: every band's gain at its frequency and away from it, 0 dB passed bit for bit, the glide
 *   PLATE      fx_amb.c rev_plate (half rate): a tail, stereo, SIZE longer, silence after, nothing but numbers
 *   REVERB     the global reverb's types (2026-10-18): DRY no tail, SPRING and PLATE tails (the AMB delay type gone) */
#define main hostsim_main
#include "../lib/hostsim.c"
#undef main
#include <math.h>

static int bad;
static uint32_t hs_seed = 12345u;
static uint32_t hs_rand(void) { return hs_seed = hs_seed * 1664525u + 1013904223u, hs_seed >> 8; }
static void check(const char *what, int ok)
{
    printf("float fx: %-82s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static double worst(double a, double b) { return a > b ? a : b; }

static void test_fastmath(void)
{
    double e_exp2 = 0, e_sin = 0, e_cos = 0, e_tanh = 0, e_sqrt = 0, e_pow = 0;
    char msg[200];
    int i;
    for (i = 0; i <= 200000; i++) {
        float x = -100.0f + 200.0f * (float)i / 200000.0f, t = -8.0f + 16.0f * (float)i / 200000.0f;
        float e = -20.0f + 40.0f * (float)i / 200000.0f, q = 1e-3f + 1e3f * (float)i / 200000.0f;
        e_exp2 = worst(e_exp2, fabs(fm_exp2f(e) - exp2(e)) / exp2(e));
        e_sin = worst(e_sin, fabs(fm_sinf(x) - sin(x)));
        e_cos = worst(e_cos, fabs(fm_cosf(x) - cos(x)));
        e_tanh = worst(e_tanh, fabs(fm_tanhf(t) - tanh(t)));
        e_sqrt = worst(e_sqrt, fabs(fm_sqrtf(q) - sqrt(q)) / sqrt(q));
        e_pow = worst(e_pow, fabs(fm_powf(q, 0.37f) - pow(q, 0.37)) / pow(q, 0.37));
    }
    snprintf(msg, sizeof msg, "fastmath against libm: exp2 %.1e sin %.1e cos %.1e tanh %.1e sqrt %.1e pow %.1e", e_exp2,
             e_sin, e_cos, e_tanh, e_sqrt, e_pow);
    check(msg, e_exp2 < 1e-6 && e_sin < 2e-6 && e_cos < 2e-6 && e_tanh < 1e-6 && e_sqrt < 1e-6 && e_pow < 2e-5);
}

/* the EQ's gain (dB) at hz: a sine through eq_block after it settles, against the sine itself */
static double eq_gain(double hz)
{
    static int32_t b[2 * CTL];
    double in = 0, out = 0;
    uint32_t blk, i, s = 0;
    memset(eq.s, 0, sizeof eq.s);
    for (blk = 0; blk < 2000u; blk++)
        for (i = 0; i < CTL; i++, s++) {
            double v = 8000.0 * sin(2.0 * M_PI * hz * s / FS);
            b[2 * i] = b[2 * i + 1] = (int32_t)v;
            if (i == CTL - 1u) {
                eq_block(b, CTL);
                if (blk >= 1000u) {
                    uint32_t j;
                    for (j = 0; j < CTL; j++) {
                        double x = 8000.0 * sin(2.0 * M_PI * hz * (s - (CTL - 1u) + j) / FS);
                        in += x * x;
                        out += (double)b[2 * j] * b[2 * j];
                    }
                }
            }
        }
    return 10.0 * log10(out / in);
}
static void eq_set(int lo, int lm, int hm, int hi)
{
    uint32_t k;
    static int32_t b[2 * CTL];
    song.g[G_EQLO] = (int16_t)lo, song.g[G_EQLM] = (int16_t)lm, song.g[G_EQHM] = (int16_t)hm, song.g[G_EQHI] = (int16_t)hi;
    for (k = 0; k < 200u; k++)                          /* (the glide: 0.25 dB a block) */
        eq_block(b, CTL);
}
static void test_eq(void)
{
    static int32_t a[2 * CTL], b[2 * CTL];
    uint32_t i, k, same = 1, blocks = 0;
    double g[8];
    char msg[240];
    eq_set(0, 0, 0, 0);
    for (k = 0; k < 50u; k++) {
        for (i = 0; i < 2u * CTL; i++)
            a[i] = b[i] = (int32_t)(hs_rand() % 60001u) - 30000;
        eq_block(b, CTL);
        same &= !memcmp(a, b, sizeof a);
    }
    check("EQ at 0 dB: the mix passed bit for bit", same);
    eq_set(12, 0, 0, 0);
    g[0] = eq_gain(30), g[1] = eq_gain(2000);
    eq_set(0, 12, 0, 0);
    g[2] = eq_gain(400), g[3] = eq_gain(4000);
    eq_set(0, 0, -12, 0);
    g[4] = eq_gain(2500), g[5] = eq_gain(200);
    eq_set(0, 0, 0, 12);
    g[6] = eq_gain(18000), g[7] = eq_gain(500);
    snprintf(msg, sizeof msg, "EQ bands: LOW +12 %.1f dB at 30 Hz (%.1f at 2k), LMID +12 %.1f at 400 (%.1f at 4k), "
             "HMID -12 %.1f at 2.5k (%.1f at 200)", g[0], g[1], g[2], g[3], g[4], g[5]);
    check(msg, g[0] > 10.5 && g[0] < 12.5 && fabs(g[1]) < 0.5 && fabs(g[2] - 12) < 0.3 && g[3] < 2.5 &&
               fabs(g[4] + 12) < 0.3 && g[5] > -1.5);
    snprintf(msg, sizeof msg, "  HIGH +12 %.1f dB at 18 kHz (%.1f at 500 Hz)", g[6], g[7]);
    check(msg, g[6] > 10.5 && g[6] < 12.5 && fabs(g[7]) < 0.5);
    song.g[G_EQLO] = 12;
    song.g[G_EQHI] = 0;
    for (blocks = 0; blocks < 200u && eq.g[0] != 12.0f; blocks++)
        eq_block(b, CTL);
    snprintf(msg, sizeof msg, "  a turn glides (0 -> 12 dB in %u blocks, %.0f ms: no click)", blocks,
             blocks * 1000.0 * CTL / FS);
    check(msg, blocks >= 40u && blocks <= 60u);
    eq_set(0, 0, 0, 0);
}

static void test_plate(void)
{
    static int32_t in[CTL], wetb[CTL];
    double e[3] = {0, 0, 0}, side = 0, last = 0;
    uint32_t blk, i, sz, nan = 0;
    char msg[200];
    for (sz = 0; sz < 2u; sz++) {
        song.g[G_RSIZE] = sz ? 127 : 0;
        song.g[G_RDAMP] = 60;
        amb_plate_init(&amb_dp, PL_MEM, AMB_NUM_HALF, AMB_PRE_HALF);
        amb_dp.decay = 0.6f + 0.375f * (float)song.g[G_RSIZE] / 127.0f;
        amb_pm = amb_ps = 0;
        for (blk = 0; blk < (uint32_t)(6.0 * FS / CTL); blk++) {
            for (i = 0; i < CTL; i++) {
                in[i] = blk < 4u ? (int32_t)(hs_rand() % 20001u) - 10000 : 0;
                wetb[i] = 0;
            }
            rev_plate(in, wetb, CTL);
            for (i = 0; i < CTL; i++) {
                double t = (double)blk * CTL / FS, v = wetb[i];
                nan += wetb[i] != wetb[i];
                if (t > 0.1 && t < 0.5)
                    side += (double)rev_side[i] * rev_side[i];
                if (t > 1.0 && t < 1.5)
                    e[sz] += v * v;
                if (sz == 0 && t > 5.5)
                    last += v * v;
            }
        }
    }
    snprintf(msg, sizeof msg, "PLATE: a stereo tail (side %.0f), SIZE 127 longer (1 .. 1.5 s: %.1f dB over SIZE 0), "
             "SIZE 0 gone by 5.5 s", sqrt(side), 10 * log10((e[1] + 1) / (e[0] + 1)));
    check(msg, side > 1e6 && e[1] > e[0] * 10 && last < 1.0 && !nan);
}

/* a reverb's energy (0.2 .. 2 s, both sides) after a 0.1 s burst into its send, SIZE 90: PLATE against HALL */
static double rev_energy(int rt)
{
    static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
    uint32_t k, i;
    double e = 0;
    host_tracks_init();
    song.g[G_RTYPE] = (int16_t)rt;
    song.g[G_RSIZE] = 90;
    song.g[G_DMIX] = 0;
    memset(c, 0, sizeof c);
    memset(d, 0, sizeof d);
    hs_seed = 12345u;
    for (k = 0; k < 3u * FS / CTL; k++) {
        for (i = 0; i < CTL; i++) {
            r[i] = k * CTL < FS / 10u ? (int32_t)(hs_rand() % 16385u) - 8192 : 0;
            w[i] = 0;
        }
        hl.side = 0;
        fx_buses(c, d, r, w, CTL);
        if (k * CTL > FS / 5u && k * CTL < 2u * FS)
            for (i = 0; i < CTL; i++) {
                double l = w[i] + (hl.side ? rev_side[i] : 0), rr = w[i] - (hl.side ? rev_side[i] : 0);
                e += l * l + rr * rr;
            }
    }
    return e;
}

int main(void)
{
    memset(&song, 0, sizeof song);
    for (uint32_t i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    test_fastmath();
    test_eq();
    test_plate();
    {   /* (2026-10-18) the global reverb's types: DRY 0, SPRING 1, GATED 2, PLATE 3 (HALL and the AMB delay gone) */
        char msg[160];
        double pl, sp, dry;
        memset(&fx, 0, sizeof fx);
        dry = rev_energy(0);
        sp = rev_energy(1);
        pl = rev_energy(3);
        snprintf(msg, sizeof msg, "REVERB DRY: no tail from silence; SPRING and PLATE tails (%.1f dB apart, SIZE 90, 0.2 .. 2 s)",
                 10.0 * log10(pl / (sp + 1)));
        check(msg, dry == 0.0 && sp > 1e9 && pl > 1e9);
    }
    printf(bad ? "float fx: %d FAILED\n" : "float fx: all passed\n", bad);
    return bad ? 1 : 0;
}
