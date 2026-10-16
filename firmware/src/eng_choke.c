/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* CHOKE (1.5, FOUNDRY): gated noise, hard limited (Blue Hare's own): snappy industrial snares, air-brakes, hisses.
 * Noise through a resonant band-pass at the key (TONE moves it, Q narrows it), with BODY a sine at the key falling an
 * octave (the snare's shell), SNAP a click at the start; the whole held open for GATE (5 ms .. ~1.3 s) then shut over
 * TAIL, SWEEP letting the band-pass fall across the gate (an air-brake's sigh), LIMIT driving it all into a hard
 * limiter (the slam). Its own envelope (engine_t.ownenv: the velocity the level).
 * Voice state: ph[0] BODY's phase, s[0] samples since the hit, s[1] the band-pass's states (s[1] s[2]), s[3] the
 * noise, s[4] the level (Q15), s[5] the gate's length, s[6] the tail's; ph[1] ph[2] the DC blocker (the limiter clips
 * lopsided). */
static void choke_note_on(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    v->s[0] = 0;
    v->s[1] = v->s[2] = 0;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[4] = 4096 + v->vel * 225;
    v->s[5] = 220 + p[P_E3] * p[P_E3] * 35 / 10;      /* GATE: 5 ms .. ~1.3 s */
    v->s[6] = 64 + p[P_E7] * p[P_E7] * 3;             /* TAIL: 1.5 ms .. ~1.1 s */
    if (!v->s[3])
        v->s[3] = (int32_t)(0x2545F491u ^ (v->age * 2654435761u));
}

static int choke_done(track_t *t, voice_t *v) { (void)t; return v->s[0] >= v->s[5] + v->s[6]; }

static void choke_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t age = v->s[0], ic1 = v->s[1], ic2 = v->s[2], amp = v->s[4], gl = v->s[5], tl = v->s[6], body = p[P_E0] * 220;
    int32_t snap = p[P_E4] * 258, lim = 256 + p[P_E6] * 30, sw = p[P_E5], k, cut, tinv = (1 << 29) / tl;
    uint32_t i, nz = (uint32_t)v->s[3], ph = v->ph[0], binc;
    int32_t x1 = (int32_t)v->ph[1], y1 = (int32_t)v->ph[2];
    tsvf_t flt;
    k = 8192 - p[P_E2] * 7800 / 127;                  /* Q: damping 2 .. ~0.1 */
    cut = (p[P_E1] << 8) + (m->pitch16 - 60 * 16) * 4 - (gl ? (sw * 40 * clamp(age, 0, gl) / gl) << 4 : 0);   /* SWEEP */
    tsvf_coef_k(&flt, cut, k);
    binc = m->inc - (uint32_t)(((uint64_t)(m->inc >> 1) * (uint32_t)clamp(age, 0, 1764)) / 1764u);   /* BODY: an octave
                                                       * down over ~40 ms (a step a block) */
    for (i = 0; i < n; i++) {
        int32_t x, v1, v2, v3, e, b;
        if (age >= gl + tl)
            break;
        e = age < gl ? 32767 : (int32_t)(((uint32_t)(gl + tl - age) * (uint32_t)tinv) >> 14);   /* the gate, then the tail */
        nz ^= nz << 13;
        nz ^= nz >> 17;
        nz ^= nz << 5;
        x = (int32_t)(int16_t)(nz >> 16) >> 1;
        v3 = x - ic2;                                 /* the band-pass (tsvf) */
        v1 = (flt.a1 * ic1 + flt.a2 * v3) >> 13;
        v2 = ic2 + ((flt.a2 * ic1 + flt.a3 * v3) >> 13);
        ic1 = clamp(2 * v1 - ic1, -150000, 150000);
        ic2 = clamp(2 * v2 - ic2, -150000, 150000);
        b = v1 * 2;
        if (body) {                                   /* BODY: a sine at the key, an octave down over ~40 ms */
            b += (sine_i(ph) * body) >> 15;
            ph += binc;
        }
        if (age < 40)
            b += (age < 3 ? snap : -snap) >> 1;       /* SNAP */
        b = (b * e) >> 15;
        b = clamp((b * lim) >> 8, -26000, 26000);     /* LIMIT: hard */
        x = b - x1 + y1 - (y1 >> 9);                  /* (the DC: the limiter's) */
        x1 = b;
        y1 = x;
        out[i] += voice_amp(clamp((x * amp) >> 15, -32767, 32767), m, i) << 1;
        age++;
    }
    v->s[0] = age;
    v->s[1] = ic1;
    v->s[2] = ic2;
    v->s[3] = (int32_t)nz;
    v->ph[0] = ph;
    v->ph[1] = (uint32_t)x1;
    v->ph[2] = (uint32_t)y1;
}

/* {BODY, TONE, Q, GATE, SNAP, SWEEP, LIMIT, TAIL} */
static const preset_t CHOKE_PRESETS[] = {
    {"SNAP SNARE", {60, 80, 40, 30, 100, 0, 60, 30}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 10, 25)},
    {"GATED SNARE", {40, 85, 30, 55, 80, 10, 90, 8}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 10, 50)},
    {"AIR BRAKE", {0, 100, 70, 110, 20, 90, 50, 60}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 20, 40)},
    {"HISS", {0, 110, 20, 80, 0, 20, 40, 50}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 20, 40)},
    {"SLAM", {90, 70, 50, 25, 127, 0, 127, 20}, {0, 0, 127, 20}, 0, 0, FX(30, 0, 10, 30)},
    {"STEAM", {0, 95, 90, 100, 10, 60, 60, 80}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 20, 50)},
    {"PISTON", {80, 60, 60, 15, 110, 0, 100, 15}, {0, 0, 127, 20}, 0, 0, FX(20, 0, 15, 20)},
    {"TIGHT CHOKE", {30, 90, 50, 10, 90, 0, 80, 4}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 10, 15)},
    {"INDUS SNARE", {70, 75, 45, 40, 110, 0, 110, 20}, {0, 0, 127, 20}, 0, 0, FX(20, 0, 10, 30)},
    {"HYDRAULIC", {20, 80, 80, 90, 30, 70, 70, 50}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 20, 40)},
    {"RIMSHOT", {50, 100, 60, 5, 127, 0, 90, 3}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 10, 20)},
    {"VENT", {0, 105, 30, 120, 0, 30, 40, 90}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 20, 50)},
    {"COMPRESSOR", {60, 70, 40, 60, 90, 20, 127, 30}, {0, 0, 127, 20}, 0, 0, FX(20, 0, 15, 30)},
    {"SHOTGUN", {100, 60, 40, 20, 127, 10, 127, 40}, {0, 0, 127, 20}, 0, 0, FX(40, 0, 10, 40)},
    {"WHIP", {10, 110, 70, 15, 120, 110, 80, 10}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 25, 40)},
    {"BIG GATE", {80, 80, 40, 70, 100, 0, 100, 6}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 10, 60)},
    {"CRACK", {40, 95, 50, 8, 127, 0, 100, 4}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 10, 30)},
    {"GATED CLAP", {30, 80, 40, 60, 100, 0, 90, 10}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 10, 50)},
    {"MARCH SNARE", {60, 90, 40, 25, 110, 0, 80, 12}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 10, 30)},
    {"BLAST DOOR", {90, 60, 60, 100, 80, 80, 110, 60}, {0, 0, 127, 20}, 0, 0, FX(20, 0, 20, 40)},
    {"PNEUMATIC", {10, 90, 90, 70, 40, 100, 60, 30}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 20, 40)},
    {"SHAKER", {0, 110, 20, 15, 60, 0, 40, 10}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 15)},
    {"STEAM VALVE", {0, 100, 60, 110, 10, 40, 50, 100}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 20, 50)},
    {"PUNCH SNARE", {110, 70, 40, 30, 127, 0, 127, 15}, {0, 0, 127, 20}, 0, 0, FX(30, 0, 10, 30)},
};

static const engine_t ENG_CHOKE = {
    .name = "CHOKE",
    .id = EID_CHOKE,
    .cc = 0x47,                  /* CC74 GATE, CC71 LIMIT */
    .cats = "DDFFDFDDDFDFDDFDDDDFFDFD",
    .page_title = {"NOISE", "GATE"},
    .edit = {
        {"BODY", F_PCT, 0, 127, 40, 0, 0},
        {"TONE", F_CUTOFF, 0, 127, 85, 0, 0},
        {"Q", F_PCT, 0, 127, 40, 0, 0},
        {"GATE", F_PCT, 0, 127, 30, 0, 0},
        {"SNAP", F_PCT, 0, 127, 80, 0, 0},
        {"SWEEP", F_PCT, 0, 127, 0, 0, 0},
        {"LIMIT", F_PCT, 0, 127, 60, 0, 0},
        {"TAIL", F_PCT, 0, 127, 20, 0, 0},
    },
    .presets = CHOKE_PRESETS,
    .npresets = NELEM(CHOKE_PRESETS),
    .poly = 6,
    .note_on = choke_note_on,
    .render = choke_render,
    .knob = {P_E3, P_E1, P_E6, P_E0},
    .ownenv = 1,
    .done = choke_done,
};
