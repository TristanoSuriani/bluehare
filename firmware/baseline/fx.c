/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* Effects: per-track DIST insert, then sends into three
 * shared buses (chorus, tempo delay, reverb), stereo dry mix. (Blue Hare) The buses give a mid (wet: to both sides)
 * and a side (wside: + to the left, - to the right): the chorus reads its line twice, half a sweep apart (the left
 * one as it always was, the right one opposite); DELAY TYPE PING echoes left, then right, and so on (one line: the
 * left at the time, the right at twice it, the loop round both; FDBK an echo's share of the one before, as CLEAN's;
 * up to 0.74 s); ROOM's combs signed in turn give its side; TAPE, WORN and SPRING stay mono, HALL has its own
 * (rev_side). The delay's and the reverb's sends lose their lows first (12 dB/oct ~220 Hz: send_lowcut), so the bass
 * stays dry and out of the tails.
 * DELAY TYPE (G_DTYPE, Blue Hare 1.5): CLEAN the delay as it always was; TAPE and WORN a tape echo in the same line
 * (dly_tape): the read wobbles (wow ~0.5 Hz, flutter ~6 Hz, read between samples), the time glides (TIME turned, the
 * tempo changed: the echoes bend in pitch), the loop low-passed (COLR) and high-passed (thinner each pass) and
 * saturated, so FDBK past ~95 runs away into a bounded howl and settles when turned back. WORN: more wow and flutter,
 * darker, thinner, driven harder.
 * DUAL (Blue Hare 2026-10): a true stereo delay, two lines in dly_buf's halves (up to 0.74 s each, as PING): the left
 * echoes at TIME, the right at three quarters of it (a straight time against its dotted one: 1/4 left, 3/16 right),
 * each its own low-passed loop (COLR, FDBK), a quarter of each fed into the other's (the echoes cross and smear) */
#define DLY_LEN DEV_NAT(65536u, 1048576u)   /* 1.49 s: 1/4 at 40 BPM fits (native: 23.8 s, 4BAR at 41 BPM; PING's right
                                              * side 11.9 s) */
#define CHO_LEN 2048u
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_comb[1116 + 1188 + 1277 + 1356] __attribute__((section(".pool")));
static union {                          /* ROOM's allpasses; SPRING's allpass chain (int32: no clamps) */
    int16_t ap[556 + 441];
    int32_t sp[(556 + 441) / 2];
} rev_u __attribute__((section(".pool")));
#define rev_ap (rev_u.ap)
static struct {
    uint32_t dly_w, cho_w, cho_ph;
    int32_t dly_lp, dly_lp2;             /* (dly_lp2: DUAL's right line) */
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
    uint8_t rtype;                       /* the reverb model running (G_RTYPE: 0 ROOM, 1 SPRING, 2 HALL, 3 GATED; native: 4
                                          * CONCERT, 5 CAVERN, 6 SHIMMER, fx_native.c) */
    uint16_t sp_w;                       /* SPRING: the loop's write index (SP_MASK) */
    int32_t sp_lp, sp_hp, sp_he, sp_size;   /* .. its loop low-pass, low cut (and its remainder), the loop
                                             * length (Q8, glides) */
    uint32_t sp_ph;                      /* .. the output tap's wobble */
    uint8_t dtype, tp_out;               /* DELAY TYPE running (G_DTYPE: 0 CLEAN, 1 TAPE, 2 WORN); TAPE going to CLEAN */
    int32_t tp_d8, tp_hp8;               /* TAPE: the delay read (Q8 samples, glides), the loop's high-pass (Q8) */
    int32_t tp_wd, tp_fd;                /* .. the wow's and the flutter's depth now (Q8 samples, glide) */
    int32_t tp_drv, tp_fbk, tp_hpm;      /* .. the drive (Q8), the feedback (Q15), the low cut's share (Q15): glide */
    uint32_t tp_wow, tp_flt;             /* .. their phases */
    uint8_t amb_on;                      /* (2026-10-17) DELAY TYPE AMB running: the line is amb-1's tape (fx_amb.c) */
    uint32_t dclr;                       /* .. switching to or from it: the line's part being cleared + 1 (0: none) */
} fx;
#define DT_AMB 5u
#define DCLR_N 4096u                     /* (a part a block: 8 KB) */

/* SPRING (G_RTYPE 1): one spring of a spring tank, mono like the other buses, in the ROOM's own buffers (no
 * RAM of its own): the input and the loop's return -> a low cut (~110 Hz: a spring carries little bass) ->
 * SP_N stretched first-order allpasses, (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel: below
 * fs / 8 = 5.5 kHz the group delay rises with frequency, the chirp; each pass round the loop adds more of
 * it: the "boing", the drips) -> the loop's delay line (rev_comb, SP_LEN) -> back through a one-pole
 * low-pass (DAMP) and the decay gain (SIZE). The output: the spring's far end, half way along the loop
 * (the first sound 15 .. 30 ms after the send: the tank's own pre-delay; a slow wobble of a sample or two
 * on it), plus a second, quieter pickup at three quarters (a shorter spring beside it: denser). SIZE sets
 * the loop's length (30 .. 60 ms) and its decay; DAMP the loop's low-pass. The allpasses' states: 4
 * samples each (int32), in rev_ap's memory. Changing the model fades the old one's block out and clears both buffers. */
#define SP_LEN 4096u                     /* the loop's line in rev_comb (4937 samples) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 (0.7: Q12 keeps (x - o) * a in 32 bits up to
                                          * |x - o| < 749000, far past any peak the chain reaches) */
_Static_assert(sizeof rev_comb / 2u >= SP_LEN && sizeof rev_u.sp / 4u >= 4u * (SP_N + 1u), "SPRING in ROOM's buffers");

/* HALL (G_RTYPE 2, 1.2): a smooth, dense, stereo reverb, again in ROOM's buffers (its few states and one block
 * of work in the pool, no .bss). ROOM's 4 combs each ring on their own (the metallic tone); here every echo
 * goes round 8 lines that all feed each other, and they run at half the rate (22.05 kHz: the same memory holds
 * twice the time, so twice the resonances, at half the work; the tail stops near 10 kHz, as a hall's air
 * does). The send (every other sample of it, low-passed 1 2 1; ROOM's level) -> a low cut (~110 Hz: no boom
 * in a long tail) -> 2 allpass diffusers (rev_ap: a drum's click smeared into a burst) -> into the 8 lines
 * (rev_comb, mutually prime lengths, 21 .. 34 ms). Lines 1, 2, 4, 7 are read through slow sines (+-HL_MOD
 * samples, between samples; 0.37 or 0.74 Hz, each in its own phase): the resonances drift instead of ringing.
 * Lines 0, 3, 5, 6, read at their ends, go through a one-pole low-pass (DAMP: the mixing takes every echo
 * through them); every
 * line through its gain (from its length: all decay alike; RT60 below the damping 0.5 s at SIZE 0, 4 s at
 * 90, 9 s at 127; ~2.4 s heard at 90 with DAMP 60); then the 8 x 8 mixing: a conference matrix (Paley,
 * order 8: every line feeds the 7 others, +-1 each, none itself: no flutter at a line's own length), with
 * the send added. Stereo: left lines 0 3 4 -7, right 1 2 5 6 (sums spread over the matrix's rows); the wet
 * bus (mono) gets left + right, rev_side left - right, which mix_block adds to the left and takes from the
 * right (rev_side_mix). Back to 44.1 kHz between samples. Rounding: a loud line's gain rounds to the nearest
 * (the tail decays as it should), a quiet one's towards zero (it ends in exact silence: no limit cycle), and
 * the low-passes keep their steps' remainders (no offset). */
#define HL_NL 8u                         /* lines */
#define HL_NAP 2u                        /* diffusers */
#define HL_NC (CTL / 2u)                 /* a block at half the rate */
#define HL_MOD 8                         /* the taps' swing, samples (read 2 .. 18 samples short of the end) */
#define HL_LPM 0x69u                     /* the lines with a low-pass */
#define HL_KD(d) (32767 - (d) * 180)     /* its coefficient (Q15) for DAMP d: none at 0 .. ~1 kHz at 127 */
#define HL_LOUD 32                       /* a line this loud (two samples' sum) rounds its gain to the nearest */
#define HL_OUT 4                         /* the outputs' gain: the level of ROOM's tail */
static const uint16_t HL_LEN[HL_NL] = {467, 509, 547, 593, 631, 673, 709, 743};   /* (+1 each: the mirror) */
static const uint16_t HL_AP[HL_NAP] = {131, 307};
static const int16_t HL_APG[HL_NAP] = {11469, 10240};               /* Q14: 0.7, 0.625 */
_Static_assert(467 + 509 + 547 + 593 + 631 + 673 + 709 + 743 + HL_NL <= sizeof rev_comb / 2u &&
               131 + 307 <= sizeof rev_u.ap / 2u, "HALL in ROOM's buffers");
_Static_assert(CTL % 2u == 0u && HL_NC + 2u * HL_MOD + 2u < 467u && HL_NC <= 131u, "HALL: one wrap a block at most");
static struct {                          /* HALL's state (in the pool: no .bss) */
    uint16_t i[HL_NL], d[HL_NAP];        /* the lines' write indices, the diffusers' */
    int32_t lp[HL_NL], le[HL_NL];        /* the low-passes and their steps' remainders */
    int32_t x, m, s, hp, he;             /* the send's last sample, its low cut and that step's remainder; the
                                          * last mid and side out (half rate) */
    int16_t g[HL_NL], sz;                /* the lines' gains / sqrt(7) (Q14), for SIZE sz - 1 (0: none yet) */
    uint32_t ph;                         /* the taps' drift */
    uint8_t side;                        /* sd holds this block's stereo difference (HALL running) */
    int32_t sd[CTL];                     /* the block's left minus right (mix_block: rev_side_mix) */
    int32_t bx[HL_NC], l[HL_NC], r[HL_NC];   /* the block at half the rate: the diffused send, left, right */
    int16_t y[HL_NL][HL_NC];             /* the lines' ends, then what goes back into them */
} hl __attribute__((section(".pool")));
#define rev_side (hl.sd)

