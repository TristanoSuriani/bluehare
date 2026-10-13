/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* VENOM (1.5, FOUNDRY): caustic acid from two filters biting each other (Fel-Tracker's own). WAVE (SAW, SQR, band-
 * limited) into a resonant low-pass at CUT1 and then a resonant band-pass at CUT2 (both RES); XMOD lets each one's
 * output move the other's cutoff at audio rate (the band-pass's the low-pass's, the low-pass's the band-pass's: a
 * toxic, unstable squelch that bubbles and spits), every note opens them by ENV, closing over DEC, DRIVE at the end.
 * The two are state-variable filters in the simple (Chamberlin) form, their tuning cheap enough to move every sample.
 * Voice state: ph[0] the oscillator, s[0] s[1] the low-pass's low and band, s[2] s[3] the band-pass's, s[4] the
 * envelope (Q15). */
static const char *const N_VENOM_WAVE[] = {"SAW", "SQR", 0};

static void venom_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[4] = 32767;
}

/* a cutoff (0 .. 127 << 8) as the simple SVF's f (Q12, ~2 tan): at most ~1.3, where it is still stable */
static inline int32_t vn_f(int32_t cut)
{
    int32_t i = clamp(cut, 0, 126 << 8) >> 8, g = SVF_G[i] + (((SVF_G[i + 1] - SVF_G[i]) * (clamp(cut, 0, 126 << 8) & 255)) >> 8);
    return clamp(g * 2, 4, 5300);
}

static void venom_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t i, ph = v->ph[0], inc = m->inc, sq = p[P_E0] != 0;
    int32_t l1 = v->s[0], b1 = v->s[1], l2 = v->s[2], b2 = v->s[3], fe = v->s[4], tau = 2 + p[P_E6] * p[P_E6] / 10;
    int32_t q = 4096 - p[P_E3] * 30, xm = p[P_E4], drive = 256 + p[P_E7] * 9, f1, f2, open;
    fe -= fe / tau + (fe > 0);                        /* DEC */
    open = ((p[P_E5] * 200 * fe) >> 15) + m->cutoff + (m->pitch16 - 60 * 16) * 4;
    f1 = vn_f((p[P_E1] << 8) + open);
    f2 = vn_f((p[P_E2] << 8) + open);
    q = q < 220 ? 220 : q;                            /* RES: damping 1 .. ~0.05 (Q12) */
    for (i = 0; i < n; i++) {
        int32_t x = (sq ? osc_pulse(ph, inc, 0x80000000u) : osc_saw(ph, inc)) >> 1, h, a, b;
        a = clamp(f1 + ((f1 * ((b2 >> 3) * xm >> 7)) >> 12), 4, 5300);   /* XMOD: the band-pass moves the low-pass .. */
        b = clamp(f2 + ((f2 * ((l1 >> 3) * xm >> 7)) >> 12), 4, 5300);   /* .. and the low-pass the band-pass */
        l1 += (a * b1) >> 12;
        h = x - l1 - ((q * b1) >> 12);
        b1 += (a * h) >> 12;
        b1 = clamp(b1, -98304, 98304);
        l1 = clamp(l1, -98304, 98304);
        l2 += (b * b2) >> 12;
        h = l1 - l2 - ((q * b2) >> 12);
        b2 += (b * h) >> 12;
        b2 = clamp(b2, -98304, 98304);
        l2 = clamp(l2, -98304, 98304);
        h = softclip((((b2 >> 1) + (l1 >> 2)) * drive) >> 8);
        out[i] += voice_amp(h, m, i) << 1;
        ph += inc;
    }
    v->ph[0] = ph;
    v->s[0] = l1;
    v->s[1] = b1;
    v->s[2] = l2;
    v->s[3] = b2;
    v->s[4] = fe;
}

