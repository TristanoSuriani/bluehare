/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* The metronome (1.1, Ideas Discussion #131; src/click.c, src/seq.c), through the real audio path
 * (audio.c audio_block: the mix, USB audio's tap, the click, the DAC):
 *   the click on the song tempo's quarter notes, sample for sample (beat k in the block a 1/16 track enters step 4k,
 *   no drift), whatever a track's DIV or SWING; the bar's first beat higher (1760 / 1320 Hz) and 6 dB louder; its
 *   length; LOW / MID / HIGH; MASTER scales it; CLICK OFF / ON; never in USB audio (bit for bit the same with it
 *   on), only the DAC; the external clock: a click on every 24 pulses, with the steps;
 *   1.5: no count-in: PLAY from a stop plays (and clicks) at once, internal or external clock.
 *   build/click_demo/: the click at 120 BPM with a pattern (DAC and USB). The steps are the tests' song player's rows
 *   (hostsim.c host_seq: the sequencer is a placeholder) */
#include <sys/stat.h>
#define main hostsim_main
#include "../lib/hostsim.c"
#undef main

#define FM1_AUDIO_HALF 0x80u
#define FM1_TICKS_PER_US 1u
static volatile uint32_t t5_nested_ticks;
static uint32_t fm1_ticks(void) { return 0; }
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return 0; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), uint32_t p)
{ (void)b; (void)n; (void)isr; (void)p; }
void isr_alnk0(void) {}
#define BLUEHARE_UAC 1                                  /* audio.c taps USB audio: here into tap */
static int32_t tap_blk[2 * CTL];
static void uac_tap(const int32_t *out, uint32_t n) { memcpy(tap_blk, out, 8u * n); }
static void uac_render_start(void) {}
#include "../../firmware/fm1/audio.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("click: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define MAXB 30000u                                    /* blocks recorded (~21.8 s) */
static int32_t dac[MAXB * CTL], usbo[MAXB * CTL];       /* left channel, Q15 (the DAC's as before OUT_SHIFT) */
static uint8_t blk_play[MAXB];
static uint16_t blk_idx[MAXB];
static int8_t blk_beat[MAXB];                          /* seq.c click_beat_now after the block (the header's metronome) */
static uint32_t nb;                                    /* blocks rendered */

static void fresh(void)
{
    uint32_t p;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(&midi_clock, 0, sizeof midi_clock);
    host_tracks_init();
    for (p = 0; p < NPART; p++) host_preset(&trk[p], 0, 0);
    for (p = 0; p < NPART; p++)
        trk[p].p[P_DIST] = trk[p].p[P_CHOR] = trk[p].p[P_DLY] = trk[p].p[P_REV] = 0;
    transport_req = panic_req = 0;
    seq_stop();
    clk_pos = CLK_START; clk_step = 0;
    lim_env = LIM_T; bc_env = 0; bc_g = 32768; dc_l = dc_r = dce_l = dce_r = 0;
    memset(&clk, 0, sizeof clk);
    click_req = 0; click_mode = CLICK_OFF; click_lvl = 1;
    fx_usb_fixed = 0;
    mi_r = mi_w = 0; midi_in_overflow = 0; midi_beat_samples = 0;
#ifndef HOST_BASELINE
    fm1_in.notes = kb_prev = 0;                  /* (the keys: the OS's) */
#endif
    fm1_ms = 0;
    song.sel = 0;
    nb = 0;
}
static void run(uint32_t n)                            /* n blocks through audio_block */
{
    int32_t out[2 * CTL];
    uint32_t i;
    while (n-- && nb < MAXB) {
        audio_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            dac[nb * CTL + i] = out[2u * i] / (1 << OUT_SHIFT);
            usbo[nb * CTL + i] = tap_blk[2u * i];
        }
        blk_play[nb] = song.playing;
        blk_idx[nb] = (uint16_t)hs.row;                /* (the song's row, a 1/16: hostsim.c host_seq) */
        blk_beat[nb] = (int8_t)click_beat_now();
        nb++;
        fm1_ms = (uint32_t)((uint64_t)nb * CTL * 1000u / FS);
    }
}
/* the clicks heard in dac[from .. nb * CTL): their first samples (after 600 silent ones), peaks, frequencies */
#define MAXC 64
static uint32_t c_at[MAXC], c_len[MAXC], nc;
static int32_t c_peak[MAXC];
static double c_hz[MAXC];
static void clicks(uint32_t from)
{
    uint32_t i, end = nb * CTL, quiet = 600;
    nc = 0;
    for (i = from; i < end; i++) {
        if (!dac[i]) {
            quiet++;
            continue;
        }
        if (quiet >= 600u && nc < MAXC) {
            uint32_t j, x = 0, last = i;
            int32_t pk = 0;
            for (j = i; j < end && j < i + 4000u; j++) {
                int32_t a = dac[j] < 0 ? -dac[j] : dac[j];
                if (a > pk) pk = a;
                if (dac[j]) last = j;
                if (j > i && j < i + 441u && (dac[j] < 0) != (dac[j - 1] < 0)) x++;
            }
            c_at[nc] = i; c_peak[nc] = pk; c_len[nc] = last + 1u - i;
            c_hz[nc] = x / 2.0 / (440.0 / FS);
            nc++;
        }
        quiet = 0;
    }
}
#define QB 22048u                                      /* a beat at 120 BPM: four 1/16 steps of 5512 samples */
static uint32_t ceil_div(uint64_t a, uint32_t b) { return (uint32_t)((a + b - 1u) / b); }
/* an empty song of 16 rows (a row a 1/16): its rows mark the grid, nothing sounds */
static void grid_track(void)
{
    memset(host_song, 0, sizeof host_song);
    host_rows = 16;
}
/* the block in which track 1 entered step s (from block `from`), else MAXB */
static uint32_t step_block(uint32_t from, uint32_t s)
{
    uint32_t b;
    for (b = from; b < nb; b++)
        if (blk_play[b] && blk_idx[b] == s && (b == 0 || !blk_play[b - 1] || blk_idx[b - 1] != s))
            return b;
    return MAXB;
}

