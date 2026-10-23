/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-23) REC: the multitrack loop recorder, the user's "the instrument is meant to be sequenced externally ...
 * a record mode that is able to record multitrack ... remember this is a performance instrument". What an external
 * sequencer (or the keys) plays on the tracks, recorded into each track's loop, the loops played back after.
 *   REC armed, the take starts on the next bar's downbeat (PLAY or the sequencer's Start: at once) and runs LEN bars
 *   (the SEQ button's LOOP page: 1, 2, 4, 8, 16), then the loops play on. REC during the take ends it there.
 *   A take replaces only the tracks that get a note during it: a track's first note in the take clears its loop
 *   (its new one LEN bars long); the tracks that get nothing keep theirs.
 *   QNT: OFF as played (96 steps a beat), 1/16 or 1/32 (the note-on moved to the grid, its note-off by as much).
 *   The clock: the transport's (CLK: INT, BPM; USB or TRS: the sequencer's MIDI clock, its Start and Stop). The
 *   position counts from the transport's start (bar 1 the sequencer's first), a loop of len plays at position mod len.
 *   LOOP > CLEAR clears the selected track's loop, ALL every loop. A stop ends a take and the loops' notes.
 * The loops live in RAM only (a performance's: not saved with the project). The loop's notes go through the ARP as
 * the keys do (a held chord in a loop arpeggiated live), never back into a take.
 * Everything here runs in the audio ISR (events_block); the UI asks through rc_req. */
#define RC_MAX 768u              /* events a track (an on or an off each: ~384 notes) */
#define RC_TPB 96u               /* ticks a beat */
#define RC_BAR (4u * RC_TPB)
enum { RC_IDLE, RC_ARMED, RC_REC };
enum { RCQ_OFF, RCQ_16, RCQ_32 };
static const uint8_t RC_BARS[5] = {1, 2, 4, 8, 16};
typedef struct {
    uint16_t tick;               /* in the loop */
    uint8_t note, vel;           /* vel 0: the note-off */
} rc_ev_t;
static struct {
    rc_ev_t ev[NPART][RC_MAX];   /* a track's loop, sorted by tick (at one tick the offs first) */
    uint16_t n[NPART];
    uint16_t len[NPART];         /* the loop's length in ticks, 0 none */
    uint16_t cur[NPART];         /* the next event to play */
    uint32_t snd[NPART][4];      /* the notes the loop is sounding (a bit each) */
    uint32_t held[NPART][4];     /* the notes a take recorded on and not yet off */
    int8_t dq[NPART][128];       /* QNT's shift of a held note's on (its off moves by as much) */
    uint8_t state, taken;        /* RC_*; the tracks this take records (a bit each) */
    uint8_t playing, full;       /* the transport as last seen; a take ran out of room (the UI says so) */
    uint32_t now, due, acc;      /* the position (ticks since the start), the next tick to play, the remainder */
    uint32_t start, tlen;        /* the take's first tick, its length */
} rl;
static volatile uint8_t rc_req;  /* the UI's: 1 REC, 2 clear the selected track's loop, 3 clear every loop */

static int rc_bit(const uint32_t *b, uint32_t note) { return (b[note >> 5] >> (note & 31u)) & 1u; }
static void rc_set(uint32_t *b, uint32_t note, int on)
{
    if (on)
        b[note >> 5] |= 1u << (note & 31u);
    else
        b[note >> 5] &= ~(1u << (note & 31u));
}

static int midi_note_held(const track_t *t, uint32_t note);
static uint32_t beat_samples(void);              /* (fx.c) */
/* a loop's note: through the ARP as a key's, not recorded */
static void rc_on(uint32_t k, uint32_t note, uint32_t vel)
{
    track_t *t = &trk[k];
    if (rc_bit(rl.snd[k], note) && !player_key_off(t, note))
        trk_note_off(t, note);
    rc_set(rl.snd[k], note, 1);
    if (!player_key_on(t, note, vel))
        trk_note_on(t, note, vel);
}
static void rc_off(uint32_t k, uint32_t note)
{
    track_t *t = &trk[k];
    if (!rc_bit(rl.snd[k], note))
        return;
    rc_set(rl.snd[k], note, 0);
    if (midi_note_held(t, note))                     /* (the sequencer holds it too: it keeps sounding) */
        return;
    if (!player_key_off(t, note))
        trk_note_off(t, note);
}
static void rc_silence(uint32_t k)               /* the loop's sounding notes end */
{
    uint32_t note;
    for (note = 0; note < 128u; note++)
        if (rc_bit(rl.snd[k], note))
            rc_off(k, note);
}
static void rc_clear(uint32_t k)
{
    rc_silence(k);
    rl.n[k] = rl.len[k] = rl.cur[k] = 0;
    memset(rl.held[k], 0, sizeof rl.held[k]);
    rl.taken &= (uint8_t)~(1u << k);
}

/* an event into track k's loop, in its place (after those of its tick and kind: the offs before the ons) */
static int rc_put(uint32_t k, uint32_t tick, uint32_t note, uint32_t vel)
{
    rc_ev_t *e = rl.ev[k];
    uint32_t lo = 0, hi = rl.n[k], key = tick * 2u + (vel != 0u);
    if (rl.n[k] >= RC_MAX || (vel && rl.n[k] >= RC_MAX - 32u)) {   /* (room kept for the held notes' offs) */
        rl.full = 1;
        return 0;
    }
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        if ((uint32_t)e[mid].tick * 2u + (e[mid].vel != 0u) <= key)
            lo = mid + 1u;
        else
            hi = mid;
    }
    for (hi = rl.n[k]; hi > lo; hi--)                 /* (the later ones a place on: no memmove on the device) */
        e[hi] = e[hi - 1u];
    e[lo].tick = (uint16_t)tick;
    e[lo].note = (uint8_t)note;
    e[lo].vel = (uint8_t)vel;
    rl.n[k]++;
    return 1;
}

