/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The FOUNDRY distribution (cc -DBLUEHARE_DIST=foundry), since the 2026-10 plan: its 8 engines in their order, two a
 * track's role (kit, bass, mid, high), 4 tracks, at least 24 presets each and a category for each, the EBM basslines,
 * DARK's and ANVIL's clean blocks, its power-on sounds, and where a sound of an engine it does not have loads (GUTS and
 * VENOM RAZOR, CHOKE RUST by their substitutes; any other kit WAR, any other sound SLAG, the mid track's) */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void ck(const char *what, int ok) { printf("foundry: %-104s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
static uint32_t eng_of(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (!strcmp(ENGINES[e]->name, n))
            return e;
    return ENG_NO;
}
static int has_preset(uint32_t e, const char *n)
{
    uint32_t i;
    for (i = 0; e < NENGINES && i < ENGINES[e]->npresets; i++)
        if (!strcmp(ENGINES[e]->presets[i].name, n))
            return 1;
    return 0;
}

int main(void)
{
    static const char *const ORDER[] = {"WAR", "ANVIL", "DARK", "RAZOR", "SLAG", "RUST", "SCREAM", "DRILL"};
    uint32_t e, ok = NENGINES == 8u && NTRK == 4u;
    for (e = 0; ok && e < NENGINES; e++)
        ok &= !strcmp(ENGINES[e]->name, ORDER[e]);
    ck("8 engines in their order (WAR ANVIL, DARK RAZOR, SLAG RUST, SCREAM DRILL), 4 tracks", ok);
    for (e = 0, ok = 1; e < NENGINES; e++)
        ok &= ENGINES[e]->npresets >= 24u && ENGINES[e]->cats && strlen(ENGINES[e]->cats) == ENGINES[e]->npresets;
    ck("every engine 24 presets or more, a category for each", ok);
    {   /* (Blue Hare) the EBM basses: RAZOR's and DARK's */
        static const char *const EBM[3] = {"EBM OCTAVES", "EBM ROOT", "EBM GALLOP"};
        uint32_t k, x, ok2 = 1;
        for (k = 0; k < 2u; k++) {
            uint32_t en = eng_of(k ? "DARK" : "RAZOR");
            for (x = 0; x < 3u; x++) {
                uint32_t j, found = 0;
                for (j = 0; j < ENGINES[en]->npresets; j++)
                    if (!strcmp(ENGINES[en]->presets[j].name, EBM[x]))
                        found = 1;
                ok2 &= found;
            }
        }
        ck("RAZOR and DARK: EBM OCTAVES, ROOT, GALLOP", ok2);
    }
    ck("DARK, ANVIL: a clean block of 8 more (CLEAN SUB, PURE BELL ..), DARK's EBM 3",
       ENGINES[eng_of("DARK")]->npresets == 35u && ENGINES[eng_of("ANVIL")]->npresets == 32u &&
       has_preset(eng_of("DARK"), "CLEAN SUB") && has_preset(eng_of("ANVIL"), "PURE BELL"));
    ok = TRK_DEF[0][0] == eng_of("WAR") && TRK_DEF[1][0] == eng_of("DARK") && TRK_DEF[2][0] == eng_of("SLAG") &&
         TRK_DEF[3][0] == eng_of("RAZOR") && !strcmp(ENGINES[TRK_DEF[2][0]]->presets[TRK_DEF[2][1]].name, "EMBER PAD") &&
         !strcmp(ENGINES[TRK_DEF[3][0]]->presets[TRK_DEF[3][1]].name, "RAZOR LEAD") && TRK_PAN[2] < 0 && TRK_PAN[3] > 0;
    ck("power-on: WAR KIT, DARK's SUB WEIGHT, SLAG's EMBER PAD (left), RAZOR LEAD (right)", ok);
    {
        static const struct { uint8_t id; const char *to; } SUB[] = {
            {EID_GUTS, "RAZOR"}, {EID_VENOM, "RAZOR"}, {EID_CHOKE, "RUST"}, {EID_KIT, "WAR"}, {EID_FLOOR, "WAR"},
            {EID_WAVE, "SLAG"}, {EID_VOSIM, "SLAG"}, {EID_BLISS, "SLAG"}};
        uint32_t i;
        int sub;
        for (i = 0, ok = 1; i < NELEM(SUB); i++)
            ok &= eng_load_id(SUB[i].id, &sub) == eng_of(SUB[i].to) && sub;
        ck("engines it lacks: GUTS VENOM -> RAZOR, CHOKE -> RUST; kits (KIT, FLOOR) -> WAR; others (WAVE, VOSIM, BLISS) -> SLAG",
           ok);
    }
    song.cpu_q8 = 255;                               /* (a loaded device) */
    ck("VOICES 12: the voice budget 12 even at full load (4 tracks freed the CPU)", VOICE_BASE == 12u && voice_limit() == 12u);
    printf("%s\n", bad ? "FOUNDRY TEST FAILED" : "foundry test passed");
    return bad ? 1 : 0;
}