/* the header's metronome (ui_draw.c) on the click's beats: in blocks 0 .. upto, click_beat_now moves exactly in the
 * blocks the clicks (clicks()) start in, to beat k % 4 at the k-th (0: the accented one), and nowhere else */
static int beats_agree(uint32_t upto)
{
    uint32_t b, k = 0;
    int ok = 1;
    for (b = 0; b < upto && b < nb; b++) {
        int moved = blk_beat[b] >= 0 && (b == 0 || blk_beat[b] != blk_beat[b - 1]);
        int click = k < nc && c_at[k] / CTL == b;
        ok &= moved == click;
        if (click) {
            ok &= blk_beat[b] == (int8_t)(k % 4u) && (blk_beat[b] == 0) == (c_hz[k] > 1600.0);
            k++;
        }
    }
    return ok && k == nc;
}

static void timing(void)
{
    static const int BPM[3] = {120, 97, 173};
    uint32_t t, k, ok = 1, okstep = 1, okacc = 1, okusb = 1, okbeat = 1;
    for (t = 0; t < 3u; t++) {
        uint32_t B;
        fresh(); grid_track();
        song.g[G_BPM] = (int16_t)BPM[t];
        B = click_beat_len();
        click_mode = CLICK_ON;
        transport_req = 1;
        run((uint32_t)((uint64_t)B * 33u / CTL));      /* 32 beats and a bit */
        clicks(0);
        ok &= nc == 33u; okbeat &= beats_agree(nb);
        for (k = 0; k < nc; k++) {                     /* beat k: the first sample of block ceil(k B / CTL) */
            ok &= c_at[k] == ceil_div((uint64_t)k * B, CTL) * CTL;
            if (k < 8u)                                /* .. the block the 1/16 track enters step 4k in */
                okstep &= c_at[k] == step_block(0, (4u * k) % 16u) * CTL || k >= 4u;
            okacc &= (k % 4u == 0u) == (c_hz[k] > 1600.0);
        }
        for (k = 0; k < nb * CTL; k++) okusb &= usbo[k] == 0;
        if (t == 0) {
            printf("click: 120 BPM: beats at samples %u %u %u %u .. %u (beat %u samples)\n", c_at[0], c_at[1], c_at[2], c_at[3],
                   c_at[32], B);
            okstep &= step_block(0, 0) == 0 && step_block(0, 4) * CTL == c_at[1] && step_block(0, 8) * CTL == c_at[2] &&
                      step_block(0, 12) * CTL == c_at[3];
        }
    }
    check("the beats at 120, 97, 173 BPM: beat k at the first sample of block ceil(k * beat / 32), 32 beats, no drift", ok);
    check("each beat in the block a 1/16 track enters step 4k (step 1 with the first)", okstep);
    check("the bar's first beat accented (1760 Hz), the others not (1320 Hz)", okacc);
    check("USB audio silent the whole time (the click goes to the DAC only)", okusb);
    check("the header's metronome (click_beat_now) moves in each click's block only, beat k % 4, 0 the accented", okbeat);

    /* DIV 1/8 and SWING on the track: the click stays on the tempo's quarters */
    fresh(); grid_track();
    trk[0].p[P_SDIV] = 1; trk[0].p[P_SSWING] = 60; song.g[G_SWING] = 30;
    song.g[G_BPM] = 120; click_mode = CLICK_ON; transport_req = 1;
    run(ceil_div(QB * 9u, CTL));
    clicks(0);
    ok = nc == 9u;
    for (k = 0; k < nc; k++) ok &= c_at[k] == ceil_div((uint64_t)k * QB, CTL) * CTL;
    check("a track at DIV 1/8 with SWING: the click on the song tempo's quarters all the same", ok);
}