/* {WAVE, CUT1, CUT2, RES, XMOD, ENV, DEC, DRIVE}; WAVE 0 SAW 1 SQR */
static const preset_t VENOM_PRESETS[] = {
    {"VENOM ACID", {0, 35, 55, 110, 40, 80, 40, 40}, {0, 0, 127, 10}, 0, 1, FX(20, 0, 25, 10)},
    {"TOXIC", {0, 30, 70, 115, 90, 90, 35, 60}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 25, 10)},
    {"CAUSTIC", {1, 40, 60, 100, 70, 70, 50, 70}, {0, 0, 127, 15}, 0, 1, FX(30, 0, 20, 10)},
    {"SPIT", {0, 50, 80, 120, 127, 100, 15, 50}, {0, 0, 127, 10}, 0, 1, FX(20, 0, 30, 10)},
    {"BUBBLE BASS", {1, 25, 45, 90, 60, 60, 60, 40}, {0, 50, 90, 15}, 0, 1, FX(20, 0, 0, 5)},
    {"ACID BITE", {0, 20, 50, 118, 30, 120, 25, 80}, {0, 0, 127, 10}, 0, 1, FX(40, 0, 25, 10)},
    {"SNAKE LEAD", {1, 60, 85, 100, 50, 60, 60, 60}, {0, 40, 110, 25}, 0, 1, FX(20, 10, 35, 25)},
    {"POISON PAD", {0, 50, 70, 80, 40, 30, 100, 30}, {70, 90, 115, 95}, 0, 0, FX(0, 60, 25, 70)},
    {"CORROSIVE", {0, 45, 65, 124, 110, 80, 45, 100}, {0, 0, 127, 15}, 0, 1, FX(50, 0, 20, 10)},
    {"STING STAB", {1, 55, 90, 105, 60, 110, 15, 70}, {0, 60, 50, 20}, 0, 0, FX(20, 0, 40, 25)},
    {"NEUROTOXIN", {0, 30, 60, 120, 127, 110, 30, 90}, {0, 0, 127, 10}, 0, 1, FX(40, 0, 25, 10)},
    {"ADDER", {1, 45, 75, 110, 80, 90, 20, 60}, {0, 0, 127, 10}, 0, 1, FX(20, 0, 25, 10)},
    {"COBRA LEAD", {0, 65, 90, 105, 60, 50, 70, 80}, {0, 40, 110, 25}, 0, 1, FX(30, 10, 35, 25)},
    {"ACID RAIN", {0, 40, 60, 100, 90, 70, 40, 50}, {0, 0, 127, 10}, 0, 1, FX(20, 0, 30, 15)},
    {"VENOM DRONE", {1, 40, 55, 115, 100, 40, 127, 60}, {60, 0, 127, 90}, 0, 0, FX(10, 40, 25, 70)},
    {"FANG", {1, 35, 80, 124, 110, 127, 10, 110}, {0, 0, 127, 10}, 0, 1, FX(50, 0, 20, 10)},
    {"BLACK MAMBA", {0, 25, 50, 122, 100, 120, 25, 100}, {0, 0, 127, 10}, 0, 1, FX(40, 0, 25, 10)},
    {"SPITTER", {1, 55, 85, 118, 127, 110, 10, 80}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 25, 10)},
    {"SCORPION", {0, 35, 70, 115, 70, 90, 35, 70}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 25, 10)},
    {"TOXIC LEAD", {1, 60, 80, 110, 80, 60, 60, 90}, {0, 40, 110, 25}, 0, 1, FX(30, 10, 35, 25)},
    {"VIPER STAB", {0, 70, 95, 110, 90, 110, 40, 100}, {0, 60, 50, 20}, 0, 0, FX(20, 0, 40, 25)},
    {"VENOM WASH", {0, 45, 65, 100, 60, 40, 110, 40}, {70, 90, 115, 90}, 0, 0, FX(0, 60, 25, 70)},
    {"HISSING", {1, 70, 95, 124, 127, 80, 40, 60}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 25, 10)},
    {"CORAL SNAKE", {1, 30, 55, 105, 50, 80, 50, 50}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 25, 10)},
};

static const engine_t ENG_VENOM = {
    .name = "VENOM",
    .id = EID_VENOM,
    .cc = 0x24,                  /* CC74 CUT1, CC71 RES */
    .cats = "BBBBBBLPBFBBLBPBBBBLKPBB",
    .page_title = {"FILTERS", "BITE"},
    .edit = {
        {"WAVE", F_ENUM, 0, 1, 0, N_VENOM_WAVE, 0},
        {"CUT1", F_CUTOFF, 0, 127, 35, 0, 0},
        {"CUT2", F_CUTOFF, 0, 127, 55, 0, 0},
        {"RES", F_PCT, 0, 127, 110, 0, 0},
        {"XMOD", F_PCT, 0, 127, 40, 0, 0},
        {"ENV", F_PCT, 0, 127, 80, 0, 0},
        {"DEC", F_PCT, 0, 127, 40, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 40, 0, 0},
    },
    .presets = VENOM_PRESETS,
    .npresets = NELEM(VENOM_PRESETS),
    .note_on = venom_note_on,
    .render = venom_render,
    .knob = {P_E1, P_E3, P_E4, P_E5},
    .keep = 0x03,                /* the low-pass's states (a slide: no click) */
};
