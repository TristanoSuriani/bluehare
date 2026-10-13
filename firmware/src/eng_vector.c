/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* VECTOR (1.5): vector synthesis, in the manner of the late-80s vector synths (Fel-Tracker's own). Four sources sit at
 * the corners of a square, A B above, C D below; a point moves over it and the sources are heard by how near it is
 * (bilinear), so the sound travels from one to another. The sources are WAVE's frames (eng_wave.c: its tables, its
 * band-limited copies, wt_read), SET picks four of them. A and C play at the note a little up, B and D a little down
 * (DTN, 0 .. ~63 cents apart: the corners beat against each other). PATH moves the point: HOLD (it stays at X, Y),
 * ENV (each note runs round from A by B, D, C and settles at X, Y: a vector envelope, RATE its speed), LOOP (round and
 * round), CIRCLE and EIGHT (around X, Y: a circle, a figure of eight), RANDOM (a smooth wander to random places);
 * RATE ~0.02 .. 5 Hz. The SHP routings (ENV / LFO DEST SHP, the matrix) move X as well. Then a low-pass (CUT, RES, the
 * key a quarter tracked, ENV -> FILTER).
 * Voice state: ph[0] / ph[1] the two pitches' phases, ph[2] the path's, s[0] s[1] the filter, s[2] s[3] the point (Q15,
 * RANDOM), s[4] s[5] where it heads (RANDOM), s[6] 1: ENV's lap over. */
static const char *const N_VC_SET[] = {"VOXSTR", "GLASS", "ORGAN", "DIGI", "AIR", "METAL", "CHOIR", "SYNTH", 0};
static const char *const N_VC_PATH[] = {"HOLD", "ENV", "LOOP", "CIRCLE", "EIGHT", "RANDOM", 0};
static const uint8_t VC_SET[8][4][2] = {   /* SET: the corners A B C D, each {WAVE's bank, its frame} */
    {{4, 0}, {1, 5}, {4, 5}, {0, 3}},      /* VOXSTR: vowels, a string-like additive, a bright vowel, a saw-ish */
    {{5, 1}, {1, 7}, {5, 6}, {0, 0}},      /* GLASS: FM bells, high partials, a sine */
    {{1, 2}, {1, 4}, {0, 2}, {5, 3}},      /* ORGAN: drawbar-like additives */
    {{6, 0}, {6, 4}, {7, 2}, {6, 7}},      /* DIGI: digital frames, bits */
    {{0, 0}, {4, 2}, {1, 1}, {5, 0}},      /* AIR: soft: a sine, a vowel, the first additives */
    {{5, 7}, {2, 5}, {6, 3}, {3, 6}},      /* METAL: hard FM, sync, digital, resonant */
    {{4, 1}, {4, 3}, {4, 6}, {4, 7}},      /* CHOIR: four vowels */
    {{0, 4}, {2, 2}, {3, 3}, {0, 7}},      /* SYNTH: the basics, a sync, a resonance */
};

static void vector_note_on(track_t *t, voice_t *v)
{
    v->ph[0] = 0;
    v->ph[1] = 0x40000000u;                           /* (a quarter cycle apart: no flam at the start) */
    v->ph[2] = 0;                                     /* the path from its start: a vector envelope a note */
    v->s[2] = t->p[P_E3] * 258;
    v->s[3] = t->p[P_E4] * 258;
    v->s[4] = (int32_t)((v->age * 2654435761u) >> 17);   /* (RANDOM: off somewhere at once) */
    v->s[5] = (int32_t)((v->age * 2246822519u) >> 17);
    v->s[6] = 0;
}

