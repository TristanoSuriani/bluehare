/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* LFO 2 (1.2, #154), on the real sound sources (with the stubs of tests/ui_test.c): SYNC one cycle per note value of the
 * tempo (several values and tempi, the external clock's measured tempo), RATE ignored meanwhile; OFF: RATE's table
 * exactly as before. TRIG: NOTE restarts at PHS on a fresh phrase (as before), FREE never (with SYNC: from PHS when
 * PLAY starts). POL: UNI 0..+1 (the wave's range, LFO DEST AMP and the matrix's AMP as ENV takes it), BI as before.
 * (Up to 1.4 tests/fun10_test.c, with 1.2's NUDGE, motion and project formats: gone with the step sequencer, 1.5.)
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

/* ---------------------------------------------------------------- LFO 2 --- */
/* the LFO's phase over `blocks` blocks, in cycles (wraps + the phase) */
static double lfo_cycles(track_t *t, uint32_t blocks)
{
    uint32_t b, wraps = 0, ph0 = t->lfo_ph;
    for (b = 0; b < blocks; b++) {
        uint32_t old = t->lfo_ph;
        track_lfo_tick(t);
        wraps += t->lfo_ph < old;
    }
    return wraps + ((double)t->lfo_ph - (double)ph0) / 4294967296.0;
}

static int lfo(void)
{
    static const uint8_t VALS[] = {1, 3, 5, 6, 7, 8, 9, 10};   /* 4BAR 1/1 1/4 1/8 8T 1/16 16T 1/32 */
    static const double BEATS[11] = {0, 16, 8, 4, 2, 1, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
    static const int16_t BPM[] = {40, 97, 120, 173, 240};
    int bad = 0, ok = 1;
    uint32_t i, j, b;
    track_t *t;
    double worst = 0;
    ui_power_on();
    t = &trk[0];
    bad += check("LFO 2 defaults: SYNC OFF, TRIG NOTE, POL BI (as before 1.2); QUANTIZE ON",
                 !TP[P_LSYNC].def && !TP[P_LTRIG].def && !TP[P_LPOL].def && TP[P_SQNT].def == 1 && !t->p[P_LSYNC] &&
                 !t->p[P_LTRIG] && !t->p[P_LPOL] && t->p[P_SQNT] == 1);
    for (i = 0; i < 128u; i += 7u) {                 /* SYNC OFF: RATE's table, the value as before */
        uint32_t ph = 0x12345678u;
        t->p[P_LRATE] = (int16_t)i;
        t->lfo_ph = ph;
        track_lfo_tick(t);
        ok &= t->lfo_ph == ph + LFO_INC[i] && t->lfo_val == osc_sine(ph + LFO_INC[i]);
    }
    bad += check("SYNC OFF: the phase moves by RATE's LFO_INC, the value the wave's, bit for bit", ok);
    ok = 1;
    for (i = 0; i < NELEM(BPM); i++)
        for (j = 0; j < NELEM(VALS); j++) {
            double cyc = FS * 60.0 / BPM[i] * BEATS[VALS[j]], want, got;
            uint32_t blocks = (uint32_t)(cyc * 8.0 / CTL);   /* eight cycles */
            song.g[G_BPM] = BPM[i];
            t->p[P_LSYNC] = VALS[j];
            t->p[P_LRATE] = 127;                     /* (ignored) */
            t->lfo_ph = 0;
            got = lfo_cycles(t, blocks);
            want = (double)blocks * CTL / (double)div_samples(LSYNC_DIV[VALS[j]]);
            ok &= fabs(got - want) < 2e-4 && fabs(got - (double)blocks * CTL / cyc) < 0.003 * got;
            if (fabs(got - want) > worst) worst = fabs(got - want);
        }
    printf("ui:   SYNC: worst phase error against div_samples over 8 cycles %.2g cycles\n", worst);
    bad += check("SYNC 4BAR..1/32 at 40, 97, 120, 173, 240 BPM: one cycle per note value (RATE ignored)", ok);
    song.g[G_BPM] = 120;
    t->p[P_LSYNC] = 5;                               /* 1/4 */
    t->lfo_ph = 0;
    ok = fabs(lfo_cycles(t, (uint32_t)(22050u * 100u / CTL)) - 100.0 * (22050u * 100u / CTL * CTL) / 2205000.0) < 1e-4;
    bad += check("  1/4 at 120 BPM: 100 beats are 100 cycles (no drift)", ok);
    {   /* the external clock: USB, 24 pulses a quarter at 100 BPM (25 ms apart): the measured tempo sets the cycle */
        uint32_t ms = 1000u;
        song.g[G_CLOCK] = 1;
        midi_beat_samples = 0;
        memset(&midi_clock, 0, sizeof midi_clock);
        for (i = 0; i < 13u; i++, ms += 25u)
            midi_clock_pulse(ms);
        t->lfo_ph = 0;
        ok = midi_beat_samples == 26460u && fabs(lfo_cycles(t, 26460u * 4u / CTL) - (double)(26460u * 4u / CTL) * CTL / 26460.0) < 1e-4;
        for (i = 0; i < 13u; i++, ms += 30u)        /* slower: 83 BPM */
            midi_clock_pulse(ms);
        t->lfo_ph = 0;
        ok &= midi_beat_samples == 31752u && fabs(lfo_cycles(t, 31752u * 2u / CTL) - (double)(31752u * 2u / CTL) * CTL / 31752.0) < 1e-4;
        bad += check("SYNC on the external clock: 1/4 = the measured beat (100, then 83 BPM)", ok);
        song.g[G_CLOCK] = 0;
        midi_beat_samples = 0;
    }
    t->p[P_LSYNC] = 0;
    /* TRIG: NOTE restarts at PHS on a fresh phrase, FREE never */
    t->p[P_LPHASE] = 32;
    t->lfo_ph = 0x55555555u;
    trk_note_on(t, 60, 100);
    ok = t->lfo_ph == 32u << 25 && t->lfo_fade == 0;
    trk_all_off(t);
    for (b = 0; b < 2000u; b++) env_tick(t, &t->v[0]);
    for (i = 0; i < NVOICE; i++) t->v[i].gate = 0;
    t->p[P_LTRIG] = 1;
    t->lfo_ph = 0x55555555u;
    t->lfo_fade = 32767;
    trk_note_on(t, 62, 100);
    ok &= t->lfo_ph == 0x55555555u && t->lfo_fade == 0;
    bad += check("TRIG NOTE: a fresh phrase restarts the LFO at PHS; FREE: never (FADE still restarts)", ok);
    t->p[P_LSYNC] = 5;
    t->lfo_ph = 0x55555555u;
    seq_start();
    ok = t->lfo_ph == 32u << 25;
    seq_stop();
    t->p[P_LSYNC] = 0;
    t->lfo_ph = 0x55555555u;
    seq_start();
    ok &= t->lfo_ph == 0x55555555u;
    seq_stop();
    bad += check("  FREE + SYNC: from PHS when PLAY starts (in time with the bars); FREE alone: not even then", ok);
    trk_all_off(t);
    t->p[P_LTRIG] = 0;
    t->p[P_LPHASE] = 0;
    /* POL: BI -1..+1 (as before), UNI 0..+1 */
    {
        int32_t lo[2] = {99999, 99999}, hi[2] = {-99999, -99999};
        uint32_t pol;
        t->p[P_LWAVE] = 2;                           /* SAW: every value */
        t->p[P_LRATE] = 100;
        for (pol = 0; pol < 2u; pol++) {
            t->p[P_LPOL] = (int16_t)pol;
            for (b = 0; b < 20000u; b++) {
                track_lfo_tick(t);
                lo[pol] = t->lfo_val < lo[pol] ? t->lfo_val : lo[pol];
                hi[pol] = t->lfo_val > hi[pol] ? t->lfo_val : hi[pol];
            }
        }
        printf("ui:   POL BI %d..%d, UNI %d..%d\n", lo[0], hi[0], lo[1], hi[1]);
        bad += check("POL UNI: 0..+1 (0..32767), BI -1..+1 as before", lo[0] < -32000 && hi[0] > 32000 && lo[1] >= 0 &&
                     lo[1] < 300 && hi[1] <= 32767 && hi[1] > 32400);
    }
    {   /* the matrix's AMP from the LFO: BI takes it as 0..1 ((x + 1) / 2), UNI as it is (as ENV) */
        int32_t g[4];
        t->p[P_M1SRC] = MS_LFO; t->p[P_M1DST] = MD_AMP; t->p[P_M1AMT] = 63;
        t->lfo_fade = 32767;
        t->p[P_LPOL] = 0; t->lfo_val = 0; mod_begin(t); g[0] = mod.gain; mod_end(t);
        t->p[P_LPOL] = 1; t->lfo_val = 0; mod_begin(t); g[1] = mod.gain; mod_end(t);
        t->lfo_val = 16384; mod_begin(t); g[2] = mod.gain; mod_end(t);
        t->p[P_LPOL] = 0; t->lfo_val = -32768; mod_begin(t); g[3] = mod.gain; mod_end(t);
        bad += check("  the matrix's LFO > AMP: UNI 0 = BI -1 (closed), UNI 0.5 = BI 0", g[1] == g[3] && abs(g[2] - g[0]) < 64 &&
                     g[1] < 1000);
        t->p[P_M1SRC] = t->p[P_M1DST] = t->p[P_M1AMT] = 0;
        t->p[P_LPOL] = 0;
    }
    bad += check("LFO 2's page SYNC TRIG POL after LFO (LFO's family)",
                 PAGES[3].id[0] == P_LSYNC && !strcmp(PAGES[3].title, "LFO 2") &&
                 PAGES[3].fam == FAM_LFO && PAGES[2].graph == GR_LFO && !strcmp(PAGES[4].title, "LFO DEST"));
    go_page(GR_LFO);
    ui.page++;                                       /* LFO 2 */
    page_entered();
    frame();
    turn(EN_K1, 3);
    turn(EN_K3, 1);
    bad += check("  LFO 2's knobs: SYNC (4BAR 2BAR 1/1 ..), POL", t->p[P_LSYNC] == 3 && t->p[P_LPOL] == 1 &&
                 !strcmp(N_LSYNC[3], "1/1"));
    return bad;
}

int main(void)
{
    int bad = lfo();
    printf(bad ? "ui: LFO 2 TEST FAILED\n" : "LFO 2 tests passed\n");
    return bad != 0;
}
