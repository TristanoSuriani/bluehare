/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The WAREHOUSE distribution (cc -DFELUCCA_DIST=warehouse): the engines no other distribution has (but KARPLUS and
 * HONKS, and BYTE: its place went to the new ones) with BREAKS, VOSIM, DUBCHORD and KIT, in their order;
 * its power-on sounds; SIREN8 a sound of every track (no DUBSIREN here) */
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
    static const char *const ORDER[] = {"LINNKIT", "KIT", "BREAKS", "FMPERC", "CHOKE", "GUTS", "VENOM", "HOOVER", "DUBCHORD",
                                        "VOSIM", "SIREN8", "ABYSS", "SHRED", "RISERS"};   /* (LINNKIT first) */
    uint32_t e, ok = NENGINES == NELEM(ORDER);
    for (e = 0; ok && e < NENGINES; e++)
        ok &= !strcmp(ENGINES[e]->name, ORDER[e]);
    ck("its engines in their order; no KARPLUS, HONKS, BYTE, nor IDM's or FOUNDRY's own",
       ok && !FELUCCA_ENG_KARPLUS && !FELUCCA_ENG_HONKS && !FELUCCA_ENG_BYTE && !FELUCCA_ENG_SID && !FELUCCA_ENG_DARK &&
       !FELUCCA_ENG_DUBSIREN && NPART == 8u);
    ok = TRK_DEF[0][0] == eng_of("KIT") && TRK_DEF[1][0] == eng_of("BREAKS") && TRK_DEF[2][0] == eng_of("GUTS") &&
         TRK_DEF[3][0] == eng_of("HOOVER") && !strcmp(ENGINES[TRK_DEF[3][0]]->presets[TRK_DEF[3][1]].name, "REESE BASS") &&
         TRK_DEF[4][0] == eng_of("DUBCHORD") && TRK_DEF[5][0] == eng_of("VOSIM") && TRK_DEF[6][0] == eng_of("HOOVER") &&
         TRK_DEF[7][0] == eng_of("SIREN8");
    ck("power-on: KIT, BREAKS, GUT ACID, REESE BASS, DUBCHORD, FORMANT PAD, HOOVER, SIREN8", ok);
    ck("SIREN8 on any track (no DUBSIREN to keep the last one)",
       eng_for_track(&trk[0], eng_of("SIREN8")) == eng_of("SIREN8") && eng_for_track(&trk[7], eng_of("SIREN8")) == eng_of("SIREN8"));
    printf("%s\n", bad ? "WAREHOUSE TEST FAILED" : "warehouse test passed");
    return bad ? 1 : 0;
}