/* the point (Q15 x, y) on the path at phase u (a lap = 2^32), the centre cx, cy */
static void vc_path(voice_t *v, uint32_t path, uint32_t u, uint32_t du, int32_t cx, int32_t cy, int32_t *x, int32_t *y)
{
    static const int32_t CX[5] = {0, 32767, 32767, 0, 0}, CY[5] = {0, 0, 32767, 32767, 0};   /* A B D C A */
    uint32_t seg, f;
    switch (path) {
    case 1u:                                          /* ENV: A B D C, then to the centre, and there */
        if (v->s[6] || u >= 0xF0000000u || u < du) {  /* (the lap over: held there) */
            v->s[6] = 1;
            *x = cx, *y = cy;
            return;
        }
        u = (uint32_t)(((uint64_t)u * 0x11111111u) >> 28);   /* (x 16/15: the last 15th of the lap held) */
        seg = u >> 30;
        f = (u >> 15) & 0x7FFFu;
        if (seg == 3u) {
            *x = CX[3] + (((cx - CX[3]) * (int32_t)f) >> 15);
            *y = CY[3] + (((cy - CY[3]) * (int32_t)f) >> 15);
        } else {
            *x = CX[seg] + (((CX[seg + 1] - CX[seg]) * (int32_t)f) >> 15);
            *y = CY[seg] + (((CY[seg + 1] - CY[seg]) * (int32_t)f) >> 15);
        }
        return;
    case 2u:                                          /* LOOP: A B D C A .. */
        seg = u >> 30;
        f = (u >> 15) & 0x7FFFu;
        *x = CX[seg] + (((CX[seg + 1] - CX[seg]) * (int32_t)f) >> 15);
        *y = CY[seg] + (((CY[seg + 1] - CY[seg]) * (int32_t)f) >> 15);
        return;
    case 3u:                                          /* CIRCLE around the centre */
        *x = cx + (sine_i(u + 0x40000000u) >> 1);
        *y = cy + (sine_i(u) >> 1);
        return;
    case 4u:                                          /* EIGHT: across and up and down twice a lap */
        *x = cx + (sine_i(u) >> 1);
        *y = cy + (sine_i(u << 1) >> 1);
        return;
    case 5u:                                          /* RANDOM: towards a target, a new one a lap */
        if (u < du) {
            uint32_t h = (v->age + u) * 2654435761u;
            v->s[4] = (int32_t)(h >> 17);
            v->s[5] = (int32_t)((h * 2654435761u) >> 17);
        }
        f = du >> 15;                                 /* (a quarter of a lap to get most of the way there) */
        f = f < 1u ? 1u : f > 32767u ? 32767u : f;
        v->s[2] += (int32_t)(((int64_t)(v->s[4] - v->s[2]) * (int32_t)f) >> 15);
        v->s[3] += (int32_t)(((int64_t)(v->s[5] - v->s[3]) * (int32_t)f) >> 15);
        *x = v->s[2], *y = v->s[3];
        return;
    default:
        *x = cx, *y = cy;
        return;
    }
}

