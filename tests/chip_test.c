/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The CHIP distribution (cc -DFELUCCA_DIST=chip): its 7 engines in their order on 4 tracks (since the 2026-10 plan), its palette and its
 * power-on sounds; CHIPKIT: each of its 12 drums sounds and ends (its voice free again), each console sounds its own
 * way, RATE moves its steps */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define STR_(x) #x
#define STR(x) STR_(x)
static int bad;
static void ck(const char *what, int ok) { printf("chip: %-107s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
static uint32_t eng_of(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (!strcmp(ENGINES[e]->name, n))
            return e;
    return ENG_NO;
}

/* drum k of CHIPKIT (its key from C2) on track 0, with CHIP = chip and RATE = rate: the render's hash, its peak, the
 * blocks until the voice is free */
static uint64_t hit(uint32_t k, int16_t chip, int16_t rate, int32_t *peak, uint32_t *blocks)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i, active = 1;
    host_tracks_init();
    host_preset(&trk[0], eng_of("CHIPKIT"), 0);
    trk[0].p[P_E2] = chip;
    trk[0].p[P_E5] = rate;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk_note_on(&trk[0], (uint8_t)(36u + k), 110);
    *peak = 0;
    *blocks = 0;
    for (b = 0; b < 44100u * 3u / CTL && active; b++) {
        int32_t o[2 * CTL];
        if (b == 4u)
            trk_note_off(&trk[0], (uint8_t)(36u + k));
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
            *peak = o[i] > *peak ? o[i] : -o[i] > *peak ? -o[i] : *peak;
        }
        active = 0;
        for (i = 0; i < NVOICE; i++)
            active |= trk[0].v[i].active;
        *blocks = b + 1u;
    }
    return h;
}

int main(void)
{
    static const char *const ORDER[] = {"CHIPKIT", "SID", "WAVE", "BYTE", "ECHOES", "SPEECH", "SIREN8"};
    static const char *const DRUM[12] = {"KICK", "SNARE", "HAT", "OPEN", "TOM L", "TOM M", "TOM H", "CRASH", "CLAP",
                                         "ZAP", "COIN", "NOISE"};
    uint32_t e, k, ok = NENGINES == NELEM(ORDER), worst = 0, quiet = 99;
    char what[200];
    for (e = 0; ok && e < NENGINES; e++)
        ok &= !strcmp(ENGINES[e]->name, ORDER[e]);
    ck("its 7 engines in their order (CHIPKIT, SID, WAVE, BYTE, ECHOES, SPEECH, SIREN8), 4 tracks, the CHIP palette first", ok && NPART == 4u && !strcmp(STR(FELUCCA_DIST_PALETTE), "UI_PAL_CHIP"));
    ok = TRK_DEF[0][0] == eng_of("CHIPKIT") && TRK_DEF[0][1] == 0u && TRK_DEF[1][0] == eng_of("SID") &&
         TRK_DEF[2][0] == eng_of("BYTE") && !strcmp(ENGINES[TRK_DEF[2][0]]->presets[TRK_DEF[2][1]].name, "BYTE ARP") &&
         TRK_DEF[3][0] == eng_of("SID") && TRK_DEF[3][1] == 2u;
    ck("power-on: CHIPKIT's NES KIT, a SID bass, BYTE ARP, a SID lead", ok);
    ok = 1;
    for (k = 0; k < 12u; k++) {
        int32_t pk;
        uint32_t blocks;
        hit(k, 0, 64, &pk, &blocks);
        ok &= pk > 2000 && blocks < 44100u * 3u / CTL;
        worst = blocks > worst ? blocks : worst;
        if (pk <= 2000 && quiet == 99)
            quiet = k;
    }
    snprintf(what, sizeof what, "CHIPKIT (NES): its 12 drums each sound and end, the longest %u ms%s%s", worst * CTL * 1000u / 44100u,
             quiet < 12u ? ", silent: " : "", quiet < 12u ? DRUM[quiet] : "");
    ck(what, ok);
    {
        int32_t pk;
        uint32_t bl, c, d, same = 0;
        uint64_t hs[4];
        for (c = 0; c < 4u; c++)
            hs[c] = hit(1, (int16_t)c, 64, &pk, &bl);   /* the snare on each console */
        for (c = 0; c < 4u; c++)
            for (d = c + 1u; d < 4u; d++)
                same += hs[c] == hs[d];
        ck("each console (NES, GB, SMS, C64) its own snare", same == 0u);
        ck("RATE: other frames, another hat", hit(2, 0, 64, &pk, &bl) != hit(2, 0, 120, &pk, &bl));
    }
    song.cpu_q8 = 255;                               /* (a loaded device) */
    ck("VOICES 12: the voice budget 12 even at full load (4 tracks freed the CPU)", VOICE_BASE == 12u && voice_limit() == 12u);
    printf("%s\n", bad ? "CHIP TEST FAILED" : "chip test passed");
    return bad ? 1 : 0;
}
