/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
 * The processing is amb-1's, by Charles Vestal (GPL-3.0): docs/excerpt_amb1.c (render_chunk) */
/* (Blue Hare, 2026-10) the studio's EFFECTS > PROCESSING AMB-1 (fx_proc): amb-1's whole chain in place of Blue Hare's
 * buses and master, its plate and echoes (fx_amb.c) at the full rate with their own buffers. Only where BLUEHARE_AMBFX
 * is 1 (the studio); included by fx.c after the mix's buffers */
#if BLUEHARE_AMBFX
/* ------------------------------------------- the studio's amb-1 chain (fx_proc) --- */
#define AMB_TAPE_N 62000                            /* 5.6 s at 11.025 kHz */
static float amb_pl[AMB_PL_SIZE(AMB_NUM_FULL, AMB_PRE_FULL)];
static int16_t amb_tape[AMB_TAPE_N], amb_plex[AMB_PLEX_N];
static amb_plate_t amb_cp;
static amb_echo_t amb_ce;
static uint8_t amb_ready;
static volatile uint8_t fx_proc;                   /* 0 Blue Hare's processing, 1 amb-1's (the studio sets it) */

/* a block: the tracks' dry mix (mix_l, mix_r) and their delay and reverb sends -> out (Q15 a channel, the master
 * level mg applied); the FX hold layer (perf) after the level, then the EQ, then amb-1's master */
static __attribute__((noinline)) void amb_block(int32_t *out, uint32_t n, int32_t mg, int perf)
{
    const float sc = 1.0f / 32768.0f;
    float motion = amb_motion(), color = amb_color(), damp, wet_e = (float)song.g[G_DMIX] * (1.0f / 90.0f);
    static int32_t dcl, dcr;
    uint32_t i;
    if (!amb_ready) {
        amb_plate_init(&amb_cp, amb_pl, AMB_NUM_FULL, AMB_PRE_FULL);
        amb_echo_init(&amb_ce, amb_tape, AMB_TAPE_N, amb_plex, 1);
        amb_ready = 1;
    }
    amb_echo_knobs(&amb_ce);
    damp = amb_plate_knobs(&amb_cp);
    amb_heads(&amb_ce, n, motion);
    for (i = 0; i < n; i++) {
        float el, er, xl, xr, wl, wr, l, r;
        amb_echoes(&amb_ce, (float)send_d[i] * sc, motion, color, &el, &er, &xl, &xr);
        amb_plate(&amb_cp, ((float)send_r[i] * sc + (el + er + xl + xr) * 0.15f) * 0.8f, damp, &wl, &wr);
        l = (float)mix_l[i] * sc + wet_e * (0.75f * el + 0.3f * xl) + 1.3f * wl;
        r = (float)mix_r[i] * sc + wet_e * (0.75f * er + 0.3f * xr) + 1.3f * wr;
        mix_l[i] = (int32_t)fm_clampf(l * 32768.0f, -2.0e6f, 2.0e6f);   /* (x MASTER below: (x >> 2) * 4096 fits) */
        mix_r[i] = (int32_t)fm_clampf(r * 32768.0f, -2.0e6f, 2.0e6f);
    }
    for (i = 0; i < n; i++) {                      /* the level (MASTER), as Blue Hare's */
        mix_l[i] = ((mix_l[i] >> 2) * mg) >> 10;
        mix_r[i] = ((mix_r[i] >> 2) * mg) >> 10;
    }
    if (perf)
        perf_block(mix_l, mix_r, n);
    for (i = 0; i < n; i++) {
        out[2u * i] = mix_l[i];
        out[2u * i + 1u] = mix_r[i];
    }
    eq_block(out, n);                              /* (Blue Hare: the patch's EQ, fx.c) */
    for (i = 0; i < n; i++) {                      /* amb-1's master: a DC block, one tanh a channel */
        int32_t l = out[2u * i], r = out[2u * i + 1u];
        dcl += (l - dcl) >> 12;
        dcr += (r - dcr) >> 12;
        out[2u * i] = softclip(clamp(l - dcl, -1 << 20, 1 << 20));
        out[2u * i + 1u] = softclip(clamp(r - dcr, -1 << 20, 1 << 20));
    }
}
#endif
