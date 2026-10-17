/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* Note input, scale and transport (the sequencer: a placeholder); the FM-1's keys: keyboard.c (2026-10-17).
 * Everything here runs in the audio ISR, once per CTL-sample block, and
 * ends in trk_note_on / trk_note_off: engines never see where a note came from.
 * Four tracks, one transport: every track's pattern loops on its own LEN / DIV /
 * SWING / GATE (polymeter). The keys play the selected track; MIDI IN by MENU > MIDI > MIDI IN (G_ROUTE):
 * CH1-4 (0) channels 1..4 play parts 1..4, channels 5..16 are ignored (midi_control.c midi_event); SEL (1)
 * every channel the selected track; 1.2: CH5-8, CH9-12, CH13-16 as CH1-4 on those four channels (midi_base). Their CC1 (mod wheel), CC11 (expression) and channel aftertouch go to
 * the same track's modulation matrix (mod.c; the selected track: the one selected then).
 * The sequencer is a placeholder (SEQ shows TODO): PLAY runs the transport alone (the click, the synced LFOs). A key or MIDI note plays a
 * chord when the track's CHRD is on (chord.c): its notes go through the same input as so many keys. */
static const uint16_t SCALE_MASK[] = {
    0xFFF,                                   /* CHR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MAJ */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* MIN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* DOR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* MIX */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* PEN */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         /* MPEN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),   /* HARM */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* PHRY */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),   /* LYD */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 10),   /* LOC */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MEL (ascending) */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              /* BLUES (minor) */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),              /* WHOLE */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 10), /* DIMHW */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11), /* DIMWH */
};

#define KB_SILENT 255u
static uint8_t last_note = 60;
static volatile uint8_t transport_req;   /* 1 start, 2 stop, 3 restart (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: release every sounding note (preset / engine change) */
static volatile uint8_t midi_hint;       /* MIDI IN played track n - 1, which is not the selected one (the UI says so) */

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

/* MENU > MIDI > MIDI IN (G_ROUTE, up to 1.1.5 GLO > SYSTEM ROUT): the first channel (0..15) of the block of four that
 * plays tracks 1..4, -1 with SEL. Stored
 * 0 CH1-4, 1 SEL, then (1.2, Discussion #143) 2 CH5-8, 3 CH9-12, 4 CH13-16 */
static int32_t midi_base(void)
{
    uint32_t r = (uint32_t)song.g[G_ROUTE];
    return r == 1u ? -1 : r ? (int32_t)(r - 1u) * NPART : 0;   /* (a block a track count wide: 4, or 8 with TRACKS 8) */
}
/* MIDI channel 0..15 of track i (keys -> MIDI out): its channel in ROUT's block (SEL, CH1-4: 1..4) */
static uint32_t trk_midi_ch(uint32_t i) { return (midi_base() > 0 ? (uint32_t)midi_base() : 0u) + i % NTRK; }

/* a track of hits (engine_t.oneshot: no bend, no sustain pedal) */
static int drum_track(const track_t *t) { return ENGINES[eng_idx(t->eng_req)]->oneshot; }


static uint32_t scale_mask(const track_t *t)
{
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, sizeof SCALE_MASK / sizeof SCALE_MASK[0] - 1)];
}

/* note n moved down onto the track's scale (ROOT, SCALE): QNT SNAP's keys */
static int32_t scale_snap(const track_t *t, int32_t n)
{
    uint32_t mask = scale_mask(t), guard = 12;
    while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
        n--;
    return n;
}

enum { QN_OFF, QN_SNAP, QN_WHITE };             /* P_QUANT */

#include "chord.c"

/* the metronome (click.c; 1.1, Discussion #131). The click follows the song tempo's quarter notes
 * (BPM, or the external clock's 24 pulses), 4 to the bar, the first accented, whatever a track's DIV or SWING: a beat is
 * four 1/16 steps as the sequencer counts them (click_beat_len: 4 x div_samples(1/16), which may be a few samples
 * shorter than beat_samples, so the click never drifts from the 1/16 tracks it is recorded against). clk_pos and
 * clk_step count the 1/16 steps as seq_tick counts a 1/16 track's (and midi_clock_pulse rescales clk_pos with them when
 * an external clock's tempo moves): CLK_START at seq_start clicks the first beat in the block step 0 starts in, and
 * beat k in the block a 1/16 track's step 4k starts in (every 4th step; step 0 of 16, the bar's first, accented).
 * 1.5: no count-in (MENU > COUNT-IN is gone): PLAY plays at once, armed or not. */
