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

/* (2026-10-23) GLO > MIDI IN: each track its input (P_MIN: 1 USB, 2 TRS, 3 BOTH, 0 OFF; the sources' bits) and its
 * channel (P_MCH: 0 its number, 1..16, 17 ALL: midi_trk_ch), in place of MENU > MIDI > MIDI IN's blocks (G_ROUTE, inert since). An event of
 * channel ch from midi_src (1 USB, 2 TRS; 0: any, outside an event) plays the first track that listens to both */
static uint8_t midi_src;
static uint32_t midi_trk_ch(uint32_t k)            /* track k's channel 0..15, 16 ALL */
{
    uint32_t c = (uint32_t)trk[k].p[P_MCH];
    return c == 0u ? k % 16u : c <= 16u ? c - 1u : 16u;
}
static track_t *midi_match(uint32_t ch)           /* (a track on the channel itself first, then one on ALL) */
{
    uint32_t k, in, c, pass;
    for (pass = 0; pass < 2u; pass++)
        for (k = 0; k < NPART; k++) {
            in = (uint32_t)trk[k].p[P_MIN];
            c = midi_trk_ch(k);
            if (in && (!midi_src || (in & midi_src)) && (pass ? c == 16u : c == ch))
                return &trk[k];
        }
    return 0;
}
/* MIDI channel 0..15 of track i (keys -> MIDI out): its own (ALL: its number) */
static uint32_t trk_midi_ch(uint32_t i)
{
    uint32_t c = midi_trk_ch(i % NTRK);
    return c < 16u ? c : i % NTRK;
}

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
static int player_key_on(track_t *t, uint32_t note, uint32_t vel);   /* (player.c: a player's keys: the ARP's) */
static int player_key_off(track_t *t, uint32_t note);
/* (2026-10-17) what the baseline asks of the instrument around it (Blue Hare OS: keyboard.c; a host without keys: none):
 * the keys' block (scanned, played through input_on / input_off, once a block in events_block) and whether a key holds a
 * note on track t (MIDI's All Notes Off leaves it) */
static void os_keys_block(void);
static int os_keys_hold_track(uint32_t track);
static int os_keys_hold_note(uint32_t track, uint32_t note);   /* (a key held on track sounds note) */
static void input_on(track_t *t, uint32_t note, uint32_t vel)   /* (2026-10-18: the patch played; no layers) */
{
    last_note = (uint8_t)note;
    if (player_key_on(t, note, vel))               /* (2026-10-17) a player playing (the ARP): the key its way */
        return;
    trk_note_on(t, note, vel);
}

static int midi_note_held(const track_t *t, uint32_t note);
static int midi_local_held(const track_t *t, uint32_t note);
static void input_off(track_t *t, uint32_t note)
{
    if (midi_note_held(t, note))
        return;
    if (player_key_off(t, note))
        return;
    trk_note_off(t, note);
}


#include "patch.c"                 /* (Blue Hare, 2026-10-17) a sound loaded into a layer */
#include "arp.c"                   /* (2026-10-23) the arpeggiator on the ARP button (TECHNO archived) */
#include "player.c"                /* (2026-10-17) the players' interface: GEN the first */

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

/* MIDI in: the track channel ch plays (midi_match; none: the event is dropped, midi_event). The map's signature
 * events_block last saw: a change lets go of what the channels no longer heard hold (midi_route_block) */
static uint32_t midi_route = 0xFFFFFFFFu;
static uint32_t midi_route_sig(void)
{
    uint32_t k, h = 0;
    for (k = 0; k < NPART; k++)
        h = h * 131u + (uint32_t)trk[k].p[P_MIN] * 17u + (uint32_t)trk[k].p[P_MCH];
    return h;
}
static int midi_in_block(uint32_t ch) { return midi_match(ch) != 0; }
static track_t *midi_track(uint32_t ch)
{
    track_t *t = midi_match(ch);
    return t ? t : TSEL;
}

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
    if (midi_route != midi_route_sig()) {              /* the map changed: the channels no longer heard let go */
        midi_route = midi_route_sig();
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
        } else {
            midi_src = (uint8_t)source;              /* (the event's input: midi_match) */
            midi_event(st, ch, d1, d2);
            midi_src = 0;
        }
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
    if (player())
        player()->block(clock_mode ? seq_n : n, n);   /* (the player, the ARP: the clock's samples, and time's) */
    if (seq_hook)
        seq_hook(song.playing ? seq_n : 0u);
    if (song.playing)
        click_tick(seq_n);
    if (song.playing)
        song.tick++;
    tgt_block_end(n);
}

/* (the host targets' side of target.h: native/target_host.c, Native's, included after this file by the build that
 * wants it: tests/lib/hostsim.c for BLUEHARE_NATIVE) */
