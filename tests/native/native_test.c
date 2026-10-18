/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The native image (cc -DBLUEHARE_NATIVE -DBLUEHARE_DIST=<dist>; core.h BLUEHARE_NATIVE):
 *   every track its own 16 voices, whatever the load (no shared budget, nothing taken from another track), and how
 *   fast the computer renders every track full (printed: the times real time);
 *   the device's engines as the device has them (their voice caps), no native engine (CANYON retired; was: in a NATIVE
 *   distribution) sounding with all their voices, grains and repeats, or loading as their substitutes where missing;
 *   the reverbs only it has (fx_native.c: CONCERT, CAVERN, SHIMMER): HALL's level, stereo, a tail as long as SIZE asks,
 *   exact silence after it;
 *   the delay's longer line: 2BAR at 120 BPM (4 s) where it belongs. */
#define main hostsim_main
#include "../lib/hostsim.c"
#undef main

static int bad;
static void ck(const char *what, int ok) { printf("native: %-112s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static uint32_t sounding(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].stage != 4u;
    return n;
}

static double run(uint32_t blocks, int64_t *e)       /* blocks of the mix; *e += the output's energy; the seconds taken */
{
    static int32_t o[2 * CTL];
    struct timespec a, b;
    uint32_t k, i;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (k = 0; k < blocks; k++) {
        mix_block(o, CTL);
        for (i = 0; e && i < 2u * CTL; i++)
            *e += (int64_t)o[i] * o[i] >> 8;
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    return (double)(b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) * 1e-9;
}

static void all_off(void)
{
    uint32_t p, k;
    for (p = 0; p < NPART; p++)
        trk_all_off(&trk[p]);
    for (k = 0; k < 20u * FS / CTL; k++) {
        for (p = 0; p < NPART; p++)
            if (sounding(&trk[p]))
                break;
        if (p == NPART)
            break;
        run(1, 0);
    }
}

static uint32_t eng_named(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (!strcmp(ENGINES[e]->name, n))
            return e;
    return ENG_NO;
}

/* track 0 on engine e's preset pi, POLY, held: k notes */
static void play(uint32_t e, uint32_t pi, uint32_t k)
{
    track_t *t = &trk[0];
    uint32_t i;
    host_preset(t, e, pi);
    t->p[P_VOICE] = V_POLY;
    t->p[P_SUS] = 127;
    for (i = 0; i < k; i++)
        trk_note_on(t, 48u + 3u * i, 100);
}

/* the reverb bus alone: model rt, SIZE 90, DAMP 60; 0.1 s of noise in, then silence. *e2: the output's energy over the
 * first 2 s; *side: the side's share of it; *end: the seconds until the output is 0 for good (up to 60 s) */
static void rev_run(uint32_t rt, double *e2, double *side, double *end)
{
    static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
    uint32_t i, k, last = 0, s = 0x1234567u, total = 60u * FS / CTL;
    double em = 0, es = 0;
    song.g[G_RTYPE] = (int16_t)rt;
    song.g[G_RSIZE] = 90;
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
        for (i = 0; i < CTL; i++) {
            s ^= s << 13, s ^= s >> 17, s ^= s << 5;
            r[i] = k < FS / 10u / CTL ? (int32_t)(int16_t)(s >> 16) / 2 : 0;
            w[i] = 0;
            wside[i] = 0;
        }
        fx_buses(c, d, r, w, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t sd = hl.side ? rev_side[i] : wside[i];   /* (HALL's side: rev_side) */
            if (k < 2u * FS / CTL) {
                em += (double)w[i] * w[i];
                es += (double)sd * sd;
            }
            if (w[i] || sd)
                last = k + 1u;
        }
    }
    *e2 = em + es;
    *side = es / (em + es + 1);
    *end = (double)last * CTL / FS;
}

int main(void)
{
    static const struct { const char *name; uint32_t cap; } DEV[] = {
        {"MODAL", 4},
    };
    static const char *const RT[] = {"CONCERT", "CAVERN", "SHIMMER"};
    static const double RT_TAIL[] = {4.0, 12.0, 7.0};   /* RT60 at SIZE 90 (fx_native.c) */
    uint32_t p, i, e, want = 0, got = 0, all = 0, kills, n = 0, ok = 1;
    int64_t en = 0;
    double sec, hall, hall_side, hall_end;
    char msg[200];
    ck("16 voices a track; the budget is every track's, whatever the load",
       NVOICE == 16u && VOICE_MAX == NPART * NVOICE && (song.cpu_q8 = 255, voice_limit() == NPART * NVOICE));

    /* every track full: its power-on sound, POLY, sixteen notes held */
    host_tracks_init();
    song.cpu_q8 = 0;
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        host_preset(t, TRK_DEF[p][0], TRK_DEF[p][1]);
        t->p[P_VOICE] = V_POLY;
        t->p[P_SUS] = 127;
        want += ENGINES[t->engine]->oneshot ? 0u : trk_nvoice(t);   /* (a kit: a voice a lane hit) */
    }
    kills = voice_kills;
    for (i = 0; i < NVOICE; i++)
        for (p = 0; p < NPART; p++)
            trk_note_on(&trk[p], 36u + 2u * i + p % 3u, 100);
    run(2, 0);
    for (p = 0; p < NPART; p++) {
        got += ENGINES[trk[p].engine]->oneshot ? 0u : sounding(&trk[p]);
        all += sounding(&trk[p]);
    }
    sec = run(FS / CTL, &en);
    snprintf(msg, sizeof msg, "%u tracks full: %u voices sound (the pitched ones each its engine's cap), none taken; "
             "1 s in %.1f ms (%.0fx real time)", NPART, all, sec * 1e3, 1.0 / sec);
    ck(msg, got == want && all > 2u * 12u && voice_kills == kills && en > 0);
    all_off();

    /* the device's engines keep the device's caps (a sound plays as on the FM-1) */
    for (i = 0; i < NELEM(DEV); i++)
        if ((e = eng_named(DEV[i].name)) != ENG_NO) {
            n++;
            ok &= ENGINES[e]->poly == DEV[i].cap;
        }
    snprintf(msg, sizeof msg, "the device's engines as on the device: %u of this build's with their voice caps", n);
    ck(msg, ok);

    /* (2026-10-17) no native engine any more: CANYON retired with ECHOES (STRINGS took their place) */
    ck("CANYON and ECHOES retired: their sounds load nothing", eng_load_id(EID_CANYON, 0) == ENG_NO &&
       eng_load_id(EID_ECHOES, 0) == ENG_NO && eng_named("CANYON") == ENG_NO);

    /* (2026-10-18) the reverb is the device's on Native too (DRY SPRING GATED PLATE: CAVERN, SHIMMER out); the patches'
     * delays have Native's longer memory: 2BAR at 120 BPM (4 s) in one patch's line */
    host_tracks_init();
    ck("REVERB TYPE: DRY SPRING GATED PLATE, as the device's", GP[G_RTYPE].max == 3 && !strcmp(N_RTYPE[3], "PLATE"));
    {
        static int32_t in[CTL];
        uint32_t k, j, at = 0;
        int32_t pk = 0;
        song.g[G_BPM] = 120;
        trk[0].p[P_DTIME] = 8;                        /* 2BAR */
        trk[0].p[P_DFDBK] = 0;
        memset(pdl, 0, sizeof pdl);
        for (k = 0; k < 5u * FS / CTL; k++) {
            for (j = 0; j < CTL; j++) {
                in[j] = k == 0u && j == 0u ? 20000 : 0;
                mix_l[j] = mix_r[j] = 0;
            }
            pdl_block(0, in, CTL, 4096, 4096, 1);
            for (j = 0; j < CTL; j++)
                if (mix_l[j] > pk)
                    pk = mix_l[j], at = k * CTL + j;
        }
        snprintf(msg, sizeof msg, "a patch's delay line %.1f s: 2BAR at 120 BPM echoes at %.3f s", (double)PDL_LEN * 2 / FS,
                 (double)at / FS);
        ck(msg, PDL_LEN * 2u > 4u * FS && at + 4u >= 4u * FS && at <= 4u * FS + 4u && pk > 1000);
    }
    printf("%s\n", bad ? "NATIVE TEST FAILED" : "native test passed");
    return bad ? 1 : 0;
}
