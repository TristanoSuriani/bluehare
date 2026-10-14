/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The WAREHOUSE distribution (cc -DFELUCCA_DIST=warehouse), since the 2026-10 plan: its 8 engines in their order, two a
 * track's role (kit, bass, mid, high; RISERS for any), 4 tracks; its power-on sounds; where a sound of an engine it does
 * not have loads (the sirens HOOVER, by their substitutes; any other kit FLOOR, any other sound DUBCHORD, the mid
 * track's); SANDSTORM: GATE plucks again at the tempo, DIVE takes the pitch down once let go */
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
    static const char *const ORDER[] = {"FLOOR", "BREAKS", "VENOM", "DUBCHORD", "STRINGS", "HOOVER", "SANDSTRM", "RISERS"};
    uint32_t e, ok = NENGINES == NELEM(ORDER);
    for (e = 0; ok && e < NENGINES; e++)
        ok &= !strcmp(ENGINES[e]->name, ORDER[e]);
    ck("8 engines in their order (FLOOR BREAKS, VENOM, DUBCHORD STRINGS, HOOVER SANDSTORM, RISERS), 4 tracks",
       ok && NPART == 4u);
    ok = TRK_DEF[0][0] == eng_of("FLOOR") && TRK_DEF[1][0] == eng_of("VENOM") && TRK_DEF[2][0] == eng_of("DUBCHORD") &&
         TRK_DEF[3][0] == eng_of("SANDSTRM") && !strcmp(ENGINES[TRK_DEF[1][0]]->presets[TRK_DEF[1][1]].name, "VENOM ACID") &&
         !strcmp(ENGINES[TRK_DEF[3][0]]->presets[TRK_DEF[3][1]].name, "LEAD") && TRK_PAN[2] < 0 && TRK_PAN[3] > 0;
    ck("power-on: FLOOR, VENOM ACID, DUBCHORD (left), SANDSTORM's LEAD (right)", ok);
    {
        static const struct { uint8_t id; const char *to; } SUB[] = {
            {EID_DUBSIREN, "HOOVER"}, {EID_DUBSIREN8, "HOOVER"}, {EID_KIT, "FLOOR"}, {EID_LINNKIT, "FLOOR"},
            {EID_GUTS, "DUBCHORD"}, {EID_VOSIM, "DUBCHORD"}};
        uint32_t i;
        int sub;
        for (i = 0, ok = 1; i < NELEM(SUB); i++)
            ok &= eng_load_id(SUB[i].id, &sub) == eng_of(SUB[i].to) && sub;
        ck("engines it lacks: the sirens -> HOOVER; kits (KIT, LINNKIT) -> FLOOR; others (GUTS, VOSIM) -> DUBCHORD", ok);
    }
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
    song.cpu_q8 = 255;                               /* (a loaded device) */
    ck("VOICES 12: the voice budget 12 even at full load (4 tracks freed the CPU)", VOICE_BASE == 12u && voice_limit() == 12u);
    printf("%s\n", bad ? "WAREHOUSE TEST FAILED" : "warehouse test passed");
    return bad ? 1 : 0;
}
