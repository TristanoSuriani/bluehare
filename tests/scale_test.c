/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the real keyboard, recording, arp and MIDI-out paths on the host.
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static const uint8_t WHITE_KEYS[] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const struct { uint8_t count, notes[12]; } EXPECTED[] = {
    {12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {7, {0, 2, 4, 5, 7, 9, 11}}, {7, {0, 2, 3, 5, 7, 8, 10}},
    {7, {0, 2, 3, 5, 7, 9, 10}}, {7, {0, 2, 4, 5, 7, 9, 10}},
    {5, {0, 2, 4, 7, 9}}, {5, {0, 3, 5, 7, 10}},
    {7, {0, 2, 3, 5, 7, 8, 11}}, {7, {0, 1, 3, 5, 7, 8, 10}},
    {7, {0, 2, 4, 6, 7, 9, 11}}, {7, {0, 1, 3, 5, 6, 8, 10}},
    {7, {0, 2, 3, 5, 7, 9, 11}}, {6, {0, 3, 5, 6, 7, 10}},
    {6, {0, 2, 4, 6, 8, 10}}, {8, {0, 1, 3, 4, 6, 7, 9, 10}},
    {8, {0, 2, 3, 5, 6, 8, 9, 11}},
};

static void mapping_test(void)
{
    track_t *t = &trk[0];
    uint32_t s, k, w;
    int root, oct, trans;
    assert(TP[P_SCALE].max + 1 == sizeof EXPECTED / sizeof EXPECTED[0]);
    assert(sizeof SCALE_MASK / sizeof SCALE_MASK[0] == sizeof EXPECTED / sizeof EXPECTED[0]);
    t->p[P_QUANT] = 2;
    for (s = 0; s <= (uint32_t)TP[P_SCALE].max; s++) {
        uint32_t mask = 0;
        t->p[P_SCALE] = (int16_t)s;
        for (k = 0; k < EXPECTED[s].count; k++)
            mask |= 1u << EXPECTED[s].notes[k];
        assert(scale_mask(t) == mask);
        for (root = 0; root < 12; root++)
            for (oct = -3; oct <= 3; oct++)
                for (trans = -24; trans <= 24; trans++) {
                    t->p[P_ROOT] = (int16_t)root;
                    song.octave = (int8_t)oct;
                    t->p[P_TRANS] = (int16_t)trans;
                    for (k = w = 0; k < 27u; k++) {
                        uint32_t actual = kb_map(t, k);
                        if (k == WHITE_KEYS[w]) {
                            /* Four white keys precede C4; use a positive cycle
                             * offset to independently handle the lower degrees. */
                            int d = (int)w - 4 + 12 * EXPECTED[s].count;
                            int want = 60 + root + 12 * (oct + d / EXPECTED[s].count - 12)
                                + EXPECTED[s].notes[d % EXPECTED[s].count] + trans;
                            assert(actual == (uint32_t)clamp(want, 0, 127));
                            w++;
                        } else {
                            assert(actual == KB_SILENT);
                        }
                    }
                }
    }
    t->p[P_QUANT] = 0;
    song.octave = 0;
    t->p[P_TRANS] = -5;
    for (k = 0; k < 27u; k++)
        assert(kb_map(t, k) == 48u + k);
    t->p[P_QUANT] = 2;
    host_preset(t, ENGI_KIT, 0);              /* DRUM (the GM map, any part; SAMPLE PERC until 1.0.2): the first */
    t->engine = t->eng_req = ENGI_KIT;        /* C is the kick */
    for (k = 0; k < 27u; k++)
        assert(kb_map(t, k) == 29u + k);
    t->engine = t->eng_req = 0;                /* SNAP (QNT 1, the old ON): every key, rounded down */
    t->p[P_QUANT] = 1;
    t->p[P_SCALE] = 2;                         /* C minor */
    t->p[P_ROOT] = 0;
    t->p[P_TRANS] = 0;
    song.octave = 0;
    assert(kb_map(t, 11) == 63u && kb_map(t, 10) == 63u && kb_map(t, 7) == 60u && kb_map(t, 8) == 60u);
    puts("scales: all 16 scales, 12 roots, octave/transpose ranges, bypass, drums and SNAP ok");
}

static void key_events_test(void)
{
    track_t *t = &trk[0];
    uint32_t before;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    tk_defaults();                          /* (1.5: recording goes into the tracker's pattern 00) */
    kb_prev = 0;
    usb.config = 1;
    mo_w = mo_r = 0;
    t->p[P_QUANT] = 2;
    t->p[P_SCALE] = 2;                      /* C minor: E key plays Eb */
    t->p[P_AMODE] = 1;
    song.playing = song.rec = 1;
    fm1_in.notes = 1u << 8;                /* C# is silent */
    keyboard_block();
    assert(t->arp_phys == 0 && t->nheld == 0 && !tk_note(tk_at(0, 0, 0)) && mo_w == 0);
    t->p[P_QUANT] = 0;                    /* releasing a muted key stays silent */
    fm1_in.notes = 0;
    keyboard_block();
    assert(mo_w == 0);
    t->p[P_QUANT] = 2;
    fm1_in.notes = (1u << 11) | (1u << 10); /* E and D#: only Eb sounds/records */
    keyboard_block();
    assert(t->arp_phys == 1 && t->nheld == 1 && t->held[0] == 63);
    assert(!tk_note(tk_at(0, 0, 0)));      /* ARP on: the arp's notes are recorded, not the keys */
    arp_tick(t, 1);
    assert(tk_note(tk_at(0, 0, 0)) == 63);
    assert(mo_w == 1 && ((midi_out_q[0] >> 16) & 127u) == 63);
    t->p[P_SCALE] = 9;
    t->p[P_ROOT] = 6;
    t->p[P_TRANS] = 12;
    song.octave = 1;
    song.sel = 1;                          /* key-up follows the original note/part */
    fm1_in.notes = 0;
    keyboard_block();
    assert(t->arp_phys == 0 && t->nheld == 0);
    assert(mo_w == 2 && ((midi_out_q[1] >> 16) & 127u) == 63);
    assert(((midi_out_q[1] >> 8) & 255u) == 0x80u);
    song.sel = 0;
    t->p[P_QUANT] = 0;
    fm1_in.notes = 1u << 8;                /* held black key must release after enabling mode */
    keyboard_block();
    before = mo_w;
    assert(t->arp_phys == 1);
    t->p[P_QUANT] = 2;
    fm1_in.notes = 0;
    keyboard_block();
    assert(t->arp_phys == 0 && t->nheld == 0 && mo_w == before + 1);
    puts("scales: silent keys, arp, live recording, MIDI out and held-note changes ok");
}

/* the voices of t holding a note (gate on), and whether note n is one of them */
static uint32_t gated(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].gate != 0;
    return n;
}
static int gated_note(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}

