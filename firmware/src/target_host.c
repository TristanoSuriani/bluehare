/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The host targets' side of target.h (FELUCCA_NATIVE: the native image, the plugin's cores, through the browser
 * emulator's layer): the host's transport and tempo, the tracks' own outputs. Included at the end of seq.c, so it sees
 * the sequencer's, the tracker's and the mix's own state; the device builds have target.h's empty versions instead. */

/* ---- the host's tempo (fx.c beat_samples) */
static uint32_t host_beat_samples;                    /* the plugin's host's tempo, over the song's and CLOCK's (host_block);
                                                       * zero: no host */
static uint32_t tgt_beat_samples(void) { return host_beat_samples; }

/* ---- the tracks' own outputs (fx.c mix_track, mix_block) */
/* (Fel-Tracker) the plugin's track outputs (plugin/core/fel_core.h outputs): a track whose bit is set in trk_own plays
 * into a stereo pair of its own, out of the main mix, at the level it has there (MASTER on it; before the bus
 * compressor and the limiter, which are the main's). Its sends still feed the FX buses, which stay on the main.
 * trk_out holds the pairs by the frame, the main's frames' order (TRK_OUT_N: a few halves) */
#define TRK_OUT_N 1024u
static uint32_t trk_own;
static float trk_out[NTRK][2][TRK_OUT_N];
static uint32_t trk_out_w;
static int32_t trk_pre[2][CTL];
static __attribute__((noinline)) void mix_own(uint32_t k, uint32_t n, int32_t mg)
{
    uint32_t i, w;
    for (i = 0; i < n; i++) {
        trk_pre[0][i] = mix_l[i];
        trk_pre[1][i] = mix_r[i];
    }
    mix_part(&trk[k], n);
    for (i = 0, w = trk_out_w; i < n; i++, w++) {        /* the track's share of the mix, taken back out of it */
        int32_t dl = mix_l[i] - trk_pre[0][i], dr = mix_r[i] - trk_pre[1][i];
        mix_l[i] = trk_pre[0][i];
        mix_r[i] = trk_pre[1][i];
        trk_out[k][0][w % TRK_OUT_N] = (float)(((dl >> 2) * mg) >> 10) * (1.0f / 65536.0f);   /* (Q15 << OUT_SHIFT, */
        trk_out[k][1][w % TRK_OUT_N] = (float)(((dr >> 2) * mg) >> 10) * (1.0f / 65536.0f);   /* as the main: 1/2^23) */
    }
}
static int tgt_track_out(uint32_t k, uint32_t n, int32_t mg)
{
    if ((trk_own >> k) & 1u) {
        mix_own(k, n, mg);
        return 1;
    }
    if (trk_own) {                                    /* (the others' pairs silent while any track has one) */
        uint32_t j;
        for (j = 0; j < n; j++)
            trk_out[k][0][(trk_out_w + j) % TRK_OUT_N] = trk_out[k][1][(trk_out_w + j) % TRK_OUT_N] = 0.0f;
    }
    return 0;
}
static void tgt_mix_end(uint32_t n) { trk_out_w += n; }

/* ---- the tracker's place (seq_locate) */
/* (Fel-Tracker) the plugin's host put the song somewhere (seq.c seq_locate, after tk_start): s samples from where PLAY
 * starts it (LOOP row 0 of the pattern, SONG the position picked), round and round as it plays; the rows' own lengths
 * (the swing). A place inside a row: that row is not played, the next one is, on time; less than `late` samples
 * into it, it plays at once (the host's start reaches the device a block or two after it: its first row) */
static void tk_locate(uint64_t s, uint32_t late)
{
    uint64_t cyc = 0;
    uint32_t i, r, len;
    if (tk_pl.mode == TK_SONG)
        for (i = 0; i < tk.norder; i++)
            for (r = 0; r < tk_len(TK_OPAT(tk.order[i])); r++)
                cyc += tk_row_len(r);
    else
        for (r = 0; r < tk_len(tk_pl.pat); r++)
            cyc += tk_row_len(r);
    tk_pl.row = 0;
    if (!cyc)
        return;
    s %= cyc;
    while (s >= (len = tk_row_len(tk_pl.row))) {
        s -= len;
        if (tk_pl.row + 1u >= tk_len(tk_pl.pat)) {
            tk_pl.row = 0;
            tk_next_pattern();
        } else {
            tk_pl.row++;
        }
    }
    tk_pl.row_pos = (uint32_t)s;
    tk_pl.fresh = s < late;                     /* (tk_tick plays it, row_pos kept: the next rows on time) */
}