static void rc_tick(uint32_t tick);
static void rc_catch_up(void)                   /* the ticks due played first: a note on the downbeat in its take */
{
    while (rl.due <= rl.now)
        rc_tick(rl.due++);
}

static uint32_t rc_q(void) { return song.g[G_GSCL] == RCQ_16 ? RC_TPB / 4u : song.g[G_GSCL] == RCQ_32 ? RC_TPB / 8u : 1u; }

/* a key or the sequencer's note on track t (input_on / input_off): into the take */
static void rec_input(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t k = (uint32_t)(t - trk), q, at;
    int32_t d;
    if (k >= NPART || note > 127u || !song.playing)
        return;
    rc_catch_up();
    if (rl.state != RC_REC)
        return;
    if (vel && !((rl.taken >> k) & 1u)) {           /* the track's first note in the take: its loop replaced */
        rc_clear(k);
        rl.taken |= (uint8_t)(1u << k);
        rl.len[k] = (uint16_t)rl.tlen;
    }
    if (!((rl.taken >> k) & 1u))
        return;
    if (vel) {
        q = rc_q();
        at = (rl.now + q / 2u) / q * q;               /* QNT: the nearest step */
        d = (int32_t)at - (int32_t)rl.now;
        rl.dq[k][note] = (int8_t)d;
        if (rc_put(k, at % rl.len[k], note, vel))
            rc_set(rl.held[k], note, 1);
    } else if (rc_bit(rl.held[k], note)) {
        at = (uint32_t)((int32_t)rl.now + rl.dq[k][note]);
        rc_put(k, at % rl.len[k], note, 0);
        rc_set(rl.held[k], note, 0);
    }
}

