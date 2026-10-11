/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* BYTE (1.5): bytebeat. A counter t runs from 0 at every note; a formula of t (shifts, products, AND / OR / XOR,
 * no divides) gives a byte, held until t moves on: the 8-bit rasp, chirps and self-similar melodies of one-line
 * C programs. Fel-Tracker's own formulas, after the form's well-known ones.
 *
 * t runs at 8 kHz on C4 and follows the key (an octave up: twice as fast), RATE shifts that in semitones: a formula
 * plays its pattern from the start at every note, transposed. A B C are the formula's constants: A 1..16 (a
 * multiplier: most formulas' pitch), B 0..7 (a shift: how fast the pattern moves), C 0..127 (a mask or a pick).
 * LOOP restarts t every 1K .. 256K steps (OFF: it runs on), so a pattern can repeat while the key is held. The byte
 * minus 128 (DC blocked: AND formulas sit low), BITS of it (8: all), through TONE (a one-pole low-pass) and the
 * ADSR. Cheap: the formula runs once per step of t, not per sample.
 * Voice state: ph[0] t, ph[1] its fraction (Q16), s[0] the low-pass, s[1] / s[2] the DC blocker's last in / out,
 * s[3] the held byte (centred, << 7). */
static const char *const N_BYTE_FORM[] = {"SAW", "PWM", "SIERP", "XOR", "OR", "CHIRP", "42", "MELO", "OCTS", "GLTCH",
                                         "NOISE", "MASK", 0};
static const char *const N_BYTE_LOOP[] = {"OFF", "1K", "2K", "4K", "8K", "16K", "32K", "64K", "128K", "256K", 0};
enum { BF_SAW, BF_PWM, BF_SIERP, BF_XOR, BF_OR, BF_CHIRP, BF_42, BF_MELO, BF_OCTS, BF_GLTCH, BF_NOISE, BF_MASK,
       BF_COUNT };

/* MELO's melodies: 8 steps of a nibble each (a multiple of t: harmonics 1..15, 0 a rest), lowest nibble first; C / 16
 * picks one */
static const uint32_t BYTE_MEL[8] = {
    0x86548654u,                 /* 4 5 6 8, twice: a major arpeggio */
    0xA8658653u,                 /* 3 5 6 8 5 6 8 10 */
    0x46596A58u,                 /* a minor-ish turn */
    0x30604080u,                 /* 8 . 4 . 6 . 3 .: sparse */
    0xCC88AA66u,                 /* pairs climbing */
    0x9A8B7C6Du,                 /* zigzag down */
    0x2468ACE2u,                 /* the even harmonics up */
    0xF5A3C8E4u,                 /* wide leaps */
};

/* the byte at step t (only the low 8 bits count) */
static inline uint32_t byte_formula(uint32_t f, uint32_t t, uint32_t a, uint32_t b, uint32_t c)
{
    switch (f) {
    case BF_PWM:   return ((t * a) & 255u) < c * 2u ? 255u : 0u;
    case BF_SIERP: return (t * a) & (t >> (4u + b));
    case BF_XOR:   return (t * a) ^ (t >> (3u + b));
    case BF_OR:    return (t * a) | (t >> (4u + b)) | (t >> (7u + b));
    case BF_CHIRP: return t * (((t >> (9u + b)) | (t >> 8)) & c & (t >> 4)) * a;
    case BF_42:    return t * a * (c & (t >> (10u + b)));
    case BF_MELO:  return t * a * ((BYTE_MEL[c >> 4] >> (((t >> (11u + b)) & 7u) * 4u)) & 15u);
    case BF_OCTS:  return (t * a) >> ((t >> (10u + b)) & 3u);
    case BF_GLTCH: return ((t * a) >> ((t >> (11u + b)) & 3u)) ^ (t >> (5u + b));
    case BF_NOISE: return (t * t * a) >> (4u + b);
    case BF_MASK:  return (t * a) & (c * 2u + 1u);
    default:       return t * a;
    }
}

static void byte_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0;                                     /* the pattern starts again */
    v->ph[1] = 0;
    v->s[0] = v->s[1] = v->s[2] = 0;
    v->s[3] = 0;
}