/* DIST: low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain (straight into tanh over the full band, it would sound like a
 * broken digital fuzz). State per part (track_t dist_*). */
static void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    int32_t d = t->p[P_DIST], i, g, k, mk, bias = 2400, b0;
    if (!d)
        return;                                         /* states kept: switching on does not click */
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = 32000 - d * 95;                                  /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = softclip((((x >> 2) * g) >> 10) + bias) - b0;   /* >> 2 first: no overflow for loud poly */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}

/* master: peak limiter in front of the soft clipper. Fast attack (~0.1 ms),
 * ~150 ms release, threshold where tanh is still nearly linear, so chords
 * get quieter instead of crushed. */
#define LIM_T 18000
static int32_t lim_env = LIM_T;
/* (Blue Hare) the bus compressor in front of it (bus_comp): 3:1 above BC_T (6 dB under the limiter's threshold), the
 * level the block's peak smoothed (~3 ms attack, ~190 ms release), the gain moving across each block. Eight tracks
 * summed ran the limiter nearly all the time at the top of MASTER (96 % of the blocks of a full song, down to -10 dB):
 * the compressor takes the body of the mix down smoothly, the limiter only the fast peaks */
#define BC_T 9000
static int32_t bc_env, bc_g = 32768;    /* the level; the gain now (Q15) */
static void bus_comp(int32_t *out, uint32_t n);   /* (below) */
static void mono_lows(int32_t *out, uint32_t n);  /* (below) */
/* MENU > USB LEVEL FIXED (for #42: record over USB with the speaker turned down): the mix goes to master_out at the
 * full MASTER level, USB audio takes that (audio.c uac_tap), and only then does MASTER scale what the DAC gets
 * (usb_fixed_dac). MASTER (0, the default): MASTER before master_out, as always (USB follows the knob) */
static volatile uint8_t fx_usb_fixed;
#define MASTER_FULL 4096                /* main.c: the MASTER knob's top, Q12 */
/* the MASTER pot (ADC 0..1023, smoothed in main.c) to its level, square law: 0 .. 4088 (Q12) */
static inline uint32_t master_of_pot(uint32_t k10) { return (k10 * k10) >> 8; }
static __attribute__((noinline)) void usb_fixed_dac(int32_t *out, uint32_t n)   /* audio ISR, after uac_tap */
{
    uint32_t i;
    int32_t m = (int32_t)song.master_q12;
    for (i = 0; i < 2u * n; i++)
        out[i] = (out[i] * m) >> 12;                /* (|out| <= 32767 after the soft clip: fits) */
}
static volatile uint8_t fx_lowcut;     /* settings: 1 LOWCUT 12 dB/oct ~110 Hz, 2 BASS+ (the small speaker):
                                        * 12 dB/oct ~220 Hz plus the harmonics of the bass (spk_bass) */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (A rounded step of (x - dc) / 4096 would stop moving at |x - dc| < 2048 and leave an offset of up
 * to +-31 at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static int32_t slc[4];                   /* (Blue Hare) send_lowcut's states: delay, reverb; two poles each */
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err, uint32_t sh)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> sh;
    *err = e - (d << sh);
    *lc += d;
    return x - *lc;
}

/* BASS+: what the speaker cannot play, heard through its harmonics. The bass below ~150 Hz is clipped at its
 * own envelope (a level-following trapezoid: odd harmonics), then band-passed ~220 Hz..1 kHz and added.
 * The low-pass has 4 poles (#42: with 2, the trapezoid rebuilt the 300 .. 600 Hz of the mix itself, late,
 * and cancelled up to 6 dB of it; now under 0.5 dB) */
static int32_t sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl;
static inline int32_t spk_bass(int32_t m)
{
    int32_t a, t, u;
    sb_lp1 += ((m - sb_lp1) * 692) >> 15;
    sb_lp2 += ((sb_lp1 - sb_lp2) * 692) >> 15;
    sb_lp3 += ((sb_lp2 - sb_lp3) * 692) >> 15;
    sb_lp4 += ((sb_lp3 - sb_lp4) * 692) >> 15;
    a = sb_lp4 < 0 ? -sb_lp4 : sb_lp4;
    if (a > sb_env)
        sb_env += (a - sb_env) >> 2;
    else if (sb_env > 0)
        sb_env -= (sb_env >> 11) + 1;
    t = clamp(sb_lp4 * 8, -sb_env, sb_env);
    sb_h1 += (t - sb_h1) >> 5;
    u = t - sb_h1;
    sb_h2 += (u - sb_h2) >> 5;
    u -= sb_h2;
    sb_hl += (u - sb_hl) >> 3;
    return sb_hl + (sb_hl >> 1);      /* x1.5 (#180: x3 peaked at 1.5 .. 3.5 x a full-scale kick, the limiter */
}                                     /* pulled the mix down up to 16 dB on each hit and the buzz took over) */

static inline void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    *l = dc_block(*l, &dc_l, &dce_l);
    *r = dc_block(*r, &dc_r, &dce_r);
    if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
        uint32_t sh = fx_lowcut == 2u ? 5u : 6u;    /* rounded step stopped at |x - lc| < 32: an offset) */
        int32_t b = fx_lowcut == 2u ? spk_bass((*l + *r) >> 1) : 0;
        *l = lowcut1(*l, &lc_l1, &lce[0], sh);
        *l = lowcut1(*l, &lc_l2, &lce[1], sh) + b;
        *r = lowcut1(*r, &lc_r1, &lce[2], sh);
        *r = lowcut1(*r, &lc_r2, &lce[3], sh) + b;
    }
    al = *l < 0 ? -*l : *l;
    ar = *r < 0 ? -*r : *r;
    a = al > ar ? al : ar;
    if (a > lim_env)
        lim_env += (a - lim_env) >> 2;
    else if (lim_env > LIM_T)
        lim_env -= ((lim_env - LIM_T) >> 12) + 1;
    if (lim_env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)lim_env);   /* < 32768 */
        *l = ((*l >> 4) * g) >> 11;                      /* >> 4 first: |l| may be far above Q15 */
        *r = ((*r >> 4) * g) >> 11;
    }
    *l = softclip(*l);
    *r = softclip(*r);
}

/* length of one division (N_DIV order) in samples at the song tempo */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* original IDs stay fixed; slow rates append */
static uint32_t midi_beat_samples;                    /* zero until an external clock has a measured tempo */
static uint32_t beat_samples(void)
{
    if (tgt_beat_samples())                           /* (target.h: the plugin's host's tempo; the device: none) */
        return tgt_beat_samples();
    return song.g[G_CLOCK] && midi_beat_samples ? midi_beat_samples : (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];
}
static uint32_t div_samples(uint32_t div)
{
    uint32_t quarter = beat_samples();
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u) : quarter / DIV_DEN[div % 6u];
}

#include "perform.c"                                 /* the FX hold layer's effects (the master) */
#include "click.c"                                   /* the metronome's click (after the master: audio.c) */

static uint32_t delay_samples(void)
{
    uint32_t s = div_samples((uint32_t)song.g[G_DTIME]);
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

/* ROOM (G_RTYPE 0): 4 damped combs + 2 allpasses (Freeverb-like), added to out; (Blue Hare) the combs signed in turn,
 * + - + -, its side (to side: uncorrelated with their sum, the room's width) */
static __attribute__((noinline)) void rev_room(const int32_t *rev_in, int32_t *out, int32_t *side, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    for (i = 0; i < n; i++) {
        int32_t a = 0, sd = 0;
        int16_t *c = rev_comb;
        int32_t in = mulq15(rev_in[i], 2580);           /* 1/8 at -4 dB: level as before the allpass fix */
        for (k = 0; k < 4u; k++) {
            int32_t o = c[fx.comb_i[k]];
            fx.comb_lp[k] = o + mulq15(fx.comb_lp[k] - o, 32767 - damp);
            c[fx.comb_i[k]] = (int16_t)clamp(in + mulq15(fx.comb_lp[k], size), -32768, 32767);
            if (++fx.comb_i[k] >= REV_COMB[k])
                fx.comb_i[k] = 0;
            a += o;
            sd += k & 1u ? -o : o;
            c += REV_COMB[k];
        }
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t o = c[fx.ap_i[k]];
            int32_t v = a + (o >> 1);
            c[fx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
            a = o - a;                                  /* Freeverb: out = buf - in (o - v is a notch comb) */
            if (++fx.ap_i[k] >= REV_AP[k])
                fx.ap_i[k] = 0;
            c += REV_AP[k];
        }
        out[i] += a;
        side[i] += mulq15(sd, 22000);                 /* (x 0.67: as wide as it can be without the combs ringing) */
    }
}

/* SPRING (see the top), added to out */
static __attribute__((noinline)) void rev_spring(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k, s = (uint32_t)song.g[G_RSIZE];
    int32_t g = 19661 + (int32_t)s * 85;                /* the loop's gain: 0.6 .. 0.93 */
    int32_t kl = 26000 - song.g[G_RDAMP] * 160;         /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = rev_comb;
    int32_t *ap = rev_u.sp;
    if (!fx.sp_size)
        fx.sp_size = len;
    fx.sp_size += clamp(len - fx.sp_size, -256, 256);   /* SIZE glides (a sample a block at most) */
    L = fx.sp_size >> 8;
    fx.sp_ph += 2u * LFO_INC[24];                       /* the wobble: a slow sine, 1.5 samples deep */
    w = (fx.sp_size >> 1) + ((osc_sine(fx.sp_ph) * 3) >> 8);   /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = fx.sp_w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        fx.sp_lp += mulq15(r - fx.sp_lp, kl);
        o = fx.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - fx.sp_hp + fx.sp_he;                    /* the low cut, its step's remainder kept (as */
        fx.sp_he = o & 63;                              /* dc_block): no dead band to hold an offset in the loop */
        fx.sp_hp += o >> 6;
        x -= fx.sp_hp;
        p = ap[j];                                      /* the chain: ap[j + k], stage k's output 4 samples ago */
        ap[j] = x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - ap[j + k]) * SP_A;         /* towards 0, as the loop's gain: floors would feed */
            o = ap[j + k];                              /* the loop a little offset and noise for ever */
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        fx.sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & SP_MASK] * 2;
    }
}

