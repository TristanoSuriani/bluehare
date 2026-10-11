/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* VOSIM (1.5): VOice SIMulation (Kaegi and Tempelaars, 1978), with two formants. Each period of the note starts a train
 * of PULS sin^2 pulses whose width sets the formant (F1, F2: ~130 Hz .. 4.2 kHz), each pulse DECAY times the one before;
 * after the train, silence until the next period. The formant stays where it is whatever the note (a voice, a reed,
 * a horn), unless KTRK makes it follow the note; SWEEP moves both formants at each note and lets them glide back
 * (~200 ms: a wah, a brass swell, a "yow"); BAL mixes F1 and F2; BRTH adds breath (noise shaped by the pulses).
 * The pulses are one-signed: a DC blocker takes the offset out. Formants are held below ~7 kHz.
 * Voice state: ph[0] the period, ph[1] / ph[2] the formants' pulse phases, s[0] / s[1] their pulse amplitudes (Q15),
 * s[2] / s[3] their pulses so far, s[4] the sweep (Q15, 1 at the note), s[5] the DC blocker, s[6] its last input,
 * s[7] the noise generator. */
#define VS_TOP 1872              /* the formants' highest pitch, 1/16 semitone (note 117, ~7 kHz) */

static void vosim_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0xFFFFFFFFu;                           /* (the first sample starts a period) */
    v->ph[1] = v->ph[2] = 0;
    v->s[0] = v->s[1] = v->s[2] = v->s[3] = v->s[5] = v->s[6] = 0;
    v->s[4] = 32767;
    v->s[7] = (int32_t)(0x6C8E9CF5u ^ (v->age * 2654435761u));
}

/* a formant's phase increment: knob f (0 .. 127: note 48 .. 108), KTRK, the sweep */
static uint32_t vs_form(int32_t f, int32_t note16, int32_t ktrk, int32_t sweep16)
{
    int32_t p = (48 * 16 + f * 960 / 127) + ((note16 - 60 * 16) * ktrk >> 7) + sweep16;
    return pitch_inc((uint32_t)clamp(p, 0, VS_TOP));
}

static void vosim_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t sw = v->s[4], sweep16, b = 16384 + p[P_E4] * 129, bal = p[P_E2], brth = p[P_E7], npul = p[P_E3];
    int32_t a1 = v->s[0], a2 = v->s[1], c1 = v->s[2], c2 = v->s[3], dc = v->s[5], dc1 = v->s[6];
    uint32_t i, f1, f2, nz = (uint32_t)v->s[7];
    sw -= sw / 200 + (sw > 0);                        /* the sweep glides back (~200 ms) */
    sweep16 = (p[P_E6] * 12 * sw) >> 15;              /* SWEEP: +-48 semitones at most, 3/4 st a step */
    f1 = vs_form(p[P_E0], m->pitch16, p[P_E5], sweep16);
    f2 = vs_form(p[P_E1], m->pitch16, p[P_E5], sweep16);
    for (i = 0; i < n; i++) {
        uint32_t o0 = v->ph[0], o1 = v->ph[1], o2 = v->ph[2];
        int32_t x, y, e;
        v->ph[0] += m->inc;
        if (v->ph[0] < o0) {                          /* a new period: the trains start again */
            v->ph[1] = v->ph[2] = 0;
            a1 = a2 = 32767;
            c1 = c2 = 0;
        } else {
            v->ph[1] += f1;
            v->ph[2] += f2;
            if (v->ph[1] < o1 && ++c1 >= npul)        /* a pulse over: the next one DECAY times it, or the train's end */
                a1 = 0;
            else if (v->ph[1] < o1)
                a1 = a1 * b >> 15;
            if (v->ph[2] < o2 && ++c2 >= npul)
                a2 = 0;
            else if (v->ph[2] < o2)
                a2 = a2 * b >> 15;
        }
        /* sin^2: (1 - cos) / 2 a pulse */
        x = (a1 * ((32767 - sine_i(v->ph[1] + 0x40000000u)) >> 1)) >> 15;
        y = (a2 * ((32767 - sine_i(v->ph[2] + 0x40000000u)) >> 1)) >> 15;
        e = (x * (128 - bal) + y * bal) >> 7;
        nz ^= nz << 13;
        nz ^= nz >> 17;
        nz ^= nz << 5;
        e += ((((int32_t)(int16_t)nz * brth) >> 7) * ((a1 + a2) >> 1)) >> 16;   /* BRTH: noise in the pulses */
        x = e - dc1 + dc - (dc >> 8);                 /* (the DC blocker: the pulses are one-signed) */
        dc1 = e;
        dc = x;
        out[i] += voice_amp(clamp(x, -32767, 32767), m, i) << 1;
    }
    v->s[0] = a1;
    v->s[1] = a2;
    v->s[2] = c1;
    v->s[3] = c2;
    v->s[4] = sw;
    v->s[5] = dc;
    v->s[6] = dc1;
    v->s[7] = (int32_t)nz;
}

