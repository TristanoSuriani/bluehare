/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-23) RHYTHM, the 808 and the 909 (firmware/baseline/eng_rhythm.c): each of the twelve drums sounds in both
 * models, every octave the same twelve; a note-off at once leaves the hit alone (a sequencer's short notes); the
 * closed hat chokes a sounding open one; the kick's length follows KICK.
 *   rhythm_test */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, int ok)
{
    printf("rhythm: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static int32_t peak_of(uint32_t blocks)               /* the mix's peak over the blocks */
{
    int32_t o[2u * CTL], pk = 0;
    uint32_t i;
    while (blocks--) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            pk = abs(o[i]) > pk ? abs(o[i]) : pk;
    }
    return pk;
}
static uint32_t sounding(const track_t *t, uint32_t drum)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].s[4] == (int32_t)drum;
    return n;
}
static void fresh(uint32_t preset)
{
    host_tracks_init();
    memset(trk[1].v, 0, sizeof trk[1].v);              /* (an earlier hit ringing on: gone) */
    host_preset(&trk[1], ENGI_RHYTHM, preset);
    trk[1].p[P_REV] = trk[1].p[P_DLY] = trk[1].p[P_CHOR] = 0;
    peak_of(4);
}
int main(void)
{
    track_t *t = &trk[1];
    uint32_t m, d, ok = 1, quiet;
    for (m = 0; m < 2u; m++)
        for (d = 0; d < 12u; d++) {
            int32_t pk;
            fresh(m ? 5u : 0u);                         /* 808 KIT, 909 KIT */
            trk_note_on(t, 36 + d, 120);
            trk_note_off(t, 36 + d);
            pk = peak_of(FS / 4u / CTL);
            if (pk < 3000 || pk > 32000) {
                printf("  %s drum %u peak %d\n", m ? "909" : "808", d, pk);
                ok = 0;
            }
        }
    check("the twelve drums sound in both models (a note-off at once: the hit plays on)", ok);
    fresh(0);
    trk_note_on(t, 48, 120);                           /* (C3: the kick again) */
    check("every octave the same twelve (C3 the kick)", t->v[0].s[4] == 0 || sounding(t, 0) == 1u);
    fresh(0);
    trk_note_on(t, 46, 120);                           /* OPEN */
    peak_of(FS / 20u / CTL);
    ok = sounding(t, 10) == 1u;
    trk_note_on(t, 42, 120);                           /* CLOSED */
    peak_of(FS / 20u / CTL);
    check("the closed hat chokes the open one (gone within 50 ms)", ok && sounding(t, 10) == 0u && sounding(t, 6) == 1u);
    fresh(0);
    t->p[P_E3] = 10;                                   /* KICK short */
    trk_note_on(t, 36, 120);
    peak_of(FS / 2u / CTL);
    quiet = (uint32_t)peak_of(FS / 4u / CTL);          /* (its level from 0.5 s to 0.75 s) */
    fresh(0);
    t->p[P_E3] = 127;                                  /* KICK long: the 808's boom */
    trk_note_on(t, 36, 120);
    peak_of(FS / 2u / CTL);
    m = (uint32_t)peak_of(FS / 4u / CTL);
    check("KICK: short ~gone after half a second, long still rings (at least 4x louder there)", quiet * 4u < m && m > 3000u);
    printf(fails ? "rhythm: %d FAILED\n" : "rhythm test passed\n", fails);
    return fails ? 1 : 0;
}
