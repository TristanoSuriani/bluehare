/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* SPEECH (1.5): words, the way the LPC speech chips of the late 70s spoke them (Blue Hare's own: no chip's data or code).
 *
 * A buzz (a glottal pulse each period) or a hiss (noise) through a 10-pole lattice filter running at 8820 Hz; the
 * filter's reflection coefficients glide from sound to sound, word by word. There are no recordings: the words are
 * phonemes (the text files in tools/speech, English), turned at build time into sounds (each one's coefficients and
 * gains) and segments (the sound, its length, how long the glide to it takes, the pitch: stress and the fall across the
 * word) by tools/gen_speech.py.
 *
 * WORD picks the word of BANK (NUM the numbers 0 .. 10; TECH machines and technology, the Latin-rooted words of the
 * control room; COSMOS planets and stars; FEEL feelings; DARK darkness, intense events, overwhelm and mystery; SYLL
 * vowels and sung syllables (AA EE AY OH OOH .., PA PARA BA DA .., DOO YEAH WOW ..); PHON single phonemes: 32 words a
 * bank, NUM's 11). MODE TONE: the keys play
 * WORD at their pitch; WORDS: the keys are the words (WORD + the key from C2, round the bank's words, the pitch fixed at
 * C3, as DRUM maps its lanes). RATE: how fast it speaks (half .. double); INTN: how much the pitch follows the word's stress and fall (0:
 * monotone); BUZZ: the voice (127) to a whisper (0: the voiced sounds hiss); FORM: the filter's clock, 0.7 .. 1.4 times
 * (every formant shifts with it, as a bent chip's clock); END: when the word is over, STOP (silence), HOLD (its last
 * voiced sound sustains while the key is held, the rest follows on release) or LOOP (it starts again while held).
 * The data per voice does not fit voice_t: sp_v[track][voice] (SP_POLY voices). Output: the filter's sample held and
 * lightly smoothed to 44.1 kHz (the grit of the low rate stays). */
#include "bluehare_speech.h"
static const char *const N_SP_BANK[] = {SP_BANK_NAMES_INIT, 0};
static const char *const N_SP_MODE[] = {"TONE", "WORDS", 0};
static const char *const N_SP_END[] = {"STOP", "HOLD", "LOOP", 0};
#define SP_POLY 4
#define SP_CLK 13107u            /* the filter's samples per output sample, Q16: 8820 / 44100 */
#define SP_MS 1486u              /* an output sample in ms, Q16 (1000 / 44100) */
#define SP_NCHIRP ((uint32_t)NELEM(SP_CHIRP))
typedef char sp_words_fit[SP_WORDS == 32 && SP_NBANK == 7 ? 1 : -1];   /* (WORD 0 .. a bank's count - 1; SP_WORD_DESC) */

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

/* the word the voice speaks: WORD of BANK (past the bank's last: its last, as shown), or in WORDS mode the one under
 * its key (round the bank's words) */
