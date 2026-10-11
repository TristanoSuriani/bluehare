/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* SPEECH (1.5): words, the way the LPC speech chips of the late 70s spoke them (Fel-Tracker's own: no chip's data or code).
 *
 * A buzz (a glottal pulse each period) or a hiss (noise) through a 10-pole lattice filter running at 8820 Hz; the
 * filter's reflection coefficients glide from sound to sound, word by word. There are no recordings: the words are
 * phonemes (the text files in tools/speech, English, German, Dutch), turned at build time into sounds (each one's coefficients and
 * gains) and segments (the sound, its length, how long the glide to it takes, the pitch: stress and the fall across the
 * word) by tools/gen_speech.py.
 *
 * WORD picks the word of BANK (EN# numbers, EN machine words, DE, NL, PHON single phonemes). MODE TONE: the keys play
 * WORD at their pitch; WORDS: the keys are the words (WORD + the key from C2, the pitch fixed at C3, as DRUM maps its
 * lanes). RATE: how fast it speaks (half .. double); INTN: how much the pitch follows the word's stress and fall (0:
 * monotone); BUZZ: the voice (127) to a whisper (0: the voiced sounds hiss); FORM: the filter's clock, 0.7 .. 1.4 times
 * (every formant shifts with it, as a bent chip's clock); END: when the word is over, STOP (silence), HOLD (its last
 * voiced sound sustains while the key is held, the rest follows on release) or LOOP (it starts again while held).
 * The data per voice does not fit voice_t: sp_v[track][voice] (SP_POLY voices). Output: the filter's sample held and
 * lightly smoothed to 44.1 kHz (the grit of the low rate stays). */
#include "felucca_speech.h"
static const char *const N_SP_BANK[] = {SP_BANK_NAMES_INIT, 0};
static const char *const N_SP_MODE[] = {"TONE", "WORDS", 0};
static const char *const N_SP_END[] = {"STOP", "HOLD", "LOOP", 0};
#define SP_POLY 4
#define SP_CLK 13107u            /* the filter's samples per output sample, Q16: 8820 / 44100 */
#define SP_MS 1486u              /* an output sample in ms, Q16 (1000 / 44100) */
#define SP_NCHIRP ((uint32_t)NELEM(SP_CHIRP))
typedef char sp_words_fit[SP_WORDS == 32 && SP_NBANK == 5 ? 1 : -1];   /* (WORD 0..31; SP_WORD_DESC below) */

typedef struct {
    uint16_t word;               /* SP_WORD index */
    uint8_t seg, done;           /* the segment (of the word), 1: the word is over */
    uint32_t pos;                /* time in the segment, ms Q16 */
    int16_t k0[SP_P], k1[SP_P], k[SP_P];   /* the glide's coefficients: from, to, now (Q15) */
    int32_t g[SP_P];             /* the lattice's delayed values */
    int32_t va0, va1, na0, na1;  /* the voice's and the noise's amplitudes (gain included, Q16): from, to */
    int32_t pt0, pt1;            /* the pitch offsets (quarter semitones): from, to */
    uint32_t clk;                /* the filter's clock, Q16 of a filter sample */
    uint32_t pph;                /* the voice's phase (a period = 2^32) */
    uint32_t ci;                 /* the pulse's place (SP_NCHIRP: between pulses) */
    uint32_t lfsr;
    int32_t y, lp;               /* the filter's sample held, smoothed */
} sp_voice_t;
static sp_voice_t sp_v[NTRK][SP_POLY];

static sp_voice_t *sp_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < SP_POLY ? &sp_v[t - trk][i] : 0;
}

/* the word the voice speaks: WORD of BANK, or in WORDS mode the one under its key */
static uint32_t sp_word_of(const track_t *t, const voice_t *v)
{
    uint32_t w = (uint32_t)t->p[P_E0];
    if (t->p[P_E2] == 1)
        w += (uint32_t)((int32_t)v->note - 36);
    return ((uint32_t)t->p[P_E1] % SP_NBANK) * SP_WORDS + (w & (SP_WORDS - 1u));
}

