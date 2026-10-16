/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The SPEECH engine (src/eng_speech.c, tools/gen_speech.py), built with a distribution that has it (run_tests.sh:
 * BLUEHARE_DIST=idm): every word of every bank speaks (not silent, no clipping) and ends; a vowel sounds at its key's
 * pitch (an octave up: twice the frequency); RATE changes the length, FORM the formants (the spectrum's centroid),
 * HOLD sustains while held, LOOP repeats; WORDS mode maps the keys to the words. Demos in DIR: each bank's words one
 * after another (<bank>.wav), the presets (presets.wav).
 *   speech_test [DIR] */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static int fails;
static void check(const char *what, int ok)
{
    printf("speech: %-90s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static track_t *T;
static FILE *W;
static uint32_t wav_n;
static int32_t blk_peak;
static double blk_e;
static int16_t *rec;                                    /* (the last render, mono, for the measurements) */
static uint32_t rec_n, rec_max;

static void wav_begin(FILE *f) { uint8_t h[44] = {0}; fwrite(h, 1, 44, f); wav_n = 0; }
static void wav_end(FILE *f)
{
    uint32_t d = wav_n * 4u;
    fseek(f, 0, SEEK_SET);
    wav_hdr(f, wav_n);
    (void)d;
    fclose(f);
}
static void render(uint32_t n)                          /* n samples (whole blocks) into the demo and rec */
{
    int32_t o[2u * CTL];
    uint32_t i, f;
    for (f = 0; f < n; f += CTL) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t l = o[2 * i];
            blk_peak = abs(l) > blk_peak ? abs(l) : blk_peak;
            blk_e += (double)l * l;
            if (rec && rec_n < rec_max)
                rec[rec_n++] = (int16_t)clamp(l, -32768, 32767);
            if (W) {
                wav_put(W, o[2 * i], o[2 * i + 1]);
                wav_n++;
            }
        }
    }
}
static sp_voice_t *sp_first(void)
{
    uint32_t v;
    for (v = 0; v < SP_POLY; v++)
        if (T->v[v].active)
            return &sp_v[0][v];
    return 0;
}
/* one word at note: until it is over (at most 2 s), then the release; -> its length in ms, its peak */
static uint32_t speak(uint32_t note, int32_t *peak)
{
    uint32_t n = 0;
    sp_voice_t *s;
    blk_peak = 0;
    blk_e = 0;
    trk_note_on(T, note, 100);
    render(CTL);
    while ((s = sp_first()) && !s->done && n < FS * 2u) {
        render(CTL);
        n += CTL;
    }
    trk_note_off(T, note);
    render(FS / 5u);
    *peak = blk_peak;
    return n * 1000u / FS;
}

/* the fundamental of rec (autocorrelation over 60 .. 600 Hz): the shortest lag within 90 % of the best one (the
 * pulses fall on the filter's samples, so periods alternate by one: two periods can correlate a little better) */
static double f0_of(const int16_t *x, uint32_t n)
{
    static double c[FS / 60u];
    uint32_t lag, best = 0, i;
    double bv = 0;
    for (lag = FS / 600u; lag < FS / 60u; lag++) {
        double e = 1e-9;
        c[lag] = 0;
        for (i = 0; i + lag < n; i++) {
            c[lag] += (double)x[i] * x[i + lag];
            e += (double)x[i + lag] * x[i + lag];
        }
        c[lag] /= sqrt(e);
        bv = c[lag] > bv ? c[lag] : bv;
    }
    for (lag = FS / 600u + 1u; lag + 1u < FS / 60u && !best; lag++)
        if (c[lag] >= 0.9 * bv && c[lag] >= c[lag - 1] && c[lag] >= c[lag + 1])
            best = lag;
    return best ? (double)FS / best : 0;
}
/* the spectrum's centroid of rec (a DFT every 50 Hz to 5 kHz) */
static double centroid(const int16_t *x, uint32_t n)
{
    double num = 0, den = 1e-9, f;
    uint32_t i;
    for (f = 50; f < 5000; f += 50) {
        double re = 0, im = 0;
        for (i = 0; i < n; i++) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * i / n);
            re += x[i] * w * cos(2 * M_PI * f * i / FS);
            im += x[i] * w * sin(2 * M_PI * f * i / FS);
        }
        num += f * (re * re + im * im);
        den += re * re + im * im;
    }
    return num / den;
}
static void sound(uint32_t bank, uint32_t word, uint32_t mode, int rate, int form, uint32_t end)
{
    host_tracks_init();
    host_preset(T, ENGI_SPEECH, 0);
    T->p[P_E0] = (int16_t)word; T->p[P_E1] = (int16_t)bank; T->p[P_E2] = (int16_t)mode; T->p[P_E3] = (int16_t)rate;
    T->p[P_E6] = (int16_t)form; T->p[P_E7] = (int16_t)end;
    T->p[P_E4] = 64; T->p[P_E5] = 127;
    T->p[P_ATK] = 0; T->p[P_SUS] = 127; T->p[P_REL] = 10;
    T->p[P_DIST] = T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 0;
    T->p[P_VOICE] = V_POLY;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/speech_demo";
    char path[512];
    uint32_t b, w, quiet = 0, loud = 0, long_ = 0, n = 0, ms, minms = 9999, maxms = 0;
    int32_t pk;
    T = &trk[0];
    rec_max = FS * 3u;
    rec = calloc(rec_max, sizeof *rec);
    host_tracks_init();
    render(FS / 10u);                                   /* (past the output's fade-in) */
    for (b = 0; b < SP_NBANK; b++) {                    /* every word: speaks, ends, no clipping; the demos */
        snprintf(path, sizeof path, "%s/%s.wav", dir, N_SP_BANK[b]);
        for (char *c = path + strlen(dir) + 1; *c; c++)
            *c = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
        W = fopen(path, "wb");
        if (W)
            wav_begin(W);
        for (w = 0; w < SP_BANK_N[b]; w++) {
            sound(b, w, 0, 64, 0, 0);
            ms = speak(48, &pk);
            quiet += pk < 1000;
            loud += pk >= 32000;
            long_ += ms >= 1990u;
            minms = ms < minms ? ms : minms;
            maxms = ms > maxms ? ms : maxms;
            n++;
        }
        if (W)
            wav_end(W);
        W = 0;
    }
    printf("speech: %u words, %u .. %u ms\n", n, minms, maxms);
    check("every word of every bank speaks (peak over -30 dBFS), ends within 2 s, never clips",
          n == (uint32_t)NELEM(SP_WORD) && !quiet && !loud && !long_);

    {   /* a vowel at C3 and C4: twice the frequency */
        double f1, f2;
        sound(SP_NBANK - 1u, 0, 0, 64, 0, 1);                       /* PHON AA, HOLD */
        rec_n = 0;
        trk_note_on(T, 48, 100); render(FS / 2u); trk_note_off(T, 48); render(FS / 5u);
        f1 = f0_of(rec + FS / 5u, 4096);
        sound(SP_NBANK - 1u, 0, 0, 64, 0, 1);
        rec_n = 0;
        trk_note_on(T, 60, 100); render(FS / 2u); trk_note_off(T, 60); render(FS / 5u);
        f2 = f0_of(rec + FS / 5u, 4096);
        printf("speech: AA at C3 %.1f Hz, at C4 %.1f Hz\n", f1, f2);
        check("a vowel at its key's pitch: C3 ~130.8 Hz, C4 twice that", fabs(f1 - 130.8) < 6 && fabs(f2 / f1 - 2) < 0.06);
    }
    {   /* RATE: double speed about half as long; FORM: the formants up (the centroid) */
        uint32_t m1, m2;
        double c1, c2;
        sound(1, 0, 0, 64, 0, 0); m1 = speak(48, &pk);
        sound(1, 0, 0, 127, 0, 0); m2 = speak(48, &pk);
        printf("speech: SYSTEM at RATE 64 %u ms, at 127 %u ms\n", m1, m2);
        check("RATE 127 (double speed): about half as long", m2 * 2u > m1 * 8u / 10u && m2 * 2u < m1 * 12u / 10u);
        sound(SP_NBANK - 1u, 0, 0, 64, -60, 1); rec_n = 0;
        trk_note_on(T, 48, 100); render(FS / 2u); trk_note_off(T, 48); render(FS / 10u);
        c1 = centroid(rec + FS / 5u, 2048);
        sound(SP_NBANK - 1u, 0, 0, 64, 60, 1); rec_n = 0;
        trk_note_on(T, 48, 100); render(FS / 2u); trk_note_off(T, 48); render(FS / 10u);
        c2 = centroid(rec + FS / 5u, 2048);
        printf("speech: AA's centroid at FORM -60 %.0f Hz, at +60 %.0f Hz\n", c1, c2);
        check("FORM up: the formants up (the centroid at least 1.5 times)", c2 > c1 * 1.5);
    }
    {   /* HOLD sustains while held; STOP ends; LOOP starts again */
        sp_voice_t *s;
        int held, stopped, looped;
        sound(1, 31, 0, 64, 0, 1);                      /* ENERGY, HOLD */
        trk_note_on(T, 48, 100); render(FS * 2u);
        held = (s = sp_first()) && !s->done;
        trk_note_off(T, 48); render(FS);
        sound(1, 31, 0, 64, 0, 0);                      /* STOP */
        trk_note_on(T, 48, 100); render(FS * 2u);
        stopped = (s = sp_first()) && s->done;
        trk_note_off(T, 48); render(FS / 2u);
        sound(1, 31, 0, 64, 0, 2);                      /* LOOP: the segment index comes back to the start */
        trk_note_on(T, 48, 100); render(FS * 2u);
        looped = (s = sp_first()) && !s->done;
        trk_note_off(T, 48); render(FS / 2u);
        check("END: HOLD still speaking after 2 s held, STOP over, LOOP still going", held && stopped && looped);
    }
    {   /* WORDS: key C2 + k is word WORD + k at C3 */
        sp_voice_t *s;
        int ok = speech_keys(T, 0) >= 0 ? 0 : 1;        /* (TONE: the scale keyboard) */
        sound(0, 3, 1, 64, 0, 0);
        ok &= speech_keys(T, 0) >= 0;
        trk_note_on(T, 36 + 2, 100); render(CTL * 2u);
        ok &= (s = sp_first()) && s->word == SP_BANK_FIRST[0] + 5u;   /* NUM: THREE + 2 = FIVE */
        trk_note_off(T, 38); render(FS / 2u);
        check("WORDS: the keys from C2 are the words from WORD (C2 + 2 with WORD THREE: FIVE); TONE: no key map", ok);
    }
    {   /* the presets, one after another */
        uint32_t p;
        snprintf(path, sizeof path, "%s/presets.wav", dir);
        W = fopen(path, "wb");
        if (W)
            wav_begin(W);
        uint32_t silent = 0;
        for (p = 0; p < ENGINES[ENGI_SPEECH]->npresets; p++) {
            host_tracks_init();
            host_preset(T, ENGI_SPEECH, p);
            speak(48, &pk);
            if (pk < 1000) {
                printf("speech:   preset %s: peak %d\n", ENGINES[ENGI_SPEECH]->presets[p].name, pk);
                silent++;
            }
            render(FS / 4u);
        }
        if (W)
            wav_end(W);
        W = 0;
        check("every preset speaks at C3 (peak over -30 dBFS)", !silent);
    }
    printf("%s\n", fails ? "SPEECH TEST FAILED" : "speech test passed");
    return fails != 0;
}
