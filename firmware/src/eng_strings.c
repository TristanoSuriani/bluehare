/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* STRINGS (1.5, WAREHOUSE): a string ensemble, after the Waldorf Streichfett and the string machines before it
 * (Fel-Tracker's own). Each voice is three saws, 16', 8' and 4' (the octave under the note, the note, the octave
 * over), and a square, blended by REG from contrabass through cello, viola and violin to an organ-like registration,
 * BODY the 16' weight on top; DTN detunes the 16' and the 4' against the 8' (the machines' slightly loose divider
 * tuning); a gentle low-pass at BRITE (each registration a little brighter or darker), ANIM a slow sweep of it (the
 * strings breathing), TREM a ~6 Hz tremolo. Then the part's ensemble (engine_t.post): three reads of a short line,
 * ~5 .. 12 ms, moved by two LFOs a third of a cycle apart each (a slow ~0.6 Hz and a fast ~6 Hz, SPEED scales
 * both), ENSEM the share of it against the dry voices: the shimmer the string machines are known by. The amplitude is
 * the ADSR (the presets: a slow attack, the crescendo, and a long release).
 * Voice state: ph[0] the 8', ph[1] the 16', ph[2] the 4'; s[0] s[1] the low-pass, s[2] ANIM's phase, s[3] TREM's.
 * The ensemble: the track's block (engines.c eng_slab): the line, its write place, the LFOs' phases. */
#define ST_LEN 1024u                     /* the ensemble's line: 23 ms */
typedef struct {
    int16_t line[ST_LEN];
    uint32_t w, slow, fast;
} st_ens_t;
/* the registrations REG moves between: {16', 8', 4', square, brightness} */
static const int16_t ST_REG[5][5] = {
    {110, 50, 0, 0, -30},                /* CONTRABASS */
    {60, 100, 10, 0, -15},               /* CELLO */
    {20, 110, 40, 0, 0},                 /* VIOLA */
    {0, 100, 80, 0, 15},                 /* VIOLIN */
    {50, 70, 50, 90, 10},                /* ORGAN */
};

static void strings_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[0] = v->s[1] = 0;
    v->s[2] = (int32_t)(v->age * 2654435761u);         /* (each voice its own place in ANIM's and TREM's cycles) */
    v->s[3] = (int32_t)(v->age * 40503u << 16);
}

static void strings_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t pos = p[P_E0] * 4, r0 = pos >> 7, fr = pos & 127, r1 = r0 < 4 ? r0 + 1 : 4, w[5], j;
    int32_t ic1 = v->s[0], ic2 = v->s[1], dt = p[P_E6] / 8, body = p[P_E7], trem = p[P_E4] * 180, cut;
    uint32_t i, ph8 = v->ph[0], ph16 = v->ph[1], ph4 = v->ph[2], ap = (uint32_t)v->s[2], tp = (uint32_t)v->s[3];
    uint32_t inc8 = m->inc, inc16 = cents_inc(m->pitch16 - 192, -dt, m->fine), inc4 = cents_inc(m->pitch16 + 192, dt, m->fine);
    tsvf_t f;
    r0 = r0 > 4 ? 4 : r0;
    for (j = 0; j < 5; j++)                           /* REG: between two registrations */
        w[j] = (ST_REG[r0][j] * (128 - fr) + ST_REG[r1][j] * fr) >> 7;
    w[0] = clamp(w[0] + body / 2, 0, 127);            /* BODY */
    ap += (uint32_t)n * 2400u;                        /* ANIM: ~0.08 Hz, once a block */
    cut = (p[P_E1] << 8) + (w[4] << 8) + ((sine_i(ap) * p[P_E5]) >> 9) + m->cutoff;
    tsvf_coef(&f, clamp(cut, 8 << 8, 127 << 8), 10);
    for (i = 0; i < n; i++) {
        int32_t x = (osc_saw(ph16, inc16) * w[0] + osc_saw(ph8, inc8) * w[1] + osc_saw(ph4, inc4) * w[2] +
                     osc_pulse(ph8, inc8, 0x80000000u) * w[3]) >> 8;
        x = tsvf_lp(&f, x, &ic1, &ic2);
        if (trem) {                                   /* TREM: ~6 Hz */
            tp += 584000u;
            x = (x * (32767 - ((((sine_i(tp) + 32768) >> 1) * trem) >> 15))) >> 15;
        }
        out[i] += voice_amp(x, m, i);
        ph8 += inc8;
        ph16 += inc16;
        ph4 += inc4;
    }
    v->ph[0] = ph8;
    v->ph[1] = ph16;
    v->ph[2] = ph4;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = (int32_t)ap;
    v->s[3] = (int32_t)tp;
}

