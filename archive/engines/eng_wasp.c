/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* WASP (1.5): two oscillators into a gritty multimode filter, after the little black-and-yellow synth of 1978
 * (Blue Hare's own design: no circuit modelled). WAVE pairs the two oscillators (band-limited SAW, SQR and a 25 %
 * PUL, and NOIS), OSC2 sits -24 .. +24 semitones from the first and DTN detunes them (each voice drifts a little
 * too, as analog oscillators do); MIX balances them. Their sum, driven by GRIT, goes into a 12 dB state-variable
 * filter (MODE LP, BP, HP, NOTCH at CUT, the key half tracked) whose band-pass integrator saturates, as inverters
 * pushed past their rails: RES towards the filter singing by itself (from ~115), past it a self-oscillation the
 * saturation holds, unsteady, chaotic with GRIT. The low-pass keeps its lows whatever RES (the state-variable filter's passband
 * stays at unity while its peak rises: no thinning as a ladder's), the buzzy, full sound of the form. GRIT also
 * cuts the integrators to fewer bits (16 .. 8): steps, limit cycles and fizz as the filter rings, the "8-bit"
 * filter. The amplitude is the ADSR.
 * Voice state: ph[0] / ph[1] the oscillators, s[0] / s[1] the filter's integrators, s[2] the noise generator, s[3]
 * the drift (a random walk, 1/512 cent). */
static const char *const N_WASP_WAVE[] = {"SAW+SAW", "SAW+SQR", "SQR+SQR", "SAW+PUL", "SQR+PUL", "PUL+PUL", "SAW+NOIS",
                                          "SQR+NOIS", 0};
static const char *const N_WASP_MODE[] = {"LP", "BP", "HP", "NOTCH", 0};
static const uint8_t WASP_W[8][2] = {   /* WAVE: the two oscillators (0 SAW, 1 SQR, 2 PUL, 3 NOIS) */
    {0, 0}, {0, 1}, {1, 1}, {0, 2}, {1, 2}, {2, 2}, {0, 3}, {1, 3},
};

static void wasp_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0;
    v->ph[1] = 0x40000000u;                           /* (a quarter cycle apart: no flam at the start) */
    v->s[0] = v->s[1] = 0;                            /* (a retrigger keeps the filter: voice.c, keep) */
    if (!v->s[2])
        v->s[2] = (int32_t)(0x2545F491u ^ (v->age * 2654435761u));
}

static inline int32_t wasp_osc(uint32_t w, uint32_t ph, uint32_t inc, uint32_t *nz)
{
    switch (w) {
    case 0u: return osc_saw(ph, inc);
    case 1u: return osc_pulse(ph, inc, 0x80000000u);
    case 2u: return osc_pulse(ph, inc, 0x40000000u);
    default:
        *nz ^= *nz << 13;
        *nz ^= *nz >> 17;
        *nz ^= *nz << 5;
        return (int16_t)(*nz >> 16);
    }
}

/* the band-pass integrator past its rails: unity while small, tanh-like towards +-65536 (the resonance's energy is
 * there: it holds the singing; the low-pass integrator stays linear, so the lows pass whatever RES) */
static inline int32_t wasp_sat(int32_t x) { return softclip(x >> 1) << 1; }

