/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-19) ONYX: phrases of the electronic music's own vocabulary (docs/SpeechSynthesisVocabulary.md), spoken by
 * Kokoro TTS's am_onyx voice (Kokoro-82M, Apache 2.0) and kept as the speech chips kept speech: LPC frames, 25 ms each
 * (tools/gen_onyx_render.py, tools/gen_onyx_lpc.py -> onyx_lpc.h: ten reflection coefficients, the loudness, the pitch;
 * ~200 bytes a second, 162 phrases in 34 KB). Here they are spoken again through SPEECH's lattice at 8820 Hz: a pulse
 * a period (voiced) or noise (unvoiced) through the frame's filter, the coefficients, the loudness and the pitch
 * gliding over the frame, de-emphasised: Onyx's timing and intonation, a machine's throat. Next to SPEECH (words
 * from phonemes): ONYX speaks whole phrases, a real voice's.
 * WORD picks the phrase of BANK (the vocabulary's subsections: DATA SIGNAL GLITCH COMMAND MACHINE IMPACT HYPNOTIC DJ
 * EUPHORIA SYSTEM RHYTHM COSMOS RETRO TOKENS SING). PLAY, how the keys play it and how it ends: TONE the key sets the
 * pitch (C3 110 Hz, the voice's contour round it); WORDS the keys are the phrases (WORD + the key from C2, round the
 * bank), at the voice's own pitch; each STOP, HOLD (the last sounding frame sustained while the key is held: SING's
 * vowels), LOOP (again while held). RATE: how fast (half .. double); INTN: how much of the voice's intonation (0:
 * monotone on the key, 64: its own, 127: exaggerated); BUZZ: the voice (127) to a whisper (0); FORM: the filter's
 * clock, 0.7 .. 1.4 times (the formants up or down, the pitch kept); VOICE: ONYX, HEART, GEORGE, NICOLE (Kokoro's:
 * every phrase in each). (2026-10-19: MODE and END one knob, PLAY, so VOICE has the eighth.)
 * The data per voice does not fit voice_t: ox_v[track][voice] (OX_POLY voices). Output: the filter's sample held and
 * lightly smoothed to 44.1 kHz, as SPEECH's */
#include "onyx_lpc.h"
#include "fastmath.h"                          /* (the float voice) */
static const char *const N_OX_PLAY[] = {"TONE", "TONE HOLD", "TONE LOOP", "WORDS", "WORD HOLD", "WORD LOOP", 0};
static const char *const N_OX_END[] = {"STOP", "HOLD", "LOOP", 0};
#define OX_POLY 4
#define OX_C3 110.0f             /* TONE's C3 (Hz): the voice's contour round it */
typedef char ox_fits[ONYX_P == 10 && ONYX_NBANK <= 16 && ONYX_NVOICE <= 8 ? 1 : -1];

typedef struct {
    uint16_t phrase, frame, nf, last;   /* the phrase, the frames read, its frames, its last sounding one (HOLD's) */
    uint8_t done, voiced, vo;    /* (vo: the voice, ONYX_VOICE_NAMES) */
    uint32_t bit;                /* the next frame's first bit */
    float k0[ONYX_P], k1[ONYX_P], g[ONYX_P];   /* the frame's coefficients: from, to; the lattice's delayed values */
    float e0, e1, p0, p1;        /* the loudness (from, to), the period (filter samples; 0: unvoiced) */
    float pos;                   /* filter samples into the frame */
    uint32_t clk;                /* the filter's clock, Q16 of a filter sample */
    float pph;                   /* the voice's phase (0 .. 1) */
    uint32_t lfsr;
    float y, de, lp;             /* the filter's sample, its de-emphasis, held and smoothed */
    float hx, hy;                /* (the DC blocker's last in and out: a held vowel's pulses carry DC) */
} ox_voice_t;
static ox_voice_t ox_v[NTRK][OX_POLY];

