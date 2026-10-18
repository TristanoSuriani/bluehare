/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* SID (1.5): a voice of the C64's sound chip, in its manner (Blue Hare's own, not a chip emulator). The oscillator is
 * the chip's: a phase accumulator whose top 12 bits make the waves, WAVE one of TRI, SAW, PUL (PW its width) and NOIS
 * (the chip's 23-bit shift register clocked by the oscillator: pitched noise), or two or three of them at once, ANDed
 * as the chip combines them (TS, PT, PS, PTS: thin, nasal, buzzing). MOD as the chip's oscillator 3 on oscillator 1:
 * a master at the note, the voice's oscillator MDTN semitones above it; RING the triangle's top bit flipped by the
 * master's (ring modulation: bells, clangs), SYNC the oscillator restarted at each of the master's periods (the heard
 * pitch the note's, MDTN the timbre: the sync lead), R+S both. PWM sweeps PW (a triangle, ~0.2 .. 8 Hz), the SHP
 * routings move PW and MDTN too (an envelope on SHP: the sync sweep). FILT the chip's 12 dB multimode filter (LP BP HP)
 * at CUT with RES, driven a little into its own grit; ENV -> FILTER moves CUT. Pure SAW or PUL without sync are band-
 * limited (high notes do not alias); the combined waves and noise are the chip's raw bits.
 * (Blue Hare) The chip runs at ~1 MHz, so its steps do not alias; at 48 kHz they would (a grainy floor between the
 * harmonics, worse up the keyboard). The steps whose place is known are band-limited, a polyBLEP each (the output runs
 * a sample late, so both of its samples are corrected when the step is found): SYNC's restart (any wave), the
 * oscillator's wrap and the pulse's edge (TRI, PT, PS, and SAW / PUL under SYNC), RING's flip. TS, PTS and the noise
 * stay the chip's raw bits.
 * Voice state: ph[0] the oscillator, ph[1] the master, s[0] / s[1] the filter, s[2] the noise register, s[3] PWM's
 * phase, s[4] the noise output (held between clocks), s[5] the sample not yet out (the polyBLEPs'). */
static const char *const N_SID_WAVE[] = {"TRI", "SAW", "PUL", "NOIS", "TS", "PT", "PS", "PTS", 0};
static const char *const N_SID_MOD[] = {"OFF", "RING", "SYNC", "R+S", 0};
static const char *const N_SID_FILT[] = {"OFF", "LP", "BP", "HP", 0};
enum { SW_T = 1, SW_S = 2, SW_P = 4, SW_N = 8 };
static const uint8_t SID_WAVES[8] = {SW_T, SW_S, SW_P, SW_N, SW_T | SW_S, SW_P | SW_T, SW_P | SW_S, SW_P | SW_T | SW_S};

static void sid_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = 0;
    v->s[0] = v->s[1] = 0;
    v->s[2] = 0x7FFFF8;                               /* (the chip's noise register at reset) */
    v->s[4] = v->s[5] = 0;
}

/* the noise register's output: its bits 22 20 16 13 11 7 4 2, as 12 bits */
static uint32_t sid_noise_out(uint32_t r)
{
    return ((r >> 11) & 0x800u) | ((r >> 10) & 0x400u) | ((r >> 7) & 0x200u) | ((r >> 5) & 0x100u) |
           ((r >> 4) & 0x080u) | ((r >> 1) & 0x040u) | ((r << 1) & 0x020u) | ((r << 2) & 0x010u);
}

/* the raw wave, 12 bits: the oscillator's phase pc, rm the master's top bit (RING), the pulse width pw12 */
static inline uint32_t sid_wave(uint32_t wv, uint32_t ring, uint32_t pc, uint32_t rm, uint32_t pw12, uint32_t nout)
{
    uint32_t w = 0xFFFu, msb;
    if (wv & SW_T) {                                  /* the triangle: the accumulator folded on its top bit (RING: */
        msb = (pc >> 31) ^ (ring & rm);               /* that bit flipped by the master's) */
        w &= ((pc ^ (msb ? 0xFFFFFFFFu : 0u)) >> 19) & 0xFFEu;
    }
    if (wv & SW_S)
        w &= pc >> 20;
    if (wv & SW_P)
        w &= (pc >> 20) >= pw12 ? 0xFFFu : 0u;
    if (wv & SW_N)
        w &= nout;
    return w;
}

/* the time since a step, in samples (Q8, 0 .. 256): x the phase past it, inc the phase a sample */
static inline uint32_t sid_frac(uint32_t x, uint32_t inc)
{
    uint32_t t = (x >> 8 << 8) / (inc >> 8 | 1u);
    return t > 256u ? 256u : t;
}

/* a step of h (12-bit units, the wave before less after) t (Q8) samples ago: its polyBLEP, the sample after it (*cur)
 * raised by h / 2 (1 - t)^2, the one before (*prev) lowered by h / 2 t^2 */
static inline void sid_step(int32_t h, uint32_t t, int32_t *prev, int32_t *cur)
{
    uint32_t u = 256u - t;
    h <<= 4;
    *cur += (h * (int32_t)(u * u >> 2)) >> 15;
    *prev -= (h * (int32_t)(t * t >> 2)) >> 15;
}

/* the multimode filter (v1 the band-pass, v2 the low-pass), driven into its grit; OFF: the wave as it is */
static inline int32_t sid_filt(int32_t s, uint32_t filt, const tsvf_t *flt, int32_t k, int32_t *ic1, int32_t *ic2)
{
    if (filt) {
        int32_t v3 = (s >> 1) - *ic2, v1 = (flt->a1 * *ic1 + flt->a2 * v3) >> 13;
        int32_t v2 = *ic2 + ((flt->a2 * *ic1 + flt->a3 * v3) >> 13);
        *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
        *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
        s = filt == 1u ? v2 : filt == 2u ? v1 : (s >> 1) - ((v1 * k) >> 12) - v2;
        s = soft_knee(s, 14000) << 1;
    }
    return clamp(s, -32767, 32767);
}

static void sid_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t wv = SID_WAVES[(uint32_t)p[P_E0] & 7u], mod = (uint32_t)p[P_E3] & 3u, filt = (uint32_t)p[P_E5] & 3u, i;
    uint32_t pc = v->ph[0], pm = v->ph[1], nr = (uint32_t)v->s[2], nout = (uint32_t)v->s[4], lph = (uint32_t)v->s[3];
    uint32_t inc, minc = m->inc, pw12, pw32, ratio;
    int32_t shp = m->shape - (64 << 8), pwq, lfo, ic1 = v->s[0], ic2 = v->s[1], k = 0, blep_ok;
    int32_t prev = v->s[5], cur_bl = 0;
    uint32_t ring = mod & 1u, steps;
    tsvf_t flt;
    if (p[P_E2]) {                                    /* PWM: a triangle a block (1378 a second): ~0.2 .. 8 Hz */
        lph += 620000u + (uint32_t)p[P_E2] * 200000u;
        lfo = osc_tri(lph);
    } else {
        lfo = 0;
    }
    pwq = clamp((p[P_E1] << 8) + shp + ((lfo * 38) >> 7), 0, 127 << 8);
    pw12 = 64u + (((uint32_t)pwq * 31u) >> 8);        /* 64 .. ~4000 of 4096 */
    pw32 = pw12 << 20;
    if (mod) {                                        /* the oscillator MDTN (and SHP: +-24) semitones above the master */
        int32_t d16 = p[P_E4] * 16 + (shp * 3 >> 9);
        inc = pitch_inc((uint32_t)clamp(m->pitch16 + (d16 > 0 ? d16 : 0), 0, 2047));
    } else {
        inc = m->inc;
    }
    ratio = inc / ((minc >> 16) | 1u);               /* (the oscillator over the master, Q16: SYNC's restart) */
    blep_ok = !(mod & 2u) && (wv == SW_S || wv == SW_P);
    steps = !blep_ok && !(wv & SW_N) && wv != (SW_T | SW_S) && wv != (SW_P | SW_T | SW_S) &&   /* (TS, PTS, noise: */
            (wv != SW_T || ring);                     /* the raw bits; TRI without RING has no step) */
    if (filt) {
        k = 8192 - p[P_E7] * 7700 / 127;              /* RES: damping 2 .. ~0.12 */
        tsvf_coef_k(&flt, (p[P_E6] << 8) + m->cutoff, k);
    }
#define SW(ph, rm) (int32_t)sid_wave(wv, ring, (ph), (rm), pw12, nout)   /* (the raw wave at a phase) */
    if (!steps && !(mod & 2u)) {                      /* no step to band-limit: the plain loop, the sample not late */
        for (i = 0; i < n; i++) {
            uint32_t o = pc;
            int32_t s;
            pm += minc;
            pc += inc;
            if ((pc ^ o) & pc & 0x08000000u) {        /* the noise register clocked by bit 27 rising (16 a period) */
                uint32_t b = ((nr >> 22) ^ (nr >> 17)) & 1u;
                nr = ((nr << 1) | b) & 0x7FFFFFu;
                nout = sid_noise_out(nr);
            }
            if (blep_ok)
                s = wv == SW_S ? osc_saw(pc, inc) : osc_pulse(pc, inc, pw32);
            else
                s = (SW(pc, pm >> 31) - 2048) << 4;
            prev = s;                                 /* (for the stepped loop, if the sound turns to it) */
            out[i] += voice_amp(sid_filt(s, filt, &flt, k, &ic1, &ic2), m, i);
        }
    } else {
        for (i = 0; i < n; i++) {
            uint32_t o = pc, om = pm, t;
            int32_t s, cur;
            pm += minc;
            pc += inc;
            if ((mod & 2u) && pm < om) {              /* SYNC: the master's period over, the oscillator restarts */
                uint32_t at;
                t = sid_frac(pm, minc);
                at = o + (inc >> 8) * (256u - t);     /* (where it was at the restart) */
                pc = (uint32_t)(((uint64_t)pm * ratio) >> 16);   /* (as far on as the master's overshoot) */
                if (steps)                            /* (the master's top bit falls there too: RING's flip) */
                    sid_step(SW(at, 1u) - SW(0u, 0u), t, &prev, &cur_bl);
            } else if (steps) {
                uint32_t rm = pm >> 31;
                if (pc < inc)                         /* the oscillator's wrap */
                    sid_step(SW(0xFFFFFFFFu, rm) - SW(0u, rm), sid_frac(pc, inc), &prev, &cur_bl);
                if ((wv & SW_P) && pc - pw32 < inc)   /* the pulse's edge */
                    sid_step(SW(pw32 - 1u, rm) - SW(pw32, rm), sid_frac(pc - pw32, inc), &prev, &cur_bl);
                if (ring && ((pm ^ om) >> 31)) {      /* RING: the master's top bit flips */
                    t = sid_frac(pm & 0x7FFFFFFFu, minc);
                    sid_step(SW(pc, rm ^ 1u) - SW(pc, rm), t, &prev, &cur_bl);
                }
            }
            if ((pc ^ o) & pc & 0x08000000u) {        /* the noise register clocked by bit 27 rising (16 a period) */
                uint32_t b = ((nr >> 22) ^ (nr >> 17)) & 1u;
                nr = ((nr << 1) | b) & 0x7FFFFFu;
                nout = sid_noise_out(nr);
            }
            cur = ((SW(pc, pm >> 31) - 2048) << 4) + cur_bl;   /* (blep_ok: never here) */
            cur_bl = 0;
            s = prev;                                 /* (a sample late: the polyBLEPs' before-sample) */
            prev = cur;
            out[i] += voice_amp(sid_filt(s, filt, &flt, k, &ic1, &ic2), m, i);
        }
#undef SW
    }
    v->ph[0] = pc;
    v->ph[1] = pm;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = (int32_t)nr;
    v->s[3] = (int32_t)lph;
    v->s[4] = (int32_t)nout;
    v->s[5] = prev;
}

/* {WAVE, PW, PWM, MOD, MDTN, FILT, CUT, RES}; WAVE 0 TRI 1 SAW 2 PUL 3 NOIS 4 TS 5 PT 6 PS 7 PTS; MOD 0 OFF 1 RING
 * 2 SYNC 3 R+S; FILT 0 OFF 1 LP 2 BP 3 HP. fenv: ENV -> FILTER */
static const preset_t SID_PRESETS[] = {
    {"SID BASS", {2, 40, 0, 0, 0, 1, 50, 80}, {0, 50, 60, 20}, 30, 1, FX(10, 0, 0, 5)},
    {"FAT PULSE", {2, 30, 50, 0, 0, 1, 90, 40}, {0, 60, 100, 30}, 10, 1, FX(0, 30, 20, 20)},
    {"SYNC LEAD", {1, 64, 0, 2, 19, 1, 110, 30}, {0, 40, 110, 30}, 0, 1, FX(0, 20, 30, 20)},
    {"SYNC SWEEP", {1, 64, 0, 2, 12, 1, 100, 40}, {0, 80, 90, 30}, 0, 1, FX(10, 20, 30, 20)},
    {"BUZZ BASS", {4, 64, 0, 0, 0, 1, 40, 100}, {0, 60, 60, 20}, 40, 1, FX(20, 0, 0, 5)},
    {"FILTER ACID", {1, 64, 0, 0, 0, 1, 30, 115}, {0, 40, 20, 20}, 60, 1, FX(30, 0, 20, 5)},
    /* (2026-10-17, from SH101, archived) noise through the resonant low-pass: the wind, LFO on the filter */
    {"WIND", {3, 64, 0, 0, 0, 1, 50, 115}, {90, 0, 127, 100}, 0, 0, FX(0, 40, 30, 70), .lrate = 20},
};

static const engine_t ENG_SID = {
    .name = "SID",
    .id = EID_SID,
    .cc = 0x78,                  /* CC74 CUT, CC71 RES */
    .cats = "BLLLBBF",
    .page_title = {"OSC", "FILTER"},
    .edit = {
        {"WAVE", F_ENUM, 0, 7, 2, N_SID_WAVE, 0},
        {"PW", F_PCT, 0, 127, 64, 0, 0},
        {"PWM", F_PCT, 0, 127, 0, 0, 0},
        {"MOD", F_ENUM, 0, 3, 0, N_SID_MOD, 0},
        {"MDTN", F_INT, 0, 48, 12, 0, "st"},
        {"FILT", F_ENUM, 0, 3, 1, N_SID_FILT, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 40, 0, 0},
    },
    .presets = SID_PRESETS,
    .npresets = NELEM(SID_PRESETS),
    .note_on = sid_note_on,
    .render = sid_render,
    .knob = {P_E1, P_E4, P_E6, P_E7},
};