static uint32_t sp_word_of(const track_t *t, const voice_t *v)
{
    uint32_t b = (uint32_t)t->p[P_E1] % SP_NBANK;
    int32_t n = SP_BANK_N[b], w = clamp(t->p[P_E0], 0, n - 1);
    if (t->p[P_E2] == 1)
        w = ((w + (int32_t)v->note - 36) % n + n) % n;
    return SP_BANK_FIRST[b] + (uint32_t)w;
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
#define SP_WD(i) {"WORD", F_ENUM, 0, SP_N_##i - 1, 0, SP_NAMES_##i, 0}
static const param_desc_t SP_WORD_DESC[SP_NBANK] = {SP_WD_ALL};
static const param_desc_t *speech_desc(const track_t *t, uint32_t k)
{
    return k == 0u ? &SP_WORD_DESC[(uint32_t)t->p[P_E1] % SP_NBANK] : 0;
}

/* {WORD, BANK, MODE, RATE, INTN, BUZZ, FORM, END}; BANK 0 NUM 1 TECH 2 COSMOS 3 FEEL 4 DARK 5 SYLL 6 PHON; END 0 STOP
 * 1 HOLD 2 LOOP */
static const preset_t SPEECH_PRESETS[] = {
    {"COMPUTER", {1, 1, 0, 64, 64, 127, 0, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 30, 20)},
    /* the keys are words: from ZERO; the syllables from PA; the planets; the feelings; the dark words */
    {"COUNT", {0, 0, 1, 64, 64, 127, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 25, 15)},
    {"SYLLABLES", {8, 5, 1, 70, 70, 127, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 30, 20)},
    {"PLANETS", {0, 2, 1, 64, 64, 127, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 30, 30)},
    {"FEELINGS", {0, 3, 1, 64, 80, 127, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 30, 35)},
    {"DARK KEYS", {0, 4, 1, 56, 50, 110, -16, 0}, {0, 0, 127, 30}, 0, 0, FX(0, 20, 35, 50)},
    /* sung vowels, held while the key is */
    {"AA VOX", {0, 5, 0, 64, 40, 127, 0, 1}, {5, 0, 127, 40}, 0, 1, FX(0, 20, 30, 40)},
    {"EE VOX", {1, 5, 0, 64, 40, 127, 0, 1}, {5, 0, 127, 40}, 0, 1, FX(0, 20, 30, 40)},
    {"AY VOX", {2, 5, 0, 64, 40, 127, 0, 1}, {5, 0, 127, 40}, 0, 1, FX(0, 20, 30, 40)},
    {"OH VOX", {3, 5, 0, 64, 40, 127, 0, 1}, {5, 0, 127, 40}, 0, 1, FX(0, 20, 30, 40)},
    {"OOH VOX", {4, 5, 0, 64, 40, 127, -8, 1}, {5, 0, 127, 40}, 0, 1, FX(0, 30, 30, 50)},
    /* syllables: PA, PARA, PA looped while held (a rhythm), DOO for backing lines */
    {"PA", {8, 5, 0, 80, 64, 127, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 30, 20)},
    {"PARA", {9, 5, 0, 76, 90, 127, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 30, 20)},
    {"PA LOOP", {8, 5, 0, 110, 64, 127, 0, 2}, {0, 0, 127, 10}, 0, 1, FX(0, 0, 40, 15)},
    {"DOO DOO", {24, 5, 0, 70, 80, 110, -10, 0}, {0, 0, 127, 25}, 0, 0, FX(0, 30, 30, 40)},
    /* AA held under the chorus; OO, low and steady; SH, short: a hi-hat */
    {"VOWEL PAD", {0, 6, 0, 64, 0, 127, 0, 1}, {60, 90, 120, 90}, 0, 0, FX(0, 60, 30, 60)},
    {"VOX DRONE", {9, 6, 0, 64, 0, 100, -20, 1}, {80, 90, 127, 100}, 0, 0, FX(0, 80, 20, 70)},
    {"SH HAT", {23, 6, 0, 127, 0, 127, 0, 0}, {0, 60, 0, 30}, 0, 0, FX(0, 0, 20, 10)},
    /* screams: a vowel held, the formants pushed up, the intonation full, driven hard; SHRIEK and BANSHEE an octave up
     * (BANSHEE half whispered, looping); HOWL low and long */
    {"SCREAM", {0, 5, 0, 64, 127, 127, 30, 1}, {0, 0, 127, 50}, 0, 1, FX(70, 0, 30, 50)},
    {"SHRIEK", {1, 5, 0, 64, 127, 127, 63, 1}, {0, 0, 127, 40}, 0, 1, FX(80, 0, 30, 40), .oct = 1},
    {"HOWL", {4, 5, 0, 48, 127, 127, -20, 1}, {20, 0, 127, 80}, 0, 1, FX(40, 20, 30, 70)},
    {"BANSHEE", {6, 5, 0, 56, 127, 40, 40, 2}, {10, 0, 127, 80}, 0, 0, FX(30, 40, 40, 80), .oct = 1},
};

static const engine_t ENG_SPEECH = {
    .name = "SPEECH",
    .id = EID_SPEECH,
    .cc = 0x75,                  /* CC74 FORM, CC71 INTN */
    .cats = "FFFFFFLLLLLFFFFPPDFFFF",
    .page_title = {"WORD", "VOICE"},
    .edit = {
        {"WORD", F_ENUM, 0, SP_N_1 - 1, 0, SP_NAMES_1, 0},
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