static ox_voice_t *ox_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < OX_POLY ? &ox_v[t - trk][i] : 0;
}
/* WORD (E0) of BANK (E1); words: the key from C2 adds to it (round the bank) */
static uint32_t ox_phrase_of(const track_t *t, const voice_t *v, int words)
{
    uint32_t b = (uint32_t)t->p[P_E1] % ONYX_NBANK;
    int32_t n = ONYX_BANK_N[b], w = clamp(t->p[P_E0], 0, n - 1);
    if (words)
        w = ((w + (int32_t)v->note - 36) % n + n) % n;
    return ONYX_BANK_FIRST[b] + (uint32_t)w;
}
static uint32_t ox_bits(ox_voice_t *s, uint32_t n)     /* n bits from the stream, MSB first */
{
    uint32_t v = 0;
    while (n--) {
        v = v << 1 | ((ONYX_BITS[s->bit >> 3] >> (7u - (s->bit & 7u))) & 1u);
        s->bit++;
    }
    return v;
}
/* the next frame: its filter, loudness and pitch become the glide's "to" (the current ones its "from") */
static void ox_frame(const track_t *t, ox_voice_t *s)
{
    static const uint8_t KB[ONYX_P] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};
    uint32_t e, j;
    (void)t;
    for (j = 0; j < ONYX_P; j++)
        s->k0[j] = s->k1[j];
    s->e0 = s->e1;
    s->p0 = s->p1;
    e = ox_bits(s, 4);
    if (!e) {                                           /* silence: the filter kept, the voice off */
        s->e1 = 0;
        s->voiced = 0;
    } else {
        uint32_t rep = ox_bits(s, 1), per = ox_bits(s, 6);
        s->e1 = (float)ONYX_E[e];
        s->voiced = per != 0;
        s->p1 = per ? (float)ONYX_PER[per] : s->p1;
        if (!rep)
            for (j = 0; j < ONYX_P; j++) {
                uint32_t q;
                if (j >= 4u && !per) {
                    s->k1[j] = 0;
                    continue;
                }
                q = ox_bits(s, KB[j]);
                s->k1[j] = (float)(KB[j] == 5u ? ONYX_K5[q] : KB[j] == 4u ? ONYX_K4[q] : ONYX_K3[q]) * (1.0f / 32768.0f);
            }
    }
    s->frame++;
}

/* the phrase's last sounding frame (HOLD sustains it): its frames walked through once, bits skipped */
static uint16_t ox_last(ox_voice_t *s)
{
    static const uint8_t KB[ONYX_P] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};
    uint32_t f, keep = s->bit, last = 0, j;
    for (f = 1; f <= s->nf; f++) {
        uint32_t e = ox_bits(s, 4), rep, per;
        if (!e)
            continue;
        last = f;
        rep = ox_bits(s, 1);
        per = ox_bits(s, 6);
        if (!rep)
            for (j = 0; j < (per ? ONYX_P : 4u); j++)
                (void)ox_bits(s, KB[j]);
    }
    s->bit = keep;
    return (uint16_t)last;
}

/* a phrase of a voice from its start (ONYX's and VOCODER's note-on) */
static void ox_start(const track_t *t, voice_t *v, ox_voice_t *s, uint32_t phrase, uint32_t vo)
{
    memset(s, 0, sizeof *s);
    s->vo = (uint8_t)(vo % ONYX_NVOICE);
    s->phrase = (uint16_t)phrase;
    s->nf = ONYX_FRAMES[s->vo][s->phrase];
    s->bit = ONYX_START[s->vo][s->phrase] * 8u;
    s->last = ox_last(s);
    s->lfsr = 0x2468ACEu + (uint32_t)(v - t->v) * 7919u;
    s->p1 = ONYX_REF[s->vo];
    ox_frame(t, s);                                     /* (the first frame: from silence) */
    s->e0 = 0;
}

static void onyx_note_on(track_t *t, voice_t *v)
{
    ox_voice_t *s = ox_of(t, v);
    if (s)
        ox_start(t, v, s, ox_phrase_of(t, v, t->p[P_E2] >= 3), (uint32_t)t->p[P_E7]);
}

/* a filter sample's frame: the next one when this is over (END: 0 STOP, 1 HOLD, 2 LOOP; gate the key held), and where
 * in it (-> 0 .. 1, the glide's); flen a frame's filter samples (RATE) */
static float ox_advance(const track_t *t, ox_voice_t *s, float flen, uint32_t end, int gate)
{
    float frac;
    if (s->pos >= flen && !s->done) {
        s->pos -= flen;
        if (end == 1u && gate && s->frame >= s->last)
            s->pos = 0;                                 /* HOLD: the last sounding frame while the key is held */
        else if (s->frame < s->nf)
            ox_frame(t, s);
        else if (end == 2u && gate) {                   /* LOOP: again while held */
            s->frame = 0;
            s->bit = ONYX_START[s->vo][s->phrase] * 8u;
            ox_frame(t, s);
        } else {
            s->done = 1;
            s->e0 = s->e1 = 0;
        }
    }
    frac = fm_clampf(s->pos / flen, 0.0f, 1.0f);
    s->pos += 1.0f;
    return frac;
}

