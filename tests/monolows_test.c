/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* MONO LOWS (fx.c mono_lows, the master before the bus compressor): a 40 Hz tone hard left comes out on both sides
 * nearly alike, 60 Hz mostly; a 2 kHz tone hard left stays left (20 dB and more); a centred signal passes untouched; silence after ends in exact zeros */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static int bad;
static void ck(const char *what, int ok) { printf("monolows: %-100s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

/* f Hz hard left (or centred: both), 1 s; the energy left and right over its last half */
static void tone(double f, int centred, double *el, double *er, int *changed)
{
    static int32_t o[2 * CTL];
    uint32_t b, i, k = 0;
    *el = *er = 0;
    *changed = 0;
    ml_s1 = ml_s2 = 0;
    for (b = 0; b < FS / CTL; b++) {
        for (i = 0; i < CTL; i++, k++) {
            int32_t x = (int32_t)(12000.0 * sin(2.0 * M_PI * f * k / FS));
            o[2 * i] = x;
            o[2 * i + 1] = centred ? x : 0;
        }
        {
            int32_t before[2 * CTL];
            memcpy(before, o, sizeof o);
            mono_lows(o, CTL);
            *changed |= memcmp(before, o, sizeof o) != 0;
        }
        if (b >= FS / CTL / 2u)
            for (i = 0; i < CTL; i++) {
                *el += (double)o[2 * i] * o[2 * i];
                *er += (double)o[2 * i + 1] * o[2 * i + 1];
            }
    }
}

int main(void)
{
    double el, er;
    int ch;
    char what[160];
    uint32_t b, i, nz = 0;
    static int32_t o[2 * CTL];
    tone(40.0, 0, &el, &er, &ch);
    snprintf(what, sizeof what, "40 Hz hard left: on both sides (right %.1f dB of left)", 10 * log10(er / el));
    ck(what, fabs(10 * log10(er / el)) < 2.0);
    tone(60.0, 0, &el, &er, &ch);
    snprintf(what, sizeof what, "60 Hz hard left: mostly both (right %.1f dB of left)", 10 * log10(er / el));
    ck(what, fabs(10 * log10(er / el)) < 4.0);
    tone(2000.0, 0, &el, &er, &ch);
    snprintf(what, sizeof what, "2 kHz hard left: stays left (right %.1f dB under it: the high-pass's phase, 7 degrees)",
             10 * log10(el / (er + 1e-9)));
    ck(what, el > er * 100.0);
    tone(60.0, 1, &el, &er, &ch);
    ck("a centred 60 Hz: untouched", !ch);
    for (b = 0; b < 2u * FS / CTL; b++) {             /* (after the 2 kHz one: silence) */
        memset(o, 0, sizeof o);
        mono_lows(o, CTL);
        if (b >= FS / CTL)
            for (i = 0; i < 2u * CTL; i++)
                nz += o[i] != 0;
    }
    ck("silence after a tone: exact zeros within a second", !nz);
    printf("%s\n", bad ? "MONOLOWS TEST FAILED" : "monolows test passed");
    return bad != 0;
}
