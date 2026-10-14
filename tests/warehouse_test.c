/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The WAREHOUSE distribution (cc -DFELUCCA_DIST=warehouse): the engines no other distribution has (but KARPLUS and
 * HONKS, and BYTE: its place went to the new ones; SHRED: SANDSTORM's) with BREAKS, VOSIM, DUBCHORD and KIT, in their
 * order; its power-on sounds; SIREN8 a sound of every track (no DUBSIREN here); SANDSTORM: GATE plucks again at the
 * tempo, DIVE takes the pitch down once let go */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void ck(const char *what, int ok) { printf("warehouse: %-102s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
static uint32_t eng_of(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (!strcmp(ENGINES[e]->name, n))
            return e;
    return ENG_NO;
}

int main(void)
{
    static const char *const ORDER[] = {"LINNKIT", "KIT", "FLOOR", "BREAKS", "FMPERC", "CHOKE", "GUTS", "VENOM", "HOOVER", "DUBCHORD",
                                        "STRINGS", "VOSIM", "SIREN8", "ABYSS", "SANDSTRM", "RISERS"};   /* (LINNKIT first) */
    uint32_t e, ok = NENGINES == NELEM(ORDER);
    for (e = 0; ok && e < NENGINES; e++)
        ok &= !strcmp(ENGINES[e]->name, ORDER[e]);
    ck("its engines in their order; no KARPLUS, HONKS, BYTE, nor IDM's or FOUNDRY's own",
       ok && !FELUCCA_ENG_KARPLUS && !FELUCCA_ENG_HONKS && !FELUCCA_ENG_BYTE && !FELUCCA_ENG_SID && !FELUCCA_ENG_DARK &&
       !FELUCCA_ENG_DUBSIREN && !FELUCCA_ENG_SHRED && NPART == 8u);
    ok = TRK_DEF[0][0] == eng_of("KIT") && TRK_DEF[1][0] == eng_of("FLOOR") && TRK_DEF[2][0] == eng_of("GUTS") &&
         TRK_DEF[3][0] == eng_of("HOOVER") && !strcmp(ENGINES[TRK_DEF[3][0]]->presets[TRK_DEF[3][1]].name, "REESE BASS") &&
         TRK_DEF[4][0] == eng_of("DUBCHORD") && TRK_DEF[5][0] == eng_of("VOSIM") && TRK_DEF[6][0] == eng_of("SANDSTRM") &&
         !strcmp(ENGINES[TRK_DEF[6][0]]->presets[TRK_DEF[6][1]].name, "LEAD") &&
         TRK_DEF[7][0] == eng_of("SIREN8");
    ck("power-on: KIT, FLOOR, GUT ACID, REESE BASS, DUBCHORD, FORMANT PAD, SANDSTORM's LEAD, SIREN8", ok);
    ck("SIREN8 on any track (no DUBSIREN to keep the last one)",
       eng_for_track(&trk[0], eng_of("SIREN8")) == eng_of("SIREN8") && eng_for_track(&trk[7], eng_of("SIREN8")) == eng_of("SIREN8"));
    {   /* SANDSTORM, GATE 1/16 at 120 BPM: a held note's level falls and rises again every 5512 samples */
        uint32_t b, i, dips = 0, was_loud = 0;
        int32_t o[2 * CTL];
        char what[160];
        host_tracks_init();
        song.g[G_BPM] = 120;
        host_preset(&trk[0], eng_of("SANDSTRM"), 0);
        trk[0].p[P_E0 + 5] = 2;                          /* GATE 1/16 */
        trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
        trk_note_on(&trk[0], 64, 110);
        for (b = 0; b < 44100u * 2u / CTL; b++) {
            int32_t pk = 0;
            mix_block(o, CTL);
            for (i = 0; i < 2u * CTL; i++)
                pk = o[i] > pk ? o[i] : -o[i] > pk ? -o[i] : pk;
            if (b > 40u && pk > 3000)
                was_loud = 1;
            if (was_loud && pk < 600) {
                dips++;
                was_loud = 0;
            }
        }
        snprintf(what, sizeof what, "SANDSTORM GATE 1/16 at 120 BPM: a held note chopped %u times in 2 s (16 steps)", dips);
        ck(what, dips >= 14u && dips <= 17u);
    }
    {   /* SANDSTORM DIVE: let go, the voices' pitch goes down */
        uint32_t b, k, lower = 0;
        int32_t o[2 * CTL];
        host_tracks_init();
        host_preset(&trk[0], eng_of("SANDSTRM"), 0);
        trk[0].p[P_E0 + 6] = 127;                        /* DIVE all the way */
        trk[0].p[P_REL] = 110;
        trk_note_on(&trk[0], 64, 110);
        for (b = 0; b < 200u; b++)
            mix_block(o, CTL);
        trk_note_off(&trk[0], 64);
        for (b = 0; b < 600u; b++)
            mix_block(o, CTL);
        for (k = 0; k < NVOICE; k++)
            if (trk[0].v[k].active && trk[0].v[k].s[2] >= 20 * 16 * 256)
                lower = 1;
        ck("SANDSTORM DIVE: let go, the pitch falls (20 semitones and more after ~450 ms)", lower);
    }
    printf("%s\n", bad ? "WAREHOUSE TEST FAILED" : "warehouse test passed");
    return bad ? 1 : 0;
}