/* the lattice, top stage down (SPEECH's), the frame's coefficients glided to frac: x through it */
static float ox_lattice(ox_voice_t *s, float frac, float x)
{
    float kf[ONYX_P], f = x;
    uint32_t j;
    for (j = 0; j < ONYX_P; j++)
        kf[j] = s->k0[j] + (s->k1[j] - s->k0[j]) * frac;
    for (j = ONYX_P; j-- > 0;) {
        f -= kf[j] * s->g[j];
        if (j + 1u < ONYX_P)
            s->g[j + 1] = fm_clampf(s->g[j] + kf[j] * f, -1e6f, 1e6f);
    }
    return s->g[0] = fm_clampf(f, -1e6f, 1e6f);
}

/* the pitch (cycles a filter sample): the key's (pitch16; C3 OX_C3), or words: the voice's own; times the voice's
 * contour (its period per against its own) to the power a (INTN: 0 monotone, 1 its own); r FORM's clock */
static float ox_inc(const ox_voice_t *s, int32_t pitch16, int words, float a, float per, float r)
{
    float ref = ONYX_REF[s->vo], base = words ? (float)ONYX_FS / ref : OX_C3;
    return base * fm_exp2f((float)(pitch16 - 48 * 16) * (1.0f / 192.0f)) * fm_exp2f(a * fm_log2f(ref / fm_maxf(per, 8.0f))) /
           ((float)ONYX_FS * r);
}

/* the output's DC blocked (a pole at 0.995: ~7 Hz at 8820), clamped */
static float ox_dc(ox_voice_t *s, float x)
{
    s->hy = x - s->hx + 0.995f * s->hy;
    s->hx = x;
    return fm_clampf(s->hy, -32767.0f, 32767.0f);
}

/* white noise, -1 .. 1 */
static float ox_noise(ox_voice_t *s)
{
    s->lfsr ^= s->lfsr << 13;
    s->lfsr ^= s->lfsr >> 17;
    s->lfsr ^= s->lfsr << 5;
    return (float)(int16_t)s->lfsr * (1.0f / 32768.0f);
}

static void onyx_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    ox_voice_t *s = ox_of(t, v);
    uint32_t i, clk, end = (uint32_t)t->p[P_E2] % 3u;
    int words = t->p[P_E2] >= 3;
    float flen, frac, e, inc, buzz = (float)t->p[P_E5] / 127.0f, a = (float)t->p[P_E4] / 64.0f, r;
    int32_t pitch16;
    if (!s)
        return;
    r = fm_exp2f((float)t->p[P_E6] * (1.5f / 192.0f) * 3.0f);   /* FORM: the filter's clock, 0.7 .. 1.4 */
    clk = (uint32_t)(13107.0f * r);                    /* (8820 / 44100, Q16) */
    flen = (float)ONYX_N / fm_exp2f((float)(t->p[P_E3] - 64) / 64.0f);   /* RATE: a frame's filter samples */
    pitch16 = words ? 48 * 16 + (m->pitch16 - (int32_t)v->note * 16) : m->pitch16;
    for (i = 0; i < n; i++) {
        s->clk += clk;
        if (s->clk >= 65536u) {                         /* a filter sample */
            float x = 0;
            s->clk -= 65536u;
            frac = ox_advance(t, s, flen, end, v->gate);
            e = s->e0 + (s->e1 - s->e0) * frac;
            inc = ox_inc(s, pitch16, words, a, s->p0 + (s->p1 - s->p0) * frac, r);
            if (s->voiced) {
                s->pph += inc;
                if (s->pph >= 1.0f) {                   /* a period: a pulse (its energy the frame's, a sample) */
                    s->pph -= 1.0f;
                    x = e * fm_sqrtf(1.0f / fm_maxf(inc, 1e-3f)) * buzz;
                }
            }
            x += ox_noise(s) * (32768.0f / 18918.0f) * e * (s->voiced ? 1.0f - buzz : 1.0f);
            s->de = ox_lattice(s, frac, x) + 0.9375f * s->de;   /* (the analysis' pre-emphasis undone) */
            s->y = ox_dc(s, s->de * 0.35f);
        }
        s->lp += (s->y - s->lp) * 0.5f;
        out[i] += voice_amp((int32_t)s->lp, m, i) << 1;
    }
}

static int32_t onyx_keys(const track_t *t, uint32_t k)
{
    return t->p[P_E2] >= 3 ? clamp(29 + 12 * song.octave + (int32_t)k, 0, 127) : -1;
}
#define OX_WD(i) {"WORD", F_ENUM, 0, (int16_t)(NELEM(ONYX_NAMES_##i) - 2), 0, ONYX_NAMES_##i, 0}
static const param_desc_t OX_WORD_DESC[ONYX_NBANK] = {OX_WD(0), OX_WD(1), OX_WD(2), OX_WD(3), OX_WD(4), OX_WD(5), OX_WD(6),
                                                     OX_WD(7), OX_WD(8), OX_WD(9), OX_WD(10), OX_WD(11), OX_WD(12),
                                                     OX_WD(13), OX_WD(14)};
