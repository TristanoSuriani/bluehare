/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* ACID (1.5): the acid bass line (Felucca's own, in the manner of the little silver bass box). An oscillator (WAVE:
 * SAW, SQR; SUB a square an octave down) into a four-pole low-pass at CUT with RES (towards its own squeal), driven
 * into its saturation (DRIV); every note opens the filter by ENV and lets it close over DEC. Accent: a note played
 * hard (velocity 100 and up) is louder (as every note: the velocity), opens the filter by ACC more and closes it
 * faster, and accents in a row build up (the filter climbing a little more each: the "wow"). Slide: LEGATO voice mode
 * with GLIDE (the presets: MONO; turn LEGATO on in VOICE for the slides). The amplitude is the ADSR (gate-like).
 * The filter: four one-pole stages (TPT, the cutoff warped), the feedback from the last stage a sample late,
 * saturated at its input; the passband's loss under resonance made up.
 * Voice state: s[0..3] the stages, s[4] the filter envelope (Q15), s[5] this note's accent (0 .. 127), s[6] the
 * accents' build-up (Q15), s[7] the last stage's output; ph[0] the oscillator, ph[1] the sub. */
static const char *const N_AC_WAVE[] = {"SAW", "SQR", 0};

static void acid_note_on(track_t *t, voice_t *v)
{
    int32_t acc = v->vel >= 100u ? t->p[P_E5] : 0;
    v->s[4] = 32767;
    v->s[5] = acc;
    v->s[6] = acc ? v->s[6] + ((32767 - v->s[6]) >> 1) : v->s[6] >> 1;   /* accents in a row build up */
}

static void acid_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t s1 = v->s[0], s2 = v->s[1], s3 = v->s[2], s4 = v->s[3], fe = v->s[4], acc = v->s[5], wow = v->s[6];
    int32_t y4 = v->s[7], tau, cut, g, G, k, comp, drive = 64 + p[P_E6] * 3, sub = p[P_E7] * 258, i0;
    uint32_t inc = m->inc, i;
    tau = acc ? 40 : 30 + p[P_E4] * p[P_E4] / 8;       /* DEC: ~30 ms .. ~2 s (an accent: short) */
    fe -= fe / tau + (fe > 0);
    wow -= wow >> 10;                                 /* (the build-up drains slowly) */
    /* the cutoff: CUT, ENV and the accent's opening on the envelope, the build-up, the routings (0 .. 127 << 8) */
    cut = (p[P_E1] << 8) + ((p[P_E3] * 200 * fe) >> 15) + ((acc * 100 * fe) >> 15) + ((acc * wow) >> 9) + m->cutoff;
    i0 = clamp(cut, 0, 126 << 8) >> 8;
    g = SVF_G[i0] + (((SVF_G[i0 + 1] - SVF_G[i0]) * (clamp(cut, 0, 126 << 8) & 255)) >> 8);   /* tan, Q12 */
    G = (int32_t)(((uint32_t)g << 14) / (uint32_t)(4096 + g));   /* the one-pole's gain g / (1 + g), Q14 (no overflow) */
    k = p[P_E2] * 120;                                /* RES: the feedback, Q12 (~3.7 at the top) */
    comp = 4096 + (k >> 1);                           /* (the passband's loss made up, half of it) */
    for (i = 0; i < n; i++) {
        int32_t x, u, w;
        x = p[P_E0] ? osc_pulse(v->ph[0], inc, 0x80000000u) : osc_saw(v->ph[0], inc);
        if (sub)
            x += mulq15(osc_pulse(v->ph[1], inc >> 1, 0x80000000u), sub) >> 1;
        x = softclip((x >> 1) * drive >> 6);          /* DRIV: into the filter's saturation */
        u = softclip(x - ((y4 * k) >> 12));           /* the input less the resonance, saturated */
        w = ((u - s1) * G) >> 14; u = w + s1; s1 = u + w;
        w = ((u - s2) * G) >> 14; u = w + s2; s2 = u + w;
        w = ((u - s3) * G) >> 14; u = w + s3; s3 = u + w;
        w = ((u - s4) * G) >> 14; u = w + s4; s4 = u + w;
        y4 = u;
        out[i] += voice_amp(clamp((y4 * comp) >> 12, -32767, 32767), m, i) << 1;
        v->ph[0] += inc;
        v->ph[1] += inc >> 1;
    }
    v->s[0] = clamp(s1, -200000, 200000);
    v->s[1] = clamp(s2, -200000, 200000);
    v->s[2] = clamp(s3, -200000, 200000);
    v->s[3] = clamp(s4, -200000, 200000);
    v->s[4] = fe;
    v->s[6] = wow;
    v->s[7] = y4;
}