/* HALL's helpers: m / 2^sh rounded towards zero (a product that feeds back must not floor: a loop of floors
 * holds an offset and a little noise for ever) */
static inline int32_t sh_tz(int32_t m, uint32_t sh) { return (m + ((m >> 31) & ((1 << sh) - 1))) >> sh; }
/* 2^-x, x Q16 (0 .. 16): Q15 (32768 at x = 0). 2^-x = 2^u / 2^(i + 1), u = 1 - frac(x); 2^u by a cubic (1e-4) */
static uint32_t exp2_neg(uint32_t x)
{
    uint32_t i = x >> 16, u = 32768u - ((x & 0xFFFFu) >> 1), p = 2579u;
    p = ((p * u) >> 15) + 7409u;
    p = ((p * u) >> 15) + 22780u;
    p = ((p * u) >> 15) + 32768u;
    return i < 15u ? p >> (i + 1u) : 0u;
}
/* the lines' gains for SIZE s: RT60 = 0.5 s * 2^(s / 30); a line of D samples (22.05 kHz) loses 60 dB in
 * RT60 * 22050 / D passes: g = 2^(-D * 3 log2(10) / (22050 * RT60)) = 2^(-D * 59.2 / 65536 * 2^(-s / 30)),
 * at most 0.99, over sqrt(7) (the matrix's rows have 7 entries of +-1: / sqrt(7) makes it orthogonal) */
static __attribute__((noinline)) void hall_gains(int32_t s)
{
    uint32_t q = exp2_neg((uint32_t)s * 65536u / 30u), k, g;
    for (k = 0; k < HL_NL; k++) {
        g = exp2_neg((HL_LEN[k] * 59u * q) >> 15);
        hl.g[k] = (int16_t)(((g > 32440u ? 32440u : g) * 6193u) >> 15);
    }
    hl.sz = (int16_t)(s + 1);
}

/* an allpass diffuser over x[0 .. n) from q (its next samples, no wrap): its state rounded towards zero */
static inline void hall_ap(int16_t *q, int32_t g, int32_t *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y = q[i], v = x[i] + sh_tz(y * g, 14);
        q[i] = (int16_t)clamp(v, -32768, 32767);
        x[i] = y - ((v * g + 8192) >> 14);
    }
}
/* a line's end over y[0 .. n) from b (no wrap; b[n] is still the line's: its mirror at the end): between b[i]
 * and b[i + 1], f / 256 of the way (rounded to the nearest: it stays between the two), times its gain g (Q14).
 * The gain's product: rounded to the nearest while the line is loud (lo 0: the tail decays as it should; a
 * truncation would make it die early, the sooner the quieter), towards zero once it is quiet (lo 1: the last
 * steps of the tail, which then ends in exact silence) */
static inline void hall_end(const int16_t *b, int32_t f, int16_t *y, uint32_t n, int32_t g, int lo)
{
    uint32_t i;
    if (lo)
        for (i = 0; i < n; i++)
            y[i] = (int16_t)sh_tz((b[i] + (((b[i + 1u] - b[i]) * f + 128) >> 8)) * g, 14);
    else
        for (i = 0; i < n; i++)
            y[i] = (int16_t)(((b[i] + (((b[i + 1u] - b[i]) * f + 128) >> 8)) * g + 8192) >> 14);
}
/* a line's end over y[0 .. n) from b (no wrap), through the low-pass (kd, Q15; the state *lp and its step's
 * remainder *le), times its gain g (rounded as hall_end's) */
static inline void hall_end_lp(const int16_t *b, int16_t *y, uint32_t n, int32_t g, int lo, int32_t *lp, int32_t *le,
                               int32_t kd)
{
    uint32_t i;
    int32_t l = *lp, r = *le, e;
    if (lo)
        for (i = 0; i < n; i++) {
            e = (b[i] - l) * kd + r, r = e & 32767, l += e >> 15;
            y[i] = (int16_t)sh_tz(l * g, 14);
        }
    else
        for (i = 0; i < n; i++) {
            e = (b[i] - l) * kd + r, r = e & 32767, l += e >> 15;
            y[i] = (int16_t)((l * g + 8192) >> 14);
        }
    *lp = l;
    *le = r;
}
/* the block's HL_NC samples of a circular buffer of len from index d in at most two runs, no wrap inside them:
 * body with s (the run's start in the block) and k (its length); d moves on */
#define HL_RUNS(d, len, k, s, body)                                                         \
    for (s = 0; s < HL_NC; s += k) {                                                        \
        k = (len) - (d) < HL_NC - s ? (len) - (d) : HL_NC - s;                              \
        body;                                                                               \
        d = (d) + k >= (len) ? 0u : (d) + k;                                                \
    }

/* HALL (see the top): a block of the send (n = CTL samples: audio.c) -> the mid added to out, the side into
 * rev_side. At half the rate (HL_NC samples) in passes over the block, each with few states (every line is
 * longer than a block: what the lines give in this block was written before it): the send, every other
 * sample, low cut, diffusers (hl.bx); the lines' ends at their drifting taps, low-passed, scaled (hl.y); the
 * outputs and the matrix; back into the lines; the outputs back to 44.1 kHz. */
#define HL_LFO ((uint32_t)(0.37 * CTL / FS * 4294967296.0))   /* the drift: 0.37 Hz (even lines), 0.74 Hz (odd) */
static __attribute__((noinline)) void rev_hall(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    int16_t *b = rev_comb, *a = rev_ap;
    uint32_t i, k, s, m, d;
    int32_t kd = HL_KD(song.g[G_RDAMP]);
    int32_t xp = hl.x, hp = hl.hp, he = hl.he, mp = hl.m, sp = hl.s;
    (void)n;
    if (hl.sz != song.g[G_RSIZE] + 1)
        hall_gains(song.g[G_RSIZE]);
    for (i = 0; i < HL_NC; i++) {
        int32_t x = (xp + 2 * rev_in[2u * i] + rev_in[2u * i + 1u]) >> 2, v;   /* every other sample, 1 2 1 */
        xp = rev_in[2u * i + 1u];
        x = mulq15(clamp(x, -262143, 262143), 2580);    /* (ROOM's level) */
        v = x - hp + he;                                /* the low cut, its step's remainder kept */
        he = v & 31;
        hp += v >> 5;
        hl.bx[i] = x - hp;
    }
    hl.x = xp, hl.hp = hp, hl.he = he;
    for (k = 0; k < HL_NAP; k++) {                      /* the diffusers */
        d = hl.d[k];
        HL_RUNS(d, HL_AP[k], m, s, hall_ap(a + d, HL_APG[k], hl.bx + s, m));
        hl.d[k] = (uint16_t)d;
        a += HL_AP[k];
    }
    hl.ph += HL_LFO;
    for (k = 0; k < HL_NL; k++) {                       /* the lines' ends */
        int32_t g = hl.g[k], f = 0, lo;
        if ((HL_LPM >> k) & 1u)
            d = hl.i[k];                                /* (low-passed: the line's end) */
        else {                                          /* (drifting: HL_MOD + 2 +- HL_MOD samples short of it) */
            uint32_t r = (uint32_t)(((HL_MOD + 2) << 8) +
                                    ((osc_sine(hl.ph * (1u + (k & 1u)) + k * 0x2C000000u) * HL_MOD) >> 7));
            f = (int32_t)(r & 255u);
            d = hl.i[k] + (r >> 8);
            d = d >= HL_LEN[k] ? d - HL_LEN[k] : d;
        }
        s = d + HL_LEN[k] / 2u;                         /* quiet? (two samples half a line apart) */
        s = s >= HL_LEN[k] ? s - HL_LEN[k] : s;
        lo = (b[d] < 0 ? -b[d] : b[d]) + (b[s] < 0 ? -b[s] : b[s]) < HL_LOUD;
        if ((HL_LPM >> k) & 1u) {
            HL_RUNS(d, HL_LEN[k], m, s, hall_end_lp(b + d, hl.y[k] + s, m, g, lo, &hl.lp[k], &hl.le[k], kd));
        } else {
            HL_RUNS(d, HL_LEN[k], m, s, hall_end(b + d, f, hl.y[k] + s, m, g, lo));
        }
        b += HL_LEN[k] + 1u;
    }
    for (i = 0; i < HL_NC; i++) {
        int32_t y0 = hl.y[0][i], y1 = hl.y[1][i], y2 = hl.y[2][i], y3 = hl.y[3][i];
        int32_t y4 = hl.y[4][i], y5 = hl.y[5][i], y6 = hl.y[6][i], y7 = hl.y[7][i], x = hl.bx[i];
        int32_t t, p0, p1, p2, p3, p4, p5, p6;
        hl.l[i] = y0 + y3 + y4 - y7;                    /* the outputs */
        hl.r[i] = y1 + y2 + y5 + y6;
        /* the matrix: C = [0 1; -1 Q], Q (7 x 7) = chi(j - i), the quadratic character mod 7 (+1 at distances
         * 1 2 4, -1 at 3 5 6): row 1 + i of Q y = 2 (the three at +1) - (the sum of the 7) + y_1+i */
        t = y1 + y2 + y3 + y4 + y5 + y6 + y7;
        p0 = y2 + y3 + y5, p1 = y3 + y4 + y6, p2 = y4 + y5 + y7, p3 = y5 + y6 + y1;
        p4 = y6 + y7 + y2, p5 = y7 + y1 + y3, p6 = y1 + y2 + y4;
        y0 += t;
        hl.y[0][i] = (int16_t)clamp(t + x, -32768, 32767);   /* (+ the send: signs spread over the rows) */
        hl.y[1][i] = (int16_t)clamp(2 * p0 + y1 - y0 + x, -32768, 32767);
        hl.y[2][i] = (int16_t)clamp(2 * p1 + y2 - y0 + x, -32768, 32767);
        hl.y[3][i] = (int16_t)clamp(2 * p2 + y3 - y0 + x, -32768, 32767);
        hl.y[4][i] = (int16_t)clamp(2 * p3 + y4 - y0 + x, -32768, 32767);
        hl.y[5][i] = (int16_t)clamp(2 * p4 + y5 - y0 + x, -32768, 32767);
        hl.y[6][i] = (int16_t)clamp(2 * p5 + y6 - y0 + x, -32768, 32767);
        hl.y[7][i] = (int16_t)clamp(2 * p6 + y7 - y0 - x, -32768, 32767);
    }
    b = rev_comb;
    for (k = 0; k < HL_NL; k++) {                       /* back into the lines (and the mirror after a wrap) */
        d = hl.i[k];
        HL_RUNS(d, HL_LEN[k], m, s, for (i = 0; i < m; i++) b[d + i] = hl.y[k][s + i]; if (!d) b[HL_LEN[k]] = b[0]);
        hl.i[k] = (uint16_t)d;
        b += HL_LEN[k] + 1u;
    }
    for (i = 0; i < HL_NC; i++) {                       /* back to 44.1 kHz, between samples */
        int32_t mm = (hl.l[i] + hl.r[i]) * HL_OUT, ss = (hl.l[i] - hl.r[i]) * HL_OUT;
        out[2u * i] += (mp + mm) >> 1;
        out[2u * i + 1u] += mm;
        rev_side[2u * i] = (sp + ss) >> 1;
        rev_side[2u * i + 1u] = ss;
        mp = mm;
        sp = ss;
    }
    hl.m = mp, hl.s = sp;
    hl.side = 1;
}

