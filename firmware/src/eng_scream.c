/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* SCREAM (1.5, FOUNDRY): a resonator screaming by itself (Fel-Tracker's own). A narrow band-pass at the key (its
 * resonator: PHYS's, phys_dsp.c px_svf_coef, MIT) fed back on itself through a saturation (GAIN: from a ringing
 * whistle to a held shriek that sings on its own, the saturation holding it), a breath of NOISE to start it and rasp
 * in it, through two formants (VOWEL: A E I O U, morphing between); SHRIEK starts each note up to an octave above,
 * falling to the key, VIB a wide fast vibrato (~6 Hz), HOWL lets the pitch wander, GRIT drives it all, TONE a
 * low-pass last.
 * Voice state: s[0] s[1] the resonator (Q22), s[2] s[3] / s[4] s[5] the formants, s[6] the noise, s[7] SHRIEK's fall
 * (Q15); ph[0] VIB's phase, ph[1] HOWL's wander, ph[2] TONE's filter. */
static const int16_t SC_VOW[5][2] = {{730, 1090}, {530, 1840}, {270, 2290}, {570, 840}, {300, 870}};   /* A E I O U, Hz */

static void scream_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[7] = 32767;
    if (!v->s[6])
        v->s[6] = (int32_t)(0x2545F491u ^ (v->age * 2654435761u));
}

/* a formant's coefficients at hz (Q8 of the TPT SVF: tsvf's), its narrowness k */
static void sc_form(tsvf_t *c, int32_t hz, int32_t k)
{
    int32_t i = 0;
    while (i < 126 && CUTOFF_HZ[i + 1] <= (uint32_t)hz)
        i++;                                          /* (the cutoff table's index under hz: once a block) */
    tsvf_coef_k(c, i << 8, k);
}

static void scream_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t s1 = v->s[0], s2 = v->s[1], a1 = v->s[2], a2 = v->s[3], b1 = v->s[4], b2 = v->s[5], sh = v->s[7];
    int32_t gain = 20000 + p[P_E0] * 130, grit = 256 + p[P_E6] * 16, noise = p[P_E4] * 40, vw = p[P_E1] * 4 / 128, vf;
    int32_t f1, f2, pitch, i, x, y, v1, v2, v3, k = 4000, lp = (int32_t)v->ph[2], kt = clamp(1500 + p[P_E7] * 250, 1500, 32767);
    uint32_t nz = (uint32_t)v->s[6], f;
    px_svf_t r;
    tsvf_t c1, c2;
    sh -= (sh >> 3) + (sh > 0);                       /* SHRIEK: falls over ~60 ms */
    v->ph[0] += 4200000u;                             /* VIB: ~6 Hz (a block's step) */
    nz ^= nz << 13;                                   /* HOWL: towards a random place, a little a block */
    nz ^= nz >> 17;
    nz ^= nz << 5;
    v->ph[1] = (uint32_t)((int32_t)v->ph[1] + ((((int32_t)(nz >> 20) - 2048) - (int32_t)v->ph[1]) >> 5));
    pitch = m->pitch16 + ((p[P_E2] * 3 * sh) >> 16) + ((sine_i(v->ph[0]) * p[P_E3]) >> 15) / 4 +   /* SHRIEK: up to an
                                                       * octave; VIB: up to ~2 semitones */
            (((int32_t)v->ph[1] * p[P_E5]) >> 12);    /* HOWL: up to ~4 semitones */
    f = pitch_inc((uint32_t)clamp(pitch, 0, 2047));
    px_svf_coef(&r, f > 0x30000000u ? 0x30000000u : f, 256u * 40u);   /* the resonator: Q 40 */
    vf = p[P_E1] * 4 % 128;                           /* VOWEL: between two of A E I O U */
    f1 = SC_VOW[vw][0] + (((SC_VOW[vw < 4 ? vw + 1 : 4][0] - SC_VOW[vw][0]) * vf) >> 7);
    f2 = SC_VOW[vw][1] + (((SC_VOW[vw < 4 ? vw + 1 : 4][1] - SC_VOW[vw][1]) * vf) >> 7);
    sc_form(&c1, f1, k);
    sc_form(&c2, f2, k);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t in, tt, vv;
        nz ^= nz << 13;
        nz ^= nz >> 17;
        nz ^= nz << 5;
        in = ((int32_t)(int16_t)(nz >> 16) * (noise + 200)) >> 6;   /* NOISE (and a breath always: it starts) */
        in += px_m(softclip(s1 >> 7), gain, 15) << 6;  /* GAIN: the resonator fed back on itself, saturated */
        tt = px_m(r.gh, (in << 2) - px_m(r.rpg, s1, 28) - s2, 30);   /* the band-pass (px_modal_run's) */
        vv = s1 + tt;
        s1 = px_clamp(vv + tt, -(1 << 28), 1 << 28);
        tt = px_m(r.g, vv, 28);
        s2 = px_clamp(s2 + tt + tt, -(1 << 28), 1 << 28);
        x = softclip((vv >> 7) * grit >> 8);           /* GRIT */
        v3 = x - a2;                                  /* the formants (tsvf band-pass) */
        v1 = (c1.a1 * a1 + c1.a2 * v3) >> 13;
        v2 = a2 + ((c1.a2 * a1 + c1.a3 * v3) >> 13);
        a1 = clamp(2 * v1 - a1, -150000, 150000);
        a2 = clamp(2 * v2 - a2, -150000, 150000);
        y = v1;
        v3 = x - b2;
        v1 = (c2.a1 * b1 + c2.a2 * v3) >> 13;
        v2 = b2 + ((c2.a2 * b1 + c2.a3 * v3) >> 13);
        b1 = clamp(2 * v1 - b1, -150000, 150000);
        b2 = clamp(2 * v2 - b2, -150000, 150000);
        y = softclip((y + v1 + (x >> 2)) * 2);
        lp += ((y - lp) * kt) >> 15;                  /* TONE */
        out[i] += voice_amp(lp, m, (uint32_t)i) << 1;
    }
    v->s[0] = s1;
    v->s[1] = s2;
    v->s[2] = a1;
    v->s[3] = a2;
    v->s[4] = b1;
    v->s[5] = b2;
    v->s[6] = (int32_t)nz;
    v->s[7] = sh;
    v->ph[2] = (uint32_t)lp;
}

