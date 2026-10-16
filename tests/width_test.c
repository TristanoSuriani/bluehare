/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* Each track's LOW CUT (core.h trk_lowcut; fx.c track_lowcut): the power-on values, its response and its silence after a
 * sound, the song store (a value out of range: OFF). (WIDTH, the Haas effect, was removed 2026-10-16: the store keeps
 * its byte, 0, and a project's WIDTH is read and dropped.) */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"
#include <math.h>

/* the level (dB) of a sine of f Hz after track k's LOW CUT, against the sine's own (1 s, the second half measured) */
static double lowcut_db(uint32_t k, double f)
{
    static int32_t b[CTL];
    double in = 0, out = 0;
    uint32_t t, i;
    tmix[k].s1 = tmix[k].s2 = 0;
    for (t = 0; t < FS; t += CTL) {
        for (i = 0; i < CTL; i++) {
            double x = 20000.0 * sin(2 * M_PI * f * (t + i) / FS);
            b[i] = (int32_t)x;
            if (t + i >= FS / 2u)
                in += x * x;
        }
        track_lowcut(k, b, CTL);
        for (i = 0; i < CTL; i++)
            if (t + i >= FS / 2u)
                out += (double)b[i] * b[i];
    }
    return 10 * log10(out / in);
}

int main(void)
{
    int bad = 0, ok;
    uint32_t k, i;
    reset();

    /* power-on */
    ok = TW_FIRST == NTRK / 2u;
    for (k = 0; k < NTRK; k++)
        ok &= tw_wide(k) ? trk_lowcut[k] == TL_DEF : !trk_lowcut[k];
    bad += check("power-on: the first half no LOW CUT, the rest LOW CUT 120 Hz", ok);

    /* LOW CUT */
    {
        double d50, d250, d1k;
        char what[160];
        trk_lowcut[0] = 5;                            /* 160 Hz */
        d50 = lowcut_db(0, 50);
        d250 = lowcut_db(0, 250);
        d1k = lowcut_db(0, 1000);
        snprintf(what, sizeof what, "LOW CUT 160 Hz: 50 Hz %.1f dB, 250 Hz %.1f dB, 1 kHz %.2f dB (12 dB/oct)", d50, d250, d1k);
        bad += check(what, d50 < -16.0 && d250 > -6.0 && d250 < -1.0 && d1k > -0.5);
        {   /* after a loud sound, silence: out exactly 0 within 0.2 s */
            static int32_t b[CTL];
            uint32_t t, nz = 0;
            for (t = 0; t < FS / 2u; t += CTL) {
                for (i = 0; i < CTL; i++)
                    b[i] = t < FS / 4u ? (int32_t)(30000.0 * sin(2 * M_PI * 70.0 * (t + i) / FS)) : 0;
                track_lowcut(0, b, CTL);
                for (i = 0; i < CTL; i++)
                    nz += t >= FS / 4u + FS / 5u && b[i] != 0;
            }
            bad += check("LOW CUT: silence after a sound is silence (the poles settle exactly)", nz == 0u);
        }
        trk_lowcut[0] = 0;
    }

    /* the store keeps them */
    trk_lowcut[NTRK - 1u] = 2;
    trk_lowcut[0] = 6;
    ok = !project_save(0);
    trk_mix_defaults();
    ok &= trk_lowcut[0] == 0u;
    project_load(0);
    ok &= proj_ram[0][PROJ_HDR] == PROJ_VER && trk_lowcut[NTRK - 1u] == 2u && trk_lowcut[0] == 6u;
    bad += check("the store keeps LOW CUT; the power-on values have it as before", ok);
    trk_lowcut[1] = 9;
    ok = !project_save(0);
    project_load(0);
    ok &= !trk_lowcut[1];
    bad += check("bounds: LOW CUT past the last -> OFF (when loaded)", ok);
    printf("%s\n", bad ? "WIDTH TEST FAILED" : "width test passed");
    return bad ? 1 : 0;
}
