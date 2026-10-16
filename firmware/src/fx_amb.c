/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
 * The processing is amb-1's, by Charles Vestal (GPL-3.0): docs/excerpt_amb1.c (reverb, echoes, render_chunk) */
/* (Blue Hare, 2026-10) amb-1's sound processing, the alternative to Blue Hare's buses and master (fx.c), for comparing
 * the two by ear (the studio's EFFECTS > PROCESSING; fx_proc). Built where BLUEHARE_AMBFX is 1 (the studio): its
 * buffers (~280 KB) do not fit the FM-1's pool beside Blue Hare's own yet (docs: the comparison). In float: on the
 * FM-1 its FPU (-mfprev1), in the host builds the host's.
 * The chain, as amb-1's render_chunk: the tracks' delay sends into amb-1's echoes (a tape loop of up to 5.6 s at a
 * quarter rate, darker and thinner each pass, wow, two heads that wander across the field, the second sliding to
 * another part of the loop now and then; and a 160 ms Echoplex, fed back, its second tap to the right); the reverb
 * sends plus the echoes into Dattorro's plate (J. Dattorro, "Effect Design, Part 1", JAES 1997: a 30 ms pre-delay, a
 * bandwidth low-pass, four input diffusers, a figure-8 tank of two modulated allpasses, delays, damping low-passes and
 * allpasses, seven taps a side); the dry mix with the echoes (x 0.75, the Echoplex x 0.3) and the plate (x 1.3); then
 * one soft clip a channel (tanh, the master's: Blue Hare's table, softclip) after a DC block. No chorus, no low cut on
 * the sends, no compressor, no limiter, no mono lows: amb-1 has none of them.
 * Blue Hare's knobs drive it: DLY TIME the echo's length (its note value, down to fit the tape), DLY FDBK the echo's
 * feedback, DLY COLR the tape's darkening, DLY MIX the echoes' level; REVERB SIZE the plate's decay (0.6 .. 0.975),
 * DAMP its damping; GEN's MOTION how much the heads wander. amb-1's own fast maths is not used: the heads' pan gains
 * come from the sine table (sine_i), the master's tanh from softclip. */
#define AMB_DS(n) (((n) * 14818 + 5000) / 10000)   /* the paper's lengths (29761 Hz) at 44.1 kHz */
#define AMB_PRE 1323                                /* 30 ms */
#define AMB_EXC AMB_DS(16)
enum { AL_PRE, AL_IN1, AL_IN2, AL_IN3, AL_IN4, AL_APL, AL_D1L, AL_AP2L, AL_D2L, AL_APR, AL_D1R, AL_AP2R, AL_D2R, AL_N };
static const int32_t AMB_LEN[AL_N] = {AMB_PRE, AMB_DS(142), AMB_DS(107), AMB_DS(379), AMB_DS(277), AMB_DS(672) + AMB_EXC + 2,
                                      AMB_DS(4453), AMB_DS(1800), AMB_DS(3720), AMB_DS(908) + AMB_EXC + 2, AMB_DS(4217),
                                      AMB_DS(2656), AMB_DS(3163)};
#define AMB_PL_TOTAL (AMB_PRE + AMB_DS(142) + AMB_DS(107) + AMB_DS(379) + AMB_DS(277) + AMB_DS(672) + AMB_DS(4453) + \
                      AMB_DS(1800) + AMB_DS(3720) + AMB_DS(908) + AMB_DS(4217) + AMB_DS(2656) + AMB_DS(3163) + 2 * (AMB_EXC + 2))
