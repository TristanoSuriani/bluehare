/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The IDM distribution (cc -DFELUCCA_DIST=idm), since the 2026-10 plan: its 8 engines in their order, two a track's role
 * (kit, bass, mid, high), 4 tracks, every engine rendering its presets; its power-on sounds; where a sound of an engine it
 * does not have loads (by its substitutes: KIT GLITCH, SID WASP, DUBCHORD WAVE, ANVIL MODAL; with none of them: a kit
 * BREAKS, any other sound WAVE, the mid track's) */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void ck(const char *what, int ok) { printf("idm: %-108s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
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
    static const char *const ORDER[] = {"BREAKS", "GLITCH", "WASP", "WEST", "WAVE", "VECTOR", "MODAL", "PARTICLE"};
    static const struct { uint8_t id; const char *to; } SUB[] = {
        {EID_KIT, "GLITCH"}, {EID_SID, "WASP"}, {EID_DUBCHORD, "WAVE"}, {EID_ANVIL, "MODAL"}, {EID_FLOOR, "BREAKS"},
        {EID_SLAG, "WAVE"}};
    uint32_t e, i, ok = NENGINES == NELEM(ORDER) && NTRK == 4u;
    int sub;
    for (e = 0; ok && e < NENGINES; e++)
        ok &= !strcmp(ENGINES[e]->name, ORDER[e]);
    ck("8 engines in their order (BREAKS GLITCH, WASP WEST, WAVE VECTOR, MODAL PARTICLE), 4 tracks", ok);
    ok = TRK_DEF[0][0] == eng_of("BREAKS") && TRK_DEF[1][0] == eng_of("WASP") && TRK_DEF[2][0] == eng_of("WAVE") &&
         TRK_DEF[3][0] == eng_of("MODAL") && !strcmp(ENGINES[TRK_DEF[1][0]]->presets[TRK_DEF[1][1]].name, "FAT BASS") &&
         !strcmp(ENGINES[TRK_DEF[2][0]]->presets[TRK_DEF[2][1]].name, "WARM PAD") &&
         !strcmp(ENGINES[TRK_DEF[3][0]]->presets[TRK_DEF[3][1]].name, "KALIMBA") && !TRK_PAN[0] && !TRK_PAN[1] &&
         TRK_PAN[2] < 0 && TRK_PAN[3] > 0;
    ck("power-on: BREAKS, WASP's FAT BASS (centred), WAVE's WARM PAD (left), MODAL's KALIMBA (right)", ok);
    for (i = 0, ok = 1; i < NELEM(SUB); i++)
        ok &= eng_load_id(SUB[i].id, &sub) == eng_of(SUB[i].to) && sub;
    ck("engines it lacks: KIT -> GLITCH, SID -> WASP, DUBCHORD -> WAVE, ANVIL -> MODAL; FLOOR -> BREAKS, SLAG -> WAVE", ok);
    {
        uint32_t pi, f, loud = 0, n = 0;
        int32_t o[2u * CTL];
        for (e = 0; e < NENGINES; e++)
            for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
                int32_t pk = 0;
                host_tracks_init();
                host_preset(&trk[0], e, pi);
                trk_note_on(&trk[0], 60, 100);
                for (f = 0; f < FS / 4u; f += CTL) {
                    mix_block(o, CTL);
                    for (i = 0; i < 2u * CTL; i++)
                        pk = o[i] > pk ? o[i] : -o[i] > pk ? -o[i] : pk;
                }
                trk_note_off(&trk[0], 60);
                loud += pk > 100;
                if (pk <= 100)
                    printf("idm:   silent: %s %s (peak %d)\n", ENGINES[e]->name, ENGINES[e]->presets[pi].name, pk);
                n++;
            }
        char what[120];
        snprintf(what, sizeof what, "every engine renders every preset (not silent): %u of %u", loud, n);
        ck(what, loud == n && n > 8u * 12u);
    }
    printf("%s\n", bad ? "IDM TEST FAILED" : "idm test passed");
    return bad ? 1 : 0;
}
