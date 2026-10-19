/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-19) The ONYX engine (firmware/baseline/eng_onyx.c, onyx_lpc.h): every phrase of every bank speaks (its peak
 * over -24 dBFS, never clipping) and ends; a key an octave up an octave higher (the voiced spectrum's lowest line
 * twice); RATE 127 about half as long; HOLD still speaking after 3 s held; WORDS maps the keys to the phrases. Demos in
 * DIR: each bank's phrases one after another (<bank>.wav).
 *   onyx_test [DIR] */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

static int fails;
static void check(const char *what, int ok)
{
    printf("onyx: %-92s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static track_t *T;
static FILE *W;
static uint32_t wav_n;
static int32_t pk;
static void render(uint32_t n)
{
    int32_t o[2u * CTL];
    uint32_t i, f;
    for (f = 0; f < n; f += CTL) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            pk = abs(o[2 * i]) > pk ? abs(o[2 * i]) : pk;
            if (W) {
                wav_put(W, o[2 * i], o[2 * i + 1]);
                wav_n++;
            }
        }
    }
}
static void sound(uint32_t bank, uint32_t word, uint32_t mode, uint32_t rate, uint32_t end)
{
    host_preset(T, ENGI_ONYX, 0);
    T->p[P_E0] = (int16_t)word;
    T->p[P_E1] = (int16_t)bank;
    T->p[P_E2] = (int16_t)mode;
    T->p[P_E3] = (int16_t)rate;
    T->p[P_E7] = (int16_t)end;
}
static ox_voice_t *first(void)
{
    uint32_t v;
    for (v = 0; v < OX_POLY; v++)
        if (T->v[v].active)
            return &ox_v[0][v];
    return 0;
}
/* a phrase at note: until it is over (at most 4 s), then the release; -> its length in ms */
static uint32_t speak(uint32_t note)
{
    uint32_t n = 0;
    ox_voice_t *s;
    pk = 0;
    trk_note_on(T, note, 100);
    render(CTL);
    while ((s = first()) && !s->done && n < FS * 4u) {
        render(CTL);
        n += CTL;
    }
    trk_note_off(T, note);
    render(FS / 6u);
    return n * 1000u / FS;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/onyx_demo";
    char path[512];
    uint32_t b, w, quiet = 0, loud = 0, long_ = 0, n = 0, ms, minms = 99999, maxms = 0, ms1, ms2;
    int32_t pmin = 1 << 30, pmax = 0;
    T = &trk[0];
    host_tracks_init();
    render(FS / 10u);
    for (b = 0; b < ONYX_NBANK; b++) {
        snprintf(path, sizeof path, "%s/%s.wav", dir, ONYX_BANK_NAMES[b]);
        for (char *c = path + strlen(dir) + 1; *c; c++)
            *c = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
        W = fopen(path, "wb");
        if (W) { uint8_t h[44] = {0}; fwrite(h, 1, 44, W); wav_n = 0; }
        for (w = 0; w < ONYX_BANK_N[b]; w++) {
            sound(b, w, 0, 64, 0);
            ms = speak(48);
            quiet += pk < 2048;
            loud += pk >= 32000;
            long_ += ms >= 3990u;
            minms = ms < minms ? ms : minms;
            maxms = ms > maxms ? ms : maxms;
            pmin = pk < pmin ? pk : pmin;
            pmax = pk > pmax ? pk : pmax;
            n++;
            render(FS / 5u);
        }
        if (W) { fseek(W, 0, SEEK_SET); wav_hdr(W, wav_n); fclose(W); W = 0; }
    }
    printf("onyx: %u phrases, %u .. %u ms, peaks %d .. %d\n", n, minms, maxms, (int)pmin, (int)pmax);
    check("every phrase of every bank speaks (peak over -24 dBFS), never clips, ends within 4 s",
          n == ONYX_NPHRASE && !quiet && !loud && !long_);
    sound(3, 0, 0, 64, 0);
    ms1 = speak(48);
    sound(3, 0, 0, 127, 0);
    ms2 = speak(48);
    printf("onyx: OBEY THE RHYTHM at RATE 64 %u ms, at 127 %u ms\n", ms1, ms2);
    check("RATE 127 (double speed): about half as long", ms2 * 10u > ms1 * 4u && ms2 * 10u < ms1 * 6u);
    {   /* HOLD: still speaking after 3 s held; STOP: over */
        uint32_t k;
        ox_voice_t *s;
        sound(13, 4, 0, 64, 1);                          /* (TOKENS: AHH) */
        trk_note_on(T, 48, 100);
        for (k = 0; k < 3u * FS; k += CTL) render(CTL);
        s = first();
        pk = 0;
        render(FS / 10u);
        check("END: HOLD still speaking after 3 s held", s && !s->done && pk > 1000);
        trk_note_off(T, 48);
        render(FS);
    }
    {   /* WORDS: the keys from C2 are the phrases from WORD */
        sound(3, 2, 1, 64, 0);
        trk_note_on(T, 36 + 3, 100);
        render(CTL);
        check("WORDS: C2 + 3 with WORD 2 speaks the bank's phrase 5", first() && first()->phrase == ONYX_BANK_FIRST[3] + 5u);
        trk_note_off(T, 39);
        render(FS);
    }
    printf(fails ? "onyx: %d FAILED\n" : "onyx test passed\n", fails);
    return fails ? 1 : 0;
}
