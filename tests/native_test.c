/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The native image's limits (cc -DFELUCCA_NATIVE -DFELUCCA_DIST=<dist>; core.h FELUCCA_NATIVE): every track its own 16
 * voices, whatever the load (no shared budget, nothing stolen from another track), the engines' caps that were the
 * device's CPU lifted (DEV_NAT), each of them sounding with all its voices; and how fast the computer renders the
 * heaviest case, every track full (printed: the times real time). */
#define main hostsim_main
#include "hostsim.c"
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
    for (k = 0; k < 6u * FS / CTL; k++) {
        for (p = 0; p < NPART; p++)
            if (sounding(&trk[p]))
                break;
        if (p == NPART)
            break;
        run(1, 0);
    }
}

int main(void)
{
    static const struct { const char *name; uint32_t dev, nat; } CAPS[] = {
        {"MODAL", 4, 8},  {"ANVIL", 4, 8},  {"PARTICLE", 4, 8}, {"ECHOES", 4, 8}, {"KARPLUS", 4, 8}, {"FLUIDS", 6, 12},
        {"SPEECH", 4, 8}, {"GLITCH", 8, 12}, {"DUBCHORD", 4, 8}, {"RAZOR", 4, 8}, {"CHOKE", 6, 12}, {"SLAG", 6, 12},
        {"VECTOR", 6, 12},
    };
    uint32_t p, i, e, want = 0, got = 0, all = 0, kills, n = 0, ok = 1;
    int64_t en = 0;
    double sec;
    char msg[160];
    ck("16 voices a track; the budget is every track's, whatever the load",
       NVOICE == 16u && VOICE_MAX == NPART * NVOICE && (song.cpu_q8 = 255, voice_limit() == NPART * NVOICE));
    song.cpu_q8 = 0;

    /* every track full: its power-on sound, POLY, sixteen notes held */
    host_tracks_init();
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        host_preset(t, TRK_DEF[p][0], TRK_DEF[p][1]);
        t->p[P_VOICE] = V_POLY;
        t->p[P_AMODE] = 0;
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
        if (getenv("NATIVE_V"))
            printf("  %u %-8s %u of %u\n", p, ENGINES[trk[p].engine]->name, sounding(&trk[p]), trk_nvoice(&trk[p]));
    }
    sec = run(FS / CTL, &en);
    snprintf(msg, sizeof msg, "%u tracks full: %u voices sound (the pitched ones each its engine's cap), none taken; "
             "1 s in %.1f ms (%.0fx real time)", NPART, all, sec * 1e3, 1.0 / sec);
    ck(msg, got == want && all > 2u * 12u && voice_kills == kills && en > 0);
    all_off();

    /* the caps lifted, and every voice of them sounds */
    for (i = 0; i < NELEM(CAPS); i++)
        for (e = 0; e < NENGINES; e++) {
            track_t *t = &trk[0];
            uint32_t k;
            if (strcmp(ENGINES[e]->name, CAPS[i].name))
                continue;
            n++;
            host_preset(t, e, 0);
            t->p[P_VOICE] = V_POLY;
            t->p[P_AMODE] = 0;
            t->p[P_SUS] = 127;
            en = 0;
            kills = voice_kills;
            for (k = 0; k < CAPS[i].nat; k++)
                trk_note_on(t, 48u + 3u * k, 100);
            run(2, &en);
            k = sounding(t);
            run(FS / 4u / CTL, &en);
            if (ENGINES[e]->poly != CAPS[i].nat || k != CAPS[i].nat || voice_kills != kills || en <= 0) {
                printf("native:   %s: cap %u, %u sounding (of %u), %u taken, energy %lld\n", CAPS[i].name,
                       ENGINES[e]->poly, k, CAPS[i].nat, voice_kills - kills, (long long)en);
                ok = 0;
            }
            all_off();
        }
    snprintf(msg, sizeof msg, "the device's CPU caps lifted: %u of this build's engines, each sounds with all its voices", n);
    ck(msg, ok && n > 0);
    printf("%s\n", bad ? "NATIVE TEST FAILED" : "native test passed");
    return bad ? 1 : 0;
}
