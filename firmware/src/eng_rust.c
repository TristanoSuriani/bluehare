/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* RUST (1.5, FOUNDRY): corroded noise (Fel-Tracker's own): metallic sizzle, lo-fi decay. Noise (COLOR: dark .. white)
 * and a pulse at the key (TONE: how much of it), held at a lower rate (RATE: 1 .. 32 samples a step) and cut to fewer
 * bits (BITS 16 .. 2), CORRUPT flipping bits and jumping samples now and then; through two resonances at the key and
 * at 2.76 times it (METAL: how much of their ringing, RES how long), a low-pass last (FILTER). The ADSR shapes it.
 * Voice state: ph[0] the pulse, s[0] s[1] / s[2] s[3] the resonances, s[4] COLOR's and FILTER's filter (two Q15
 * halves), s[5] the noise, s[6] the held sample, s[7] its count. */
static void rust_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
    if (!v->s[5])
        v->s[5] = (int32_t)(0x2545F491u ^ (v->age * 2654435761u));
}

static void rust_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t i, nz = (uint32_t)v->s[5], ph = v->ph[0];
    int32_t a1 = v->s[0], a2 = v->s[1], b1 = v->s[2], b2 = v->s[3], hold = v->s[6], hc = v->s[7];
    int32_t clp = (int16_t)(v->s[4] & 0xFFFF), flp = v->s[4] >> 16, ck = 1500 + p[P_E0] * 245, tone = p[P_E1];
    int32_t rate = 1 + p[P_E2] / 4, bits = 16 - p[P_E3] * 14 / 127, corrupt = p[P_E4], metal = p[P_E5];
    int32_t fk = clamp(1500 + p[P_E7] * 250, 1500, 32767), mask = ~((1 << (16 - bits)) - 1), k, kt;
    tsvf_t r1, r2;
    k = 8192 - p[P_E6] * 8000 / 127;                  /* RES: damping 2 .. ~0.05 */
    kt = (m->pitch16 - 60 * 16) * 16;                 /* (the resonances at the key) */
    tsvf_coef_k(&r1, 60 * 256 + kt, k);
    tsvf_coef_k(&r2, 60 * 256 + kt + 17 * 256 + 145, k);   /* (2.76 times: 17.6 semitones up) */
    ck = ck > 32767 ? 32767 : ck;
    for (i = 0; i < n; i++) {
        int32_t x, v1, v2, v3, y;
        nz ^= nz << 13;
        nz ^= nz >> 17;
        nz ^= nz << 5;
        clp += (((int32_t)(int16_t)(nz >> 16) - clp) * ck) >> 15;   /* COLOR */
        x = (clp * (127 - tone) + osc_pulse(ph, m->inc, 0x60000000u) * tone) >> 7;
        ph += m->inc;
        if (--hc <= 0) {                              /* RATE, BITS, CORRUPT */
            hc = rate;
            hold = x & mask;
            if (corrupt && ((nz >> 8) & 1023u) < (uint32_t)corrupt)
                hold ^= 1 << (8 + (nz & 7u));         /* a bit flipped */
            if (corrupt > 60 && ((nz >> 20) & 255u) < (uint32_t)(corrupt - 60) / 2u)
                hold = (int32_t)(int16_t)(nz >> 3); /* a sample jumped */
        }
        x = hold >> 1;
        y = x;
        if (metal) {                                  /* METAL: the two resonances (tsvf band-pass) */
            v3 = x - a2;
            v1 = (r1.a1 * a1 + r1.a2 * v3) >> 13;
            v2 = a2 + ((r1.a2 * a1 + r1.a3 * v3) >> 13);
            a1 = clamp(2 * v1 - a1, -150000, 150000);
            a2 = clamp(2 * v2 - a2, -150000, 150000);
            y = v1;
            v3 = x - b2;
            v1 = (r2.a1 * b1 + r2.a2 * v3) >> 13;
            v2 = b2 + ((r2.a2 * b1 + r2.a3 * v3) >> 13);
            b1 = clamp(2 * v1 - b1, -150000, 150000);
            b2 = clamp(2 * v2 - b2, -150000, 150000);
            y = (x * (127 - metal) + (y + v1) * metal) >> 7;
        }
        flp += ((y - flp) * fk) >> 15;                /* FILTER */
        flp = clamp(flp, -32767, 32767);
        out[i] += voice_amp(soft_knee(flp << 1, 24000), m, i) << 1;
    }
    v->ph[0] = ph;
    v->s[0] = a1;
    v->s[1] = a2;
    v->s[2] = b1;
    v->s[3] = b2;
    v->s[4] = (int32_t)(((uint32_t)flp << 16) | ((uint32_t)clp & 0xFFFFu));
    v->s[5] = (int32_t)nz;
    v->s[6] = hold;
    v->s[7] = hc;
}