static void sound(void)
{
    uint32_t k, lv;
    int32_t pk[3];
    double ms;
    fresh(); grid_track(); song.g[G_BPM] = 120; click_mode = CLICK_ON; transport_req = 1;
    run(ceil_div(QB * 4u + 2000u, CTL));
    clicks(0);
    ms = c_len[0] * 1000.0 / FS;
    printf("click: MID: downbeat %.0f Hz peak %d, beat %.0f Hz peak %d (%.1f dB), %.1f ms long\n", c_hz[0], c_peak[0], c_hz[1],
           c_peak[1], 20.0 * log10((double)c_peak[0] / c_peak[1]), ms);
    check("the downbeat ~1760 Hz, a beat ~1320 Hz (within 3 %)",
          nc == 5u && fabs(c_hz[0] / 1760.0 - 1.0) < 0.03 && fabs(c_hz[1] / 1320.0 - 1.0) < 0.03 && fabs(c_hz[2] / 1320.0 - 1.0) < 0.03);
    check("the downbeat 6 dB louder (+-1 dB), MID's peak -12 dBFS of the mix (+-1 dB)",
          fabs(20.0 * log10((double)c_peak[0] / c_peak[1]) - 6.02) < 1.0 && fabs(20.0 * log10(c_peak[0] / 8192.0)) < 1.0);
    check("short: silent within 40 ms, no step at its start (a sine from 0: its first sample sin(2 pi f / FS) of the peak)",
          ms > 20.0 && ms < 40.0 && abs(dac[c_at[0]]) < c_peak[0] * 3 / 10);
    for (lv = 0; lv < 3u; lv++) {
        fresh(); grid_track(); click_mode = CLICK_ON; click_lvl = (uint8_t)lv; transport_req = 1;
        run(100);
        clicks(0);
        pk[lv] = nc ? c_peak[0] : 0;
    }
    printf("click: LOW / MID / HIGH downbeat peaks %d %d %d\n", pk[0], pk[1], pk[2]);
    check("CLICK LEVEL LOW / MID / HIGH: -18 / -12 / -6 dBFS of the mix (6 dB steps, +-0.5 dB)",
          fabs(20.0 * log10(pk[1] / (double)pk[0]) - 6.02) < 0.5 && fabs(20.0 * log10(pk[2] / (double)pk[1]) - 6.02) < 0.5 &&
          fabs(20.0 * log10(pk[2] / 16384.0)) < 1.0);
    fresh(); grid_track(); click_mode = CLICK_ON; song.master_q12 = 1024; transport_req = 1;
    run(100); clicks(0);
    k = nc ? (uint32_t)c_peak[0] : 0u;
    fresh(); grid_track(); click_mode = CLICK_ON; song.master_q12 = 0; transport_req = 1;
    run(100); clicks(0);
    check("MASTER scales it as the rest of the DAC's signal (1/4: -12 dB; 0: silent)",
          fabs(20.0 * log10(pk[1] / (double)k) - 12.04) < 0.3 && nc == 0u);
}

static void modes(void)
{
    uint32_t n_off, n_on, n_stop;
    fresh(); grid_track(); transport_req = 1; run(3000); clicks(0); n_off = nc;
    fresh(); grid_track(); click_mode = CLICK_ON; transport_req = 1; run(3000); clicks(0); n_on = nc;
    fresh(); grid_track(); click_mode = CLICK_ON; run(3000); clicks(0); n_stop = nc;
    check("CLICK OFF: no click; ON: always while playing; stopped: never", n_off == 0u && n_on == 5u && n_stop == 0u);
}