/* {GAIN, VOWEL, SHRIEK, VIB, NOISE, HOWL, GRIT, TONE} */
static const preset_t SCREAM_PRESETS[] = {
    {"SHRIEK", {110, 40, 80, 30, 30, 20, 60, 110}, {0, 0, 127, 40}, 0, 1, FX(30, 0, 30, 40)},
    {"FEEDBACK", {127, 20, 0, 10, 10, 40, 40, 110}, {10, 0, 127, 60}, 0, 1, FX(20, 0, 30, 50)},
    {"BANSHEE", {115, 90, 60, 60, 50, 30, 50, 110}, {20, 0, 127, 80}, 0, 0, FX(20, 30, 30, 70)},
    {"HOWLER", {120, 100, 30, 40, 30, 90, 70, 110}, {30, 0, 127, 80}, 0, 1, FX(30, 20, 30, 60)},
    {"WHISTLE", {95, 80, 10, 20, 5, 10, 20, 110}, {5, 0, 127, 40}, 0, 1, FX(0, 20, 30, 40)},
    {"RASP", {105, 10, 50, 20, 110, 30, 100, 110}, {0, 0, 127, 30}, 0, 1, FX(40, 0, 25, 30)},
    {"SIREN SCREAM", {118, 60, 127, 80, 20, 50, 60, 110}, {0, 0, 127, 50}, 0, 1, FX(20, 0, 40, 40)},
    {"MOUTH OF HELL", {127, 0, 40, 30, 80, 70, 127, 110}, {40, 0, 127, 100}, 0, 0, FX(50, 20, 30, 80)},
};

static const engine_t ENG_SCREAM = {
    .name = "SCREAM",
    .id = EID_SCREAM,
    .cc = 0x12,                  /* CC74 GAIN, CC71 VOWEL */
    .cats = "LFFFLLFF",
    .page_title = {"VOICE", "THROAT"},
    .edit = {
        {"GAIN", F_PCT, 0, 127, 110, 0, 0},
        {"VOWEL", F_PCT, 0, 127, 40, 0, 0},
        {"SHRIEK", F_PCT, 0, 127, 60, 0, 0},
        {"VIB", F_PCT, 0, 127, 30, 0, 0},
        {"NOISE", F_PCT, 0, 127, 30, 0, 0},
        {"HOWL", F_PCT, 0, 127, 20, 0, 0},
        {"GRIT", F_PCT, 0, 127, 60, 0, 0},
        {"TONE", F_PCT, 0, 127, 110, 0, 0},
    },
    .presets = SCREAM_PRESETS,
    .npresets = NELEM(SCREAM_PRESETS),
    .poly = 4,
    .note_on = scream_note_on,
    .render = scream_render,
    .knob = {P_E0, P_E1, P_E2, P_E6},
    .keep = 0x03,                /* the resonator (a retrigger: it goes on screaming) */
};