static void vector_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const uint8_t (*c)[2] = VC_SET[(uint32_t)p[P_E0] & 7u];
    uint32_t path = (uint32_t)p[P_E1] % 6u, du, inc1, inc2, l1, l2, i, k;
    int32_t cx = clamp(p[P_E3] * 258 + (m->shape - (64 << 8)) * 2, 0, 32767), cy = p[P_E4] * 258, x, y, w[4], dt;
    int32_t ic1 = v->s[0], ic2 = v->s[1];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1];
    const int16_t *f[4];
    tsvf_t flt;
    du = pitch_inc((uint32_t)p[P_E2] * 12u) / 13u;    /* RATE: a lap ~50 s .. ~0.2 s (the path's phase a block) */
    vc_path(v, path, v->ph[2] + du, du, cx, cy, &x, &y);
    v->ph[2] += du;
    x = clamp(x, 0, 32767);
    y = clamp(y, 0, 32767);
    w[0] = ((32767 - x) * (32767 - y)) >> 15;         /* A B C D by nearness */
    w[1] = (x * (32767 - y)) >> 15;
    w[2] = ((32767 - x) * y) >> 15;
    w[3] = (x * y) >> 15;
    dt = p[P_E5] >> 1;                                /* DTN: A C up, B D down */
    inc1 = cents_inc(m->pitch16, dt, m->fine);
    inc2 = cents_inc(m->pitch16, -dt, m->fine);
    l1 = wt_level(inc1, WT_LIM);
    l2 = wt_level(inc2, WT_LIM);
    for (k = 0; k < 4u; k++)
        f[k] = WT_DATA + ((uint32_t)c[k][0] % WT_NBANK * WT_NFRAME + c[k][1] % WT_NFRAME) * WT_FRAME_LEN +
               WT_LVL_OFF[k & 1u ? l2 : l1];
    tsvf_coef(&flt, (p[P_E6] << 8) + m->cutoff + (v->pitch16 - 60 * 16) * 4, p[P_E7]);
    for (i = 0; i < n; i++) {
        int32_t s = 0;
        if (w[0] > 32)                                /* (a corner hardly heard: not read) */
            s += wt_read(f[0], l1, ph0) * w[0];
        if (w[1] > 32)
            s += wt_read(f[1], l2, ph1) * w[1];
        if (w[2] > 32)
            s += wt_read(f[2], l1, ph0) * w[2];
        if (w[3] > 32)
            s += wt_read(f[3], l2, ph1) * w[3];
        s >>= 15;
        s = soft_knee(tsvf_lp(&flt, s >> 1, &ic1, &ic2), 16000) << 1;
        out[i] += voice_amp(s, m, i) << 1;
        ph0 += inc1;
        ph1 += inc2;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* {SET, PATH, RATE, X, Y, DTN, CUT, RES}; SET 0 VOXSTR 1 GLASS 2 ORGAN 3 DIGI 4 AIR 5 METAL 6 CHOIR 7 SYNTH; PATH 0 HOLD
 * 1 ENV 2 LOOP 3 CIRCLE 4 EIGHT 5 RANDOM; RATE ~0.02 Hz at 0, ~0.15 at 30, ~0.5 at 50, ~2 at 80 */
static const preset_t VECTOR_PRESETS[] = {
    {"VS PAD", {0, 3, 25, 64, 64, 12, 90, 10}, {80, 90, 115, 95}, 0, 0, FX(0, 60, 25, 75)},
    {"GLASS LOOP", {1, 2, 35, 64, 64, 8, 100, 10}, {60, 90, 115, 90}, 0, 0, FX(0, 50, 30, 75)},
    {"ORGAN MOTION", {2, 4, 30, 64, 64, 6, 95, 5}, {20, 0, 127, 40}, 0, 0, FX(0, 40, 20, 40)},
    {"DIGI TRAVEL", {3, 2, 45, 64, 64, 0, 105, 15}, {10, 60, 110, 50}, 0, 0, FX(0, 20, 35, 45)},
    {"AIR SWEEP", {4, 1, 40, 80, 90, 10, 85, 10}, {90, 90, 115, 100}, 0, 0, FX(0, 60, 25, 80)},
    {"METAL DRIFT", {5, 5, 30, 64, 64, 10, 100, 20}, {60, 90, 110, 95}, 0, 0, FX(0, 40, 30, 70)},
    {"CHOIR MORPH", {6, 3, 20, 64, 64, 14, 85, 5}, {90, 90, 120, 100}, 0, 0, FX(0, 70, 20, 80)},
    {"SYNTH VECTOR", {7, 1, 60, 30, 70, 10, 90, 20}, {0, 60, 90, 30}, 20, 1, FX(0, 10, 35, 25)},
    {"EVOLUTION", {0, 5, 22, 64, 64, 16, 90, 10}, {90, 90, 115, 100}, 0, 0, FX(0, 60, 25, 80)},
    {"SLOW CIRCLE", {1, 3, 10, 64, 64, 12, 95, 5}, {100, 90, 120, 105}, 0, 0, FX(0, 60, 30, 85)},
    {"VECTOR KEYS", {2, 1, 80, 20, 30, 6, 100, 10}, {0, 70, 50, 40}, 20, 0, FX(0, 20, 30, 35)},
    {"TEXTURE", {3, 5, 50, 64, 64, 20, 90, 25}, {70, 90, 115, 95}, 0, 0, FX(0, 50, 35, 70)},
    {"DARK VECTOR", {5, 4, 18, 40, 60, 10, 60, 20}, {90, 90, 120, 100}, 10, 0, FX(0, 60, 25, 80)},
    {"BELL VECTOR", {1, 1, 90, 90, 20, 6, 110, 5}, {0, 90, 0, 80}, 0, 0, FX(0, 20, 40, 50)},
    {"VOX VECTOR", {6, 4, 35, 64, 64, 10, 90, 10}, {50, 0, 127, 70}, 0, 0, FX(0, 60, 25, 70)},
    {"MOTION BASS", {7, 2, 55, 30, 30, 0, 70, 30}, {0, 60, 80, 20}, 30, 1, FX(10, 0, 10, 5)},
};

static const engine_t ENG_VECTOR = {
    .name = "VECTOR",
    .id = EID_VECTOR,
    .cc = 0x78,                  /* CC74 CUT, CC71 RES */
    .cats = "PPKLPPPLPPKPPUPB",
    .page_title = {"VECTOR", "TONE"},
    .edit = {
        {"SET", F_ENUM, 0, 7, 0, N_VC_SET, 0},
        {"PATH", F_ENUM, 0, 5, 3, N_VC_PATH, 0},
        {"RATE", F_PCT, 0, 127, 30, 0, 0},
        {"X", F_PCT, 0, 127, 64, 0, 0},
        {"Y", F_PCT, 0, 127, 64, 0, 0},
        {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"CUT", F_CUTOFF, 0, 127, 95, 0, 0},
        {"RES", F_PCT, 0, 127, 10, 0, 0},
    },
    .presets = VECTOR_PRESETS,
    .npresets = NELEM(VECTOR_PRESETS),
    .poly = 6,
    .note_on = vector_note_on,
    .render = vector_render,
    .knob = {P_E3, P_E4, P_E2, P_E6},
    .keep = 0x33,                /* the filter, RANDOM's target (a retrigger, a slide: no click, no jump) */
};
