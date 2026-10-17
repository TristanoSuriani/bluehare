/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* CONCERT on the device (REVERB TYPE 4, fx.c rev_concert): the reverb bus alone, a 30 ms burst into its send. Its level
 * near HALL's (SIZE 90, DAMP 60); a longer SIZE rings longer; stable at the corners (SIZE 127, DAMP 0); the tail ends
 * in exact silence; stereo (its side); a model change and back without trouble; in every build's list */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void ck(const char *what, int ok) { printf("concert: %-100s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

/* model rt at SIZE size, DAMP damp: a burst at 0; the energy over [a, b) s (mid and side), the peak, the last nonzero
 * sample's time (s), the side's share */
static double band(uint32_t rt, uint32_t size, uint32_t damp, double a, double b, int32_t *peak, double *last, double *side)
{
    static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
    uint32_t i, k, s = 0x1234567u, total = (uint32_t)(b * FS / CTL) + 1u;
    double e = 0, es = 0;
    host_tracks_init();
    song.g[G_RTYPE] = (int16_t)rt;
    song.g[G_RSIZE] = (int16_t)size;
    song.g[G_RDAMP] = (int16_t)damp;
    song.g[G_DMIX] = 0;
    memset(c, 0, sizeof c);
    memset(d, 0, sizeof d);
    for (k = 0; k < 4u; k++) {                        /* (the model in: a switch, silence) */
        memset(r, 0, sizeof r);
        memset(w, 0, sizeof w);
        memset(wside, 0, sizeof wside);
        fx_buses(c, d, r, w, CTL);
    }
    if (peak)
        *peak = 0;
    if (last)
        *last = 0;
    for (k = 0; k < total; k++) {
        double t = (double)k * CTL / FS;
        for (i = 0; i < CTL; i++) {
            s ^= s << 13, s ^= s >> 17, s ^= s << 5;
            r[i] = t < 0.03 ? (int32_t)(int16_t)(s >> 16) / 2 : 0;
            w[i] = 0;
            wside[i] = 0;
        }
        fx_buses(c, d, r, w, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t m = w[i], sd = hl.side ? rev_side[i] : 0, x = m < 0 ? -m : m;
            if (peak && x > *peak)
                *peak = x;
            if (last && (m || sd))
                *last = t;
            if (t >= a && t < b) {
                e += (double)m * m + (double)sd * sd;
                es += (double)sd * sd;
            }
        }
    }
    if (side)
        *side = e > 0 ? es / e : 0;
    return e;
}

int main(void)
{
    char what[200];
    int32_t pk;
    double last, side;
    double h = band(2, 90, 60, 0.05, 1.0, 0, 0, 0), cc = band(4, 90, 60, 0.05, 1.0, 0, 0, &side);
    double c40a = band(4, 40, 60, 0.6, 1.2, 0, 0, 0), c110a = band(4, 110, 60, 0.6, 1.2, 0, 0, 0);
    ck("REVERB TYPE 4 is CONCERT on the device (5 PLATE after it)", !strcmp(N_RTYPE[4], "CONCERT") && !strcmp(N_RTYPE[5], "PLATE") &&
       GP[G_RTYPE].max == 5);
    snprintf(what, sizeof what, "SIZE 90 DAMP 60: its level near HALL's (%+.1f dB over 50 ms .. 1 s)", 10 * log10(cc / h));
    ck(what, fabs(10 * log10(cc / h)) < 3.0);
    snprintf(what, sizeof what, "stereo: the side %.0f %% of its energy", 100 * side);
    ck(what, side > 0.2 && side < 0.8);
    snprintf(what, sizeof what, "SIZE 110 rings longer than SIZE 40 (0.6 .. 1.2 s: %.0f dB more)", 10 * log10(c110a / (c40a + 1)));
    ck(what, c110a > c40a * 10.0);
    band(4, 127, 0, 0, 12.0, &pk, 0, 0);
    snprintf(what, sizeof what, "SIZE 127 DAMP 0: stable (peak %d over 12 s)", pk);
    ck(what, pk > 0 && pk < 400000);
    band(4, 40, 60, 0, 15.0, 0, &last, 0);
    snprintf(what, sizeof what, "SIZE 40: the tail ends in exact silence (the last sample at %.1f s of 15)", last);
    ck(what, last > 0.3 && last < 14.0);
    {   /* HALL -> CONCERT -> HALL while it rings: nothing blows up, it goes on */
        static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
        uint32_t k, i;
        int32_t p = 0;
        host_tracks_init();
        song.g[G_RTYPE] = 2;
        song.g[G_RSIZE] = 90;
        for (k = 0; k < FS / CTL; k++) {
            if (k == FS / CTL / 3u)
                song.g[G_RTYPE] = 4;
            if (k == 2u * FS / CTL / 3u)
                song.g[G_RTYPE] = 2;
            for (i = 0; i < CTL; i++) {
                r[i] = k < 40u ? ((int32_t)(k * 7919u + i * 104729u) % 20000) - 10000 : 0;
                w[i] = c[i] = d[i] = 0;
                wside[i] = 0;
            }
            fx_buses(c, d, r, w, CTL);
            for (i = 0; i < CTL; i++)
                p = abs(w[i]) > p ? abs(w[i]) : p;
        }
        snprintf(what, sizeof what, "HALL -> CONCERT -> HALL while it rings: bounded (peak %d)", p);
        ck(what, p < 400000);
    }
    printf("%s\n", bad ? "CONCERT TEST FAILED" : "concert test passed");
    return bad != 0;
}