static int32_t wside[CTL];               /* (Blue Hare) the buses' side: + left, - right (mix_block) */
#include "fx_amb.c"                      /* (Blue Hare, 2026-10-17) amb-1's plate and echoes: PLATE, AMB (float) */

/* (Blue Hare, 2026-10-17) the patch's EQ (G_EQLO .. G_EQHI): four bands on the mix after MASTER, before the lows in
 * mono and the compressor, in float on the FPU (fastmath.h): a low shelf at 100 Hz, peaks at 400 Hz and 2.5 kHz (Q
 * 0.9), a high shelf at 8 kHz (shelf slope 1), -12 .. +12 dB each (R. Bristow-Johnson's cookbook biquads, transposed
 * direct form II). A band's gain glides to its knob by 0.25 dB a block (a turn does not click); a band at 0 dB is
 * passed by (its biquad is then the identity, its states 0: it starts again from silence, seamlessly) */
static const float EQ_HZ[4] = {100.0f, 400.0f, 2500.0f, 8000.0f};
static struct {
    float g[4];                          /* the gains now (dB) */
    float b0[4], b1[4], b2[4], a1[4], a2[4];
    float s[4][2][2];                    /* the states: band, channel, two */
} eq;
static void eq_coef(uint32_t k, float db)
{
    float A = fm_exp2f(db * (0.166096405f * 0.5f)), w = 2.0f * FM_PI * EQ_HZ[k] / (float)FS, c = fm_cosf(w),
          sn = fm_sinf(w), b0, b1, b2, a0, a1, a2;
    if (k == 0u || k == 3u) {                           /* shelves, S = 1: alpha = sin / 2 * sqrt(2) */
        float al = sn * 0.70710678f, r = 2.0f * fm_sqrtf(A) * al, ap = A + 1.0f, am = A - 1.0f;
        if (!k) {
            b0 = A * (ap - am * c + r); b1 = 2.0f * A * (am - ap * c); b2 = A * (ap - am * c - r);
            a0 = ap + am * c + r; a1 = -2.0f * (am + ap * c); a2 = ap + am * c - r;
        } else {
            b0 = A * (ap + am * c + r); b1 = -2.0f * A * (am + ap * c); b2 = A * (ap + am * c - r);
            a0 = ap - am * c + r; a1 = 2.0f * (am - ap * c); a2 = ap - am * c - r;
        }
    } else {                                            /* peaks, Q 0.9 */
        float al = sn * (1.0f / (2.0f * 0.9f));
        b0 = 1.0f + al * A; b1 = -2.0f * c; b2 = 1.0f - al * A;
        a0 = 1.0f + al / A; a1 = -2.0f * c; a2 = 1.0f - al / A;
    }
    a0 = 1.0f / a0;
    eq.b0[k] = b0 * a0; eq.b1[k] = b1 * a0; eq.b2[k] = b2 * a0; eq.a1[k] = a1 * a0; eq.a2[k] = a2 * a0;
}
static __attribute__((noinline)) void eq_block(int32_t *out, uint32_t n)
{
    uint32_t k, i, ch;
    if (!BLUEHARE_FX_EQ)                                /* (the EQ not in this build) */
        return;
    for (k = 0; k < 4u; k++) {
        float t = (float)clamp(song.g[G_EQLO + k], -12, 12), d = t - eq.g[k];
        if (d != 0.0f) {
            eq.g[k] += d > 0.25f ? 0.25f : d < -0.25f ? -0.25f : d;
            eq_coef(k, eq.g[k]);
            if (eq.g[k] == 0.0f)
                eq.s[k][0][0] = eq.s[k][0][1] = eq.s[k][1][0] = eq.s[k][1][1] = 0.0f;
        }
        if (eq.g[k] == 0.0f)
            continue;
        for (ch = 0; ch < 2u; ch++) {
            const float b0 = eq.b0[k], b1 = eq.b1[k], b2 = eq.b2[k], a1 = eq.a1[k], a2 = eq.a2[k];
            float s1 = eq.s[k][ch][0], s2 = eq.s[k][ch][1];
            int32_t *o = out + ch;
            for (i = 0; i < n; i++, o += 2) {
                float x = (float)*o, y = b0 * x + s1;
                s1 = b1 * x - a1 * y + s2;
                s2 = b2 * x - a2 * y;
                *o = (int32_t)fm_clampf(y, -8.0e6f, 8.0e6f);
            }
            eq.s[k][ch][0] = fm_flush(s1);
            eq.s[k][ch][1] = fm_flush(s2);
        }
    }
}


#ifndef BLUEHARE_NATIVE
/* (Blue Hare, 2026-10) CONCERT on the device (G_RTYPE 4; the native image has its own, floating point, fx_native.c):
 * HALL grown into the memory four tracks freed. At half the rate as HALL (22.05 kHz): the send (two samples
 * averaged, ROOM's level) -> a low cut (~110 Hz) -> a pre-delay (12 ms) -> 4 allpass diffusers -> 16 lines (26 .. 67
 * ms, mutually prime: twice HALL's lines, twice as long: a denser, smoother, longer tail), each read through its own
 * slow sine (+-CN_MOD samples, between samples: no line rings at its length), low-passed (DAMP, LOW CUT's poles) and
 * scaled (SIZE: RT60 0.5 s .. 9 s, as HALL's; rounded to the nearest while loud, towards zero once quiet: the tail
 * decays as it should and ends in exact silence), then mixed by a 16 x 16 Hadamard matrix
 * (a fast Walsh transform, / 4: lossless) and written back with the send. Out: the even lines left, the odd right;
 * the mid to the wet bus, the side to rev_side, as HALL's. In the pool: ~34 KB */
#define CN_NL 16u
#define CN_NAP 4u
#define CN_MOD 3                         /* the taps' swing, samples */
#define CN_PRE 265u                      /* the pre-delay: 12 ms at 22.05 kHz */
#define CN_LOUD 64                       /* a line's sample this loud rounds its gain to the nearest (HALL's way) */
static const uint16_t CN_LEN[CN_NL] = {577, 641, 691, 751, 809, 863, 937, 997, 1061, 1117, 1181, 1237, 1301, 1361, 1423, 1489};
static const uint16_t CN_AP[CN_NAP] = {71, 53, 191, 139};
#define CN_SUM (577 + 641 + 691 + 751 + 809 + 863 + 937 + 997 + 1061 + 1117 + 1181 + 1237 + 1301 + 1361 + 1423 + 1489)
typedef struct {
    int16_t buf[CN_SUM];                 /* the lines, one after the other */
    int16_t ap[71 + 53 + 191 + 139];     /* the diffusers */
    int16_t pre[512];                    /* the pre-delay */
    uint16_t i[CN_NL], d[CN_NAP], pw;    /* the lines' write places, the diffusers', the pre-delay's */
    int32_t lp[CN_NL];                   /* the low-passes (Q8) */
    int16_t g[CN_NL], sz;                /* the lines' gains (Q15), for SIZE sz - 1 (0: none yet) */
    int32_t x, hp, he, m, s;             /* the send's last sample, its low cut; the last mid and side out */
    uint32_t ph;                         /* the drift */
} cn_t;
static union {                           /* (2026-10-17) CONCERT's state or PLATE's lines: one reverb runs at a time */
    cn_t c;
#if BLUEHARE_FX_PLATE
    float pl[AMB_PL_HALF];
#endif
} rv_u __attribute__((section(".pool")));
#define cn (rv_u.c)
#define PL_MEM (rv_u.pl)