/* {F1, F2, BAL, PULS, DECAY, KTRK, SWEEP, BRTH}. F: 0 .. 127 is note 48 .. 108 (~130 Hz .. 4.2 kHz): 30 ~300 Hz,
 * 41 ~400, 45 ~450, 61 ~700, 66 ~800, 68 ~850, 81 ~1.2 kHz, 103 ~2.2 kHz, 112 ~2.7 kHz */
static const preset_t VOSIM_PRESETS[] = {
    {"VOWEL AH", {61, 81, 40, 4, 90, 0, 0, 6}, {2, 0, 127, 40}, 0, 0, FX(0, 20, 20, 30)},
    {"VOWEL OH", {45, 66, 35, 4, 96, 0, 0, 6}, {2, 0, 127, 40}, 0, 0, FX(0, 20, 20, 30)},
    {"VOWEL EE", {30, 112, 55, 3, 90, 0, 0, 6}, {2, 0, 127, 40}, 0, 0, FX(0, 20, 20, 30)},
    {"VOWEL OO", {30, 68, 25, 5, 100, 0, 0, 6}, {2, 0, 127, 40}, 0, 0, FX(0, 20, 20, 30)},
    {"TALK LEAD", {55, 90, 45, 3, 80, 20, 10, 0}, {0, 0, 127, 30}, 0, 1, FX(10, 0, 30, 20)},
    {"VOSIM BASS", {24, 50, 30, 2, 70, 90, 0, 0}, {0, 60, 90, 20}, 0, 1, FX(20, 0, 0, 5)},
    {"WAH BASS", {20, 40, 40, 3, 90, 60, 40, 0}, {0, 70, 60, 20}, 0, 1, FX(30, 0, 0, 5)},
    {"BRASS", {40, 70, 50, 3, 85, 50, -20, 4}, {12, 0, 127, 40}, 0, 0, FX(0, 20, 20, 30)},
    {"YOW", {70, 95, 50, 4, 95, 0, -40, 0}, {0, 0, 127, 30}, 0, 1, FX(20, 0, 30, 20)},
    {"CHOIR", {50, 78, 40, 5, 100, 10, 0, 40}, {50, 0, 127, 80}, 0, 0, FX(0, 70, 20, 60)},
    {"REED", {64, 100, 60, 6, 110, 30, 0, 10}, {6, 0, 127, 40}, 0, 0, FX(0, 10, 15, 30)},
    {"WHISTLE", {110, 120, 30, 8, 120, 80, 0, 20}, {10, 0, 127, 50}, 0, 0, FX(0, 20, 30, 40)},
    {"GROWL", {16, 34, 70, 2, 60, 40, 8, 30}, {0, 0, 127, 20}, 0, 1, FX(60, 0, 0, 5)},
    {"FORMANT PAD", {48, 84, 50, 6, 104, 0, 0, 16}, {70, 0, 127, 90}, 0, 0, FX(0, 80, 30, 70)},
};

static const engine_t ENG_VOSIM = {
    .name = "VOSIM",
    .id = EID_VOSIM,
    .cc = 0x12,                  /* CC74 F1, CC71 F2 */
    .cats = "LLLLLBBLLPLLBP",
    .page_title = {"FORMANT", "PULSE"},
    .edit = {
        {"F1", F_PCT, 0, 127, 61, 0, 0},
        {"F2", F_PCT, 0, 127, 81, 0, 0},
        {"BAL", F_PCT, 0, 127, 40, 0, 0},
        {"PULS", F_INT, 1, 8, 4, 0, 0},
        {"DECAY", F_PCT, 0, 127, 90, 0, 0},
        {"KTRK", F_PCT, 0, 127, 0, 0, 0},
        {"SWEEP", F_BIPCT, -64, 63, 0, 0, 0},
        {"BRTH", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = VOSIM_PRESETS,
    .npresets = NELEM(VOSIM_PRESETS),
    .note_on = vosim_note_on,
    .render = vosim_render,
    .knob = {P_E0, P_E1, P_E4, P_E6},
};
