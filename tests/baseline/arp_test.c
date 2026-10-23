/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-23) The arpeggiator back (firmware/baseline/arp.c, the player on ARP): stopped, a key plays as before;
 * started, a held chord plays its notes one after another at RATE (UP: low to high), each track its own list,
 * the steps stop with the keys; HOLD latches the chord, a new chord replaces it; OCT 2 the list an octave up again;
 * GATE shortens the notes; CHORD plays them together; MODE OFF on a track leaves its keys alone; stopping it ends
 * its notes and the keys play directly again; the visuals' roles are the tracks (BASS DRUMS VOICE).
 *   arp_test */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, int ok)
{
    printf("arp: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint8_t seq[64];                                 /* the notes track 0's arp started, in order */
static uint32_t nseq;
static void render(uint32_t n)
{
    int32_t o[2u * CTL];
    uint32_t f, last = 0;
    for (f = 0; f < n; f += CTL) {
        uint8_t before = ar.t[0].note;
        mix_block(o, CTL);
        if (ar.t[0].note && ar.t[0].note != before && nseq < 64u && ar.t[0].played != last)
            seq[nseq++] = ar.t[0].note;
        last = ar.t[0].played;
    }
}
static uint32_t sounding(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate;
    return n;
}

int main(void)
{
    track_t *t = &trk[0], *t3 = &trk[2];
    uint32_t i, p0, p1, ok;
    host_tracks_init();
    song.g[G_BPM] = 120;
    host_preset(t, ENGI_SID, 0);
    host_preset(t3, ENGI_VOSIM, 0);
    t->p[P_VOICE] = t3->p[P_VOICE] = V_POLY;
    render(CTL);

    input_on(t, 60, 100);
    render(CTL);
    check("stopped: a key plays its note at once, the arp untouched", sounding(t) == 1u && ar.t[0].played == 0u && !ar.on);
    input_off(t, 60);
    render(FS / 4u);

    player()->request(1);
    render(CTL);
    check("ARP started (the player's interface): playing", player()->playing() && ar.on);
    t->p[P_AMODE] = AM_UP;
    t->p[P_ARATE] = 2;                                  /* (1/16 at 120 BPM: 8 a second) */
    t->p[P_AOCT] = 1;
    t->p[P_AGATE] = 64;
    nseq = 0;
    input_on(t, 60, 100);
    input_on(t, 64, 100);
    input_on(t, 67, 100);
    render(FS);
    p0 = ar.t[0].played;
    check("a held chord: about 8 steps a second at 1/16, 120 BPM", p0 >= 7u && p0 <= 9u);
    ok = nseq >= 6u;
    for (i = 0; i + 2u < nseq && ok; i++)
        ok = seq[i + 1u] == (seq[i] == 60u ? 64u : seq[i] == 64u ? 67u : 60u);
    check("UP: C E G, round again", ok);
    input_on(t3, 50, 100);
    render(FS / 2u);
    check("track 3 its own list: its arp plays its key, track 1's list keeps its three",
          ar.t[2].played >= 3u && ar.t[2].nheld == 1u && ar.t[0].nheld == 3u);
    input_off(t3, 50);
    input_off(t, 60);
    input_off(t, 64);
    input_off(t, 67);
    render(FS / 4u);
    p1 = ar.t[0].played;
    render(FS / 2u);
    check("the keys let go: no more steps, the note ended", ar.t[0].played == p1 && sounding(t) == 0u);

    t->p[P_AHOLD] = 1;                                  /* HOLD: the chord latched */
    input_on(t, 48, 100);
    input_on(t, 55, 100);
    input_off(t, 48);
    input_off(t, 55);
    p1 = ar.t[0].played;
    render(FS / 2u);
    check("HOLD: the chord plays on after the keys let go", ar.t[0].played >= p1 + 3u && ar.t[0].nheld == 2u);
    input_on(t, 62, 100);
    input_off(t, 62);
    render(FS / 4u);
    check("HOLD: a new chord replaces the latched one", ar.t[0].nheld == 1u && ar.t[0].held[0] == 62u);
    t->p[P_AHOLD] = 0;
    render(CTL);
    check("HOLD off with no key down: the latched chord goes", ar.t[0].nheld == 0u);
    render(FS / 4u);

    t->p[P_AOCT] = 2;                                   /* OCT 2: the list again an octave up */
    nseq = 0;
    input_on(t, 60, 100);
    input_on(t, 67, 100);
    render(FS);
    ok = 0;
    for (i = 0; i < nseq; i++)
        ok |= seq[i] == 72u || seq[i] == 79u;
    check("OCT 2: the notes an octave up in the list too", ok);
    t->p[P_AGATE] = 20;                                 /* GATE short: the note ends before the next step */
    render(FS / 4u);
    for (i = ok = 0; i < 40u; i++) {
        render(CTL);
        ok += sounding(t) == 0u;
    }
    check("GATE 20: silence between the steps", ok >= 10u);
    t->p[P_AGATE] = 100;
    t->p[P_AMODE] = AM_CHORD;
    input_on(t, 64, 100);
    render(FS / 2u);
    check("CHORD: the held notes together (three sounding)", sounding(t) >= 3u);
    input_off(t, 60);
    input_off(t, 64);
    input_off(t, 67);
    render(FS / 4u);

    t->p[P_AMODE] = AM_OFF;                             /* MODE OFF: the track's keys play directly */
    input_on(t, 60, 100);
    render(CTL);
    check("MODE OFF on a track: its key plays directly while the arp runs", sounding(t) == 1u && ar.t[0].nheld == 0u);
    input_off(t, 60);
    t->p[P_AMODE] = AM_UP;
    input_on(t, 60, 100);
    render(FS / 4u);
    player()->request(0);
    render(CTL);
    check("ARP stopped: its notes end, the lists cleared", !ar.on && sounding(t) == 0u && ar.t[0].nheld == 0u);
    input_off(t, 60);
    input_on(t, 65, 100);
    render(CTL);
    check("after the stop a key plays directly again", sounding(t) == 1u);
    input_off(t, 65);
    check("the visuals' roles: the tracks, 1 BASS 2 DRUMS 3 VOICE", player()->nroles == NPART &&
          str_eq(player()->roles[0], "BASS") && str_eq(player()->roles[1], "DRUMS") && str_eq(player()->roles[2], "VOICE"));
    printf(fails ? "arp: %d FAILED\n" : "arp test passed\n", fails);
    return fails ? 1 : 0;
}
