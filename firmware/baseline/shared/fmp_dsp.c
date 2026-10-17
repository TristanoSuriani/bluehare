/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-18) FMPERC's drum voice, for KIT and BREAKS (FMPERC itself stays archived: archive/engines/eng_fmperc.c).
   FMPERC (1.5): FM percussion. Two operators made to be hit (Blue Hare's own design, in the spirit of the FM drum voices
 * of the 90s machines): a modulating sine at RATIO times the note moves the carrier's phase by INDEX, and every hit
 * sweeps both down over FDEC: the pitch from SWEEP semitones above the note back to it, the index from INDEX to a
 * tenth of it, so a hit starts bright and high and settles into its tone. FDBK feeds the modulator back into itself
 * (towards noise: snares, hats, cymbals). NOISE mixes in noise (0 the tone alone, 127 the noise alone) of the kind
 * NTYP sets: SNAP a short burst (a snare's
 * snap, a click), CLAP four bursts 9 ms apart and a tail, band-passed (a hand clap), HISS as long as the note (open
 * hats, cymbals: the ADSR shapes it). TONE: below 0 a low-pass (darker, -64 ~200 Hz), above 0 a high-pass (thinner,
 * +63 ~5 kHz), 0 open. The amplitude is the ADSR (the presets: SUS 0, a hit). The index gives way on high notes so the
 * spectrum stays below ~14 kHz (as WEST: no aliasing).
 * Voice state: ph[0] the carrier, ph[1] the modulator, ph[2] the clap band's high-pass, s[0] the sweep (Q15, 1 at the
 * hit), s[1] the index envelope (Q15), s[2] the modulator's last output (feedback), s[3] the noise envelope (Q15),
 * s[4] the noise generator, s[5] TONE's filter, s[6] blocks since the hit, s[7] the clap band's low-pass. */
static const char *const N_FMP_NTYP[] = {"SNAP", "CLAP", "HISS", 0};
static const char *const N_FMP_RATIO[] = {"1/2", "1", "3/2", "2", "5/2", "3", "4", "5", "6", "7", "8", "1.41", "1.73",
                                         "2.76", "3.55", "4.61", 0};
static const uint16_t FMP_RATIO[16] = {128, 256, 384, 512, 640, 768, 1024, 1280, 1536, 1792, 2048, 361, 443, 707, 909,
                                       1180};   /* Q8 */
#define FMP_BW 1363500000u      /* 14 kHz as a phase increment (14000 x 2^32 / 44100); / (inc >> 8): in notes, Q8 */


static void fmperc_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[0] = v->s[1] = v->s[3] = 32767;              /* the hit: sweep, index, noise at their tops */
    v->s[2] = v->s[5] = v->s[6] = v->s[7] = 0;
    v->s[4] = (int32_t)(0x9E3779B9u ^ v->age);
}

/* an envelope's step per block (CTL samples) for time knob k (0 .. 127: ~2 ms .. ~2 s to a tenth) */
static int32_t fmp_fall(int32_t env, int32_t k)
{
    int32_t tau = 1 + k * k / 6;                      /* blocks to fall by ~1 / e */
    return env - env / tau - (env > 0);
}

/* the noise envelope a block: SNAP falls in ~18 ms; CLAP strikes again 12 and 24 and 36 blocks (9 ms apart) after the
 * hit, each strike falling in ~1.5 ms, the last one slowly (~100 ms); HISS stays (the ADSR shapes it) */
static int32_t fmp_noise_env(int32_t nb, uint32_t type, int32_t blocks)
{
    if (type == 2u)
        return 32767;
    if (type == 1u) {
        if (blocks == 12 || blocks == 24 || blocks == 36)
            return 32767;
        return fmp_fall(nb, blocks < 36 ? 3 : 34);
    }
    return fmp_fall(nb, 12);
}

/* the voice's hit: its 8 values e[] (RATIO .. TONE, as EDIT's), at pitch16 (base_inc its increment, 0: worked out)
 * into y[] (n <= CTL), no amplitude yet; v holds its state (KIT and BREAKS run it on their own lanes too) */