/* 2^(x / 192) in Q16 (x in 1/16 semitones, -1024 .. 1023): a ratio of the pitch table (32-bit divides only: the
 * device has no 64-bit one) */
static uint32_t sp_pow2(int32_t x)
{
    return pitch_inc((uint32_t)clamp(1024 + x, 0, 2047)) / ((pitch_inc(1024u) >> 16) | 1u);
}

/* start segment s of the voice's word: the glide from where it is now to the segment's sound */
static void sp_seg_start(const track_t *t, sp_voice_t *s, uint32_t n)
{
    const sp_word_t *w = &SP_WORD[s->word];
    const sp_seg_t *g = &SP_SEG[w->first + n];
    const sp_target_t *tg = &SP_TARGET[g->t];
    int32_t buzz = t->p[P_E5];
    uint32_t i;
    for (i = 0; i < SP_P; i++) {
        s->k0[i] = s->k[i];
        s->k1[i] = tg->k[i];
    }
    s->va0 = s->va1;
    s->na0 = s->na1;
    s->pt0 = s->pt1;
    s->va1 = (int32_t)(((uint32_t)tg->av * (uint32_t)buzz / 127u * (tg->gv >> 4)) >> 4);   /* av / 256 x gv, Q16 */
    s->na1 = (int32_t)((((uint32_t)tg->an + (uint32_t)tg->av * (uint32_t)(127 - buzz) / 127u) * (tg->gn >> 4)) >> 4);
    s->pt1 = g->pitch;
    s->seg = (uint8_t)n;
    s->pos = 0;
}

static void speech_note_on(track_t *t, voice_t *v)
{
    sp_voice_t *s = sp_of(t, v);
    const sp_target_t *sil;
    uint32_t i;
    if (!s)
        return;
    memset(s, 0, sizeof *s);
    s->word = (uint16_t)sp_word_of(t, v);
    sil = &SP_TARGET[SP_SEG[SP_WORD[s->word].first].t];
    for (i = 0; i < SP_P; i++)
        s->k[i] = sil->k[i];
    s->ci = SP_NCHIRP;
    s->lfsr = 0x1234567u + (uint32_t)(v - t->v) * 7919u;
    sp_seg_start(t, s, 0);
}

/* the block's step through the word: time (RATE), the next segments, END; then the glide's place in the segment */
static void sp_advance(const track_t *t, const voice_t *v, sp_voice_t *s, uint32_t n)
{
    const sp_word_t *w = &SP_WORD[s->word];
    int32_t rate = t->p[P_E3];
    uint32_t step = (uint32_t)((uint64_t)SP_MS * n * sp_pow2((rate - 64) * 3) >> 16), end = (uint32_t)t->p[P_E7];
    if (s->done)
        return;
    s->pos += step;
    for (;;) {
        const sp_seg_t *g = &SP_SEG[w->first + s->seg];
        uint32_t dur = (uint32_t)g->dur * 2u << 16;
        if (end == 1u && v->gate && s->seg == w->hold) {   /* HOLD: the last voiced sound while the key is held */
            if (s->pos > dur)
                s->pos = dur;
            break;
        }
        if (s->pos < dur)
            break;
        s->pos -= dur;
        if (s->seg + 1u < w->n) {
            sp_seg_start(t, s, s->seg + 1u);
        } else if (end == 2u && v->gate) {                 /* LOOP: again while held */
            sp_seg_start(t, s, 0);
        } else {
            s->done = 1;
            s->va0 = s->va1 = s->na0 = s->na1 = 0;
            break;
        }
    }
}