static void rc_take_end(void)                    /* the take's held notes end at its end (the loop's point) */
{
    uint32_t k, note;
    for (k = 0; k < NPART; k++)
        if ((rl.taken >> k) & 1u) {
            for (note = 0; note < 128u; note++)
                if (rc_bit(rl.held[k], note))
                    rc_put(k, (rl.start + rl.tlen) % rl.len[k], note, 0);
            memset(rl.held[k], 0, sizeof rl.held[k]);
            rl.cur[k] = 0;
            while (rl.cur[k] < rl.n[k] && rl.ev[k][rl.cur[k]].tick < rl.now % rl.len[k])
                rl.cur[k]++;
        }
    rl.taken = 0;
    rl.state = RC_IDLE;
}

static void rc_tick(uint32_t tick)               /* tick's events: the take's start and end, the loops' notes */
{
    uint32_t k;
    if (rl.state == RC_ARMED && tick % RC_BAR == 0u) {
        rl.state = RC_REC;
        rl.start = tick;
        rl.tlen = RC_BAR * RC_BARS[clamp(song.g[G_GPRE], 0, 4)];
        rl.taken = 0;
        rl.full = 0;
    } else if (rl.state == RC_REC && tick >= rl.start + rl.tlen)
        rc_take_end();
    for (k = 0; k < NPART; k++) {
        uint32_t lp;
        const rc_ev_t *e = rl.ev[k];
        if (!rl.len[k] || ((rl.taken >> k) & 1u))    /* (a track recording: its new loop not yet played) */
            continue;
        lp = tick % rl.len[k];
        if (lp == 0u)
            rl.cur[k] = 0;
        while (rl.cur[k] < rl.n[k] && e[rl.cur[k]].tick < lp)
            rl.cur[k]++;
        while (rl.cur[k] < rl.n[k] && e[rl.cur[k]].tick == lp) {
            const rc_ev_t *x = &e[rl.cur[k]++];
            if (x->vel)
                rc_on(k, x->note, x->vel);
            else
                rc_off(k, x->note);
        }
    }
}

/* events_block: n samples the transport moved on (the internal clock's, or the MIDI clock's) */
static void rec_block(uint32_t n)
{
    uint32_t k, req = rc_req, bs;
    if (req) {
        rc_req = 0;
        if (req == 1u && rl.state == RC_REC)
            rc_take_end();                            /* REC in the take: it ends there */
        else if (req == 1u)
            rl.state = rl.state == RC_IDLE ? RC_ARMED : RC_IDLE;
        else if (req == 2u)
            rc_clear(song.sel % NPART);
        else
            for (k = 0; k < NPART; k++)
                rc_clear(k);
    }
    if (!song.playing) {
        if (rl.playing) {                             /* stopped: the take ends, the loops' notes too */
            if (rl.state == RC_REC)
                rc_take_end();
            for (k = 0; k < NPART; k++) {
                rc_silence(k);
                rl.cur[k] = 0;
            }
            rl.playing = 0;
        }
        rl.now = rl.due = rl.acc = 0;
        return;
    }
    rl.playing = 1;
    rc_catch_up();                                    /* (the start's tick 0 at once) */
    bs = beat_samples();
    rl.acc += n * RC_TPB;
    while (bs && rl.acc >= bs) {
        rl.acc -= bs;
        rl.now++;
    }
}

/* for the UI: 0 idle, 1 armed, 2 recording; the take's bar (1 ..) and its bars; a track's loop in bars (0 none) */
static uint32_t rec_state(void) { return rl.state; }
static uint32_t rec_take_bar(void) { return rl.state == RC_REC ? (rl.now - rl.start) / RC_BAR + 1u : 0u; }
static uint32_t rec_take_bars(void) { return rl.tlen / RC_BAR; }
static uint32_t rec_loop_bars(uint32_t k) { return k < NPART ? rl.len[k] / RC_BAR : 0u; }
static uint32_t rec_loop_pos(uint32_t k) { return k < NPART && rl.len[k] ? rl.now % rl.len[k] * 256u / rl.len[k] : 0u; }
static int rec_any(void)
{
    uint32_t k, any = 0;
    for (k = 0; k < NPART; k++)
        any |= rl.len[k];
    return any != 0u;
}