/* {WAVE, CUT, RES, ENV, DEC, ACC, DRIV, SUB}; WAVE 0 SAW 1 SQR. MONO; the ADSR gate-like */
static const preset_t ACID_PRESETS[] = {
    {"ACID LINE", {0, 40, 95, 70, 50, 80, 20, 0}, {0, 0, 127, 10}, 0, 1, FX(20, 0, 20, 10)},
    {"SQUELCH", {0, 30, 115, 90, 40, 100, 40, 0}, {0, 0, 127, 10}, 0, 1, FX(40, 0, 30, 10)},
    {"SQUARE ACID", {1, 35, 100, 75, 55, 80, 20, 0}, {0, 0, 127, 10}, 0, 1, FX(20, 0, 20, 10)},
    {"DEEP ACID", {0, 25, 80, 50, 70, 50, 10, 60}, {0, 0, 127, 15}, 0, 1, FX(10, 0, 10, 5)},
    {"RUBBER", {1, 20, 60, 60, 30, 40, 0, 40}, {0, 0, 127, 10}, 0, 1, FX(0, 0, 10, 5)},
    {"DIST ACID", {0, 45, 110, 80, 45, 90, 110, 0}, {0, 0, 127, 10}, 0, 1, FX(80, 0, 20, 10)},
    {"SCREAMER", {0, 60, 122, 100, 60, 110, 70, 0}, {0, 0, 127, 15}, 0, 1, FX(50, 0, 40, 20)},
    {"BLIP BASS", {1, 30, 70, 90, 10, 60, 30, 20}, {0, 30, 40, 10}, 0, 1, FX(10, 0, 20, 5)},
    {"SUB ACID", {0, 20, 70, 40, 60, 40, 10, 110}, {0, 0, 127, 15}, 0, 1, FX(10, 0, 0, 5)},
    {"ACID LEAD", {0, 70, 105, 60, 70, 70, 60, 0}, {0, 0, 127, 30}, 0, 1, FX(30, 30, 50, 30)},
};

static const engine_t ENG_ACID = {
    .name = "ACID",
    .id = EID_ACID,
    .cc = 0x23,                  /* CC74 CUT, CC71 RES */
    .cats = "BBBBBBLBBL",
    .page_title = {"FILTER", "ACCENT"},
    .edit = {
        {"WAVE", F_ENUM, 0, 1, 0, N_AC_WAVE, 0},
        {"CUT", F_CUTOFF, 0, 127, 40, 0, 0},
        {"RES", F_PCT, 0, 127, 90, 0, 0},
        {"ENV", F_PCT, 0, 127, 70, 0, 0},
        {"DEC", F_TIME, 0, 127, 50, 0, 0},
        {"ACC", F_PCT, 0, 127, 80, 0, 0},
        {"DRIV", F_PCT, 0, 127, 20, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = ACID_PRESETS,
    .npresets = NELEM(ACID_PRESETS),
    .note_on = acid_note_on,
    .render = acid_render,
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .keep = 0x83,                /* the filter, its last output (a retrigger, a slide: no click; note_on reads the
                                  * build-up before the retrigger, s[2] s[3] are never reset) */
};