/* the ensemble on the part's voices (engine_t.post) */
static void strings_post(track_t *t, int32_t *out, uint32_t n)
{
    st_ens_t *e = eng_slab(t, SLAB_STRINGS, sizeof(st_ens_t));
    int32_t mix = t->p[P_E2] * 258, sp = 16 + t->p[P_E3];
    uint32_t i, j, w;
    if (!e || !mix)
        return;
    w = e->w;
    for (i = 0; i < n; i++) {
        int32_t y = 0, x = out[i];
        e->line[w & (ST_LEN - 1u)] = (int16_t)clamp(x >> 1, -32768, 32767);
        e->slow += (uint32_t)sp * 3500u;              /* ~0.6 Hz at SPEED 64 */
        e->fast += (uint32_t)sp * 35000u;             /* ~6 Hz */
        for (j = 0; j < 3u; j++) {                    /* three reads, a third of a cycle apart */
            uint32_t o = j * 0x55555555u;
            int32_t d = (360 << 8) + ((sine_i(e->slow + o) + 32768) * 3 >> 4) + ((sine_i(e->fast + o) + 32768) >> 6);
            uint32_t di = (uint32_t)d >> 8;
            int32_t fr = d & 255, a = e->line[(w - di) & (ST_LEN - 1u)], b = e->line[(w - di - 1u) & (ST_LEN - 1u)];
            y += a + (((b - a) * fr) >> 8);
        }
        y = (y << 1) / 3;
        out[i] = x + mulq15(y - x, mix);
        w++;
    }
    e->w = w;
}

