/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* DUBCHORD (1.5): the dub-techno chord stab, a whole chord in one voice (Fel-Tracker's own): a key plays CHORD on its
 * note, four notes through one filter, so a chord costs one voice of the budget, not four. The chord's quality stays
 * what CHORD says wherever the root goes (the genre's parallel chords): MIN7, MIN9 (0 3 10 14: the fifth left out),
 * MIN11 (0 3 10 17), MAJ7, SUS2, SUS4, 5THS (stacked fifths), MIN (with the octave). VOIC: CLOSE as listed, OPEN (the
 * second note an octave up), SPREAD (the second and the fourth an octave up). WAVE: SAW, SQR (band-limited), SINE,
 * ORGAN (a sine and its octave). DTN detunes the notes in turn up and down (0 .. 30 cents: the chord beats against
 * itself). Then a resonant low-pass (CUT, RES, the key half tracked) that every note opens by ENV and lets close over
 * DEC: the stab's "pew"; the amplitude is the ADSR. It wants the TAPE delay and HALL: the sends of its presets.
 * Voice state: ph[0] ph[1] ph[2] s[0] the four notes' phases, s[1] s[2] the filter, s[3] its envelope (Q15). */
static const char *const N_DC_CHORD[] = {"MIN7", "MIN9", "MIN11", "MAJ7", "SUS2", "SUS4", "5THS", "MIN", 0};
static const char *const N_DC_VOIC[] = {"CLOSE", "OPEN", "SPREAD", 0};
static const char *const N_DC_WAVE[] = {"SAW", "SQR", "SINE", "ORGAN", 0};
static const int8_t DC_IV[8][4] = {   /* CHORD: the notes above the root, semitones */
    {0, 3, 7, 10}, {0, 3, 10, 14}, {0, 3, 10, 17}, {0, 4, 7, 11}, {0, 2, 7, 12}, {0, 5, 7, 12}, {0, 7, 14, 21}, {0, 3, 7, 12},
};

static void dubchord_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0;                                     /* the notes a quarter of a cycle apart: no click of four at once */
    v->ph[1] = 0x40000000u;
    v->ph[2] = 0x80000000u;
    v->s[0] = (int32_t)0xC0000000u;
    v->s[1] = v->s[2] = 0;
    v->s[3] = 32767;                                  /* the filter's envelope: open at the hit */
}

static inline int32_t dc_osc(uint32_t wave, uint32_t ph, uint32_t inc)
{
    switch (wave) {
    case 0u: return osc_saw(ph, inc);
    case 1u: return osc_pulse(ph, inc, 0x80000000u);
    case 2u: return sine_i(ph);
    default: return ((sine_i(ph) + (sine_i(ph << 1) >> 1)) * 21845) >> 15;   /* ORGAN: a sine and its octave, x 2/3 */
    }
}

static void dubchord_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const int8_t *iv = DC_IV[(uint32_t)p[P_E0] & 7u];
    uint32_t voic = (uint32_t)p[P_E1] % 3u, wave = (uint32_t)p[P_E2] & 3u, inc[4], ph[4], i, k;
    int32_t dt = p[P_E3] * 30 / 127, fe = v->s[3], tau = 1 + p[P_E7] * p[P_E7] / 6, ic1 = v->s[1], ic2 = v->s[2], cut;
    tsvf_t flt;
    for (k = 0; k < 4u; k++) {                        /* the chord's notes: their place, OPEN / SPREAD an octave up */
        int32_t up = iv[k] + ((voic == 1u && k == 1u) || (voic == 2u && (k == 1u || k == 3u)) ? 12 : 0);
        inc[k] = cents_inc(m->pitch16 + up * 16, k & 1u ? dt : -dt, m->fine);
    }
    ph[0] = v->ph[0];
    ph[1] = v->ph[1];
    ph[2] = v->ph[2];
    ph[3] = (uint32_t)v->s[0];
    fe -= fe / tau + (fe > 0);                        /* DEC: ~a block .. ~2.7 s */
    cut = (p[P_E4] << 8) + ((p[P_E6] * 220 * fe) >> 15) + m->cutoff + (m->pitch16 - 60 * 16) * 4;
    tsvf_coef(&flt, cut, p[P_E5]);
    for (i = 0; i < n; i++) {
        int32_t s = 0;
        for (k = 0; k < 4u; k++) {
            s += dc_osc(wave, ph[k], inc[k]);
            ph[k] += inc[k];
        }
        s = soft_knee(tsvf_lp(&flt, s >> 3, &ic1, &ic2), 16000) << 1;   /* (four notes: a quarter each, half scale) */
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph[0];
    v->ph[1] = ph[1];
    v->ph[2] = ph[2];
    v->s[0] = (int32_t)ph[3];
    v->s[1] = ic1;
    v->s[2] = ic2;
    v->s[3] = fe;
}

/* {CHORD, VOIC, WAVE, DTN, CUT, RES, ENV, DEC}; CHORD 0 MIN7 1 MIN9 2 MIN11 3 MAJ7 4 SUS2 5 SUS4 6 5THS 7 MIN; VOIC 0 CLOSE
 * 1 OPEN 2 SPREAD; WAVE 0 SAW 1 SQR 2 SINE 3 ORGAN. The stabs short (SUS 0), the pads slow; the delay and reverb sends
 * high: the chord is half its echoes */
static const preset_t DUBCHORD_PRESETS[] = {
    {"DUB STAB", {0, 0, 0, 15, 25, 70, 75, 28}, {0, 85, 0, 40}, 0, 0, FX(0, 10, 75, 45)},
    {"DEEP CHORD", {1, 1, 0, 20, 20, 40, 50, 45}, {0, 60, 30, 50}, 0, 0, FX(0, 20, 60, 55)},
    {"CHANNEL", {0, 2, 1, 10, 35, 85, 60, 20}, {0, 80, 0, 35}, 0, 0, FX(0, 0, 85, 50)},
    {"FOG CHORD", {2, 1, 0, 30, 18, 30, 20, 90}, {60, 80, 100, 90}, 0, 0, FX(0, 40, 50, 80)},
    {"ECHO STAB", {1, 0, 1, 12, 40, 90, 85, 15}, {0, 78, 0, 25}, 0, 0, FX(0, 0, 95, 40)},
    {"ORGAN DUB", {0, 0, 3, 0, 60, 20, 10, 40}, {0, 70, 80, 40}, 0, 0, FX(0, 30, 60, 50)},
    {"SUS DRIFT", {5, 2, 0, 25, 25, 50, 40, 70}, {40, 80, 90, 80}, 0, 0, FX(0, 50, 50, 75)},
    {"MAJ7 LIGHT", {3, 1, 2, 0, 70, 10, 20, 50}, {0, 70, 60, 60}, 0, 0, FX(0, 30, 40, 60)},
    {"FIFTHS", {6, 0, 1, 8, 30, 60, 60, 35}, {0, 50, 20, 40}, 0, 0, FX(0, 10, 70, 45)},
    {"TAPE CHORD", {1, 2, 0, 40, 30, 50, 40, 55}, {10, 60, 50, 60}, 0, 0, FX(10, 40, 70, 55)},
    {"MINOR PAD", {7, 0, 0, 35, 22, 30, 25, 100}, {80, 90, 110, 100}, 0, 0, FX(0, 60, 30, 85)},
    {"GLASS SUS", {4, 1, 2, 0, 90, 20, 30, 30}, {0, 80, 40, 70}, 0, 0, FX(0, 30, 50, 65)},
    {"HOUSE ORGAN", {0, 1, 3, 4, 75, 30, 30, 20}, {0, 45, 30, 30}, 0, 0, FX(0, 20, 30, 35)},
    {"DRONE FIFTHS", {6, 1, 0, 30, 15, 40, 10, 120}, {90, 90, 120, 110}, 0, 0, FX(0, 50, 40, 90)},
};

static const engine_t ENG_DUBCHORD = {
    .name = "DUBCHORD",
    .id = EID_DUBCHORD,
    .cc = 0x56,                  /* CC74 CUT, CC71 RES */
    .cats = "UKUPUKPKKKPKKP",
    .page_title = {"CHORD", "FILTER"},
    .edit = {
        {"CHORD", F_ENUM, 0, 7, 0, N_DC_CHORD, 0},
        {"VOIC", F_ENUM, 0, 2, 0, N_DC_VOIC, 0},
        {"WAVE", F_ENUM, 0, 3, 0, N_DC_WAVE, 0},
        {"DTN", F_PCT, 0, 127, 15, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 30, 0, 0},
        {"RES", F_PCT, 0, 127, 60, 0, 0},
        {"ENV", F_PCT, 0, 127, 60, 0, 0},
        {"DEC", F_TIME, 0, 127, 35, 0, 0},
    },
    .presets = DUBCHORD_PRESETS,
    .npresets = NELEM(DUBCHORD_PRESETS),
    .poly = 4,
    .note_on = dubchord_note_on,
    .render = dubchord_render,
    .knob = {P_E4, P_E5, P_E6, P_E7},
};
