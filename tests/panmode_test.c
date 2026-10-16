/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* PAN MODE (core.h trk_pmode; voice.c voice_start, fx.c mix_part): ALT everywhere at power-on; RND on a POLY track: each
 * new note on a side picked at random (both sides, not in turn); RND on a MONO track at SPRD 0: the notes move too (half
 * the width); the song store keeps it, the power-on values have none */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"
#include <math.h>

/* n short notes on track 3 (WAVE), each played alone; the side of each: 0 left, 1 right (its L / R energy) */
static uint32_t notes(uint32_t mode, uint32_t vmode, uint32_t sprd, uint8_t *side, uint32_t n)
{
    uint32_t i, b, k, left = 0;
    int32_t o[2 * CTL];
    host_tracks_init();
    ml_bypass = 1;                                   /* (the track's own sides: MONO LOWS off) */
    host_preset(&trk[2], ENGI_WAVE, 0);
    trk[2].p[P_CHOR] = trk[2].p[P_DLY] = trk[2].p[P_REV] = 0;
    trk[2].p[P_PAN] = 0;
    trk[2].p[P_SPRD] = (int16_t)sprd;
    trk[2].p[P_VOICE] = (int16_t)vmode;
    trk[2].p[P_REL] = 0;
    trk_width[2] = 0;
    trk_pmode[2] = (uint8_t)mode;
    for (i = 0; i < n; i++) {
        double el = 0, er = 0;
        trk_note_on(&trk[2], 60, 100);
        for (b = 0; b < 60u; b++) {
            mix_block(o, CTL);
            for (k = 0; k < CTL; k++) {
                el += (double)o[2 * k] * o[2 * k];
                er += (double)o[2 * k + 1] * o[2 * k + 1];
            }
        }
        trk_note_off(&trk[2], 60);
        for (b = 0; b < 200u; b++)                   /* (let it end: the next note a fresh voice) */
            mix_block(o, CTL);
        side[i] = er > el;
        left += !side[i];
    }
    ml_bypass = 0;
    return left;
}

int main(void)
{
    int bad = 0, ok;
    uint32_t k, left, runs = 0;
    uint8_t side[40];
    char what[200];
    reset();
    for (ok = 1, k = 0; k < NTRK; k++)
        ok &= trk_pmode[k] == TP_ALT;
    bad += check("power-on: PAN MODE ALT on every track", ok);

    left = notes(TP_ALT, V_POLY, 64, side, 20);
    for (ok = 1, k = 1; k < 20u; k++)
        ok &= side[k] != side[k - 1];
    bad += check("ALT, POLY, SPRD 64: the notes left, right, left .. in turn (SPREAD as it was)", ok && left == 10u);
    left = notes(TP_RND, V_POLY, 64, side, 40);
    for (k = 1; k < 40u; k++)
        runs += side[k] == side[k - 1];
    snprintf(what, sizeof what, "RND, POLY, SPRD 64: each note a side at random (%u of 40 left, %u repeats of the side before)", left, runs);
    bad += check(what, left >= 8u && left <= 32u && runs >= 4u);
    left = notes(TP_RND, V_MONO, 0, side, 40);
    snprintf(what, sizeof what, "RND, MONO, SPRD 0: the notes move too, half the width (%u of 40 left)", left);
    bad += check(what, left >= 8u && left <= 32u);

    /* the store */
    reset();
    trk_pmode[3] = TP_RND;
    ok = !project_save(0);
    trk_mix_defaults();
    ok &= trk_pmode[3] == TP_ALT;
    project_load(0);
    ok &= proj_ram[0][PROJ_HDR] == PROJ_VER && trk_pmode[3] == TP_RND;
    bad += check("the store keeps PAN MODE; the power-on values have none", ok);
    printf("%s\n", bad ? "PAN MODE TEST FAILED" : "pan mode test passed");
    return bad != 0;
}