#define CLK_START 0xFFFFFFFFu
static uint32_t clk_pos = CLK_START, clk_step;  /* samples into the 1/16 step, the step of the bar (0..15) */
static uint32_t click_beat_len(void) { return 4u * div_samples(2); }   /* DIV 2: 1/16 */
static int click_wanted(void) { return click_mode == CLICK_ON; }

/* -------------------------------------------------------- note input --- */
/* (Blue Hare, 2026-10-16) the layers (core.h lay): a note played goes to the ON layers its ranges reach (LM_STACK), or
 * to the next of them in turn (LM_ROBIN), each at the note plus its TRANS; with no layer ON, to the track it was played
 * on (the selected one; MIDI IN's channel's) as before. lay_tn[note][k]: the note layer k sounds for it (0xFF none),
 * so its release ends exactly what it started, whatever changed since */
static uint8_t lay_tn[128][NTRK];
static uint8_t lay_rr;                     /* LM_ROBIN: the layer that played last */
static uint8_t lay_init;
static int layers_on(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (lay.on[k])
            return 1;
    return 0;
}
static uint32_t layer_targets(uint32_t note, uint32_t vel)   /* the layers a note of vel reaches (a bit each) */
{
    uint32_t k, m = 0;
    for (k = 0; k < NTRK; k++)
        if (lay.on[k] && note >= lay.klo[k] && note <= lay.khi[k] && vel >= lay.vlo[k] && vel <= lay.vhi[k])
            m |= 1u << k;
    if (lay.mode == LM_ROBIN && m) {           /* the next one after the last that played */
        for (k = 1; k <= NTRK; k++) {
            uint32_t j = (lay_rr + k) % NTRK;
            if ((m >> j) & 1u) {
                lay_rr = (uint8_t)j;
                return 1u << j;
            }
        }
    }
    return m;
}
/* (2026-10-17) the crossfade (core.h lay.xsrc): layer k's level (Q15) at s (0 .. 127) along it: equal power between
 * the two places around s (sine and cosine of a quarter turn), the ON layers with a place taking part; a layer without
 * one (or XFADE OFF) at full level */
static int32_t lay_xgain(uint32_t k, int32_t s)
{
    int32_t p = lay.xpos[k], lo = -1, hi = 128, mn = 128, mx = -1, q;
    uint32_t j;
    if (lay.xsrc == LX_OFF || p < 0)
        return 32767;
    for (j = 0; j < NTRK; j++) {
        if (!lay.on[j] || (q = lay.xpos[j]) < 0)
            continue;
        if (q <= s && q > lo)
            lo = q;
        if (q >= s && q < hi)
            hi = q;
        mn = q < mn ? q : mn;
        mx = q > mx ? q : mx;
    }
    if (lo < 0)
        return p == mn ? 32767 : 0;                     /* (below the first place: the first) */
    if (hi > 127)
        return p == mx ? 32767 : 0;                     /* (above the last: the last) */
    if (lo == hi || (p != lo && p != hi))
        return p == lo ? 32767 : 0;
    {
        uint32_t ph = (uint32_t)(s - lo) * (0x40000000u / (uint32_t)(hi - lo));   /* (the share of a quarter turn) */
        int32_t g = sine_i(p == hi ? ph : 0x40000000u + ph);
        return g < 0 ? 0 : g > 32767 ? 32767 : g;
    }
}
/* once a block (events_block): WHEEL's levels, each gliding to its place's (~5 ms) */
static void lay_wheel(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        int32_t t = lay.xsrc == LX_WHEEL && lay.on[k] ? lay_xgain(k, lay.xval) : 32767, d = t - lay_wg[k];
        lay_wt[k] = t;
        lay_wg[k] += d > 4096 ? 4096 : d < -4096 ? -4096 : d;
    }
}
#define LAY_CHORD 1323u                    /* MONO layers: notes this close (30 ms) are a chord's, not a phrase's */
static uint32_t lay_clk, lay_last[NTRK];   /* (samples: events_block) the last note each layer took */
static int gen_play(uint32_t note, uint32_t vel);   /* (gen.c: PLAY ALONG) */
/* (2026-10-17) what the baseline asks of the instrument around it (Blue Hare OS: keyboard.c; a host without keys: none):
 * the keys' block (scanned, played through input_on / input_off, once a block in events_block) and whether a key holds a
 * note on track t (MIDI's All Notes Off leaves it) */