static inline int32_t tl_pole(int32_t *s, int32_t x, int32_t k);   /* (LOW CUT's pole, below) */
static __attribute__((noinline)) void concert_gains(int32_t sz)   /* (as hall_gains, no matrix factor: the / 4 is its) */
{
    uint32_t q = exp2_neg((uint32_t)sz * 65536u / 30u), k, g;
    for (k = 0; k < CN_NL; k++) {
        g = exp2_neg((CN_LEN[k] * 59u * q) >> 15);
        cn.g[k] = (int16_t)(g > 32440u ? 32440u : g);
    }
    cn.sz = (int16_t)(sz + 1);
}
static void concert_clear(void)
{
    memset(&cn, 0, sizeof cn);
}
static __attribute__((noinline)) void rev_concert(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k, off;
    int32_t kd = HL_KD(song.g[G_RDAMP]), mp = cn.m, sp = cn.s;
    (void)n;
    if (cn.sz != song.g[G_RSIZE] + 1)
        concert_gains(song.g[G_RSIZE]);
    cn.ph += HL_LFO;
    for (i = 0; i < HL_NC; i++) {
        int32_t x = (rev_in[2u * i] + rev_in[2u * i + 1u]) >> 1, v, y[CN_NL], l, r, a, b;
        uint32_t ao = 0;
        x = mulq15(clamp(x, -262143, 262143), 2580);    /* (ROOM's level) */
        v = x - cn.hp + cn.he;                          /* the low cut, its step's remainder kept */
        cn.he = v & 31;
        cn.hp += v >> 5;
        x = x - cn.hp;
        cn.pre[cn.pw & 511u] = (int16_t)clamp(x, -32768, 32767);
        x = cn.pre[(cn.pw - CN_PRE) & 511u];
        cn.pw++;
        for (k = 0; k < CN_NAP; k++) {                  /* the diffusers (g 0.7) */
            int16_t *q = cn.ap + ao + cn.d[k];
            int32_t yy = *q, w = x + sh_tz(yy * 11469, 14);
            *q = (int16_t)clamp(w, -32768, 32767);
            x = yy - ((w * 11469 + 8192) >> 14);
            cn.d[k] = (uint16_t)(cn.d[k] + 1u >= CN_AP[k] ? 0u : cn.d[k] + 1u);
            ao += CN_AP[k];
        }
        for (k = 0, off = 0; k < CN_NL; k++) {          /* the lines' ends: drifting, low-passed, scaled */
            uint32_t len = CN_LEN[k], r8 = (uint32_t)(((CN_MOD + 2) << 8) +
                                                     ((osc_sine(cn.ph * (1u + (k & 3u)) + k * 0x1B000000u) * CN_MOD) >> 7));
            uint32_t p0 = cn.i[k] + (r8 >> 8), p1;
            int32_t f = (int32_t)(r8 & 255u), z;
            p0 = p0 >= len ? p0 - len : p0;
            p1 = p0 + 1u >= len ? 0u : p0 + 1u;
            z = cn.buf[off + p0] + (((cn.buf[off + p1] - cn.buf[off + p0]) * f + 128) >> 8);
            z = tl_pole(&cn.lp[k], z, kd);
            y[k] = z >= CN_LOUD || z <= -CN_LOUD ? (z * cn.g[k] + 16384) >> 15   /* (loud: to the nearest; */
                                                 : sh_tz(z * cn.g[k], 15);               /* quiet: towards zero) */
            off += len;
        }
        l = y[0] + y[2] + y[4] + y[6] - y[8] - y[10] + y[12] - y[14];   /* the outputs (signs spread) */
        r = y[1] + y[3] - y[5] + y[7] + y[9] - y[11] + y[13] + y[15];
        for (k = 1; k < CN_NL; k <<= 1)                 /* the matrix: Hadamard 16, a fast Walsh transform */
            for (a = 0; a < (int32_t)CN_NL; a += (int32_t)(2u * k))
                for (b = a; b < a + (int32_t)k; b++) {
                    int32_t u = y[b], w = y[b + (int32_t)k];
                    y[b] = u + w;
                    y[b + (int32_t)k] = u - w;
                }
        for (k = 0, off = 0; k < CN_NL; k++) {          /* back into the lines, / 4, with the send */
            cn.buf[off + cn.i[k]] = (int16_t)clamp(sh_tz(y[k], 2) + ((k & 1u) ? -x : x), -32768, 32767);
            cn.i[k] = (uint16_t)(cn.i[k] + 1u >= CN_LEN[k] ? 0u : cn.i[k] + 1u);
            off += CN_LEN[k];
        }
        {                                               /* back to 44.1 kHz, between samples */
            int32_t mm = ((l + r) * 3) >> 1, ss = ((l - r) * 3) >> 1;   /* (x 1.5: HALL's level, tests/baseline/concert_test.c) */
            out[2u * i] += (mp + mm) >> 1;
            out[2u * i + 1u] += mm;
            rev_side[2u * i] = (sp + ss) >> 1;
            rev_side[2u * i + 1u] = ss;
            mp = mm;
            sp = ss;
        }
    }
    cn.m = mp, cn.s = sp;
    hl.side = 1;
}
#endif

#ifdef BLUEHARE_NATIVE
#include "fx_native.c"                                   /* (Blue Hare) the native image's reverbs: CONCERT, CAVERN, SHIMMER */
#if BLUEHARE_FX_PLATE
static float pl_mem_native[AMB_PL_HALF];
#endif
#define PL_MEM pl_mem_native
#define RT_LAST 7                                        /* (5 PLATE; 6 CAVERN, 7 SHIMMER: fx_native.c's 5, 6) */
#else
#define RT_LAST (4 + BLUEHARE_FX_PLATE)                  /* (2026-10: the device's CONCERT; 10-17: PLATE, a component) */
#endif
#define RT_PLATE 5
#define RN_OF(rt) ((rt) == 4 ? 4u : (uint32_t)(rt) - 1u)   /* (native: fx_native.c's numbering of CONCERT CAVERN SHIMMER) */

/* (Blue Hare) GATED (G_RTYPE 3): HALL, dense, cut off sharply: a hit in the send opens the gate, it stays open for
 * SIZE's hold (~60 .. ~500 ms) after the last one, then shuts over ~10 ms (the 80s gated snare; LINNKIT's, WAR's).
 * DAMP as HALL's. Its stereo difference (rev_side) gated with it */
#define GT_OPEN 1500                     /* the send's block peak that opens the gate */
static struct { int32_t g; uint32_t hold; } gt;
static __attribute__((noinline)) void rev_gated(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    static int32_t t[CTL];
    uint32_t i, hold = (60u + (uint32_t)song.g[G_RSIZE] * 35u / 10u) * (FS / 1000u);
    int32_t pk = 0, g0 = gt.g, g1;
    for (i = 0; i < n; i++) {
        int32_t a = rev_in[i] < 0 ? -rev_in[i] : rev_in[i];
        pk = a > pk ? a : pk;
        t[i] = 0;
    }
    if (pk > GT_OPEN)
        gt.hold = hold;                         /* (a hit: open for the hold from now) */
    gt.hold = gt.hold > n ? gt.hold - n : 0u;
    g1 = gt.hold ? (g0 + 16384 > 32767 ? 32767 : g0 + 16384) : g0 - (g0 >> 4) - (g0 > 0);   /* (~1 ms up, ~10 ms down) */
    rev_hall(rev_in, t, n);
    for (i = 0; i < n; i++) {
        int32_t g = g0 + (g1 - g0) * (int32_t)(i + 1u) / (int32_t)n;
        out[i] += mulq16(t[i], (uint32_t)g << 1);       /* (mulq16: the tail may be far above Q15) */
        rev_side[i] = mulq16(rev_side[i], (uint32_t)g << 1);
    }
    gt.g = g1;
}

/* the reverb's buffers and states to silence (the model changed) */
static void rev_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        rev_comb[i] = 0;
    for (i = 0; i < sizeof rev_u.ap / 2u; i++)          /* (int16: 997 of them, an odd count the int32 view misses one of) */
        rev_u.ap[i] = 0;
    for (i = 0; i < 4u; i++)
        fx.comb_lp[i] = 0;
    for (i = 0; i < HL_NL; i++)
        hl.lp[i] = hl.le[i] = 0;
    fx.sp_lp = fx.sp_hp = fx.sp_he = 0;
    hl.x = hl.m = hl.s = hl.hp = hl.he = 0;
    gt.g = 0;                                           /* (Blue Hare: GATED shut) */
    gt.hold = 0;
#ifdef BLUEHARE_NATIVE
    rev_native_clear();
#else
    concert_clear();
#endif
}

static int32_t part_buf[CTL];                            /* a part's block (mix_part); the fade of a model change */

/* the model changed (to rt): the old one's block fades out (HALL's stereo difference with it), its buffers are
 * cleared, the new one starts from silence */
static __attribute__((noinline)) void rev_switch(const int32_t *rev_in, int32_t *wet, uint32_t n, int32_t rt)
{
    static int32_t ts[CTL];                         /* (ROOM's side, faded with it) */
    int32_t *t = part_buf, g = 65536, d = 65536 / (int32_t)n;
    uint32_t i;
    for (i = 0; i < n; i++)
        t[i] = ts[i] = 0;
    if (fx.rtype == RT_PLATE)
        rev_plate(rev_in, t, n);
    else
#ifdef BLUEHARE_NATIVE
    if (fx.rtype >= 4u)
        rev_native(RN_OF(fx.rtype), rev_in, t, ts, n);
    else
#else
    if (fx.rtype == 4u)
        rev_concert(rev_in, t, n);
    else
#endif
    if (fx.rtype == 3u)
        rev_gated(rev_in, t, n);
    else if (fx.rtype == 2u)
        rev_hall(rev_in, t, n);
    else if (fx.rtype)
        rev_spring(rev_in, t, n);
    else
        rev_room(rev_in, t, ts, n);
    for (i = 0; i < n; i++, g -= d) {
        wet[i] += mulq16(t[i], (uint32_t)g);
        wside[i] += mulq16(ts[i], (uint32_t)g);
        rev_side[i] = mulq16(rev_side[i], (uint32_t)g);   /* (HALL only: hl.side set) */
    }
    rev_clear();
    fx.rtype = (uint8_t)rt;
#if BLUEHARE_FX_PLATE
    if (rt == RT_PLATE)                             /* (its lines over CONCERT's memory, cleared: from silence) */
        amb_plate_init(&amb_dp, PL_MEM, AMB_NUM_HALF, AMB_PRE_HALF), amb_pm = amb_ps = 0.0f;
#endif
}

/* TAPE / WORN (DELAY TYPE 1, 2): the delay bus as a tape echo, its output added to wet (see the top). dl the delay
 * (samples), fb col dmix the DLY page's as the clean delay takes them. Everything that differs from CLEAN glides,
 * so a type changed while it rings does not click, and going to CLEAN the tape ends exactly as CLEAN reads and
 * writes (fx_buses then runs CLEAN's own loop) */