/* the USB stream bit for bit the same with the click on, a pattern playing; the DAC differs only by the click */
static void usb_clean(void)
{
    static int32_t dac0[MAXB * CTL], usb0[MAXB * CTL];
    uint32_t i, n, same_usb = 1, diff_only_clicks = 1, pass;
    FILE *f;
    for (pass = 0; pass < 2u; pass++) {
        fresh();
        grid_track();
        for (i = 0; i < 16u; i++)                   /* (a note, or OFF on every third row) */
            host_song[i][0] = (uint8_t)(i % 3u ? 48u + (i * 7u) % 12u : HS_OFF);
        song.g[G_BPM] = 120;
        click_mode = pass ? CLICK_ON : CLICK_OFF;
        transport_req = 1;
        run(ceil_div(QB * 8u, CTL));
        if (!pass) {
            memcpy(dac0, dac, sizeof dac0);
            memcpy(usb0, usbo, sizeof usb0);
        }
    }
    n = nb * CTL;
    for (i = 0; i < n; i++) {
        uint32_t ph = i % QB;                       /* a beat starts at the block ceil(k * QB / 32) */
        same_usb &= usbo[i] == usb0[i];
        if (dac[i] != dac0[i] && ph > 1600u && ph < QB - 64u) diff_only_clicks = 0;
    }
    check("USB audio bit for bit the same with CLICK ON (a pattern playing): the click is not in it", same_usb);
    check("the DAC: the same but for the clicks (each within 40 ms of its beat)", diff_only_clicks);
    mkdir("build/click_demo", 0755);
    if ((f = fopen("build/click_demo/click_120bpm_dac.wav", "wb"))) {
        wav_hdr(f, n);
        for (i = 0; i < n; i++) wav_put(f, dac[i], dac[i]);
        fclose(f);
    }
    if ((f = fopen("build/click_demo/click_120bpm_usb.wav", "wb"))) {
        wav_hdr(f, n);
        for (i = 0; i < n; i++) wav_put(f, usbo[i], usbo[i]);
        fclose(f);
    }
}

/* 1.2 (Discussion #165): MENU > SCOPE. OUT (the default) the HOME scope reads the music after MASTER (audio.c, before
 * the click); MIX the mix before MASTER (fx.c scope_take_mix): the same picture at any MASTER. The click in neither */
static int16_t scope_run(uint32_t mix, int32_t master, uint32_t click)
{
    uint32_t i;
    int16_t pk = 0;
    fresh();
    grid_track();
    for (i = 0; i < 16u; i++)                       /* (a note every row) */
        host_song[i][0] = (uint8_t)(48u + (i * 5u) % 12u);
    song.g[G_BPM] = 120; song.master_q12 = (uint16_t)master;
    click_mode = click ? CLICK_ON : CLICK_OFF;
    scope_mix = (uint8_t)mix;
    scope_w = 0;
    memset(scope_buf, 0, sizeof scope_buf);
    transport_req = 1;
    run(ceil_div(QB * 2u, CTL) + 8u);                  /* (the window: the start of beat 3, its click) */
    for (i = 0; i < SCOPE_N; i++)
        pk = (int16_t)(abs(scope_buf[i]) > pk ? abs(scope_buf[i]) : pk);
    return pk;
}
static void scope(void)
{
    static int16_t mix_full[SCOPE_N], out_full[SCOPE_N];
    static int32_t dac_off[MAXB * CTL];
    int16_t pk_out, pk_out4, pk_mix, pk_mix4;
    uint32_t w, n, i, dac_click = 0;
    int same_out;
    pk_out = scope_run(0, 4096, 0);
    memcpy(out_full, scope_buf, sizeof out_full);
    memcpy(dac_off, dac, sizeof dac_off);
    w = scope_w;
    n = nb;
    pk_out4 = scope_run(0, 1024, 0);
    pk_mix = scope_run(1, 4096, 0);
    memcpy(mix_full, scope_buf, sizeof mix_full);
    check("SCOPE: OUT and MIX take every other sample alike (as many, the music heard)",
          scope_w == w && w == n * CTL / 2u && pk_out > 1000);
    pk_mix4 = scope_run(1, 1024, 0);
    printf("click: scope peaks: OUT %d, OUT at MASTER 1/4 %d; MIX %d, MIX at MASTER 1/4 %d\n", pk_out, pk_out4, pk_mix, pk_mix4);
    check("SCOPE OUT follows MASTER (1/4: -12 dB +-1.5); MIX the same at any MASTER (bit for bit), as OUT at its top (+-1.5 dB)",
          fabs(20.0 * log10((double)pk_out / pk_out4) - 12.04) < 1.5 && !memcmp(mix_full, scope_buf, sizeof mix_full) &&
          fabs(20.0 * log10((double)pk_out / pk_mix)) < 1.5);
    (void)scope_run(0, 4096, 1);
    for (i = 0; i < nb * CTL; i++)
        dac_click |= dac[i] != dac_off[i];
    same_out = !memcmp(out_full, scope_buf, sizeof out_full);
    (void)scope_run(1, 4096, 1);
    check("the click: in the DAC, in neither scope (OUT, MIX bit for bit as without it)",
          dac_click && same_out && !memcmp(mix_full, scope_buf, sizeof mix_full));
    scope_mix = 0;
}