static void wasp_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t w1 = WASP_W[(uint32_t)p[P_E0] & 7u][0], w2 = WASP_W[(uint32_t)p[P_E0] & 7u][1], mode = (uint32_t)p[P_E4] & 3u;
    uint32_t nz = (uint32_t)v->s[2], ph1 = v->ph[0], ph2 = v->ph[1], inc1, inc2, i;
    int32_t mix = p[P_E3], grit = p[P_E7], ic1 = v->s[0], ic2 = v->s[1], drift = v->s[3], dc, k, cut, drive, q, half;
    tsvf_t flt;
    nz ^= nz << 13;                                   /* the drift: a random walk a block, pulled back (~+-3 cents) */
    nz ^= nz >> 17;
    nz ^= nz << 5;
    drift += ((int32_t)(nz >> 20) - 2048) >> 3;
    drift -= drift >> 6;
    dc = p[P_E2] * 40 / 127;                          /* DTN: the two apart by up to 40 cents */
    inc1 = cents_inc(m->pitch16, -(dc >> 1) + (drift >> 9), m->fine);
    inc2 = cents_inc(m->pitch16 + p[P_E1] * 16, dc - (dc >> 1) - (drift >> 9), m->fine);
    drive = 256 + grit * 6;                           /* GRIT: the drive into the filter, 1x .. ~4x (Q8) */
    q = 1 << (grit * 8 / 127);                        /* GRIT: the integrators' step, 1 .. 256 (16 .. 8 bits) */
    half = q >> 1;
    k = 8192 - p[P_E6] * 9000 / 127;                  /* RES: damping 2 .. 0 at ~116 .. -0.2 (it sings) */
    cut = (p[P_E5] << 8) + m->cutoff + (m->pitch16 - 60 * 16) * 8;
    tsvf_coef_k(&flt, cut, k);
    for (i = 0; i < n; i++) {
        int32_t a = wasp_osc(w1, ph1, inc1, &nz), b = wasp_osc(w2, ph2, inc2, &nz), x, v1, v2, v3, y;
        x = (a * (128 - mix) + b * mix) >> 8;         /* (+-16k) */
        x = softclip((x * drive) >> 8) >> 1;          /* driven, saturated: +-16k into the filter */
        v3 = x - ic2;                                 /* the trapezoidal SVF (tsvf_lp), its integrators saturating */
        v1 = (flt.a1 * ic1 + flt.a2 * v3) >> 13;
        v2 = ic2 + ((flt.a2 * ic1 + flt.a3 * v3) >> 13);
        ic1 = wasp_sat(2 * v1 - ic1);
        ic2 = clamp(2 * v2 - ic2, -150000, 150000);
        if (q > 1) {                                  /* GRIT: fewer bits, rounded */
            ic1 = (ic1 + half) & -q;
            ic2 = (ic2 + half) & -q;
        }
        y = mode == 0u ? v2 : mode == 1u ? v1 * 2 : mode == 2u ? x - ((v1 * k) >> 12) - v2 : x - ((v1 * k) >> 12);
        y = soft_knee(clamp(y * 2, -98304, 98304), 22000);   /* (BP: x 2, its narrow band; the singing: a soft top) */
        out[i] += voice_amp(clamp(y, -32767, 32767), m, i) << 1;
        ph1 += inc1;
        ph2 += inc2;
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = (int32_t)nz;
    v->s[3] = drift;
}

/* {WAVE, OSC2, DTN, MIX, MODE, CUT, RES, GRIT}; WAVE 0 SAW+SAW 1 SAW+SQR 2 SQR+SQR 3 SAW+PUL 4 SQR+PUL 5 PUL+PUL
 * 6 SAW+NOIS 7 SQR+NOIS; MODE 0 LP 1 BP 2 HP 3 NOTCH. RES ~116: the filter sings by itself. fenv: ENV -> FILTER */