static __attribute__((noinline)) void dly_tape(const int32_t *dly_in, int32_t *wet, uint32_t n, uint32_t dl, int32_t fb,
                                               int32_t col, int32_t dmix)
{
    static const struct { int16_t wow, flt, hp, drive, dark; } TP[2] = {
        {6000, 600, 7, 256, 4},                       /* TAPE: +-23 / 2.3 samples (~0.3 % pitch), ~55 Hz, x1 */
        {18000, 1500, 5, 384, 3},                     /* WORN: three and two and a half times that, ~220 Hz, x1.5, COLR 3/4 */
    };
    const uint32_t w = fx.dtype == 2u ? 1u : 0u, hs = (uint32_t)TP[w].hp, out = fx.tp_out;
    const int32_t c = out ? col : col * TP[w].dark >> 2;
    int32_t dd, d0 = fx.tp_d8, step;
    uint32_t i, lim = (DLY_LEN - 4u) << 8;
    /* a block at a time, to the type's (to CLEAN's going there): the wobble's depths (~20 .. 50 ms), the drive, the
     * feedback (FDBK 120: ~1.06 a pass, the saturation bounds it), the low cut's share */
    fx.tp_wd += clamp((out ? 0 : TP[w].wow) - fx.tp_wd, -256, 256);
    fx.tp_fd += clamp((out ? 0 : TP[w].flt) - fx.tp_fd, -32, 32);
    fx.tp_drv += clamp((out ? 256 : TP[w].drive) - fx.tp_drv, -4, 4);
    fx.tp_fbk += clamp((out ? fb : fb * 290 / 230) - fx.tp_fbk, -256, 256);
    fx.tp_hpm += clamp((out ? 0 : 32768) - fx.tp_hpm, -1024, 1024);
    /* TIME glides, sample by sample (the echoes bend; at most +-25 % in pitch, the last of it at once) */
    dd = (int32_t)(dl << 8) - d0;
    dd = dd > -256 && dd < 256 ? dd : clamp(dd >> 7, -(8 << 8), 8 << 8);
    fx.tp_d8 = d0 + dd;
    step = dd / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t r, f, a, b, x, lo;
        uint32_t ri;
        fx.tp_wow += 48700u;                          /* ~0.5 Hz */
        fx.tp_flt += 584000u;                         /* ~6 Hz */
        r = (i + 1u == n ? fx.tp_d8 : d0 + step * (int32_t)(i + 1u)) +
            ((osc_sine(fx.tp_wow) * fx.tp_wd) >> 15) + ((osc_sine(fx.tp_flt) * fx.tp_fd) >> 15);
        r = clamp(r, 16 << 8, (int32_t)lim);
        ri = (uint32_t)r >> 8;
        f = r & 255;
        a = dly_buf[(fx.dly_w - ri) & (DLY_LEN - 1u)];
        b = dly_buf[(fx.dly_w - ri - 1u) & (DLY_LEN - 1u)];
        x = a + (((b - a) * f) >> 8);                 /* (the read between samples) */
        fx.dly_lp += mulq15(x - fx.dly_lp, c);        /* COLR: the loop darkens */
        fx.tp_hp8 += ((fx.dly_lp << 8) - fx.tp_hp8) >> hs;   /* .. and thins (Q8: no offset left by its rounding) */
        lo = mulq15(fx.tp_hp8 >> 8, fx.tp_hpm);
        lo = (dly_in[i] >> 1) + mulq15(fx.dly_lp - lo, fx.tp_fbk);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] = (int16_t)(out && fx.tp_drv == 256 && !fx.tp_hpm
            ? clamp(lo, -32768, 32767) : softclip((lo * fx.tp_drv) >> 8));   /* (CLEAN's write once all of it is) */
        fx.dly_w++;
        wet[i] += mulq15(x << 1, dmix);
    }
}

/* process the three buses for one block; sends in, the mid (wet: to both sides) and the side (wside) out */
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet,
                     uint32_t n)
{
    uint32_t i, dl = delay_samples(), dt = (uint32_t)song.g[G_DTYPE], ping = dt == 3u, dual = dt == 4u, dl2;
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240, fb2;
    int32_t dmix = song.g[G_DMIX] * 258;
    int32_t cdepth = song.g[G_CDEPTH] * 6, rt;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
    dt = dt <= 2u ? dt : 0u;                          /* (PING: CLEAN's loop, two reads) */
    if ((ping || dual) && dl > (DLY_LEN - 4u) / 2u)
        dl = (DLY_LEN - 4u) / 2u;                     /* (PING's right one at twice the time, DUAL's halves: 0.74 s) */
    dl2 = dual ? dl * 3u / 4u : dl * 2u;
    fb2 = mulq15(fb, fb);                             /* PING's loop: a pair of echoes a pass */
    if (dt && !fx.dtype) {                            /* CLEAN -> TAPE / WORN: the read starts where CLEAN's is */
        fx.tp_d8 = (int32_t)(dl << 8);
        fx.tp_hp8 = fx.dly_lp << 8;
        fx.tp_wd = fx.tp_fd = fx.tp_hpm = 0;
        fx.tp_drv = 256;
        fx.tp_fbk = fb;
        fx.tp_wow = fx.tp_flt = 0;
    }
    if ((BLUEHARE_FX_AMB && song.g[G_DTYPE] == (int16_t)DT_AMB) != fx.amb_on && !fx.dclr)
        fx.dclr = 1;                                  /* AMB on or off: the line cleared first, the delay silent */
    if (fx.dclr) {
        uint32_t a = (fx.dclr - 1u) * DCLR_N;
        memset(&dly_buf[a], 0, DCLR_N * sizeof dly_buf[0]);
        if (a + DCLR_N < DLY_LEN) {
            fx.dclr++;
        } else {
            fx.dclr = 0;
            fx.amb_on = BLUEHARE_FX_AMB && song.g[G_DTYPE] == (int16_t)DT_AMB;
            fx.dtype = fx.tp_out = 0;
            fx.dly_lp = fx.dly_lp2 = 0;
            fx.dly_w = 0;
            if (fx.amb_on)
                amb_echo_init(&amb_de, dly_buf, AMB_TAPE_DEV, dly_buf + AMB_TAPE_DEV, 0);
        }
    }
    if (fx.amb_on || fx.dclr) {
        dt = 0;
    } else if (dt) {
        fx.dtype = (uint8_t)dt;
        fx.tp_out = 0;
    } else if (fx.dtype) {                            /* .. -> CLEAN: the tape until its read is CLEAN's, then CLEAN */
        fx.tp_out = 1;
        if (!fx.tp_wd && !fx.tp_fd && fx.tp_d8 == (int32_t)(dl << 8) && fx.tp_drv == 256 && fx.tp_fbk == fb && !fx.tp_hpm)
            fx.dtype = fx.tp_out = 0;
    }
    for (i = 0; i < n; i++) {
        int32_t y = 0, x, r, ca, cb, sd, sw;
        /* chorus: modulated short delay, 5..15 ms; the left read, and the right one half a sweep away */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        fx.cho_ph += cinc;
        sw = osc_sine(fx.cho_ph);
        r = (400 << 8) + ((sw + 32768) * cdepth >> 8);   /* Q8 delay: read between samples */
        {
            uint32_t ri = (uint32_t)r >> 8;
            int32_t f = r & 255, c0 = cho_buf[(fx.cho_w - ri) & (CHO_LEN - 1u)];
            int32_t c1 = cho_buf[(fx.cho_w - ri - 1u) & (CHO_LEN - 1u)];
            ca = c0 + (((c1 - c0) * f) >> 8);
        }
        r = (400 << 8) + ((32768 - sw) * cdepth >> 8);    /* (half a sweep away: the sine's other half) */
        {
            uint32_t ri = (uint32_t)r >> 8;
            int32_t f = r & 255, c0 = cho_buf[(fx.cho_w - ri) & (CHO_LEN - 1u)];
            int32_t c1 = cho_buf[(fx.cho_w - ri - 1u) & (CHO_LEN - 1u)];
            cb = c0 + (((c1 - c0) * f) >> 8);
        }
        y += ca + cb;                                 /* (left 2 ca, right 2 cb: each as loud as the mono one was) */
        sd = ca - cb;
        fx.cho_w++;
        if (fx.dtype || fx.amb_on || fx.dclr) {       /* TAPE, WORN, AMB: after the loop (the same line) */
            wet[i] = y;
            wside[i] = sd;
            continue;
        }
        if (ping) {                                   /* PING: left at dl, right at 2 dl (FDBK's share), round */
            int32_t xl = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)], xr = dly_buf[(fx.dly_w - dl2) & (DLY_LEN - 1u)];
            int32_t el, er;
            fx.dly_lp += mulq15(xr - fx.dly_lp, col);
            dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
                (int16_t)clamp((dly_in[i] >> 1) + mulq15(fx.dly_lp, fb2), -32768, 32767);
            fx.dly_w++;
            el = mulq15(xl << 1, dmix);
            er = mulq15(mulq15(xr, fb) << 1, dmix);
            wet[i] = y + ((el + er) >> 1);
            wside[i] = sd + ((el - er) >> 1);
            continue;
        }
        if (dual) {                                   /* DUAL: two lines, the left at dl, the right at 3/4 of it */
            const uint32_t H = DLY_LEN / 2u;
            int32_t xl = dly_buf[(fx.dly_w - dl) & (H - 1u)], xr = dly_buf[H + ((fx.dly_w - dl2) & (H - 1u))];
            int32_t el, er, ml, mr;
            fx.dly_lp += mulq15(xl - fx.dly_lp, col);
            fx.dly_lp2 += mulq15(xr - fx.dly_lp2, col);
            ml = fx.dly_lp - (fx.dly_lp >> 2) + (fx.dly_lp2 >> 2);   /* (3/4 its own, 1/4 the other's) */
            mr = fx.dly_lp2 - (fx.dly_lp2 >> 2) + (fx.dly_lp >> 2);
            dly_buf[fx.dly_w & (H - 1u)] = (int16_t)clamp((dly_in[i] >> 1) + mulq15(ml, fb), -32768, 32767);
            dly_buf[H + (fx.dly_w & (H - 1u))] = (int16_t)clamp((dly_in[i] >> 1) + mulq15(mr, fb), -32768, 32767);
            fx.dly_w++;
            el = mulq15(xl << 1, dmix);
            er = mulq15(xr << 1, dmix);
            wet[i] = y + ((el + er) >> 1);
            wside[i] = sd + ((el - er) >> 1);
            continue;
        }
        /* delay with a low-passed feedback */
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
            (int16_t)clamp((dly_in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        y += mulq15(x << 1, dmix);
        wet[i] = y;
        wside[i] = sd;
    }
    if (fx.amb_on && !fx.dclr)
        dly_amb(dly_in, (int32_t *)rev_in, wet, n);   /* (its echoes feed the reverb's send: amb-1's) */
    else if (fx.dtype)
        dly_tape(dly_in, wet, n, dl, fb, col, dmix);
    rt = song.g[G_RTYPE];
    if (rt == RT_PLATE && !BLUEHARE_FX_PLATE)
        rt = 2;                                       /* (PLATE not in this build: HALL) */
    rt = rt >= 1 && rt <= RT_LAST ? rt : 0;
    hl.side = 0;
    if (rt != fx.rtype)
        rev_switch(rev_in, wet, n, rt);
    else if (rt == RT_PLATE)
        rev_plate(rev_in, wet, n);
#ifdef BLUEHARE_NATIVE
    else if (rt >= 4)
        rev_native(RN_OF(rt), rev_in, wet, wside, n);
#else
    else if (rt == 4)
        rev_concert(rev_in, wet, n);
#endif
    else if (rt == 3)
        rev_gated(rev_in, wet, n);
    else if (rt == 2)
        rev_hall(rev_in, wet, n);
    else if (rt)
        rev_spring(rev_in, wet, n);
    else
        rev_room(rev_in, wet, wside, n);
}

