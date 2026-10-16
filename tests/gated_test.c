/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* GATED (REVERB TYPE 3, fx.c rev_gated): the reverb bus alone, a 30 ms burst into its send. While the gate holds, the
 * tail is HALL's (the same level); after the hold (SIZE's: SIZE 40 ~200 ms) it is cut: silent within ~20 ms while HALL
 * rings on; a longer SIZE holds longer; a second hit opens it again; it is in every build's list */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void ck(const char *what, int ok) { printf("gated: %-104s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

/* model rt at SIZE size: a burst at 0 (and at `again` s, if > 0); the output's energy over [a, b) seconds */
static double band(uint32_t rt, uint32_t size, double a, double b, double again)
{
    static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
    uint32_t i, k, s = 0x1234567u, total = (uint32_t)(b * FS / CTL) + 1u;
    double e = 0;
    host_tracks_init();
    song.g[G_RTYPE] = (int16_t)rt;
    song.g[G_RSIZE] = (int16_t)size;
    song.g[G_RDAMP] = 60;
    song.g[G_DMIX] = 0;
    memset(c, 0, sizeof c);
    memset(d, 0, sizeof d);
    for (k = 0; k < 4u; k++) {                        /* (the model in: a switch, silence) */
        memset(r, 0, sizeof r);
        memset(w, 0, sizeof w);
        memset(wside, 0, sizeof wside);
        fx_buses(c, d, r, w, CTL);
    }
    for (k = 0; k < total; k++) {
        double t = (double)k * CTL / FS;
        for (i = 0; i < CTL; i++) {
            s ^= s << 13, s ^= s >> 17, s ^= s << 5;
            r[i] = t < 0.03 || (again > 0 && t >= again && t < again + 0.03) ? (int32_t)(int16_t)(s >> 16) / 2 : 0;
            w[i] = 0;
            wside[i] = 0;
        }
        fx_buses(c, d, r, w, CTL);
        if (t >= a && t < b)
            for (i = 0; i < CTL; i++)
                e += (double)w[i] * w[i] + (double)rev_side[i] * rev_side[i];
    }
    return e;
}

int main(void)
{
    char what[200];
    double h_in = band(2, 40, 0.05, 0.15, 0), g_in = band(3, 40, 0.05, 0.15, 0);
    double h_out = band(2, 40, 0.30, 0.60, 0), g_out = band(3, 40, 0.30, 0.60, 0);
    double g_long = band(3, 110, 0.30, 0.40, 0), g_again = band(3, 40, 0.80, 0.90, 0.70);
    ck("REVERB TYPE 3 is GATED, on every build", !strcmp(N_RTYPE[3], "GATED") && GP[G_RTYPE].max >= 3);
    snprintf(what, sizeof what, "SIZE 40: while it holds (50 .. 150 ms) as loud as HALL (%+.1f dB)", 10 * log10(g_in / h_in));
    ck(what, fabs(10 * log10(g_in / h_in)) < 1.0);
    snprintf(what, sizeof what, "after its hold (300 .. 600 ms): cut, %.0f dB under HALL's ringing tail", 10 * log10(g_out / h_out + 1e-30));
    ck(what, g_out < h_out * 1e-4);
    snprintf(what, sizeof what, "SIZE 110 holds longer: open at 300 .. 400 ms (%.0f dB of SIZE 40's band in)", 10 * log10(g_long / g_in));
    ck(what, g_long > g_in * 0.05);
    snprintf(what, sizeof what, "a second hit at 700 ms opens it again (%.0f dB of the first)", 10 * log10(g_again / g_in));
    ck(what, g_again > g_in * 0.3);
    printf("%s\n", bad ? "GATED TEST FAILED" : "gated test passed");
    return bad != 0;
}