static const param_desc_t *onyx_desc(const track_t *t, uint32_t k)
{
    return k == 0u ? &OX_WORD_DESC[(uint32_t)t->p[P_E1] % ONYX_NBANK] : 0;
}

/* {WORD, BANK, PLAY, RATE, INTN, BUZZ, FORM, VOICE}; BANK 0 DATA 1 SIGNAL 2 GLITCH 3 COMMAND 4 MACHINE 5 IMPACT
 * 6 HYPNOTIC 7 DJ 8 EUPHORIA 9 SYSTEM 10 RHYTHM 11 COSMOS 12 RETRO 13 TOKENS 14 SING; PLAY 0 TONE 1 TONE HOLD 2 TONE
 * LOOP 3 WORDS 4 WORD HOLD 5 WORD LOOP; VOICE 0 ONYX 1 HEART 2 GEORGE 3 NICOLE */
static const preset_t ONYX_PRESETS[] = {
    {"OBEY", {0, 3, 0, 64, 64, 127, 0, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 25, 25)},
    {"COMMANDS", {0, 3, 3, 64, 64, 127, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 25, 25)},
    {"IMPACT", {0, 5, 3, 60, 80, 127, -8, 0}, {0, 0, 127, 30}, 0, 0, FX(40, 0, 20, 30)},
    {"DATA KEYS", {0, 0, 3, 70, 40, 120, 8, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 35, 30)},
    {"HYPNOTIC", {0, 6, 3, 56, 50, 110, -12, 0}, {0, 0, 127, 60}, 0, 0, FX(0, 30, 40, 50)},
    {"GLITCH", {0, 2, 3, 90, 64, 127, 16, 0}, {0, 0, 127, 10}, 0, 0, FX(20, 0, 30, 15)},
    {"SYSTEM", {0, 9, 3, 64, 70, 127, 0, 0}, {0, 0, 127, 30}, 0, 0, FX(20, 0, 40, 35)},
    {"ROBOT", {0, 3, 3, 64, 0, 127, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(30, 0, 20, 20)},
    /* (2026-10-19: the other voices) HEART's euphoria, GEORGE the selecta, NICOLE whispering; HEART singing AH */
    {"EUPHORIA", {0, 8, 3, 60, 64, 127, 0, 1}, {0, 0, 127, 60}, 0, 0, FX(0, 30, 45, 60)},
    {"SELECTA", {0, 9, 3, 64, 64, 127, 0, 2}, {0, 0, 127, 30}, 0, 0, FX(10, 0, 50, 35)},
    {"WHISPER", {0, 6, 3, 56, 64, 30, 0, 3}, {0, 0, 127, 60}, 0, 0, FX(0, 20, 50, 60)},
    {"SING AH", {0, 14, 1, 64, 20, 127, 0, 1}, {20, 0, 127, 50}, 0, 0, FX(0, 40, 30, 55)},
};

static const engine_t ENG_ONYX = {
    .name = "ONYX",
    .id = EID_ONYX,
    .cc = 0x75,                  /* CC74 FORM, CC71 INTN */
    .cats = "FFFFFFFFFFFF",
    .page_title = {"PHRASE", "VOICE"},
    .edit = {
        {"WORD", F_ENUM, 0, (int16_t)(NELEM(ONYX_NAMES_3) - 2), 0, ONYX_NAMES_3, 0},
        {"BANK", F_ENUM, 0, ONYX_NBANK - 1, 3, ONYX_BANK_NAMES, 0},
        {"PLAY", F_ENUM, 0, 5, 0, N_OX_PLAY, 0},
        {"RATE", F_PCT, 0, 127, 64, 0, 0},
        {"INTN", F_PCT, 0, 127, 64, 0, 0},
        {"BUZZ", F_PCT, 0, 127, 127, 0, 0},
        {"FORM", F_INT, -64, 63, 0, 0, 0},
        {"VOICE", F_ENUM, 0, ONYX_NVOICE - 1, 0, ONYX_VOICE_NAMES, 0},
    },
    .presets = ONYX_PRESETS,
    .npresets = NELEM(ONYX_PRESETS),
    .poly = OX_POLY,
    .note_on = onyx_note_on,
    .render = onyx_render,
    .desc = onyx_desc,
    .keys = onyx_keys,
    .knob = {P_E0, P_E3, P_E6, P_E4},
};