/* ---- the host's transport (seq.c events_block) */
/* (Fel-Tracker) the plugin's host (plugin/core/fel_core.h transport): its tempo, its transport and its place, always,
 * over BPM, CLOCK and PLAY. The host says which beat it is at a frame of the device's (frames since power-on, as
 * events_block counts them) and its tempo; each block the sequencer is moved on to where that puts it at the block's
 * end (sample exact, whatever the device's whole-sample beat), and put there anew when the host starts or jumps (a
 * loop, a locate). Tempo alone while it is stopped: the delay, the arp, the LFOs follow it. Off until the host speaks */
static struct {
    uint8_t on, playing;
    double bpm, beats0;                     /* the host's tempo, its beat at frame0 */
    uint64_t frame0;
    double at;                              /* the beat the sequencer is at */
} host_tr;
static uint64_t dev_frames;                 /* frames rendered since power-on */

/* the transport running from beat b (>= 0): the song there (tracker.c tk_locate), the metronome's, the FX layer's,
 * the SLICER's and the synced LFOs' places with it */
static void seq_locate(double b)
{
    double lb = 4.0 * div_samples(2), q = b * 4.0, f;
    uint32_t i, p = div_samples(2), six;
    b = b < 0.0 ? 0.0 : b;
    seq_start();
    tk_locate((uint64_t)(b * lb + 0.5), 2u * HALF_FRAMES);
    six = (uint32_t)q;
    f = q - six;
    if (six & 15u || f > 0.0) {                    /* (on a bar's first 1/16: CLK_START, as PLAY) */
        clk_step = f > 0.0 ? six & 15u : (six - 1u) & 15u;
        clk_pos = f > 0.0 ? (uint32_t)(f * p) : p;
    }
    beat_pos = (uint32_t)((b - (double)(uint32_t)b) * beat_samples());
    beat_n = (uint32_t)b & 3u;
    pf.ph = (uint32_t)(f * (beat_samples() >> 2));
    pf.sync = 0;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        double st = b * SL_DEN[(uint32_t)tp_vo(t, VO_SLRATE) % 6u];
        uint32_t k = (uint32_t)st;
        sl_t *s = &sl[i];
        if (st > (double)k) {                      /* inside a step: entered, then on by the part already gone */
            s->idx = (uint8_t)((k - 1u) & 15u);
            sl_enter(t, s);
            s->pos = (uint32_t)((st - k) * s->base);
            s->pos = s->pos < s->len ? s->pos : s->len - 1u;
        } else {
            s->idx = (uint8_t)((k - 1u) & 15u);
        }
        if (t->p[P_LTRIG] && t->p[P_LSYNC])        /* (seq_start set the phase: on by the blocks since beat 0) */
            t->lfo_ph += lfo_sync_inc(t) * (uint32_t)(b * lb / CTL);
    }
}

/* the samples the sequencer moves on in this block of n (events_block, the host's transport on) */
static uint32_t host_block(uint32_t n)
{
    double spb = 60.0 * FS / (host_tr.bpm > 1.0 ? host_tr.bpm : 1.0);
    double now = host_tr.beats0 + (double)(int64_t)(dev_frames - host_tr.frame0) / spb, lb, s;
    uint32_t k;
    host_beat_samples = (uint32_t)(spb + 0.5);
    transport_req = 0;                             /* (PLAY shows the host's transport, does not change it) */
    if (!host_tr.playing) {
        if (song.playing)
            seq_stop();
        return 0;
    }
    if (!song.playing || now < host_tr.at - 1.0 / 64.0 || now > host_tr.at + 1.0) {
        if (song.playing)
            seq_stop();
        seq_locate(now);
        host_tr.at = now;
        if (tk_pl.fresh)
            return 0;                              /* (its row plays now; the block's samples with the next) */
    }
    lb = 4.0 * div_samples(2);
    s = (now + n / spb - host_tr.at) * lb;
    s = s < 0.0 ? 0.0 : s > 4.0 * n ? 4.0 * n : s;
    k = (uint32_t)(s + 0.5);
    host_tr.at += k / lb;
    return k;
}

static uint32_t tgt_seq_block(uint32_t n, uint32_t *clock_mode)
{
    if (!host_tr.on)
        return n;
    *clock_mode = 0;                                  /* (the host's clock: no other) */
    return host_block(n);
}
static void tgt_block_end(uint32_t n) { dev_frames += n; }