/* {REG, BRITE, ENSEM, SPEED, TREM, ANIM, DTN, BODY}; REG 0 contrabass .. 32 cello .. 64 viola .. 96 violin .. 127 organ */
static const preset_t STRINGS_PRESETS[] = {
    {"STRINGFETT", {64, 70, 100, 64, 0, 30, 40, 0}, {60, 0, 127, 80}, 0, 0, FX(0, 20, 15, 60)},
    {"VIOLINS", {96, 80, 90, 64, 0, 20, 30, 0}, {50, 0, 127, 70}, 0, 0, FX(0, 20, 15, 60)},
    {"CELLOS", {32, 60, 90, 56, 0, 20, 30, 20}, {50, 0, 127, 70}, 0, 0, FX(0, 20, 10, 50)},
    {"CONTRABASS", {0, 50, 70, 50, 0, 10, 20, 40}, {30, 0, 127, 60}, 0, 1, FX(0, 0, 5, 30)},
    {"FULL SECTION", {60, 75, 110, 64, 0, 30, 50, 30}, {70, 0, 127, 90}, 0, 0, FX(0, 20, 20, 70)},
    {"SOLINA", {80, 72, 127, 70, 0, 20, 40, 10}, {40, 0, 127, 80}, 0, 0, FX(0, 10, 15, 50)},
    {"STRING ORGAN", {127, 70, 90, 64, 0, 10, 20, 20}, {10, 0, 127, 40}, 0, 0, FX(0, 20, 15, 40)},
    {"SLOW SWELL", {64, 65, 110, 50, 0, 40, 40, 10}, {110, 0, 127, 110}, 0, 0, FX(0, 20, 20, 80)},
    {"BRIGHT ENS", {100, 100, 110, 70, 0, 20, 40, 0}, {40, 0, 127, 70}, 0, 0, FX(0, 20, 15, 60)},
    {"DARK ENS", {40, 45, 110, 56, 0, 30, 40, 30}, {60, 0, 127, 90}, 0, 0, FX(0, 20, 15, 65)},
    {"TREMOLO STR", {80, 75, 90, 64, 70, 10, 30, 0}, {40, 0, 127, 70}, 0, 0, FX(0, 10, 15, 55)},
    {"SHIMMER", {96, 90, 127, 100, 0, 40, 50, 0}, {60, 0, 127, 90}, 0, 0, FX(0, 30, 25, 75)},
    {"WARM PAD", {48, 55, 100, 50, 0, 50, 30, 20}, {90, 0, 127, 100}, 0, 0, FX(0, 30, 20, 75)},
    {"RAVE STRINGS", {90, 90, 110, 80, 0, 10, 60, 10}, {10, 0, 127, 50}, 0, 0, FX(0, 20, 25, 50)},
    {"STAB STRINGS", {80, 85, 90, 70, 0, 0, 40, 10}, {0, 60, 40, 40}, 0, 0, FX(0, 10, 30, 45)},
    {"PIZZICATO", {96, 80, 50, 64, 0, 0, 20, 0}, {0, 40, 0, 30}, 0, 0, FX(0, 0, 20, 40)},
    {"HOUSE STRINGS", {80, 85, 100, 64, 0, 20, 40, 10}, {20, 0, 127, 50}, 0, 0, FX(0, 20, 20, 55)},
    {"TRANCE STR", {100, 95, 110, 80, 0, 30, 60, 0}, {30, 0, 127, 60}, 0, 0, FX(0, 20, 30, 65)},
    {"CINEMA", {50, 60, 110, 50, 0, 40, 40, 30}, {100, 0, 127, 120}, 0, 0, FX(0, 20, 20, 85)},
    {"GHOST STR", {110, 50, 127, 40, 0, 60, 60, 0}, {110, 0, 127, 120}, 0, 0, FX(0, 30, 30, 90)},
    {"DISCO STR", {96, 90, 100, 70, 0, 10, 40, 0}, {15, 0, 127, 45}, 0, 0, FX(0, 10, 20, 50)},
    {"LOW DRONE", {10, 40, 110, 40, 0, 50, 40, 60}, {80, 0, 127, 100}, 0, 0, FX(0, 20, 15, 70)},
    {"DRY STRINGS", {64, 70, 0, 64, 0, 0, 30, 0}, {40, 0, 127, 60}, 0, 0, FX(0, 0, 10, 30)},
    {"ORGAN ENS", {127, 80, 110, 64, 0, 20, 30, 0}, {10, 0, 127, 40}, 0, 0, FX(0, 30, 15, 45)},
};

static const engine_t ENG_STRINGS = {
    .name = "STRINGS",
    .id = EID_STRINGS,
    .cc = 0x23,                  /* CC74 BRITE, CC71 ENSEM */
    .cats = "PPPBPPKPPPPPPPUUPPPPPPPK",
    .page_title = {"SECTION", "ENSEMBLE"},
    .edit = {
        {"REG", F_PCT, 0, 127, 64, 0, 0},
        {"BRITE", F_PCT, 0, 127, 70, 0, 0},
        {"ENSEM", F_PCT, 0, 127, 100, 0, 0},
        {"SPEED", F_PCT, 0, 127, 64, 0, 0},
        {"TREM", F_PCT, 0, 127, 0, 0, 0},
        {"ANIM", F_PCT, 0, 127, 30, 0, 0},
        {"DTN", F_PCT, 0, 127, 40, 0, 0},
        {"BODY", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = STRINGS_PRESETS,
    .npresets = NELEM(STRINGS_PRESETS),
    .note_on = strings_note_on,
    .render = strings_render,
    .post = strings_post,
    .knob = {P_E0, P_E1, P_E2, P_ATK},
};