#define AMB_TAPE_N 62000                            /* 5.6 s at 11.025 kHz */
#define AMB_PLEX_N 7100                             /* 160 ms */
#define AMB_PLEX_TAP 2700                           /* the right's tap: ~100 ms */
static float amb_pl[AMB_PL_TOTAL];
static int16_t amb_tape[AMB_TAPE_N], amb_plex[AMB_PLEX_N];
typedef struct { float *b; int32_t n, i; } amb_dl_t;
static struct {
    uint8_t ready;
    amb_dl_t dl[AL_N];
    float bw, damp_l, damp_r, decay, dd2, lfo_s, lfo_c;
    int32_t tape_w, tape_ph, plex_w;
    float tape_acc, tape_lp, tape_hp, tape_p1, tape_p2, tape_c1, tape_c2, echo_len, echo_len_t, echo_fb;
    float head2, head2_t, head2_v, fb_lean, hg[4];
    uint32_t head_left, rng;
    float mod_v[4], mod_t[4], mod_r[4];
    uint32_t mod_left[4];
    float plex_lp, plex_lp2;
    int32_t dcl, dcr;
} amb;
static volatile uint8_t fx_proc;                   /* 0 Blue Hare's processing, 1 amb-1's (the studio sets it) */

static inline float amb_flush(float x) { return (x < 0.0f ? -x : x) < 1e-20f ? 0.0f : x; }
static inline float amb_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float amb_frand(void)
{
    uint32_t s = amb.rng;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    amb.rng = s;
    return (float)(s >> 8) * (1.0f / 16777216.0f);
}
static void amb_init(void)
{
    float *p = amb_pl;
    int32_t k;
    memset(&amb, 0, sizeof amb);
    memset(amb_pl, 0, sizeof amb_pl);
    memset(amb_tape, 0, sizeof amb_tape);
    memset(amb_plex, 0, sizeof amb_plex);
    for (k = 0; k < AL_N; k++) {
        amb.dl[k].b = p;
        amb.dl[k].n = AMB_LEN[k];
        p += AMB_LEN[k];
    }
    amb.lfo_c = 1.0f;
    amb.decay = 0.5f;
    amb.dd2 = 0.5f;
    amb.head2 = amb.head2_t = 0.75f;
    amb.hg[0] = amb.hg[3] = 1.0f;
    amb.rng = 0x2545F491u;
    amb.ready = 1;
}

