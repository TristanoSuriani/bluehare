/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* Each track's place in the mix (core.h trk_width, trk_lowcut; fx.c track_lowcut, mix_haas): the power-on values, LOW
 * CUT's response and its silence after a sound, WIDTH's highs late on one side (the side changing track by track) with
 * the lows as they are on both, the mono sum's lows whole, the song store (version 5) and its bounds, the editor's
 * WIDTH (TRACKER op 11). */
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

/* track k's x[0 .. len) through mix_haas (PAN centre, level 1, no sends) into l[], r[] */
static void haas_run(uint32_t k, const int32_t *x, int32_t *l, int32_t *r, uint32_t len)
{
    static int32_t b[CTL];
    uint32_t t, i;
    memset(tw_buf, 0, sizeof tw_buf);
    tmix[k].h1 = tmix[k].h2 = 0;
    for (t = 0; t < len; t += CTL) {
        for (i = 0; i < CTL; i++) {
            b[i] = x[t + i] << 2;                     /* (mix_haas: (b >> 2) * 4096 >> 10 = x) */
            mix_l[i] = mix_r[i] = 0;
        }
        mix_haas(&trk[k], k, b, CTL, 1024, 4096, 4096, 0, 0, 0, 0x7FFFFFFF);
        for (i = 0; i < CTL; i++) {
            l[t + i] = mix_l[i];
            r[t + i] = mix_r[i];
        }
    }
}

int main(void)
{
    int bad = 0, ok;
    uint32_t k, i;
    static int32_t x[FS / 4u], l[FS / 4u], r[FS / 4u];
    reset();

    /* power-on */
    ok = TW_FIRST == NTRK / 2u;
    for (k = 0; k < NTRK; k++)
        ok &= tw_has(k) ? trk_width[k] == TW_DEF && trk_lowcut[k] == TL_DEF : !trk_width[k] && !trk_lowcut[k];
    bad += check("power-on: the rhythm section (the first half) dry and centred; the rest WIDTH 12 ms, LOW CUT 120 Hz", ok);

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

    /* WIDTH: a click on the first wide track: left at once, its highs on the right 12 ms later; the next track the
     * other way round */
    {
        uint32_t dl = (TW_DEF * 441u + 5u) / 10u, kk;
        for (kk = TW_FIRST; kk < TW_FIRST + 2u; kk++) {
            int32_t *now, *late;
            memset(x, 0, sizeof x);
            x[100] = 20000;
            trk_width[kk] = TW_DEF;
            haas_run(kk, x, l, r, FS / 8u);
            now = kk == TW_FIRST ? l : r;
            late = kk == TW_FIRST ? r : l;
            ok = now[100] == 20000 && late[100] < 2000 && late[100 + dl] > 15000 && abs(now[100 + dl]) < 200;
            bad += check(kk == TW_FIRST ? "WIDTH: the first wide track's click left at once, its highs right 12 ms later"
                                        : "  the next track the other way round (late on the left)", ok);
        }
    }
    {   /* the lows: a 60 Hz sine as loud on both sides, in phase (the mono sum whole) */
        double sl = 0, sr = 0, slr = 0;
        for (i = 0; i < FS / 4u; i++)
            x[i] = (int32_t)(20000.0 * sin(2 * M_PI * 60.0 * i / FS));
        haas_run(TW_FIRST, x, l, r, FS / 4u);
        for (i = FS / 8u; i < FS / 4u; i++) {
            sl += (double)l[i] * l[i];
            sr += (double)r[i] * r[i];
            slr += (double)l[i] * r[i];
        }
        {
            char what[160];
            double c = slr / sqrt(sl * sr), g = 10 * log10(sr / sl);
            snprintf(what, sizeof what, "WIDTH: 60 Hz on both sides alike (correlation %.3f, %.2f dB apart): the mono sum's lows "
                     "whole", c, g);
            bad += check(what, c > 0.97 && fabs(g) < 0.5);
        }
    }

    /* the store: version 5 keeps them; a song of before gets them as at power-on */
    trk_width[NTRK - 1u] = 7;
    trk_lowcut[NTRK - 1u] = 2;
    trk_lowcut[0] = 6;
    ok = !project_save(0);
    project_new();
    ok &= trk_width[NTRK - 1u] == TW_DEF && trk_lowcut[0] == 0u;
    project_load(0);
    ok &= proj_ram[0][PROJ_HDR] == 5u && trk_width[NTRK - 1u] == 7u && trk_lowcut[NTRK - 1u] == 2u && trk_lowcut[0] == 6u;
    bad += check("the store: version 5 keeps WIDTH and LOW CUT; a new song has them as at power-on", ok);
    trk_width[0] = 5;                                 /* (the rhythm section has no WIDTH) */
    trk_width[TW_FIRST] = 99;
    trk_lowcut[1] = 9;
    tk_bound();
    bad += check("bounds: WIDTH on a rhythm track -> 0, past 20 -> 0; LOW CUT past the last -> OFF",
                 !trk_width[0] && !trk_width[TW_FIRST] && !trk_lowcut[1]);

    /* the editor's WIDTH (op 11) */
    {
        uint8_t a[4] = {EDT_WIDTH};
        uint32_t m = request(ED_TRACKER, a, 1);
        ok = m == 9u + 2u * NTRK && host_wire[7] == TW_FIRST && host_wire[8 + 2u * (NTRK - 1u)] == 7u;
        a[1] = (uint8_t)(NTRK - 1u); a[2] = 15; a[3] = 3;
        m = request(ED_TRACKER, a, 4);
        ok &= m == 9u + 2u * NTRK && !host_wire[6] && trk_width[NTRK - 1u] == 15u && trk_lowcut[NTRK - 1u] == 3u;
        a[1] = 0; a[2] = 4; a[3] = 0;                 /* (WIDTH on track 1: none) */
        ok &= request(ED_TRACKER, a, 4) && host_wire[6] == 1 && !trk_width[0];
        a[1] = (uint8_t)(NTRK - 1u); a[2] = 21;
        ok &= request(ED_TRACKER, a, 4) && host_wire[6] == 1 && trk_width[NTRK - 1u] == 15u;
        a[2] = 0; a[3] = TL_N;
        ok &= request(ED_TRACKER, a, 4) && host_wire[6] == 1 && trk_lowcut[NTRK - 1u] == 3u;
        a[1] = NTRK;
        ok &= !request(ED_TRACKER, a, 4);
        bad += check("the editor's WIDTH: read, written (15 ms, 80 Hz); a rhythm track's WIDTH, 21 ms, a cutoff past the "
                     "last: rc 1; a track past the last: no reply", ok);
    }
    printf("%s\n", bad ? "WIDTH TEST FAILED" : "width test passed");
    return bad ? 1 : 0;
}
