/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* DELAY TYPE DUAL (G_DTYPE 4, fx.c fx_buses): the delay bus alone, a click into its send. The left's first echo at
 * TIME, the right's at three quarters of it, each on its own side; the echoes go on and die away (FDBK); at FDBK 127
 * the loop stays bounded; it is in every build's list */
#define main hostsim_main
#include "../lib/hostsim.c"
#undef main

static int bad;
static void ck(const char *what, int ok) { printf("dual: %-104s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static int32_t L[4 * FS], R[4 * FS];
/* a click at 0, n s of the bus: L and R (mid + side, mid - side) */
static void run(uint32_t fdbk, uint32_t n)
{
    static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
    uint32_t b, i, k = 0;
    host_tracks_init();
    song.g[G_DTYPE] = 4;
    song.g[G_DFDBK] = (int16_t)fdbk;
    song.g[G_DMIX] = 100;
    song.g[G_DCOLOR] = 127;
    memset(dly_buf, 0, sizeof dly_buf);
    fx.dly_lp = fx.dly_lp2 = 0;
    for (b = 0; b < n * FS / CTL; b++) {
        for (i = 0; i < CTL; i++) {
            c[i] = r[i] = 0;
            d[i] = b == 0 && i < 8u ? 20000 : 0;
        }
        memset(wside, 0, sizeof wside);
        fx_buses(c, d, r, w, CTL);
        for (i = 0; i < CTL; i++, k++) {
            L[k] = w[i] + wside[i];
            R[k] = w[i] - wside[i];
        }
    }
}
static uint32_t first(const int32_t *x, uint32_t n, int32_t th)
{
    uint32_t i;
    for (i = 16; i < n; i++)
        if (x[i] > th || x[i] < -th)
            return i;
    return 0;
}

int main(void)
{
    char what[200];
    uint32_t dl, fl, fr, i;
    double e1 = 0, e2 = 0;
    int32_t pk = 0;
    run(70, 3);
    dl = delay_samples();
    fl = first(L, 3u * FS, 300);
    fr = first(R, 3u * FS, 300);
    snprintf(what, sizeof what, "the left's first echo at TIME (%u of %u samples), the right's at 3/4 of it (%u of %u)", fl, dl, fr,
             dl * 3u / 4u);
    ck(what, fl >= dl && fl < dl + 16u && fr >= dl * 3u / 4u && fr < dl * 3u / 4u + 16u);
    ck("each on its own side at first (the right silent at the left's first echo's start, and the other way round)",
       abs(R[fr]) > 4 * abs(L[fr]) + 1 && abs(L[fl]) > 0);
    for (i = 0; i < FS / 2u; i++) {
        e1 += (double)L[FS / 2u + i] * L[FS / 2u + i] + (double)R[FS / 2u + i] * R[FS / 2u + i];
        e2 += (double)L[2u * FS + i] * L[2u * FS + i] + (double)R[2u * FS + i] * R[2u * FS + i];
    }
    snprintf(what, sizeof what, "FDBK 70: the echoes go on and die away (2 .. 2.5 s %.0f dB under 0.5 .. 1 s)", 10 * log10(e1 / (e2 + 1)));
    ck(what, e1 > 0 && e2 < e1 * 0.5);
    run(127, 4);
    for (i = 0; i < 4u * FS; i++) {
        int32_t a = abs(L[i]) > abs(R[i]) ? abs(L[i]) : abs(R[i]);
        pk = a > pk ? a : pk;
    }
    snprintf(what, sizeof what, "FDBK 127: bounded (peak %d over 4 s)", pk);
    ck(what, pk < 200000);
    ck("DELAY TYPE 4 is DUAL, in every build's list", !strcmp(N_DTYPE[4], "DUAL") && GP[G_DTYPE].max >= 4);
    printf("%s\n", bad ? "DUAL TEST FAILED" : "dual test passed");
    return bad != 0;
}