/* ------------------------------------------------------ the plate (amb-1 reverb) --- */
static inline float amb_tap(const amb_dl_t *d, int32_t k)
{
    int32_t j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void amb_push(amb_dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float amb_ap(amb_dl_t *d, int32_t len, float x, float g)
{
    float z = amb_tap(d, len), v = x - g * z;
    amb_push(d, amb_flush(v));
    return z + g * v;
}
static inline float amb_ap_mod(amb_dl_t *d, float len, float x, float g)
{
    int32_t k = (int32_t)len;
    float f = len - (float)k, z = amb_tap(d, k) + (amb_tap(d, k + 1) - amb_tap(d, k)) * f, v = x - g * z;
    amb_push(d, amb_flush(v));
    return z + g * v;
}
static inline void amb_plate(float in, float damp, float *ol, float *or_)
{
    amb_dl_t *dl = amb.dl;
    float x, a, b, fb_l = amb_tap(&dl[AL_D2R], AMB_DS(3163)), fb_r = amb_tap(&dl[AL_D2L], AMB_DS(3720));
    amb_push(&dl[AL_PRE], in);
    x = amb_tap(&dl[AL_PRE], AMB_PRE);
    amb.bw += 0.6f * (x - amb.bw);
    x = amb_ap(&dl[AL_IN1], AMB_DS(142), amb.bw, 0.75f);
    x = amb_ap(&dl[AL_IN2], AMB_DS(107), x, 0.75f);
    x = amb_ap(&dl[AL_IN3], AMB_DS(379), x, 0.625f);
    x = amb_ap(&dl[AL_IN4], AMB_DS(277), x, 0.625f);
    {
        float s = amb.lfo_s + 0.0001f * amb.lfo_c, c = amb.lfo_c - 0.0001f * s;
        amb.lfo_s = s;
        amb.lfo_c = c;
    }
    a = amb_ap_mod(&dl[AL_APL], (float)AMB_DS(672) + (float)AMB_EXC * amb.lfo_s, x + amb.decay * fb_l, -0.7f);
    amb_push(&dl[AL_D1L], a);
    b = amb_tap(&dl[AL_D1L], AMB_DS(4453));
    amb.damp_l += damp * (b - amb.damp_l);
    b = amb_ap(&dl[AL_AP2L], AMB_DS(1800), amb.damp_l * amb.decay, amb.dd2);
    amb_push(&dl[AL_D2L], b);
    a = amb_ap_mod(&dl[AL_APR], (float)AMB_DS(908) + (float)AMB_EXC * amb.lfo_c, x + amb.decay * fb_r, -0.7f);
    amb_push(&dl[AL_D1R], a);
    b = amb_tap(&dl[AL_D1R], AMB_DS(4217));
    amb.damp_r += damp * (b - amb.damp_r);
    b = amb_ap(&dl[AL_AP2R], AMB_DS(2656), amb.damp_r * amb.decay, amb.dd2);
    amb_push(&dl[AL_D2R], b);
    *ol = 0.6f * (amb_tap(&dl[AL_D1R], AMB_DS(266)) + amb_tap(&dl[AL_D1R], AMB_DS(2974)) - amb_tap(&dl[AL_AP2R], AMB_DS(1913)) +
                  amb_tap(&dl[AL_D2R], AMB_DS(1996)) - amb_tap(&dl[AL_D1L], AMB_DS(1990)) - amb_tap(&dl[AL_AP2L], AMB_DS(187)) -
                  amb_tap(&dl[AL_D2L], AMB_DS(1066)));
    *or_ = 0.6f * (amb_tap(&dl[AL_D1L], AMB_DS(353)) + amb_tap(&dl[AL_D1L], AMB_DS(3627)) - amb_tap(&dl[AL_AP2L], AMB_DS(1228)) +
                   amb_tap(&dl[AL_D2L], AMB_DS(2673)) - amb_tap(&dl[AL_D1R], AMB_DS(2111)) - amb_tap(&dl[AL_AP2R], AMB_DS(335)) -
                   amb_tap(&dl[AL_D2R], AMB_DS(121)));
}

/* --------------------------------------------- the echoes (amb-1 tape and Echoplex) --- */
static inline float amb_tape_read(float d)        /* d quarter-rate samples ago */
{
    int32_t k = (int32_t)d, i0 = amb.tape_w - k, i1;
    float f = d - (float)k, a, c;
    i1 = i0 - 1;
    if (i0 < 0)
        i0 += AMB_TAPE_N;
    if (i1 < 0)
        i1 += AMB_TAPE_N;
    a = (float)amb_tape[i0];
    c = (float)amb_tape[i1];
    return (a + (c - a) * f) * (1.0f / 12000.0f);
}
static inline int16_t amb_s16(float x)
{
    x *= 12000.0f;
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
}
/* once a block: the slow random motion, where the heads sit in the field, the feedback's lean, the second head's slide
 * (amb-1 mods_step, heads_step; its mod 5 the wow, 6 and 7 the heads' places, 8 the lean: here 0, 1, 2, 3) */
static void amb_heads(uint32_t n, float motion)
{
    static const float HEAD2[5] = {0.75f, 0.6667f, 0.625f, 0.5f, 0.375f};
    float want, vmax;
    int32_t i;
    for (i = 0; i < 4; i++) {
        if (amb.mod_left[i] <= n) {
            float secs = 7.0f + 33.0f * amb_frand();
            amb.mod_t[i] = amb_frand() * 2.0f - 1.0f;
            amb.mod_left[i] = (uint32_t)(secs * (float)FS);
            amb.mod_r[i] = (float)n / (secs * (float)FS * 0.5f);
        } else {
            amb.mod_left[i] -= n;
        }
        amb.mod_v[i] += (amb.mod_t[i] - amb.mod_v[i]) * (amb.mod_r[i] < 1.0f ? amb.mod_r[i] : 1.0f);
    }
    for (i = 0; i < 2; i++) {                      /* pan gains: cos and sin of 0 .. pi/2, from the sine table */
        float p = amb_clampf((i ? 0.7f : -0.7f) + 0.9f * motion * amb.mod_v[1 + i], -1.0f, 1.0f);
        uint32_t ph = (uint32_t)((p + 1.0f) * 0.125f * 4294967295.0f);   /* (p + 1) pi/4 as a turn: x 1/8 */
        amb.hg[2 * i] = (float)sine_i(ph + 0x40000000u) * (1.0f / 32767.0f);
        amb.hg[2 * i + 1] = (float)sine_i(ph) * (1.0f / 32767.0f);
    }
    amb.fb_lean = 0.25f * motion * (1.0f + amb.mod_v[3]);
    if (amb.head_left <= n) {
        amb.head_left = (uint32_t)((20.0f + 40.0f * amb_frand()) * (float)FS);
        if (amb_frand() < motion)
            amb.head2_t = HEAD2[(int32_t)(amb_frand() * 4.999f)];
    } else {
        amb.head_left -= n;
    }
    vmax = 0.008f / (amb.echo_len > 100.0f ? amb.echo_len : 100.0f);
    want = amb_clampf((amb.head2_t - amb.head2) * (1.0f / (2.0f * 11025.0f)), -vmax, vmax);
    amb.head2_v += (want - amb.head2_v) * ((float)n * (1.0f / (float)FS) < 1.0f ? (float)n * (1.0f / (float)FS) : 1.0f);
}
static inline void amb_echoes(float in, float motion, float color, float *el, float *er, float *xl, float *xr)
{
    float y = (float)amb_plex[amb.plex_w] * (1.0f / 12000.0f);
    int32_t t = amb.plex_w - AMB_PLEX_TAP;
    amb.plex_lp += 0.35f * (y - amb.plex_lp);
    amb.plex_lp2 += 0.35f * ((float)amb_plex[t < 0 ? t + AMB_PLEX_N : t] * (1.0f / 12000.0f) - amb.plex_lp2);
    amb_plex[amb.plex_w] = amb_s16(in + 0.55f * amb.plex_lp);
    if (++amb.plex_w >= AMB_PLEX_N)
        amb.plex_w = 0;
    *xl = amb.plex_lp;
    *xr = amb.plex_lp2;
    amb.tape_acc += in + 0.3f * amb.plex_lp;
    if (++amb.tape_ph >= 4) {                       /* the tape, a quarter of the time */
        float d, h1, h2, fb;
        amb.tape_ph = 0;
        amb.echo_len += (amb.echo_len_t - amb.echo_len) * 0.001f;
        amb.head2 += amb.head2_v;
        d = amb.echo_len * (1.0f + 0.003f * motion * amb.mod_v[0]);   /* wow */
        if (d > (float)(AMB_TAPE_N - 4))
            d = (float)(AMB_TAPE_N - 4);
        h1 = amb_tape_read(d);
        h2 = amb_tape_read(d * amb.head2);
        amb.tape_lp += color * (h1 + amb.fb_lean * (h2 - h1) - amb.tape_lp);   /* each time round: darker, thinner */
        amb.tape_hp += 0.03f * (amb.tape_lp - amb.tape_hp);
        fb = amb.tape_lp - amb.tape_hp;
        if (++amb.tape_w >= AMB_TAPE_N)
            amb.tape_w = 0;
        amb_tape[amb.tape_w] = amb_s16(amb.tape_acc * 0.25f + amb.echo_fb * fb);
        amb.tape_acc = 0.0f;
        amb.tape_p1 = amb.tape_c1;
        amb.tape_p2 = amb.tape_c2;
        amb.tape_c1 = h1;
        amb.tape_c2 = h2;
    }
    {
        float f = (float)amb.tape_ph * 0.25f;
        float h1 = amb.tape_p1 + (amb.tape_c1 - amb.tape_p1) * f, h2 = amb.tape_p2 + (amb.tape_c2 - amb.tape_p2) * f;
        *el = amb.hg[0] * h1 + amb.hg[2] * h2;
        *er = amb.hg[1] * h1 + amb.hg[3] * h2;
    }
}

/* ------------------------------------------------------------------ the chain --- */
/* a block: the tracks' dry mix (mix_l, mix_r) and their delay and reverb sends -> out (Q15 a channel, the master
 * level mg applied); the FX hold layer (perf) between the level and the soft clip, as Blue Hare's perf_master */
static __attribute__((noinline)) void amb_block(int32_t *out, uint32_t n, int32_t mg, int perf)
{
    const float sc = 1.0f / 32768.0f;
    float motion = (float)clamp(song.g[G_GMOT], 0, 127) * (1.0f / 127.0f);
    float color = 0.2f + (float)song.g[G_DCOLOR] * 0.005f;                /* (COLR 70: amb-1's 0.55) */
    float damp = 0.9f - (float)song.g[G_RDAMP] * 0.0075f;                 /* (DAMP 60: amb-1's 0.45) */
    float wet_e = (float)song.g[G_DMIX] * (1.0f / 90.0f);                 /* (MIX 90: amb-1's levels) */
    float decay_t = 0.6f + 0.375f * (float)song.g[G_RSIZE] * (1.0f / 127.0f);
    float lt = (float)div_samples((uint32_t)song.g[G_DTIME]) * 0.25f;     /* (the tape runs at a quarter rate) */
    uint32_t i, div = (uint32_t)song.g[G_DTIME];
    if (!amb.ready)
        amb_init();
    while (lt > (float)(AMB_TAPE_N - 8) && div < 10u)                    /* too long: the next shorter value */
        lt = (float)div_samples(div = div ? div - 1u : 0u) * 0.25f;
    amb.echo_len_t = amb_clampf(lt, 64.0f, (float)(AMB_TAPE_N - 8));
    if (amb.echo_len <= 0.0f)
        amb.echo_len = amb.echo_len_t;
    amb.echo_fb = 0.3f + (float)song.g[G_DFDBK] * 0.0045f;               /* (FDBK 60: 0.57; 120: 0.84) */
    amb.decay += (decay_t - amb.decay) * 0.002f;
    amb.dd2 = amb_clampf(amb.decay + 0.15f, 0.25f, 0.5f);
    amb_heads(n, motion);
    for (i = 0; i < n; i++) {
        float el, er, xl, xr, wl, wr, l, r;
        amb_echoes((float)send_d[i] * sc, motion, color, &el, &er, &xl, &xr);
        amb_plate(((float)send_r[i] * sc + (el + er + xl + xr) * 0.15f) * 0.8f, damp, &wl, &wr);
        l = (float)mix_l[i] * sc + wet_e * (0.75f * el + 0.3f * xl) + 1.3f * wl;
        r = (float)mix_r[i] * sc + wet_e * (0.75f * er + 0.3f * xr) + 1.3f * wr;
        mix_l[i] = (int32_t)amb_clampf(l * 32768.0f, -2.0e6f, 2.0e6f);   /* (x MASTER below: (x >> 2) * 4096 fits) */
        mix_r[i] = (int32_t)amb_clampf(r * 32768.0f, -2.0e6f, 2.0e6f);
    }
    for (i = 0; i < n; i++) {                      /* the level (MASTER), as Blue Hare's */
        mix_l[i] = ((mix_l[i] >> 2) * mg) >> 10;
        mix_r[i] = ((mix_r[i] >> 2) * mg) >> 10;
    }
    if (perf)
        perf_block(mix_l, mix_r, n);
    for (i = 0; i < n; i++) {                      /* amb-1's master: a DC block, one tanh a channel */
        int32_t l = mix_l[i], r = mix_r[i];
        amb.dcl += (l - amb.dcl) >> 12;
        amb.dcr += (r - amb.dcr) >> 12;
        out[2u * i] = softclip(clamp(l - amb.dcl, -1 << 20, 1 << 20));
        out[2u * i + 1u] = softclip(clamp(r - amb.dcr, -1 << 20, 1 << 20));
    }
}
