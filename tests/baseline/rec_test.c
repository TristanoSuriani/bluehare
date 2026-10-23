/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-23) REC, the loop recorder (firmware/baseline/rec.c): armed before PLAY, the take starts at once and runs
 * LEN bars, then the loop plays its notes where they were played; armed while playing, on the next bar's downbeat; a
 * take replaces only the tracks that got notes (a track's first note clears its loop), the others play on through it;
 * QNT 1/16 moves a note-on to the grid and its note-off by as much; REC in the take ends it there; a held note ends
 * at the loop's point; a stop ends the loops' notes; CLEAR and ALL; the MIDI clock: Start begins the take, 24 pulses
 * a beat move it.
 *   rec_test */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, int ok)
{
    printf("rec: %-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void render(uint32_t blocks)
{
    int32_t o[2u * CTL];
    while (blocks--)
        mix_block(o, CTL);
}
static void to_tick(uint32_t tk)                        /* until the position is tk */
{
    uint32_t guard = 200000u;
    while (rl.now < tk && guard--)
        render(1);
}
static int gated(const track_t *t, uint32_t note)       /* note sounding, its key down */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
static int near(uint32_t a, uint32_t b) { return a + 1u >= b && a <= b + 1u; }

int main(void)
{
    track_t *t1 = &trk[0], *t3 = &trk[2];
    uint32_t ok, b0;
    host_tracks_init();
    song.g[G_BPM] = 120;
    song.g[G_CLOCK] = 0;
    host_preset(t1, ENGI_SID, 0);
    host_preset(t3, ENGI_VOSIM, 0);
    t1->p[P_VOICE] = t3->p[P_VOICE] = V_POLY;
    t1->p[P_SUS] = t3->p[P_SUS] = 127;
    song.g[G_GPRE] = 0;                                 /* BARS 1 */
    song.g[G_GSCL] = RCQ_OFF;
    render(2);

    rc_req = 1;                                         /* REC, stopped: armed */
    render(1);
    check("REC while stopped: armed, nothing recorded yet", rec_state() == RC_ARMED);
    transport_req = 1;
    render(1);
    check("PLAY: the take starts at once (tick 0, BARS 1)", rec_state() == RC_REC && rl.start == 0u && rec_take_bars() == 1u);
    to_tick(96);
    input_on(t1, 60, 100);
    to_tick(144);
    input_off(t1, 60);
    to_tick(192);
    input_on(t1, 64, 90);
    to_tick(240);
    input_off(t1, 64);
    render(1);
    check("in the take the notes go into track 1's loop (an on and an off each, at their ticks)",
          rl.n[0] == 4u && near(rl.ev[0][0].tick, 96) && rl.ev[0][0].note == 60 && near(rl.ev[0][1].tick, 144) &&
          !rl.ev[0][1].vel && near(rl.ev[0][2].tick, 192) && rl.ev[0][2].vel == 90);
    check("track 3, no notes: no loop", rl.n[2] == 0u && !rl.len[2]);
    to_tick(384 + 4);
    check("after BARS 1 the take ends; track 1's loop is one bar", rec_state() == RC_IDLE && rec_loop_bars(0) == 1u);
    to_tick(384 + 110);
    ok = gated(t1, 60) && !gated(t1, 64);
    to_tick(384 + 200);
    ok &= gated(t1, 64) && !gated(t1, 60);
    to_tick(384 + 300);
    ok &= !gated(t1, 64) && !gated(t1, 60);
    check("the loop plays it back: 60 on beat 2, then 64, both ended at their times", ok);
    to_tick(768 + 110);
    check("..and again the next bar", gated(t1, 60));

    to_tick(768 + 200);
    rc_req = 1;                                         /* armed while playing: the next downbeat */
    render(1);
    ok = rec_state() == RC_ARMED;
    to_tick(1152 - 2);
    ok &= rec_state() == RC_ARMED;
    to_tick(1152);
    render(1);
    check("REC while playing: armed until the next bar's downbeat, then the take", ok && rec_state() == RC_REC &&
          rl.start == 1152u);
    to_tick(1152 + 48);
    input_on(t3, 50, 100);
    to_tick(1152 + 96);
    input_off(t3, 50);
    to_tick(1152 + 110);
    check("..track 1's loop plays on through a take that only gives track 3 notes", gated(t1, 60));
    to_tick(1536 + 4);
    check("..then track 3 has its loop, track 1 keeps its own", rl.n[2] == 2u && rl.ev[2][0].note == 50 &&
          rl.n[0] == 4u && rec_loop_bars(2) == 1u);
    to_tick(1536 + 60);
    check("..and plays it", gated(t3, 50));

    rc_req = 1;                                         /* a take on track 1 alone: its loop replaced */
    to_tick(1920);
    render(1);
    to_tick(1920 + 20);
    input_on(t1, 67, 100);
    to_tick(1920 + 300);
    input_off(t1, 67);
    to_tick(2304 + 4);
    check("a new take on track 1: its loop replaced (67 alone), track 3's kept", rl.n[0] == 2u && rl.ev[0][0].note == 67 &&
          rl.n[2] == 2u);

    song.g[G_GSCL] = RCQ_16;                            /* QNT 1/16 (24 ticks) */
    rc_req = 1;
    to_tick(2688);
    render(1);
    to_tick(2688 + 30);
    input_on(t1, 72, 100);
    to_tick(2688 + 60);
    input_off(t1, 72);
    to_tick(3072 + 4);
    check("QNT 1/16: the on at 30 moved to 24, its off by as much (60 -> 54)",
          rl.n[0] == 2u && rl.ev[0][0].tick == 24u && near(rl.ev[0][1].tick, 54));
    song.g[G_GSCL] = RCQ_OFF;

    song.g[G_GPRE] = 2;                                 /* BARS 4, REC again in the take: it ends there */
    rc_req = 1;
    to_tick(3456);
    render(1);
    to_tick(3456 + 10);
    input_on(t1, 48, 100);
    to_tick(3456 + 500);
    rc_req = 1;
    render(1);
    check("REC in the take ends it: the loop as long as BARS (4), the held note's off at its point",
          rec_state() == RC_IDLE && rec_loop_bars(0) == 4u && rl.n[0] == 2u && !rl.ev[0][0].vel);
    input_off(t1, 48);

    to_tick(rl.now + 50);
    transport_req = 2;                                  /* STOP: the loops' notes end */
    render(2);
    check("a stop: nothing of the loops sounds, the position back to 0", !gated(t1, 48) && !gated(t3, 50) && rl.now == 0u);
    song.sel = 0;
    rc_req = 2;
    render(1);
    check("LOOP > CLEAR: the selected track's loop gone, track 3's kept", !rec_loop_bars(0) && rec_loop_bars(2) == 1u);
    rc_req = 3;
    render(1);
    check("LOOP > ALL: every loop gone", !rec_any());

    /* the sequencer's clock: Start begins the armed take, 24 pulses a beat move it */
    song.g[G_GPRE] = 0;
    song.g[G_CLOCK] = 1;
    render(4);
    rc_req = 1;
    render(1);
    {
        uint32_t p = 0, blk, sent = 0;
        double pulse = (double)FS * 0.5 / 24.0, now;
        fm1_ms = 0;
        for (blk = 0; blk < (uint32_t)(FS * 3u / CTL); blk++) {
            now = (double)blk * CTL;
            if (blk == 10u) {
                midi_enqueue(0xFu | 0xFAu << 8, 1u);
                sent = 1;
            }
            while (sent && 10.0 * CTL + p * pulse <= now + CTL - 1) {
                midi_enqueue(0xFu | 0xF8u << 8, 1u);
                p++;
            }
            render(1);
            fm1_ms = (uint32_t)((uint64_t)(blk + 1u) * CTL * 1000u / FS);
            if (blk == 12u)
                b0 = rec_state();
            if (p == 48u && sent == 1u) {
                input_on(t1, 62, 100);
                sent = 2;
            }
            if (p == 72u && sent == 2u) {
                input_off(t1, 62);
                sent = 3;
            }
        }
        check("MIDI clock: Start begins the armed take (bar 1 the sequencer's)", b0 == RC_REC);
        check("..the notes at the clock's ticks (pulse 48: beat 3, tick 192), the take over after a bar",
              rl.n[0] == 2u && rl.ev[0][0].note == 62 && rl.ev[0][0].tick + 6u >= 192u && rl.ev[0][0].tick <= 198u &&
              rec_state() == RC_IDLE && rec_loop_bars(0) == 1u);
    }
    printf(fails ? "rec: %d FAILED\n" : "rec test passed\n", fails);
    return fails ? 1 : 0;
}