static void byte_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t f = (uint32_t)p[P_E0] % BF_COUNT, a = 1u + ((uint32_t)p[P_E1] >> 3), b = (uint32_t)p[P_E2] >> 4;
    uint32_t c = (uint32_t)p[P_E3] & 127u, loop = (uint32_t)p[P_E6], i;
    uint32_t inc = p[P_E4] ? pitch_inc((uint32_t)clamp(m->pitch16 + p[P_E4] * 16, 0, 2047)) : m->inc;
    uint32_t tinc = ((inc >> 12) * 7828u) >> 12;      /* steps of t a sample, Q16: 8 kHz at C4 (8000 / 261.63 Hz) */
    uint32_t mask = loop ? (1024u << (loop - 1u)) - 1u : 0xFFFFFFFFu;
    uint32_t tt = v->ph[0], fr = v->ph[1], sh = 8u - (uint32_t)clamp(p[P_E7], 1, 8);
    int32_t lpk = clamp(2000 + p[P_E5] * 250, 2000, 32767);
    int32_t lp = v->s[0], x1 = v->s[1], y1 = v->s[2], held = v->s[3];
    if (loop > 9u)
        mask = 0xFFFFFFFFu;
    for (i = 0; i < n; i++) {
        int32_t x, y;
        fr += tinc;
        if (fr >= 65536u) {                           /* t moves on (one step or several): a new byte */
            tt = (tt + (fr >> 16)) & mask;
            fr &= 65535u;
            held = ((int32_t)(((byte_formula(f, tt, a, b, c) & 255u) >> sh) << sh) - 128) << 7;
        }
        x = held;
        y = x - x1 + y1 - (y1 >> 9);                  /* the DC blocker: ~14 Hz */
        x1 = x;
        y1 = y;
        lp += mulq15(y - lp, lpk);
        out[i] += voice_amp(lp, m, i) << 1;           /* (half scale held: room for the blocker's overshoot) */
    }
    v->ph[0] = tt;
    v->ph[1] = fr;
    v->s[0] = lp;
    v->s[1] = x1;
    v->s[2] = y1;
    v->s[3] = held;
}

static const preset_t BYTE_PRESETS[] = {
    {"BYTE ARP", {BF_MELO, 0, 0, 16, 0, 90, 0, 8}, {0, 70, 100, 30}, 0, 0, FX(0, 0, 45, 25)},
    {"SIERPINSKI", {BF_SIERP, 0, 32, 64, 0, 100, 0, 8}, {0, 90, 110, 40}, 0, 0, FX(0, 20, 40, 30)},
    {"XOR BASS", {BF_XOR, 8, 64, 64, -24, 70, 0, 8}, {0, 60, 90, 20}, 0, 1, FX(10, 0, 10, 5)},
    {"42 MELODY", {BF_42, 0, 0, 42, 0, 110, 0, 8}, {0, 80, 110, 40}, 0, 0, FX(0, 0, 35, 25)},
    {"GLITCH", {BF_GLTCH, 16, 16, 64, 0, 127, 3, 6}, {0, 70, 80, 25}, 0, 0, FX(0, 0, 40, 20)},
    {"BIT NOISE", {BF_NOISE, 24, 48, 64, 0, 80, 0, 5}, {0, 50, 0, 30}, 0, 0, FX(0, 0, 30, 30)},
};

static const engine_t ENG_BYTE = {
    .name = "BYTE",
    .id = EID_BYTE,
    .cc = 0x60,
    .cats = "LFBLFF",
    .page_title = {"FORM", "TONE"},
    .edit = {
        {"FORM", F_ENUM, 0, BF_COUNT - 1, 0, N_BYTE_FORM, 0},
        {"A", F_INT, 0, 127, 0, 0, 0},
        {"B", F_INT, 0, 127, 0, 0, 0},
        {"C", F_INT, 0, 127, 64, 0, 0},
        {"RATE", F_SEMI, -36, 24, 0, 0, 0},
        {"TONE", F_PCT, 0, 127, 127, 0, 0},
        {"LOOP", F_ENUM, 0, 9, 0, N_BYTE_LOOP, 0},
        {"BITS", F_INT, 1, 8, 8, 0, 0},
    },
    .presets = BYTE_PRESETS,
    .npresets = NELEM(BYTE_PRESETS),
    .note_on = byte_note_on,
    .render = byte_render,
    .knob = {P_E0, P_E1, P_E2, P_E3},
};