/* (Blue Hare) the delay's and the reverb's sends without their lows: two one-pole high-passes each (~220 Hz, 12
 * dB/oct), so the bass stays in the dry mix and out of the tails. A step is the difference / 32, floored, and at least
 * 1 up when the difference is 1 .. 31 (sl_step): the state reaches its input exactly (silence in, silence out: a
 * floored step alone would stop up to 31 below it, an offset into the tails), cheaper than master_out's error feedback */
static inline int32_t sl_step(int32_t e) { return (e >> 5) + ((uint32_t)(e - 1) < 31u); }
static __attribute__((noinline)) void send_lowcut(int32_t *d, int32_t *r, uint32_t n)
{
    uint32_t i;
    int32_t a = slc[0], b = slc[1], c = slc[2], e = slc[3];
    for (i = 0; i < n; i++) {
        int32_t x = d[i], y = r[i];
        a += sl_step(x - a);
        x -= a;
        b += sl_step(x - b);
        d[i] = x - b;
        c += sl_step(y - c);
        y -= c;
        e += sl_step(y - e);
        r[i] = y - e;
    }
    slc[0] = a; slc[1] = b; slc[2] = c; slc[3] = e;
}

/* after the buses: HALL's stereo difference to the left and from the right of the dry mix (the wet bus adds the
 * same to both) */
static __attribute__((noinline)) void rev_side_mix(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        l[i] += rev_side[i];
        r[i] -= rev_side[i];
    }
}

/* one block of the whole mix (shared with hostsim.c): events -> each part (with its modulation matrix)
 * -> dist -> level / pan / sends -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL], wet[CTL], mix_l[CTL], mix_r[CTL];

static void track_lowcut(uint32_t k, int32_t *b, uint32_t n);   /* (Blue Hare: LOW CUT, below) */
static void track_filter(uint32_t k, int32_t *b, uint32_t n);   /* (Blue Hare: the layer's filter, below) */

/* SPREAD (#148): a POLY or UNISON part whose SPRD is above 0. Its voices start left and right of PAN in turn (voice.c
 * voice_start; UNISON's detuned voices alternate), at PAN -/+ SPRD / 2 (SPRD 127: hard left and right), with the pan
 * law PAN has (the far side down, the near one as it is). track_render gives every voice (b, as always) and the left
 * ones (spread_buf); here d = left - right, the side signal, so for a pair of pans L = b (gla + glb) / 2 + d (gla - glb)
 * / 2 (R alike): the mono sum is b's, the level of a centre pan less by SPRD / 256 at most. b goes through DIST and
 * the FX layer's mute key as always; d gets the mute's ramp (perf_mute_sd) and passes DIST by (the spread of a distorted chord is its voices' clean difference). The sends
 * take b, mono, as before. SPRD 0, MONO or LEGATO: mix_part's own loop, bit for bit as before. The cost: a block's
 * clear and sum of spread_buf, the side and a wider mix loop, per part; per voice none (it picks its buffer) */
static __attribute__((noinline)) void mix_spread(track_t *t, int32_t *b, uint32_t n)
{
    int32_t *d = spread_buf;
    uint32_t i;
    int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] & 127], pan = t->p[P_PAN], s = (t->p[P_SPRD] + 1) >> 1;
    if (lay_wg[(uint32_t)(t - trk) % NTRK] < 32767)     /* (the layers' crossfade on WHEEL) */
        lvl = (lvl * lay_wg[(uint32_t)(t - trk) % NTRK]) >> 15;
    if (!s)
        s = 32;                                         /* (PAN MODE RND at SPRD 0: half the width) */
    int32_t pa = clamp(pan - s, -64, 63), pb = clamp(pan + s, -64, 63);
    int32_t gla = 4096 - (pa > 0 ? pa * 64 : 0), gra = 4096 + (pa < 0 ? pa * 64 : 0);
    int32_t glb = 4096 - (pb > 0 ? pb * 64 : 0), grb = 4096 + (pb < 0 ? pb * 64 : 0);
    int32_t glm = (gla + glb) >> 1, grm = (gra + grb) >> 1, gld = (gla - glb) >> 1, grd = (gra - grb) >> 1;
    int32_t c = t->p[P_CHOR] * 258, dl = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak;
    int32_t xmax = c > dl ? c : dl;
    xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);
    for (i = 0; i < n; i++)
        d[i] = (d[i] << 1) - b[i];                      /* left - right */
    track_dist(t, b, n);
    if (trk_lowcut[(t - trk) % NTRK])
        track_lowcut((uint32_t)(t - trk) % NTRK, b, n);   /* (Blue Hare; the side d passes it, as it passes DIST) */
    if (lay.ftype[(t - trk) % NTRK])
        track_filter((uint32_t)(t - trk) % NTRK, b, n);   /* (Blue Hare: the layer's filter; the side passes it too) */
    if ((pf.mute >> (t - trk)) & 1u)
        perf_mute_sd((uint32_t)(t - trk), b, d, n);
    for (i = 0; i < n; i++) {
        int32_t x = ((b[i] >> 2) * lvl) >> 10, a = x < 0 ? -x : x, y = ((d[i] >> 2) * lvl) >> 10;
        int32_t xs = clamp(x, -xmax, xmax);
        if (a > pk)
            pk = a;
        if (c)
            send_c[i] += mulq15(xs, c);
        if (dl)
            send_d[i] += mulq15(xs, dl);
        if (r)
            send_r[i] += mulq15(xs, r);
        mix_l[i] += ((x * glm) >> 12) + ((y * gld) >> 12);
        mix_r[i] += ((x * grm) >> 12) + ((y * grd) >> 12);
    }
    t->peak = pk;
}

/* (Blue Hare) LOW CUT (core.h trk_lowcut): two one-pole high-passes at TL_HZ[] on the track's block, after DIST (so the
 * sends lose the lows too). The states are Q8 and step by the difference / 256 * k / 128, at least one
 * unit while they differ (tl_pole), so they reach their input exactly: silence in, silence out */
static const int16_t TL_K[TL_N] = {0, 186, 279, 371, 555, 739, 1101, 1458};   /* Q15: 1 - e^(-2 pi f / 44100) */
static struct { int32_t s1, s2, f1, f2; } tmix[NTRK];   /* LOW CUT's poles (Q8); the layer filter's integrators */
static inline int32_t tl_pole(int32_t *s, int32_t x, int32_t k)   /* the one-pole low-pass of x (k Q15), its state Q8 */
{
    int32_t d = (x << 8) - *s, st = (d >> 8) * k >> 7;
    *s += st ? st : (d > 0) - (d < 0);
    return *s >> 8;
}
static __attribute__((noinline)) void track_lowcut(uint32_t k, int32_t *b, uint32_t n)
{
    int32_t kk = TL_K[trk_lowcut[k] % TL_N], s1 = tmix[k].s1, s2 = tmix[k].s2;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t hp = b[i] - tl_pole(&s1, b[i], kk);
        b[i] = hp - tl_pole(&s2, hp, kk);
    }
    tmix[k].s1 = s1;
    tmix[k].s2 = s2;
}

/* ((Blue Hare) WIDTH, the Haas effect, removed 2026-10-16: inaudible) */

/* (Blue Hare, 2026-10-16) the layer's filter (core.h lay.ftype): a 12 dB trapezoidal state-variable filter (dsp.c's,
 * tsvf), low-, band- or high-pass at lay.fcut (CUTOFF_HZ) with lay.fres, on the track's block after DIST and LOW CUT.
 * The block can be several voices loud: it is filtered at a quarter of its size (the integrators held to +-150000) */
