/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The FOUNDRY distribution (cc -DFELUCCA_DIST=foundry): its 16 engines in their order, the clean ones it lives of in
 * contrast among them (VOSIM, DUBCHORD, MODAL, BLISS, ECHOES, WAVE), at least 24 presets each and a category for each,
 * DARK's, ANVIL's and FLUIDS' clean blocks, BLISS (FMKEYS until then) by its id, its power-on sounds, and where a
 * sound of the engines that left it loads (HONKS VOSIM, GUTS and VENOM RAZOR, CHOKE RUST, ABYSS ECHOES, SHRED SCRATCH) */
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
    static const char *const ORDER[] = {"SCREAM", "DARK", "VOSIM", "WAR", "SCRATCH", "FLUIDS", "RAZOR", "DUBCHORD",
                                        "RUST", "ANVIL", "DRILL", "SLAG", "MODAL", "BLISS", "ECHOES", "WAVE"};
    uint32_t e, ok = NENGINES == 16u;
    for (e = 0; ok && e < NENGINES; e++)
        ok &= !strcmp(ENGINES[e]->name, ORDER[e]);
    ck("16 engines in their order: the clean VOSIM DUBCHORD MODAL BLISS ECHOES WAVE in HONKS GUTS VENOM CHOKE ABYSS SHRED's places",
       ok && !FELUCCA_ENG_HONKS && !FELUCCA_ENG_GUTS && !FELUCCA_ENG_VENOM && !FELUCCA_ENG_CHOKE && !FELUCCA_ENG_ABYSS &&
       !FELUCCA_ENG_SHRED);
    for (e = 0, ok = 1; e < NENGINES; e++)
        ok &= ENGINES[e]->npresets >= 24u && ENGINES[e]->cats && strlen(ENGINES[e]->cats) == ENGINES[e]->npresets;
    ck("every engine 24 presets or more, a category for each", ok);
    {   /* (Fel-Tracker) the EBM basslines: RAZOR's and DARK's, each with its arp figure */
        static const char *const EBM[3] = {"EBM OCTAVES", "EBM ROOT", "EBM GALLOP"};
        uint32_t k, x, ok2 = 1;
        for (k = 0; k < 2u; k++) {
            uint32_t en = eng_of(k ? "DARK" : "RAZOR");
            for (x = 0; x < 3u; x++) {
                uint32_t j, found = 0;
                for (j = 0; j < ENGINES[en]->npresets; j++)
                    if (!strcmp(ENGINES[en]->presets[j].name, EBM[x]) && ENGINES[en]->presets[j].arp == x + 1u &&
                        ARP_FIG[x + 1u].rate == (x == 2u ? 5u : 2u))
                        found = 1;
                ok2 &= found;
            }
        }
        ck("RAZOR and DARK: EBM OCTAVES, ROOT, GALLOP, each its arp figure (16ths, octave / root; 16th triplets)", ok2);
    }
    ck("DARK, ANVIL, FLUIDS: a clean block of 8 more (CLEAN SUB, PURE BELL, CLEAR PAD ..), DARK's EBM 3",
       ENGINES[eng_of("DARK")]->npresets == 35u && ENGINES[eng_of("ANVIL")]->npresets == 32u &&
       ENGINES[eng_of("FLUIDS")]->npresets == 32u && has_preset(eng_of("DARK"), "CLEAN SUB") &&
       has_preset(eng_of("ANVIL"), "PURE BELL") && has_preset(eng_of("FLUIDS"), "CLEAR PAD"));
    ck("BLISS (FMKEYS until then): id 26, its electric pianos and the new RHODES MKI",
       eng_of("BLISS") != ENG_NO && ENGINES[eng_of("BLISS")]->id == 26u && has_preset(eng_of("BLISS"), "E.PIANO") &&
       has_preset(eng_of("BLISS"), "RHODES MKI"));
    ok = TRK_DEF[0][0] == eng_of("WAR") && TRK_DEF[2][0] == eng_of("DARK") && TRK_DEF[3][0] == eng_of("RAZOR") &&
         TRK_DEF[4][0] == eng_of("DUBCHORD") && TRK_DEF[5][0] == eng_of("VOSIM") && TRK_DEF[6][0] == eng_of("RAZOR") &&
         TRK_DEF[7][0] == eng_of("SCREAM") && !strcmp(ENGINES[TRK_DEF[7][0]]->presets[TRK_DEF[7][1]].name, "SIREN CRY");
    ck("power-on: WAR's kits, DARK and RAZOR basses, DUBCHORD, FORMANT PAD, RAZOR LEAD, SCREAM's SIREN CRY", ok);
    {
        static const struct { uint8_t id; const char *to; } SUB[] = {
            {EID_HONKS, "VOSIM"}, {EID_GUTS, "RAZOR"}, {EID_VENOM, "RAZOR"}, {EID_CHOKE, "RUST"}, {EID_ABYSS, "ECHOES"},
            {EID_SHRED, "SCRATCH"}};
        uint32_t i;
        int sub;
        for (i = 0, ok = 1; i < NELEM(SUB); i++)
            ok &= eng_load_id(SUB[i].id, &sub) == eng_of(SUB[i].to) && sub;
        ck("the engines that left: HONKS -> VOSIM, GUTS VENOM -> RAZOR, CHOKE -> RUST, ABYSS -> ECHOES, SHRED -> SCRATCH",
           ok);
    }
    printf("%s\n", bad ? "FOUNDRY TEST FAILED" : "foundry test passed");
    return bad ? 1 : 0;
}
