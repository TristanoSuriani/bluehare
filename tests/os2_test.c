/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* 2x OVERSAMPLING (dsp.c os2_up / os2_down: a polyphase IIR halfband) around the engines' shapers. The pair alone (no
 * shaper): 1 kHz and 15 kHz come out at their level; silence ends in zeros. The engines (this
 * build's of RAZOR, VENOM, SANDSTORM): C7 (2093 Hz) at DRIVE 127, the filter open, one voice, no detune: the energy off
 * the note's harmonics under 15 kHz (the aliases) against all of it, oversampled and as before (os2_off): lower */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void ck(const char *what, int ok) { printf("os2: %-106s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
static uint32_t eng_of(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (!strcmp(ENGINES[e]->name, n))
            return e;
    return ENG_NO;
}

#define NA 16384u
static double xs[NA];
static double band_e(double f)                       /* (the windowed render's energy at f) */
{
    double re = 0, im = 0;
    uint32_t k;
    for (k = 0; k < NA; k++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * k / NA);
        re += xs[k] * w * cos(2 * M_PI * f * k / FS);
        im += xs[k] * w * sin(2 * M_PI * f * k / FS);
    }
    return re * re + im * im;
}
/* the aliases' share (dB) of a render: energy more than 40 Hz off every harmonic of the note, under 15 kHz */
static double f0_used;
static double alias_db(double f0)                    /* (f0: the note, the preset's OCT applied) */
{
    double all = 0, off = 0, f;
    f0_used = f0;
    for (f = 100.0; f < 15000.0; f += 5.0) {
        double e = band_e(f), h = f / f0, d;
        d = fabs(h - floor(h + 0.5)) * f0;
        all += e;
        if (d > 40.0)
            off += e;
    }
    return 10 * log10(off / all + 1e-30);
}
static double render(uint32_t e, const int16_t *set, int off)
{
    int32_t o[2 * CTL];
    uint32_t b, i, k = 0;
    host_tracks_init();
    for (i = 0; i < NVOICE; i++)                     /* (the last render's voices gone: none of it in this one) */
        trk[0].v[i].active = 0;
    ml_bypass = 1;
    os2_off = (uint8_t)off;
    host_preset(&trk[0], e, 0);
    for (i = 0; i < 8u; i++)
        if (set[i] >= 0)
            trk[0].p[P_E0 + i] = set[i];
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_DIST] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_VOICE] = V_MONO;
    trk_note_on(&trk[0], 96, 110);
    for (b = 0; b < (FS / 10u + NA) / CTL + 1u; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++, k++)
            if (k >= FS / 10u && k - FS / 10u < NA)
                xs[k - FS / 10u] = o[2 * i];
    }
    os2_off = 0;
    ml_bypass = 0;
    return alias_db(2093.0 * pow(2.0, trk[0].p[P_OCT]));
}

int main(void)
{
    char what[200];
    {   /* the pair alone */
        static os2_t s;
        uint32_t i, f, nz = 0;
        double g[2];
        int32_t a, b;
        for (f = 0; f < 2u; f++) {
            double ei = 0, eo = 0, hz = f ? 15000.0 : 1000.0;
            memset(&s, 0, sizeof s);
            for (i = 0; i < 8000u; i++) {
                int32_t x = (int32_t)(20000.0 * sin(2 * M_PI * hz * i / FS)), y;
                os2_up(&s, x, &a, &b);
                y = os2_down(&s, a, b);
                if (i >= 200u) {
                    ei += (double)x * x;
                    eo += (double)y * y;
                }
            }
            g[f] = 10 * log10(eo / ei);
        }
        for (i = 0; i < 200u; i++) {                 /* (its allpasses ring out within ~40 samples) */
            os2_up(&s, 0, &a, &b);
            nz += os2_down(&s, a, b) != 0 && i >= 100u;
        }
        snprintf(what, sizeof what, "the pair alone: 1 kHz %+.2f dB, 15 kHz %+.2f dB; silence: zeros", g[0], g[1]);
        ck(what, fabs(g[0]) < 0.05 && fabs(g[1]) < 0.1 && !nz);
    }
    {
        static const struct { const char *name; int16_t set[8]; } E[] = {
            {"RAZOR", {1, 0, 127, 0, 0, 127, 0, 0}},              /* VOICES 1, SPREAD 0, CUT open, DRIVE 127 */
            {"VENOM", {0, 90, 90, 0, 0, -1, -1, 127}},            /* SAW, both cuts high (over ~100 its filters' own clamps,
                                                                   * not the drive, make the most of what is off), DRIVE 127 */
            {"SANDSTRM", {0, 127, 0, -1, -1, 0, 0, 127}},         /* SPRD 0, CUT open, no GATE or DIVE, DRIVE 127 */
        };
        uint32_t i, n = 0;
        for (i = 0; i < NELEM(E); i++) {
            uint32_t e = eng_of(E[i].name);
            double before, after;
            if (e == ENG_NO)
                continue;
            n++;
            before = render(e, E[i].set, 1);
            after = render(e, E[i].set, 0);
            snprintf(what, sizeof what, "%s C7 (%.0f Hz) at DRIVE 127: the aliases %.1f dB of it as before, %.1f dB oversampled "
                     "(%.1f dB less)", E[i].name, f0_used, before, after, before - after);
            ck(what, after < before - 3.0);
        }
        snprintf(what, sizeof what, "(%u of RAZOR, VENOM, SANDSTORM in this build)", n);
        ck(what, 1);
    }
    printf("%s\n", bad ? "OS2 TEST FAILED" : "os2 test passed");
    return bad != 0;
}