static __attribute__((noinline)) void track_filter(uint32_t k, int32_t *b, uint32_t n)
{
    tsvf_t c;
    uint32_t i, ty = lay.ftype[k];
    int32_t kk = 8192 - lay.fres[k] * 7600 / 127, f1 = tmix[k].f1, f2 = tmix[k].f2;
    tsvf_coef_k(&c, lay.fcut[k] << 8, kk);
    for (i = 0; i < n; i++) {
        int32_t x = b[i] >> 2, v3 = x - f2, v1 = (c.a1 * f1 + c.a2 * v3) >> 13, v2 = f2 + ((c.a2 * f1 + c.a3 * v3) >> 13), y;
        f1 = clamp(2 * v1 - f1, -150000, 150000);
        f2 = clamp(2 * v2 - f2, -150000, 150000);
        y = ty == LF_LP ? v2 : ty == LF_BP ? v1 : x - ((v1 * kk) >> 12) - v2;
        b[i] = y << 2;
    }
    tmix[k].f1 = f1;
    tmix[k].f2 = f2;
}

/* one synth part into the dry mix and the sends; a part with no voice sounding costs
 * the LFO tick and a cleared buffer only (after the DIST tail has run out) */
static void mix_part(track_t *t, uint32_t n)
{
    int32_t *b = part_buf;
    uint32_t i;
    mod_begin(t);                                       /* the matrix's per-block values into t->p (mod.c) */
    spread_on = (t->p[P_SPRD] > 0 && (trk_vmode(t) == V_POLY || trk_vmode(t) == V_UNISON)) ||   /* (SPREAD, mix_spread; */
                trk_pmode[(uint32_t)(t - trk) % NTRK] == TP_RND;                                   /* PAN MODE RND: any) */
    if (track_render(t, b, n))
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if (!t->tail || !t->p[P_DIST] || !--t->tail) {
        if (mod.on)
            mod_end(t);
        return;
    }
    if (spread_on)
        mix_spread(t, b, n);
    else {
        int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] & 127], pan = t->p[P_PAN];
        if (lay_wg[(uint32_t)(t - trk) % NTRK] < 32767)   /* (the layers' crossfade on WHEEL: seq.c lay_wheel) */
            lvl = (lvl * lay_wg[(uint32_t)(t - trk) % NTRK]) >> 15;
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = t->p[P_CHOR] * 258, d = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak;
        int32_t xmax = c > d ? c : d;
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        uint32_t k = (uint32_t)(t - trk) % NTRK;
        track_dist(t, b, n);
        if (trk_lowcut[k])
            track_lowcut(k, b, n);                      /* (Blue Hare) LOW CUT */
        if (lay.ftype[k])
            track_filter(k, b, n);                      /* (Blue Hare) the layer's filter */
        if ((pf.mute >> (t - trk)) & 1u)
            perf_mute((uint32_t)(t - trk), b, n);       /* perform.c: a black key in the FX layer */
        for (i = 0; i < n; i++) {
            int32_t x = ((b[i] >> 2) * lvl) >> 10, a = x < 0 ? -x : x;   /* pre-shift: 8 loud voices */
            int32_t xs = clamp(x, -xmax, xmax);         /* sends: mulq15 would overflow */
            if (a > pk)
                pk = a;
            if (c)
                send_c[i] += mulq15(xs, c);
            if (d)
                send_d[i] += mulq15(xs, d);
            if (r)
                send_r[i] += mulq15(xs, r);
            mix_l[i] += (x * gl) >> 12;
            mix_r[i] += (x * gr) >> 12;
        }
        t->peak = pk;
    }
    if (mod.on)
        mod_end(t);                                     /* the stored values back */
}

/* the HOME oscilloscope (ui_graph.c graph_scope): every other sample of the left channel. MENU > DISPLAY > SCOPE (1.2,
 * Discussion #165): OUT (0, the default) what goes out, after MASTER (audio.c audio_block, before the click); MIX the
 * mix before MASTER (and before the FX layer, SPEAKER EQ and the limiter), at the level MASTER at its top gives, so its
 * size does not follow the volume. The click is in neither */
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
static volatile uint8_t scope_mix;
static __attribute__((noinline)) void scope_take_mix(uint32_t n)
{
    uint32_t i;
    for (i = 1; i < n; i += 2u)                         /* (((x >> 2) * MASTER_FULL) >> 10 is x) */
        scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)clamp(mix_l[i] + wet[i] + wside[i], -32767, 32767);
}

/* the master with the FX layer's effects between its level and master_out (perform.c) */
static __attribute__((noinline)) void perf_master(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB LEVEL FIXED: MASTER after) */
    for (i = 0; i < n; i++) {
        mix_l[i] = (((mix_l[i] + wet[i] + wside[i]) >> 2) * mg) >> 10;
        mix_r[i] = (((mix_r[i] + wet[i] - wside[i]) >> 2) * mg) >> 10;
    }
    perf_block(mix_l, mix_r, n);
    for (i = 0; i < n; i++) {
        out[2u * i] = mix_l[i];
        out[2u * i + 1u] = mix_r[i];
    }
    eq_block(out, n);                                   /* (Blue Hare, 2026-10-17) */
    mono_lows(out, n);                                  /* (Blue Hare, 2026-10) */
    bus_comp(out, n);                                   /* (Blue Hare) */
    for (i = 0; i < n; i++) {
        int32_t l = out[2u * i], r = out[2u * i + 1u];
        master_out(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}

/* (Blue Hare, 2026-10) the lows in mono, as a club's system wants them: the mix's side (L - R) / 2 high-passed at
 * ~120 Hz, 12 dB/oct (LOW CUT's two one-pole high-passes: tl_pole, so silence ends in exact silence), the rest of it (its
 * lows) taken out of both sides: 40 Hz a tenth of its width, 60 Hz a fifth, 120 Hz half. The chorus's and the reverbs'
 * width stays above; a centred track is untouched (its side is nothing) */
static int32_t ml_s1, ml_s2;            /* (Q8) */
static uint8_t ml_bypass;               /* (the host tests of a track's own panning: its sides as the track made them) */
static __attribute__((noinline)) void mono_lows(int32_t *out, uint32_t n)
{
    uint32_t i;
    if (ml_bypass)
        return;
    for (i = 0; i < n; i++) {
        int32_t s = (out[2u * i] - out[2u * i + 1u]) >> 1, hp, lo;
        hp = s - tl_pole(&ml_s1, s, 555);            /* (Q15 555: 120 Hz, TL_K's) */
        hp -= tl_pole(&ml_s2, hp, 555);
        lo = s - hp;
        out[2u * i] -= lo;
        out[2u * i + 1u] += lo;
    }
}

/* (Blue Hare) the bus compressor (above) on a block of the mix (out: L R interleaved, MASTER applied) */
static __attribute__((noinline)) void bus_comp(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t pk = 0, g0 = bc_g, g1 = 32768, step;
    for (i = 0; i < 2u * n; i++) {
        int32_t a = out[i] < 0 ? -out[i] : out[i];
        if (a > pk)
            pk = a;
    }
    if (pk > bc_env)
        bc_env += (pk - bc_env) >> 2;               /* (a block is 0.73 ms: ~3 ms) */
    else
        bc_env -= (bc_env >> 8) + (bc_env > 0);     /* (~190 ms) */
    if (bc_env > BC_T) {                            /* g = (T + (env - T) / 3) / env, Q15; both under 2^16 first */
        uint32_t e = (uint32_t)bc_env, num = BC_T + (((uint32_t)bc_env - BC_T) * 21845u >> 16);
        while (e > 65535u) {
            e >>= 1;
            num >>= 1;
        }
        g1 = (int32_t)((num << 15) / (e | 1u));
    }
    step = n == CTL ? (g1 - g0) >> CTL_LOG2 : 0;
    for (i = 0; i < 2u * n; i++) {                   /* x * g in two parts (the mix may be far above Q15): exact at */
        int32_t g = n == CTL ? g0 + step * (int32_t)(i / 2u + 1u) : g1, x = out[i];   /* g 1, no floor of a */
        out[i] = ((x >> 8) * g >> 7) + ((x & 255) * g >> 15);                          /* small negative one */
    }
    bc_g = g1;
}

/* one track into the mix (native: its own pair if it has one; the others' pairs silent while any has) */
static void mix_track(uint32_t i, uint32_t n, int32_t mg)
{
    if (tgt_track_out(i, n, mg))                        /* (target.h: the plugin's own output of this track) */
        return;
    mix_part(&trk[i], n);
}

#ifndef BLUEHARE_AMBFX
#define BLUEHARE_AMBFX 0
#endif
#if BLUEHARE_AMBFX
#include "fx_amb_chain.c"                           /* (Blue Hare, 2026-10) amb-1's whole chain, the studio's choice */
#endif

static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    int perf;
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB LEVEL FIXED: MASTER after) */
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    events_block(n);
    perf = perf_begin(n);                               /* the FX hold layer at work (perform.c) */
    for (i = 0; i < NPART; i++)
        mix_track(i, n, mg);
    tgt_mix_end(n);
    if (perf)
        perf_pre(mix_l, mix_r, send_d, send_r, n);
#if BLUEHARE_AMBFX
    if (fx_proc) {                                      /* amb-1's processing instead of the buses and the master */
        amb_block(out, n, mg, perf);
        return;
    }
#endif
    send_lowcut(send_d, send_r, n);                     /* (Blue Hare) */
    fx_buses(send_c, send_d, send_r, wet, n);
    if (hl.side)
        rev_side_mix(mix_l, mix_r, n);                  /* HALL: stereo */
    if (scope_mix)
        scope_take_mix(n);
    if (perf) {
        perf_master(out, n);
        return;
    }
    for (i = 0; i < n; i++) {
        out[2u * i] = (((mix_l[i] + wet[i] + wside[i]) >> 2) * mg) >> 10;
        out[2u * i + 1u] = (((mix_r[i] + wet[i] - wside[i]) >> 2) * mg) >> 10;
    }
    eq_block(out, n);                                   /* (Blue Hare, 2026-10-17) */
    mono_lows(out, n);                                  /* (Blue Hare, 2026-10) */
    bus_comp(out, n);                                   /* (Blue Hare) */
    for (i = 0; i < n; i++) {
        int32_t l = out[2u * i], r = out[2u * i + 1u];
        master_out(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}