/* (noinline: KIT, BREAKS and FMPERC call it; a build with BREAKS alone would fold it in, its cost then BREAKS's:
 * tests/target_budget.py counts it on its own) */
static __attribute__((noinline)) void fmp_run(const int16_t *e, voice_t *v, int32_t *y, uint32_t n, int32_t base16, uint32_t base_inc)
{
    uint32_t rm = FMP_RATIO[(uint32_t)e[0] & 15u], inc, minc, i, nz = (uint32_t)v->s[4], type = (uint32_t)e[4] % 3u;
    int32_t sw = v->s[0], ix = v->s[1], nb = v->s[3], prev = v->s[2], tf = v->s[5], bl = v->s[7];
    int32_t bh = (int32_t)v->ph[2];
    int32_t pitch16, idx, wx, bw, fq, fb = e[5], noise = e[6], tone = e[7], kt;
    sw = fmp_fall(sw, e[2]);                          /* the envelopes, once a block */
    ix = fmp_fall(ix, e[2] + 8);
    nb = fmp_noise_env(nb, type, v->s[6]);
    if (v->s[6] < 1000)
        v->s[6]++;
    pitch16 = base16 + ((e[3] * 16 * sw) >> 15);     /* SWEEP: semitones above the note at the hit */
    inc = pitch16 == base16 && base_inc ? base_inc : pitch_inc((uint32_t)clamp(pitch16, 0, 2047));
    minc = (inc >> 8) >= 0xFFFFFFFFu / rm ? 0xFFFFFFFFu : (inc >> 8) * rm;
    idx = (e[1] * 12 * (3277 + ((29490 * ix) >> 15))) >> 15;   /* INDEX x (0.1 .. 1), radians Q8 (at most ~6) */
    bw = (int32_t)(FMP_BW / ((inc >> 8) | 4096u));
    wx = 256 + (((idx + (idx < 256 ? idx : 256)) * (int32_t)rm) >> 8);   /* (Carson: the sidebands' top, in notes) */
    if (wx > bw)
        idx = bw > 256 + (int32_t)rm ? (((bw - 256) << 8) / (int32_t)rm) >> 1 : 0;
    fq = idx * 20861 >> 12;                           /* idx as phase per unit of the modulator, >> 4 */
    kt = tone < 0 ? 32767 + tone * 495 : tone * 400;  /* TONE: the low-pass's coefficient (< 0) or the high-pass's */
    for (i = 0; i < n; i++) {
        int32_t mo = sine_i(v->ph[1] + ((uint32_t)(prev * fb) << 8)), s, w;
        prev = (prev + mo) >> 1;
        s = sine_i(v->ph[0] + ((uint32_t)(mo * fq) << 4));
        nz ^= nz << 13;
        nz ^= nz >> 17;
        nz ^= nz << 5;
        w = ((int32_t)(int16_t)nz * noise) >> 7;
        if (type == 1u) {                             /* CLAP: the noise band-passed (~800 Hz .. 2.5 kHz) */
            bh += ((w - bh) * 3700) >> 15;
            bl += (((w - bh) - bl) * 10500) >> 15;
            w = bl * 2;
        }
        s = ((s * (128 - noise)) >> 7) + ((w * nb) >> 15);   /* NOISE: the mix, the tone giving way to the noise */
        if (tone < 0) {
            tf += ((s - tf) * kt) >> 15;
            s = tf;
        } else if (tone > 0) {
            tf += ((s - tf) * kt) >> 15;
            s -= tf;
        }
        y[i] = clamp(s, -32767, 32767);
        v->ph[0] += inc;
        v->ph[1] += minc;
    }
    v->s[0] = sw;
    v->s[1] = ix;
    v->s[2] = prev;
    v->s[3] = nb;
    v->s[4] = (int32_t)nz;
    v->s[5] = tf;
    v->s[7] = bl;
    v->ph[2] = (uint32_t)bh;
}