static const preset_t WASP_PRESETS[] = {
    /* leads */
    {"WASP LEAD", {1, 12, 30, 64, 0, 80, 95, 85}, {0, 40, 100, 30}, 35, 1, FX(30, 0, 30, 20)},
    {"STING LEAD", {3, 0, 75, 64, 0, 78, 85, 70}, {0, 30, 100, 25}, 35, 1, FX(25, 20, 35, 20)},
    {"BUZZ LEAD", {5, 0, 22, 64, 0, 85, 75, 110}, {0, 40, 110, 30}, 25, 1, FX(30, 10, 30, 20)},
    {"FUZZ LEAD", {2, 12, 10, 64, 0, 90, 60, 100}, {0, 30, 110, 30}, 15, 1, FX(30, 0, 30, 20)},
    {"SINGING LP", {0, 0, 8, 30, 0, 60, 112, 80}, {5, 40, 100, 40}, 25, 1, FX(0, 20, 40, 35)},
    /* basses: resonant and not */
    {"WASP BASS", {1, -12, 10, 64, 0, 50, 65, 110}, {0, 50, 60, 20}, 45, 1, FX(10, 0, 10, 5)},
    {"RESO BASS", {0, 0, 25, 64, 0, 53, 95, 58}, {0, 35, 40, 15}, 55, 1, FX(10, 0, 15, 5)},
    {"FAT BASS", {2, -12, 18, 60, 0, 55, 20, 20}, {0, 60, 80, 20}, 20, 1, FX(0, 0, 0, 5)},
    {"GRIT BASS", {4, -12, 8, 64, 0, 35, 85, 110}, {0, 40, 50, 15}, 50, 1, FX(20, 0, 10, 5)},
    {"SUB BUZZ", {5, -12, 5, 50, 0, 50, 30, 60}, {0, 50, 90, 15}, 15, 1, FX(10, 0, 0, 5)},
    {"PULSE BASS", {5, -12, 10, 64, 0, 55, 70, 80}, {0, 45, 70, 20}, 40, 1, FX(15, 0, 10, 5)},
    {"BP BASS", {1, -12, 12, 64, 1, 45, 75, 30}, {0, 45, 50, 20}, 35, 1, FX(10, 0, 10, 5)},
    /* reeses: two saws (or a saw and a pulse) far apart in tune, beating; the drift keeps them moving */
    {"REESE", {0, 0, 110, 64, 0, 55, 40, 60}, {0, 0, 127, 20}, 0, 1, FX(20, 20, 10, 10)},
    {"BUZZ REESE", {3, 0, 100, 64, 0, 62, 60, 105}, {0, 0, 127, 20}, 0, 1, FX(30, 15, 10, 10)},
    {"DARK REESE", {0, -12, 90, 64, 0, 40, 70, 80}, {0, 0, 127, 20}, 10, 1, FX(20, 10, 10, 10)},
    {"GRIT REESE", {1, 0, 120, 64, 0, 50, 95, 127}, {0, 0, 127, 20}, 15, 1, FX(35, 10, 10, 10)},
    /* pads */
    {"WASP PAD", {0, 7, 40, 64, 0, 60, 50, 20}, {70, 80, 110, 90}, 20, 0, FX(0, 60, 30, 70)},
    {"DUSTY PAD", {6, 12, 35, 40, 0, 55, 40, 70}, {80, 90, 110, 100}, 10, 0, FX(0, 60, 25, 80)},
    {"HOLLOW PAD", {2, 12, 30, 64, 3, 70, 60, 30}, {70, 80, 110, 90}, 15, 0, FX(0, 60, 30, 70)},
    {"SWARM PAD", {5, 7, 60, 64, 1, 65, 80, 50}, {90, 90, 110, 100}, 10, 0, FX(0, 70, 30, 80)},
    /* keys */
    {"WASP KEYS", {1, 12, 12, 64, 0, 75, 40, 20}, {0, 50, 60, 30}, 30, 0, FX(0, 20, 25, 30)},
    {"BUZZ ORGAN", {2, 12, 8, 64, 0, 100, 10, 30}, {2, 0, 127, 20}, 0, 0, FX(0, 30, 20, 30)},
    {"PLUCK KEYS", {3, 0, 15, 64, 0, 76, 72, 65}, {0, 72, 0, 50}, 50, 0, FX(0, 10, 30, 30)},
    {"CMOS CLAV", {5, 0, 5, 64, 2, 35, 70, 50}, {0, 30, 50, 20}, 30, 0, FX(10, 0, 20, 15)},
    /* self-oscillating weirdness, grit */
    {"SELF OSC", {0, 0, 0, 0, 0, 70, 127, 0}, {0, 0, 127, 40}, 0, 0, FX(0, 20, 30, 40)},
    {"WASP NEST", {7, 19, 90, 64, 1, 60, 127, 127}, {0, 0, 127, 60}, 0, 0, FX(0, 30, 30, 50)},
    {"BROKEN BITS", {4, 6, 70, 64, 0, 50, 120, 127}, {0, 60, 40, 40}, 40, 0, FX(20, 0, 30, 30)},
    {"HORNET", {6, 1, 100, 64, 1, 85, 115, 90}, {10, 0, 127, 40}, 0, 0, FX(10, 20, 30, 40)},
    {"GRIT DRONE", {7, 0, 0, 40, 0, 62, 108, 127}, {60, 0, 127, 90}, 0, 0, FX(20, 50, 30, 70)},
    {"8BIT SQUEAL", {2, 24, 40, 64, 2, 95, 122, 120}, {0, 40, 60, 30}, -30, 0, FX(10, 0, 40, 30)},
};

static const engine_t ENG_WASP = {
    .name = "WASP",
    .id = EID_WASP,
    .cc = 0x67,                  /* CC74 CUT, CC71 RES */
    .cats = "LLLLLBBBBBBBBBBBPPPPKKUKFFFFFF",
    .page_title = {"OSC", "FILTER"},
    .edit = {
        {"WAVE", F_ENUM, 0, 7, 1, N_WASP_WAVE, 0},
        {"OSC2", F_INT, -24, 24, 12, 0, "st"},
        {"DTN", F_PCT, 0, 127, 20, 0, 0},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"MODE", F_ENUM, 0, 3, 0, N_WASP_MODE, 0},
        {"CUT", F_CUTOFF, 0, 127, 70, 0, 0},
        {"RES", F_PCT, 0, 127, 60, 0, 0},
        {"GRIT", F_PCT, 0, 127, 30, 0, 0},
    },
    .presets = WASP_PRESETS,
    .npresets = NELEM(WASP_PRESETS),
    .note_on = wasp_note_on,
    .render = wasp_render,
    .knob = {P_E5, P_E6, P_E7, P_E3},
    .keep = 0x03,                /* the filter (a retrigger, a slide: no click); the noise and the drift go on */
};
