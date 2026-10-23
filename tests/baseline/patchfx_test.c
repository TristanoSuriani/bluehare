/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (Blue Hare, 2026-10-18) The patches' delays (fx.c pdl_block): the first echo at TIME, FEED's next one quieter,
 * MIX's echo panned as its patch, one patch's line none of another's, an unfed line asleep once silent, FEED 120 held
 * for a minute bounded. On the baseline alone (tests/lib/hostsim.c, HOST_BASELINE) */
#define main hostsim_main
#include "../lib/hostsim.c"
#undef main
#include <math.h>

static int bad;
static void check(const char *what, int ok)
{
    printf("patchfx: %-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* patch k's delay over blocks: an impulse of amp at the first sample (fed: MIX on), the left and right out */
static void run(uint32_t k, uint32_t blocks, int32_t amp, int32_t gl, int32_t gr, int32_t *L, int32_t *R, int fed)
{
    static int32_t in[CTL];
    uint32_t b, i;
    for (b = 0; b < blocks; b++) {
        for (i = 0; i < CTL; i++) {
            in[i] = b == 0 && i == 0 ? amp : 0;
            mix_l[i] = mix_r[i] = 0;
        }
        pdl_block(k, in, CTL, gl, gr, fed);
        for (i = 0; i < CTL; i++) {
            L[b * CTL + i] = mix_l[i];
            R[b * CTL + i] = mix_r[i];
        }
    }
}

int main(void)
{
    static int32_t L[FS * 2], R[FS * 2];
    uint32_t i, D, blocks = FS * 2 / CTL, at1 = 0, at2 = 0, pk1 = 0, pk2 = 0;
    char msg[200];
    host_tracks_init();
    song.g[G_BPM] = 120;
    memset(pdl, 0, sizeof pdl);
    memset(dly_buf, 0, sizeof dly_buf);
    trk[0].p[P_DTIME] = 1;                              /* 1/8 at 120 BPM: 0.25 s */
    trk[0].p[P_DFDBK] = 60;
    D = div_samples(1);
    run(0, blocks, 20000, 4096, 4096, L, R, 1);
    for (i = 0; i < blocks * CTL; i++) {
        uint32_t a = (uint32_t)(L[i] < 0 ? -L[i] : L[i]);
        if (i > D / 2u && i < D + D / 2u && a > pk1) pk1 = a, at1 = i;
        if (i > D + D / 2u && i < 2u * D + D / 2u && a > pk2) pk2 = a, at2 = i;
    }
    snprintf(msg, sizeof msg, "the first echo at TIME (1/8 at 120 BPM, %u samples): at %u, peak %u", D, at1, pk1);
    check(msg, at1 + 4u >= D && at1 <= D + 4u && pk1 > 1000u);
    snprintf(msg, sizeof msg, "  FEED 60: the next at twice TIME (%u), quieter (%u under %u)", at2, pk2, pk1);
    check(msg, at2 + 6u >= 2u * D && at2 <= 2u * D + 6u && pk2 > 100u && pk2 < pk1);
    memset(pdl, 0, sizeof pdl);
    memset(dly_buf, 0, sizeof dly_buf);
    run(0, blocks, 20000, 4096, 0, L, R, 1);            /* (a patch panned hard left) */
    for (i = 0, pk1 = 0, pk2 = 0; i < blocks * CTL; i++) {
        pk1 = (uint32_t)abs(L[i]) > pk1 ? (uint32_t)abs(L[i]) : pk1;
        pk2 = (uint32_t)abs(R[i]) > pk2 ? (uint32_t)abs(R[i]) : pk2;
    }
    check("  panned as its patch: hard left, nothing on the right", pk1 > 1000u && pk2 == 0u);
    run(1, blocks, 0, 4096, 4096, L, R, 0);             /* patch 2: never fed */
    for (i = 0, pk1 = 0; i < blocks * CTL; i++)
        pk1 = (uint32_t)abs(L[i]) > pk1 ? (uint32_t)abs(L[i]) : pk1;
    check("  one patch's line none of another's: patch 2 silent while patch 1 echoes", pk1 == 0u);
    trk[0].p[P_DFDBK] = 0;
    run(0, (PDL_LEN * 2u + FS) / CTL + 4u, 0, 4096, 4096, L, R, 0);
    check("  unfed and silent for a line's length: asleep (the block returns at once)", pdl[0].quiet > PDL_SLEEP);
    trk[0].p[P_DFDBK] = 120;
    trk[0].p[P_DTIME] = 2;
    {
        static int32_t in[CTL];
        uint32_t b, pk = 0, s = 0x1234567u;
        for (b = 0; b < 60u * FS / CTL; b++) {
            for (i = 0; i < CTL; i++) {
                s ^= s << 13, s ^= s >> 17, s ^= s << 5;
                in[i] = b < 100u ? (int32_t)(int16_t)(s >> 16) : 0;
                mix_l[i] = mix_r[i] = 0;
            }
            pdl_block(0, in, CTL, 4096, 4096, b < 100u);
            for (i = 0; i < CTL; i++)
                pk = (uint32_t)abs(mix_l[i]) > pk ? (uint32_t)abs(mix_l[i]) : pk;
        }
        snprintf(msg, sizeof msg, "  FEED 120 for a minute: bounded (peak %u: the line's 16 bits hold it)", pk);
        check(msg, pk < 140000u);
    }
    {   /* (2026-10-18) through the whole mix: a short note on a patch whose voice is over long before its echoes; the
         * echoes after the release (the mixer used to return before the delay once the last voice ended: a short
         * sound got none) */
        int32_t o[2u * CTL];
        uint32_t b, f, pk_on = 0, pk_after = 0, e = 0, k;
        memset(pdl, 0, sizeof pdl);
        memset(dly_buf, 0, sizeof dly_buf);
        host_tracks_init();
        song.g[G_BPM] = 120;
        song.g[G_RMIX] = 0;
        const uint32_t be = BLUEHARE_ENG_WAVE ? ENGI_WAVE : ENGI_SID;   /* (WAVE's SUB BASS, else SID's SID BASS: */
        for (k = 0; k < ENGINES[be]->npresets; k++)                      /* a mono bass, its release short) */
            if (!strcmp(ENGINES[be]->presets[k].name, BLUEHARE_ENG_WAVE ? "SUB BASS" : "SID BASS"))
                e = k;
        host_preset(&trk[0], be, e);
        trk[0].p[P_DLY] = 100;
        trk[0].p[P_DTIME] = 0;                          /* (1/4 at 120 BPM: 0.5 s) */
        trk[0].p[P_DFDBK] = 50;
        trk[0].p[P_REL] = 0;
        trk_note_on(&trk[0], 48, 110);
        for (f = 0; f < FS / 10u; f += CTL) {
            mix_block(o, CTL);
            for (b = 0; b < 2u * CTL; b++) pk_on = (uint32_t)abs(o[b]) > pk_on ? (uint32_t)abs(o[b]) : pk_on;
        }
        trk_note_off(&trk[0], 48);
        for (f = 0; f < FS * 3u / 10u; f += CTL)        /* (the release over: 0.3 s) */
            mix_block(o, CTL);
        for (f = 0; f < FS / 2u; f += CTL) {            /* (0.4 .. 0.9 s: the first echo at 0.5 s) */
            mix_block(o, CTL);
            for (b = 0; b < 2u * CTL; b++) pk_after = (uint32_t)abs(o[b]) > pk_after ? (uint32_t)abs(o[b]) : pk_after;
        }
        snprintf(msg, sizeof msg, "  through the mix: a short note's echo after its voice is over (peak %u, the note %u)",
                 pk_after, pk_on);
        check(msg, pk_after > pk_on / 20u && pk_after > 200u && !trk[0].v[0].active);
    }
    printf(bad ? "patchfx: %d FAILED\n" : "patchfx: all passed\n", bad);
    return bad ? 1 : 0;
}