static void speech_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    sp_voice_t *s = sp_of(t, v);
    const sp_seg_t *g;
    uint32_t i, j, trans, clk, pinc;
    int32_t frac, va, na, pitch16, form = t->p[P_E6];
    if (!s)
        return;
    sp_advance(t, v, s, n);
    g = &SP_SEG[SP_WORD[s->word].first + s->seg];
    trans = (uint32_t)g->trans * 2u << 16;
    frac = s->done || !trans || s->pos >= trans ? 32768 : (int32_t)(s->pos / ((trans >> 15) | 1u));
    frac = frac > 32768 ? 32768 : frac;
    for (j = 0; j < SP_P; j++)                            /* the glide: once a block (0.7 ms) */
        s->k[j] = (int16_t)(s->k0[j] + (((int32_t)(s->k1[j] - s->k0[j]) * frac) >> 15));
    va = s->va0 + (int32_t)(((int64_t)(s->va1 - s->va0) * frac) >> 15);
    na = s->na0 + (int32_t)(((int64_t)(s->na1 - s->na0) * frac) >> 15);
    pitch16 = (t->p[P_E2] == 1 ? 48 * 16 + (m->pitch16 - (int32_t)v->note * 16) : m->pitch16) +
              (s->pt0 + (((s->pt1 - s->pt0) * frac) >> 15)) * 4 * t->p[P_E4] / 64;
    clk = (uint32_t)(((uint64_t)SP_CLK * sp_pow2(form * 3 / 2)) >> 16);  /* FORM: 0.7 .. 1.4 */
    pinc = (uint32_t)(((uint64_t)pitch_inc((uint32_t)clamp(pitch16, 0, 2047)) * (0xFFFFFFFFu / clk)) >> 16);   /* per filter
                                                     * sample (a multiply by 1 / clk) */
    for (i = 0; i < n; i++) {
        s->clk += clk;
        if (s->clk >= 65536u) {                           /* a filter sample (at most one an output sample) */
            int32_t e = 0, f;
            s->clk -= 65536u;
            s->pph += pinc;
            if (s->pph < pinc)
                s->ci = 0;                                /* a new period: a pulse */
            if (s->ci < SP_NCHIRP)
                e = (SP_CHIRP[s->ci++] * (va >> 2)) >> 14;
            s->lfsr ^= s->lfsr << 13;
            s->lfsr ^= s->lfsr >> 17;
            s->lfsr ^= s->lfsr << 5;
            e += ((int32_t)(int16_t)s->lfsr * (na >> 2)) >> 14;
            f = e;
            for (j = SP_P; j-- > 0;) {                    /* the lattice, top stage down */
                f -= (int32_t)(((int64_t)s->k[j] * s->g[j]) >> 15);
                if (j + 1u < SP_P)
                    s->g[j + 1] = clamp(s->g[j] + (int32_t)(((int64_t)s->k[j] * f) >> 15), -(1 << 24), 1 << 24);
            }
            f = clamp(f, -(1 << 24), 1 << 24);
            s->g[0] = f;
            s->y = clamp(f, -32767, 32767);
        }
        s->lp += (s->y - s->lp) >> 1;                     /* (a light smoothing: the 8.8 kHz grit stays) */
        out[i] += voice_amp(s->lp, m, i) << 1;
    }
}

/* WORDS: the keys from C2 are the words (the key's note: sp_word_of), as DRUM's lanes; TONE: the scale keyboard */
static int32_t speech_keys(const track_t *t, uint32_t k)
{
    return t->p[P_E2] == 1 ? clamp(29 + 12 * song.octave + (int32_t)k, 0, 127) : -1;
}

/* WORD's names: the bank's words */
#define SP_WD(i) {"WORD", F_ENUM, 0, SP_WORDS - 1, 0, SP_NAMES_##i, 0}
static const param_desc_t SP_WORD_DESC[SP_NBANK] = {SP_WD(0), SP_WD(1), SP_WD(2), SP_WD(3), SP_WD(4)};
static const param_desc_t *speech_desc(const track_t *t, uint32_t k)
{
    return k == 0u ? &SP_WORD_DESC[(uint32_t)t->p[P_E1] % SP_NBANK] : 0;
}