static void os_keys_block(void);
static int os_keys_hold_track(uint32_t track);
static int os_keys_hold_note(uint32_t track, uint32_t note);   /* (a key held on track sounds note) */
static int gen_play_off(uint32_t note);
static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t k, m;
    last_note = (uint8_t)note;
    if (gen_play(note, vel))                       /* (2026-10-17) GEN playing: the key in its scale, on its bells */
        return;
    if (!lay_init) {
        memset(lay_tn, 0xFF, sizeof lay_tn);
        lay_init = 1;
    }
    if (!layers_on() || note > 127u) {
        trk_note_on(t, note, vel);
        return;
    }
    m = layer_targets(note, vel);
    for (k = 0; k < NTRK; k++) {
        if (lay_tn[note][k] != 0xFFu) {            /* (played again before its release: that one ends first) */
            trk_note_off(&trk[k], lay_tn[note][k]);
            lay_tn[note][k] = 0xFF;
        }
        if ((m >> k) & 1u) {
            uint32_t n2 = (uint32_t)clamp((int32_t)note + lay.tr[k], 0, 127);
            int32_t g = lay.xsrc == LX_VEL ? lay_xgain(k, (int32_t)vel) : lay.xsrc == LX_KEY ? lay_xgain(k, (int32_t)note)
                                                                                           : 32767;
            if (!g)
                continue;                                /* (faded out for this note: not played) */
            voice_xg = (uint16_t)g;
            voice_chord = trk_vmode(&trk[k]) != V_POLY && lay_clk - lay_last[k] < LAY_CHORD;   /* (a MONO layer: a */
            trk_note_on(&trk[k], n2, vel);                                                     /* chord, no stutter) */
            voice_xg = 0xFFFFu;
            voice_chord = 0;
            lay_last[k] = lay_clk;
            lay_tn[note][k] = (uint8_t)n2;
        }
    }
}

static int midi_note_held(const track_t *t, uint32_t note);
static int midi_local_held(const track_t *t, uint32_t note);
static void input_off(track_t *t, uint32_t note)
{
    uint32_t k, any = 0;
    if (midi_note_held(t, note))
        return;
    if (gen_play_off(note))
        return;
    if (lay_init && note < 128u)
        for (k = 0; k < NTRK; k++)
            if (lay_tn[note][k] != 0xFFu) {        /* the layers it sounds on */
                trk_note_off(&trk[k], lay_tn[note][k]);
                lay_tn[note][k] = 0xFF;
                any = 1;
            }
    if (!any)
        trk_note_off(t, note);
}


#include "gen.c"                   /* (Blue Hare, 2026-10) GEN: the generative engine, on the ARP button */

/* -------------------------------------------------------- sequencer --- */
/* the sequencer: a placeholder for now (SEQ shows TODO). seq_hook, when set, gets every block the samples the
 * transport moved on (events_block, after the keys and MIDI IN; 0 while stopped): where the sequencer will play. The
 * host tests play their songs through it (tests/lib/hostsim.c host_seq) */
static void (*seq_hook)(uint32_t n);

/* PLAY: the transport from its start: the metronome with it */
static void seq_start(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (t->p[P_LTRIG] && t->p[P_LSYNC])        /* LFO 2 FREE + SYNC: in time with the bars from PLAY */
            t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
    }
    song.tick = 0;
    clk_pos = CLK_START;                           /* the metronome's first beat now */
    song.playing = 1;
}

static void seq_stop(void)
{
    song.playing = 0;
}

/* MIDI in: the track a channel plays (0..15): G_ROUTE CH1-4 (0) channels 1..4 their parts (5..16 never get
 * here: midi_event drops them), SEL (1) every channel the selected track; CH5-8 .. CH13-16 (2..4) as CH1-4 on
 * their four channels (midi_base) */
static uint8_t midi_route;                 /* the G_ROUTE events_block last saw (a change to a block: midi_route_block) */
static int midi_in_block(uint32_t ch)      /* ch is one of ROUT's block (not with SEL) */
{
    int32_t b = midi_base();
    return b >= 0 && ch - (uint32_t)b < NPART;
}
static track_t *midi_track(uint32_t ch) { return midi_in_block(ch) ? &trk[ch - (uint32_t)midi_base()] : TSEL; }

#include "midi_control.c"
#include "midi_clock.c"

