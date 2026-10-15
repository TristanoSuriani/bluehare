/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* RAZOR (1.5, FOUNDRY): a stack of saws and a sharp edge (Fel-Tracker's own). VOICES saws (1 .. 7, band-limited)
 * spread SPREAD apart in tune (up to ~+-50 cents at the outside: the reese's beating, the supersaw's sheen), SUB a
 * square an octave down, NOISE on top, DRIVE into a resonant low-pass (CUT, RES, the key half tracked, ENV ->
 * FILTER) and EDGE a high-pass after it (the cut's sharpness: thin, icy, laser). 4 voices a track (7 saws each).
 * Voice state: ph[0 .. 2] and s[0 .. 3] the seven saws' phases, s[4] s[5] the filter, s[6] EDGE's, s[7] SUB's phase
 * (NOISE: a hash of the first saw's phase, no state). */
#define RZ_N 7
static const uint16_t RZ_LVL[RZ_N + 1] = {0, 32767, 23170, 18919, 16384, 14654, 13377, 12385};   /* 1 / sqrt(n) */

static void razor_note_on(track_t *t, voice_t *v)
{
    uint32_t k;
    (void)t;
    for (k = 0; k < 3u; k++)                          /* (spread phases: the stack does not flam at its start) */
        v->ph[k] = (k + 1u) * 0x2492492u * 7u;
    for (k = 0; k < 4u; k++)
        v->s[k] = (int32_t)((k + 4u) * 0x2492492u * 7u);
    v->s[7] = 0;
}

static void razor_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    os2_t *os = os2_of(t, v);                         /* (Fel-Tracker 2026-10: the shaper's 2x, dsp.c) */
    const int16_t *p = t->p;
    uint32_t nv = (uint32_t)clamp(p[P_E0], 1, RZ_N), inc[RZ_N], ph[RZ_N], i, k, sph = (uint32_t)v->s[7], sinc;
    int32_t lvl = RZ_LVL[nv], sub = p[P_E6] * 258, noise = p[P_E7] * 100, drive = 256 + p[P_E5] * 8;
    int32_t ic1 = v->s[4], ic2 = v->s[5], hp = v->s[6], ek = p[P_E4] * p[P_E4] / 4;   /* EDGE: ~10 Hz .. ~900 Hz */
    tsvf_t flt;
    for (k = 0; k < nv; k++) {                        /* the outer ones the furthest: -3 .. +3 x SPREAD / 3 */
        int32_t off = nv > 1u ? (int32_t)(2u * k) - (int32_t)(nv - 1u) : 0;
        inc[k] = cents_inc(m->pitch16, (off * p[P_E1] * 50) / (127 * (int32_t)(nv > 1u ? nv - 1u : 1u)), m->fine);
    }
    for (k = 0; k < RZ_N; k++)
        ph[k] = k < 3u ? v->ph[k] : (uint32_t)v->s[k - 3u];
    sinc = cents_inc(m->pitch16, 0, m->fine) >> 1;    /* SUB: an octave down, at the note's centre */
    tsvf_coef(&flt, (p[P_E2] << 8) + m->cutoff + (m->pitch16 - 60 * 16) * 8, p[P_E3]);
    for (i = 0; i < n; i++) {
        int32_t s = 0, y;
        for (k = 0; k < nv; k++) {
            s += osc_saw(ph[k], inc[k]);
            ph[k] += inc[k];
        }
        s = (s * lvl) >> 15;
        if (sub)
            s += (osc_pulse(sph, sinc, 0x80000000u) * sub) >> 16;
        sph += sinc;
        if (noise)
            s += ((int32_t)(int16_t)((ph[0] * 2654435761u) >> 16) * noise) >> 15;
        if (os2_off)
            s = softclip(((s >> 1) * drive) >> 8);    /* DRIVE: into the filter */
        else {                                        /* (Fel-Tracker 2026-10: at twice the rate, dsp.c os2) */
            int32_t sa, sb;
            os2_up(os, s >> 1, &sa, &sb);
            s = os2_down(os, softclip((sa * drive) >> 8), softclip((sb * drive) >> 8));
        }
        y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);
        hp += ((y - hp) * ek) >> 15;                  /* EDGE: the lows taken off after it */
        y -= ek ? hp : 0;
        out[i] += voice_amp(soft_knee(clamp(y << 2, -98304, 98304), 24000), m, i) << 1;
    }
    for (k = 0; k < RZ_N; k++) {
        if (k < 3u)
            v->ph[k] = ph[k];
        else
            v->s[k - 3u] = (int32_t)ph[k];
    }
    v->s[4] = ic1;
    v->s[5] = ic2;
    v->s[6] = hp;
    v->s[7] = (int32_t)sph;
}

/* {VOICES, SPREAD, CUT, RES, EDGE, DRIVE, SUB, NOISE} */
static const preset_t RAZOR_PRESETS[] = {
    {"ICY REESE", {2, 60, 70, 40, 10, 30, 40, 0}, {0, 0, 127, 20}, 10, 1, FX(20, 20, 10, 15)},
    {"NEURO REESE", {3, 80, 60, 60, 20, 60, 50, 0}, {0, 0, 127, 20}, 20, 1, FX(40, 10, 10, 10)},
    {"RAZOR LEAD", {7, 40, 90, 30, 30, 20, 0, 0}, {0, 40, 110, 30}, 20, 1, FX(10, 30, 35, 25)},
    {"LASER CUT", {5, 30, 110, 80, 60, 40, 0, 0}, {0, 40, 80, 20}, 40, 1, FX(10, 0, 40, 20)},
    {"SUPERSAW", {7, 70, 100, 10, 0, 10, 0, 0}, {10, 60, 120, 40}, 0, 0, FX(0, 50, 30, 50)},
    {"STEEL PAD", {7, 90, 70, 20, 20, 10, 0, 10}, {80, 90, 115, 95}, 0, 0, FX(0, 70, 25, 75)},
    {"THIN EDGE", {4, 50, 100, 50, 90, 30, 0, 0}, {0, 50, 60, 20}, 30, 1, FX(10, 10, 30, 20)},
    {"DIRTY STACK", {6, 60, 75, 50, 10, 100, 30, 20}, {0, 40, 100, 20}, 20, 1, FX(40, 10, 20, 15)},
    {"COLD BASS", {2, 25, 45, 30, 0, 40, 90, 0}, {0, 50, 90, 15}, 25, 1, FX(10, 0, 0, 5)},
    {"BLADE STAB", {7, 50, 85, 60, 30, 50, 0, 0}, {0, 35, 0, 25}, 40, 0, FX(10, 10, 45, 30)},
    {"HOOVER EDGE", {7, 110, 80, 40, 40, 60, 50, 0}, {0, 40, 110, 30}, 20, 1, FX(20, 30, 30, 30)},
    {"DARK REESE", {2, 45, 40, 50, 0, 50, 80, 0}, {0, 0, 127, 20}, 10, 1, FX(20, 0, 0, 5)},
    {"WIDE REESE", {4, 100, 55, 40, 10, 40, 60, 0}, {0, 0, 127, 20}, 10, 1, FX(10, 30, 10, 15)},
    {"SHARD", {5, 20, 120, 100, 100, 30, 0, 20}, {0, 40, 60, 20}, 30, 1, FX(10, 0, 40, 25)},
    {"FROST PAD", {7, 60, 90, 30, 50, 10, 0, 30}, {90, 90, 115, 100}, 0, 0, FX(0, 70, 25, 85)},
    {"SAW STORM", {7, 127, 110, 50, 30, 100, 30, 40}, {0, 40, 110, 30}, 20, 1, FX(40, 20, 30, 30)},
    {"MONO SAW", {1, 0, 80, 40, 10, 30, 30, 0}, {0, 40, 110, 20}, 20, 1, FX(20, 10, 30, 25)},
    {"TRANCE GATE", {7, 60, 95, 30, 20, 20, 0, 0}, {0, 30, 0, 15}, 20, 0, FX(0, 40, 40, 40)},
    {"DNB REESE", {3, 70, 50, 55, 15, 70, 60, 0}, {0, 0, 127, 20}, 15, 1, FX(30, 10, 0, 10)},
    {"ICE SHEET", {7, 80, 110, 40, 80, 10, 0, 40}, {80, 90, 120, 100}, 0, 0, FX(0, 70, 30, 90)},
    {"HARD STAB", {6, 40, 90, 70, 40, 80, 20, 0}, {0, 35, 0, 20}, 40, 0, FX(30, 10, 40, 30)},
    {"TEAR GAS", {5, 110, 75, 60, 30, 90, 0, 60}, {0, 40, 110, 30}, 20, 1, FX(40, 20, 30, 30)},
    {"SUB RAZOR", {2, 30, 35, 30, 0, 40, 127, 0}, {0, 50, 100, 15}, 20, 1, FX(10, 0, 0, 5)},
    {"GLASS LEAD", {4, 30, 120, 60, 110, 20, 0, 10}, {0, 40, 100, 30}, 30, 1, FX(0, 30, 35, 35)},
    /* (Fel-Tracker) EBM basslines: the arp's figure with the sound (core.h ARP_FIG): hold a note, it plays the line */
    {"EBM OCTAVES", {2, 40, 55, 40, 0, 50, 70, 0}, {0, 30, 60, 10}, 30, 0, FX(15, 0, 10, 10), .arp = 1},
    {"EBM ROOT", {2, 30, 50, 45, 0, 60, 80, 0}, {0, 25, 50, 8}, 35, 0, FX(20, 0, 10, 10), .arp = 2},
    {"EBM GALLOP", {3, 45, 60, 40, 10, 50, 60, 0}, {0, 25, 50, 8}, 30, 0, FX(15, 0, 15, 10), .arp = 3},
};

static const engine_t ENG_RAZOR = {
    .name = "RAZOR",
    .id = EID_RAZOR,
    .cc = 0x34,                  /* CC74 CUT, CC71 RES */
    .cats = "BBLLPPLBBKLBBLPLLKBPKLBLBBB",
    .page_title = {"STACK", "EDGE"},
    .edit = {
        {"VOICES", F_INT, 1, 7, 3, 0, 0},
        {"SPREAD", F_PCT, 0, 127, 60, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"EDGE", F_PCT, 0, 127, 10, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 20, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"NOISE", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = RAZOR_PRESETS,
    .npresets = NELEM(RAZOR_PRESETS),
    .poly = 4,
    .note_on = razor_note_on,
    .render = razor_render,
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .keep = 0x30,                /* the filter (a retrigger, a slide: no click) */
};