/* {COLOR, TONE, RATE, BITS, CORRUPT, METAL, RES, FILTER} */
static const preset_t RUST_PRESETS[] = {
    {"SIZZLE", {120, 0, 8, 40, 20, 60, 90, 110}, {0, 30, 60, 40}, 0, 0, FX(0, 10, 20, 30)},
    {"RUSTED BELL", {90, 20, 4, 30, 10, 110, 115, 100}, {0, 70, 0, 80}, 0, 0, FX(0, 20, 30, 50)},
    {"LO-FI DECAY", {80, 30, 40, 80, 30, 40, 80, 70}, {0, 80, 40, 70}, 0, 0, FX(0, 10, 30, 40)},
    {"CORRODED", {110, 10, 20, 70, 90, 70, 100, 100}, {0, 40, 80, 40}, 0, 0, FX(10, 0, 20, 30)},
    {"OXIDE PAD", {60, 40, 16, 50, 20, 60, 100, 70}, {70, 90, 110, 95}, 0, 0, FX(0, 50, 25, 70)},
    {"FERRIC HAT", {127, 0, 2, 20, 10, 50, 70, 127}, {0, 15, 0, 10}, 0, 0, FX(0, 0, 10, 15)},
    {"BROKEN WIRE", {100, 60, 30, 90, 120, 80, 110, 90}, {0, 50, 60, 40}, 0, 0, FX(10, 10, 25, 30)},
    {"SCRAP DRONE", {70, 50, 12, 60, 40, 90, 120, 60}, {60, 0, 127, 100}, 0, 0, FX(10, 30, 25, 70)},
    {"IRON OXIDE", {90, 30, 20, 60, 40, 80, 110, 90}, {0, 40, 80, 40}, 0, 0, FX(10, 0, 20, 30)},
    {"DEAD RADIO", {110, 10, 30, 90, 100, 30, 60, 80}, {0, 40, 80, 40}, 0, 0, FX(0, 10, 25, 30)},
    {"RUST BASS", {40, 90, 30, 60, 30, 40, 80, 50}, {0, 60, 100, 20}, 0, 1, FX(20, 0, 0, 5)},
    {"FLAKE HAT", {127, 0, 4, 30, 40, 40, 60, 127}, {0, 10, 0, 8}, 0, 0, FX(0, 0, 10, 15)},
    {"CORRODE KEYS", {70, 70, 20, 40, 20, 80, 110, 110}, {0, 60, 100, 40}, 0, 0, FX(0, 20, 30, 40)},
    {"STATIC STORM", {120, 20, 50, 100, 127, 60, 90, 100}, {20, 0, 127, 60}, 0, 0, FX(10, 10, 25, 50)},
    {"TETANUS", {100, 50, 60, 110, 110, 100, 120, 90}, {0, 50, 60, 40}, 0, 0, FX(20, 0, 25, 30)},
    {"SALT WATER", {80, 30, 10, 40, 60, 90, 127, 70}, {60, 0, 127, 100}, 0, 0, FX(0, 40, 25, 75)},
};

static const engine_t ENG_RUST = {
    .name = "RUST",
    .id = EID_RUST,
    .cc = 0x46,                  /* CC74 BITS .. METAL, CC71 RES */
    .cats = "FKFFPDFPFFBDKFFP",
    .page_title = {"NOISE", "METAL"},
    .edit = {
        {"COLOR", F_PCT, 0, 127, 100, 0, 0},
        {"TONE", F_PCT, 0, 127, 20, 0, 0},
        {"RATE", F_PCT, 0, 127, 10, 0, 0},
        {"BITS", F_PCT, 0, 127, 40, 0, 0},
        {"CORRUPT", F_PCT, 0, 127, 20, 0, 0},
        {"METAL", F_PCT, 0, 127, 60, 0, 0},
        {"RES", F_PCT, 0, 127, 90, 0, 0},
        {"FILTER", F_PCT, 0, 127, 100, 0, 0},
    },
    .presets = RUST_PRESETS,
    .npresets = NELEM(RUST_PRESETS),
    .note_on = rust_note_on,
    .render = rust_render,
    .knob = {P_E3, P_E4, P_E5, P_E6},
};