/* {WORD, BANK, MODE, RATE, INTN, BUZZ, FORM, END} */
static const preset_t SPEECH_PRESETS[] = {
    {"ROBOT", {0, 1, 0, 64, 64, 127, 0, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 30, 20)},
    {"COUNT", {0, 0, 1, 64, 64, 127, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 25, 15)},
    {"SPELL", {1, 0, 0, 58, 110, 127, 12, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 30, 25)},
    {"WHISPER", {2, 1, 0, 60, 40, 0, 0, 0}, {0, 0, 127, 30}, 0, 1, FX(0, 0, 40, 40)},
    {"ROBOTER", {15, 2, 0, 64, 50, 127, -24, 0}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 30, 20)},
    {"STEM", {30, 3, 0, 70, 64, 127, 0, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 30, 20)},
    {"MONOTONE", {1, 1, 0, 52, 0, 127, -8, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 35, 20)},
    {"VOWEL PAD", {0, 4, 0, 64, 0, 127, 0, 1}, {60, 90, 120, 90}, 0, 0, FX(0, 60, 30, 60)},
    /* ELECTRO into the delay; ERROR slowed down an octave of formants (a giant); HELLO sped up and squeezed */
    {"ELECTRO", {7, 1, 0, 64, 80, 127, 0, 0}, {0, 0, 127, 25}, 0, 1, FX(10, 0, 60, 25)},
    {"GIANT", {3, 1, 0, 40, 30, 127, -60, 0}, {0, 0, 127, 50}, 0, 1, FX(20, 0, 30, 50)},
    {"CHIPMUNK", {17, 1, 0, 96, 110, 127, 50, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 30, 25)},
    /* GO, looped while held, fast: a rhythm of syllables */
    {"STUTTER", {14, 1, 0, 110, 64, 127, 0, 2}, {0, 0, 127, 10}, 0, 1, FX(0, 0, 40, 15)},
    /* the keys count: in German from NULL, in Dutch from NUL */
    {"ZAHLEN", {0, 2, 1, 64, 64, 127, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 25, 15)},
    {"TELLEN", {0, 3, 1, 64, 64, 127, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 25, 15)},
    /* OO held, low and steady under the chorus; SH, short: a hi-hat */
    {"VOX DRONE", {13, 4, 0, 64, 0, 100, -20, 1}, {80, 90, 127, 100}, 0, 0, FX(0, 80, 20, 70)},
    {"SH HAT", {24, 4, 0, 127, 0, 127, 0, 0}, {0, 60, 0, 30}, 0, 0, FX(0, 0, 20, 10)},
};

static const engine_t ENG_SPEECH = {
    .name = "SPEECH",
    .id = EID_SPEECH,
    .cc = 0x75,                  /* CC74 FORM, CC71 INTN */
    .cats = "FFFFFFFPFFFFFFPD",
    .page_title = {"WORD", "VOICE"},
    .edit = {
        {"WORD", F_ENUM, 0, SP_WORDS - 1, 0, SP_NAMES_1, 0},
        {"BANK", F_ENUM, 0, SP_NBANK - 1, 1, N_SP_BANK, 0},
        {"MODE", F_ENUM, 0, 1, 0, N_SP_MODE, 0},
        {"RATE", F_PCT, 0, 127, 64, 0, 0},
        {"INTN", F_PCT, 0, 127, 64, 0, 0},
        {"BUZZ", F_PCT, 0, 127, 127, 0, 0},
        {"FORM", F_INT, -64, 63, 0, 0, 0},
        {"END", F_ENUM, 0, 2, 0, N_SP_END, 0},
    },
    .presets = SPEECH_PRESETS,
    .npresets = NELEM(SPEECH_PRESETS),
    .poly = SP_POLY,
    .note_on = speech_note_on,
    .render = speech_render,
    .desc = speech_desc,
    .keys = speech_keys,
    .knob = {P_E0, P_E3, P_E6, P_E4},
};