/* the metronome's beat: adv samples on while the transport runs (click.c sounds it in this block) */
static void click_tick(uint32_t adv)
{
    uint32_t p = div_samples(2), beat = 0;
    if (clk_pos == CLK_START) {
        clk_pos = 0;
        clk_step = 0;
        beat = 2;
    } else {
        clk_pos += adv;
        while (clk_pos >= p) {                      /* (as seq_tick: the remainder kept) */
            clk_pos -= p;
            clk_step = (clk_step + 1u) & 15u;
            if (!(clk_step & 3u))
                beat = clk_step ? 1u : 2u;
        }
    }
    if (beat && click_wanted())
        click_req = (uint8_t)beat;
}
/* the beat of the bar the click is on (0..3: 0 the accented first), -1 stopped: the playing transport's (clk_step,
 * internal or external clock). It moves in the block a beat clicks in (the header's metronome,
 * ui_draw.c, swings on it; CLICK OFF it moves all the same) */
static int32_t click_beat_now(void)
{
    if (!song.playing)
        return -1;
    return clk_pos == CLK_START ? 0 : (int32_t)(clk_step >> 2);
}

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i, pr, seq_n = n;
    uint32_t clock_mode = (uint32_t)song.g[G_CLOCK];
    seq_n = tgt_seq_block(n, &clock_mode);         /* (target.h: the plugin's host's transport over CLOCK) */
    lay_clk += n;                                  /* (the layers: the chord window, WHEEL's levels) */
    lay_wheel();
    if (midi_clock.mode != clock_mode) {
        seq_stop();
        memset(&midi_clock, 0, sizeof midi_clock);
        midi_clock.mode = (uint8_t)clock_mode;
        midi_beat_samples = 0;
    }
    if (transport_req == 1u) {
        if (clock_mode)
            midi_clock_transport(0xFAu, fm1_ms);
        else
            seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    } else if (transport_req == 3u) {                 /* GLO + PLAY: from the top without stopping (INT clock) */
        if (!clock_mode) {
            seq_stop();
            seq_start();
        }
        transport_req = 0;
    }
    if (midi_route != (uint8_t)song.g[G_ROUTE]) {     /* ROUT changed: a block lets go of the other channels */
        midi_route = (uint8_t)song.g[G_ROUTE];
        if (midi_route != 1u)
            midi_route_block();
    }
    pr = panic_req;
    panic_req = 0;
    if (midi_in_overflow) {                            /* a lost note-off must never leave a held note */
        mi_r = mi_w;
        memset(midi_sel_on, 0, sizeof midi_sel_on);
        memset(midi_ch, 0, sizeof midi_ch);
        memset(midi_owners, 0, sizeof midi_owners);
        memset(mchord, 0, sizeof mchord);
        midi_hint = 0;
        pr |= (1u << NTRK) - 1u;
        RING_PUBLISH();
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            midi_forget_track(i);
            trk_all_off(t);
        }
        engine_block(t);                              /* engine switch: fade, then switch (voice.c) */
    }
    os_keys_block();                              /* (the instrument's keys: keyboard.c) */
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], status = (pkt >> 8) & 0xFFu;
        uint32_t st = status & 0xF0u, ch = status & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        uint32_t source = midi_in_source[at] ? midi_in_source[at] : 1u, ms = midi_in_ms[at];
        mi_r++;
        if (status >= 0xF8u) {
            if (clock_mode == source) {
                if (status == 0xF8u)
                    midi_clock_pulse(ms);
                else
                    midi_clock_transport(status, ms);
            }
        } else
            midi_event(st, ch, d1, d2);
    }
    if (clock_mode) {
        seq_n = 0;
        if (song.playing) {
            uint32_t last = midi_clock.have_pulse ? midi_clock.last_ms : midi_clock.start_ms;
            if (fm1_ms - last > 500u) {
                seq_stop();                         /* cable loss must not leave a running held note */
                midi_clock.tempo_valid = 0;
            } else
                seq_n = midi_clock_advance(fm1_ms);
        }
    }
    gen_block(n);                                     /* (Blue Hare) GEN: its own clock, transport or not */
    if (seq_hook)
        seq_hook(song.playing ? seq_n : 0u);
    if (song.playing)
        click_tick(seq_n);
    if (song.playing)
        song.tick++;
    tgt_block_end(n);
}

#ifdef BLUEHARE_NATIVE
#include "target_host.c"                /* (Blue Hare) the host targets' side of target.h */
#endif