/* a voice of t held (gate on) by a note the tracker does not hold: a note-off that will never come */
static int stray(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].gate && t->v[i].note != tk_pl.note[trk_index(t)])
            return 1;
    return 0;
}

/* QNT SEQ (#37): the sequencer's notes snap to the scale as they play, the steps stay as written; a scale
 * changed while notes ring ends exactly the notes that sounded (no stuck notes); drum kits never snap */
static void seq_quant_test(void)                    /* 1.5: the tracker's rows (a note per cell) */
{
    static const uint8_t ROWS[7] = {61, 66, 61, 0, 0, 60, TK_OFF};   /* C#4 F#4 C#4 - - C4 OFF */
    track_t *t = &trk[0];
    uint32_t i, period;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    tk_defaults();
    song.g[G_BPM] = 120;
    host_preset(t, 0, 0);                          /* ANALOG, POLY */
    t->engine = t->eng_req = 0;
    t->p[P_VOICE] = V_POLY;
    tk.len[0] = (uint8_t)NELEM(ROWS);
    for (i = 0; i < NELEM(ROWS); i++)
        *tk_at(0, i, 0) = tk_pack(ROWS[i], 0, 0, 0);
    t->p[P_SCALE] = 1;                             /* C major */
    t->p[P_ROOT] = 0;
    period = div_samples(2);

    t->p[P_QUANT] = 0;                             /* OFF and SNAP: the rows play as written */
    seq_start();
    events_block(CTL);
    assert(tk_pl.note[0] == 61 && gated_note(t, 61));
    seq_stop();
    t->p[P_QUANT] = 1;
    seq_start();
    events_block(CTL);
    assert(tk_pl.note[0] == 61 && gated_note(t, 61));
    seq_stop();
    assert(gated(t) == 0);

    t->p[P_QUANT] = 3;                             /* SEQ */
    seq_start();
    events_block(CTL);
    assert(tk_pl.note[0] == 60 && gated_note(t, 60) && !gated_note(t, 61) && gated(t) == 1);
    assert(tk_note(tk_at(0, 0, 0)) == 61);         /* stored as written */
    t->p[P_SCALE] = 2;                             /* C minor: F#4 -> F4 */
    events_block(period);
    assert(tk_pl.row == 1 && tk_pl.note[0] == 65 && gated_note(t, 65) && !gated_note(t, 60) && gated(t) == 1);
    events_block(period);                          /* C#4 -> C4, ringing over the empty rows */
    t->p[P_SCALE] = 0;                             /* CHR while it rings: C4 sounds on */
    events_block(2u * period);
    assert(tk_pl.row == 4 && gated_note(t, 60) && gated(t) == 1);
    t->p[P_SCALE] = 1;
    events_block(period);                          /* C4 again: retriggered, one note */
    assert(tk_pl.row == 5 && tk_pl.note[0] == 60 && gated(t) == 1);
    events_block(period);                          /* OFF: released */
    assert(tk_pl.row == 6 && !tk_pl.note[0] && gated(t) == 0);
    events_block(period);                          /* round again, then stop with a note sounding */
    assert(tk_pl.row == 0 && gated_note(t, 60));
    seq_stop();
    assert(gated(t) == 0);

    /* a scale change on every block for a while: nothing left held */
    seq_start();
    for (i = 0; i < 4000u; i++) {
        t->p[P_SCALE] = (int16_t)(i * 7u % 16u);
        t->p[P_ROOT] = (int16_t)(i * 5u % 12u);
        events_block(CTL);
        assert(!stray(t));
    }
    seq_stop();
    assert(gated(t) == 0);

    /* the DRUM engine never snaps: its notes are GM drums */
    host_preset(t, ENGI_KIT, 0);
    t->engine = t->eng_req = ENGI_KIT;
    t->p[P_SCALE] = 1;
    t->p[P_ROOT] = 0;
    *tk_at(0, 0, 0) = tk_pack(37, 0, 0, 0);
    seq_start();
    events_block(CTL);
    assert(tk_pl.note[0] == 37);
    seq_stop();
    assert(gated(t) == 0);
    puts("scales: QNT SEQ snaps the tracker's notes as they play (cells unchanged), no stuck notes over scale changes, kits never");
}

int main(void)
{
    host_tracks_init();
    mapping_test();
    key_events_test();
    seq_quant_test();
    return 0;
}