/* 1.5: no count-in: PLAY from stop plays at once (step 1 in the first block), the click's first beat with it
 * (accented) */
static void no_count_in(void)
{
    uint32_t ok;
    fresh(); grid_track();
    song.g[G_BPM] = 120; click_mode = CLICK_ON;
    transport_req = 1;
    run(ceil_div(QB * 3u, CTL) + 1u);
    clicks(0);
    ok = blk_play[0] && blk_idx[0] == 0u && step_block(0, 0) == 0u && nc >= 3u && c_at[0] == 0u && c_hz[0] > 1600.0 &&
         c_at[1] == step_block(0, 4) * CTL;
    check("no count-in: PLAY from a stop plays step 1 at once, the click's accented beat with it", ok);
}

/* the external clock (USB, 24 pulses a quarter at 120 BPM): Start starts at once (no count-in), a click on every 24th
 * pulse with the steps */
static void ext_clock(void)
{
    uint32_t p = 0, k, ok, b, sent_start = 0;
    double pulse = 22050.0 / 24.0;
    fresh(); grid_track();
    song.g[G_CLOCK] = 1;
    click_mode = CLICK_ON;
    run(4);                                            /* (the clock mode taken) */
    for (b = 0; b < ceil_div(QB * 9u, CTL); b++) {
        double now = (double)nb * CTL;
        if (!sent_start && b == 10u) {
            midi_enqueue(0xFu | 0xFAu << 8, 1u);
            sent_start = 1;
        }
        while (sent_start && 10.0 * CTL + p * pulse <= now + CTL - 1) {   /* the pulses due in this block */
            midi_enqueue(0xFu | 0xF8u << 8, 1u);
            p++;
        }
        run(1);
        if (b == 10u) ok = song.playing && hs.row == 0u;
    }
    {
        int startok = ok;
        clicks(0);
        ok = nc >= 8u;
        for (k = 0; k < nc && k < 8u; k++) {
            uint32_t sb = step_block(0, (4u * k) % 16u);
            if (k >= 4u) {                             /* the second bar: after the first bar's step 12 */
                uint32_t b12 = step_block(0, 12u);
                sb = step_block(b12 + 1u, (4u * k) % 16u);
            }
            ok &= c_at[k] == sb * CTL && (k % 4u == 0u) == (c_hz[k] > 1600.0);
        }
        printf("click: external clock: %u pulses, %u clicks, at samples %u %u %u %u ..\n", p, nc, c_at[0], c_at[1], c_at[2], c_at[3]);
        check("external clock: Start plays at step 1 at once", startok);
        check("external clock: a click every 24 pulses, in the block the steps 1, 5, 9, 13 start, the bar's first accented", ok);
        check("external clock: the header's metronome on its clicks", beats_agree(nb));
    }
    fresh(); grid_track();
    song.g[G_CLOCK] = 1;
    run(2);
    transport_req = 1;                                 /* PLAY on the FM-1 with CLK EXT: as a Start */
    run(2);
    check("external clock: PLAY on the FM-1 starts as before (as a Start)", song.playing);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    timing();
    sound();
    modes();
    usb_clean();
    scope();
    no_count_in();
    ext_clock();
    printf(bad ? "click: %d FAILED\n" : "click: all passed\n", bad);
    return bad != 0;
}
