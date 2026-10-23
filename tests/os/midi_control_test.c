/* SPDX-License-Identifier: GPL-3.0-only
 * End-to-end channel input and external-clock tests using the real queues,
 * UART parser, ownership model, sequencer, voice renderer and UI source. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void midi_test_reset(void)
{
    ui_power_on();
    memset(midi_ch, 0, sizeof midi_ch); memset(midi_notes, 0, sizeof midi_notes);
    memset(midi_owners, 0, sizeof midi_owners); memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target); memset(&midi_clock, 0, sizeof midi_clock);
    memset(&um, 0, sizeof um); memset(midi_in_source, 0, sizeof midi_in_source);
    mi_r = mi_w = 0; midi_in_overflow = 0; midi_beat_samples = 0;
    fm1_in.notes = kb_prev = 0; fm1_ms = 0; song.sel = 0;
    events_block(CTL);
}
static void queued(uint32_t st, uint32_t d1, uint32_t d2, uint32_t source)
{
    midi_enqueue((st >= 0xF8u ? 0xFu : st >> 4) | st << 8 | d1 << 16 | d2 << 24, source);
    events_block(CTL);
}
static int gate_note(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}
static int controls_test(void)
{
    int bad = 0; midi_test_reset(); track_t *t = &trk[0];
    queued(0xE0, 127, 127, 1);
    bad += check("USB maximum bend is exactly the default +2 semitones", midi_bend_target[0] == 512);
    queued(0xE0, 0, 0, 1);
    bad += check("USB minimum bend is exactly the default -2 semitones", midi_bend_target[0] == -512);
    queued(0xE0, 0, 64, 1);
    bad += check("bend centre is zero", midi_bend_target[0] == 0);
    queued(0xB0, 101, 0, 1); queued(0xB0, 100, 0, 1); queued(0xB0, 6, 12, 1); queued(0xB0, 38, 50, 1);
    queued(0xE0, 127, 127, 1);
    bad += check("RPN0 supports semitone and cent bend sensitivity", midi_ch[0].semis == 12 && midi_ch[0].cents == 50 && midi_bend_target[0] == 3200);
    queued(0xB0, 99, 0, 1); queued(0xB0, 6, 24, 1);
    bad += check("selecting NRPN cancels RPN data entry", midi_ch[0].semis == 12);
    queued(0x90, 60, 100, 1); int32_t out[CTL]; int32_t before = midi_bend_q8[0];
    track_render(t, out, CTL);
    bad += check("bend is smoothed toward the target in the real voice renderer", midi_bend_q8[0] > before && midi_bend_q8[0] < midi_bend_target[0]);
    for (uint32_t i = 0; i < 64; i++) track_render(t, out, CTL);
    bad += check("bend smoothing reaches the exact target without changing saved pitch", midi_bend_q8[0] == 3200 && t->p[P_TRANS] == 0 && t->v[0].note == 60);
    queued(0xB0, 121, 0, 1);
    bad += check("Reset Controllers centres bend and preserves RPN sensitivity", !midi_bend_target[0] && midi_ch[0].semis == 12 && gate_note(t, 60));
    queued(0xB0, 1, 87, 1); queued(0xB0, 11, 23, 1); queued(0xD0, 90, 0, 1);
    bad += check("existing modwheel/expression/aftertouch stay routed to the track", t->mw == 87 && t->ex_off == 104 && t->at == 90);
    return bad;
}
static int sustain_test(void)
{
    int bad = 0; midi_test_reset(); track_t *t = &trk[0];
    queued(0x90, 60, 100, 1); queued(0xB0, 64, 127, 1); queued(0x80, 60, 0, 1);
    bad += check("pedal holds a released synth note and its owner", gate_note(t, 60) && midi_notes[0][60] == (1u | MIDI_PEDAL_NOTE));
    queued(0xB0, 64, 0, 1);
    bad += check("pedal release releases its held note", !gate_note(t, 60) && !midi_notes[0][60] && !midi_owners[0]);
    queued(0x90, 62, 100, 1); queued(0xB0, 64, 127, 1); queued(0xB0, 123, 0, 1);
    bad += check("All Notes Off honours sustain rather than hard-killing", gate_note(t, 62) && (midi_notes[0][62] & MIDI_PEDAL_NOTE));
    queued(0xB0, 121, 0, 1);
    bad += check("Reset Controllers releases pedal-held notes", !gate_note(t, 62) && !midi_notes[0][62]);
    queued(0x90, 67, 100, 1); queued(0xB0, 64, 127, 1); queued(0x80, 67, 0, 1); queued(0xB0, 120, 0, 1);
    bad += check("All Sound Off ignores pedal and discards all track ownership", !midi_notes[0][67] && !midi_owners[0] && !gate_note(t, 67));
    return bad;
}
static int ownership_test(void)
{
    int bad = 0; midi_test_reset(); song.g[G_ROUTE] = 1; track_t *t = &trk[0];
    queued(0x94, 60, 100, 1); queued(0x95, 60, 100, 2);
    bad += check("USB/TRS channels can share one sounding note with independent owners", midi_owners[0] == 2u && gate_note(t, 60));
    queued(0x84, 60, 0, 1);
    bad += check("one channel's note-off preserves another channel's hold", midi_owners[0] == 1u && gate_note(t, 60));
    queued(0x85, 60, 0, 2);
    bad += check("last channel owner releases the shared note", !midi_owners[0] && !gate_note(t, 60));
    fm1_in.notes = 1u; events_block(CTL); uint32_t local = kb_note[0];
    queued(0x94, local, 100, 1); queued(0x84, local, 0, 1);
    bad += check("MIDI release cannot cut a still-held local keyboard note", gate_note(t, local));
    fm1_in.notes = 0; events_block(CTL);
    bad += check("local release after MIDI release ends the shared note", !gate_note(t, local));
    queued(0x94, 65, 100, 1); song.sel = 1; queued(0x84, 65, 0, 1);
    bad += check("note-off follows its note-on across selected-track changes", !gate_note(&trk[0], 65) && !midi_notes[4][65]);
    song.sel = 0; queued(0x94, 67, 100, 1); queued(0xB4, 64, 127, 1); queued(0x84, 67, 0, 1);
    panic_req |= 1u; events_block(CTL);
    queued(0x90, 67, 100, 1); queued(0xB4, 64, 0, 1);
    bad += check("preset panic forgets old pedal owners so later pedal-up preserves new notes", !midi_notes[4][67] && gate_note(&trk[0], 67));
    for (uint32_t i = 0; i <= MQ; i++) midi_enqueue(0x09u | 0x90u << 8 | 70u << 16 | 100u << 24, 1);
    events_block(CTL);
    bad += check("queue overflow recovers ownership and releases stuck notes", !midi_in_overflow && mi_r == mi_w && !midi_owners[0] && !gate_note(&trk[0], 67));
    return bad;
}
static void clock_setup(uint32_t mode)
{
    midi_test_reset(); song.g[G_CLOCK] = (int16_t)mode;
    memset(host_song, 0, sizeof host_song); memset(&hs, 0, sizeof hs); host_rows = 0;   /* (a song of 16 rows, a note */
    seq_hook = host_seq;                                 /* a row: the tests' player, hostsim.c host_seq) */
    for (uint32_t i = 0; i < 16; i++) put_row(0, i, 60u + i);
    events_block(CTL);
}
static void clock_packet(uint32_t source, uint32_t status, uint32_t ms)
{
    fm1_ms = ms;
    if (source == 2u) { um_byte(status); events_block(CTL); }
    else queued(status, 0, 0, source);
}
static void clock_to(uint32_t source, uint32_t start, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) clock_packet(source, 0xF8, start + (i * 125u) / 6u);
}
static int clock_test(uint32_t source)
{
    int bad = 0; clock_setup(source);
    clock_packet(source == 1u ? 2u : 1u, 0xFA, 1);
    bad += check(source == 1 ? "USB mode ignores TRS Start" : "TRS mode ignores USB Start", !song.playing);
    clock_packet(source, 0xFA, 2);
    bad += check(source == 1 ? "USB Start begins at row zero" : "TRS Start begins at row zero", song.playing && hs.row == 0u);
    clock_to(source, 2, 7);
    bad += check(source == 1 ? "USB six pulses advance exactly one 16th row" : "TRS six pulses advance exactly one 16th row", hs.row == 1u && song.g[G_BPM] == 120);
    fm1_ms = 132; events_block(CTL); uint32_t idx = hs.row, pos = hs.pos;
    clock_packet(source, 0xFC, 133);
    bad += check("external Stop preserves the row position and releases the song's notes", !song.playing && hs.row == idx && hs.pos == pos && !hs.note[0]);
    clock_packet(source, 0xFB, 150);
    bad += check("external Continue resumes the existing row position", song.playing && hs.row == idx && hs.pos == pos);
    clock_packet(source, 0xF8, 151); fm1_ms = 672; events_block(CTL);
    bad += check("lost clock times out and releases transport notes", !song.playing && !hs.note[0]);
    clock_packet(source, 0xFA, 700); clock_to(source, 700, 7);
    bad += check("Start after timeout restarts cleanly at the master's phase", song.playing && hs.row == 1u);
    return bad;
}
static int clock_source_and_boundaries(void)
{
    int bad = 0; clock_setup(1);
    clock_packet(1, 0xFA, 10); clock_to(1, 10, 1); uint32_t before = hs.row;   /* (its 1/16) */
    clock_to(2, 20, 7);
    bad += check("wrong-source clocks cannot advance the transport a 1/16", hs.row == before);
    clock_to(1, 10, 7);
    bad += check("selected-source MIDI clock advances the transport a 1/16", hs.row > before);
    clock_setup(2); fm1_ms = 100; um_byte(0x90); um_byte(60); um_byte(0xFA); um_byte(100); events_block(CTL);
    bad += check("TRS realtime interleaving preserves the incomplete note message", gate_note(&trk[0], 60) && song.playing);
    /* Millisecond counter wraps naturally during external-clock operation. */
    clock_setup(1); clock_packet(1, 0xFA, 0xFFFFFFF0u); clock_packet(1, 0xF8, 0xFFFFFFF0u);
    clock_packet(1, 0xF8, 5u); fm1_ms = 10; events_block(CTL);
    bad += check("external clock timestamps survive uint32 millisecond wrap", song.playing && midi_clock.last_ms == 5u);
    clock_setup(1); transport_req = 1; events_block(CTL); fm1_ms = 501; events_block(CTL);
    bad += check("local PLAY waiting for missing external clock also times out", !song.playing);
    return bad;
}
static void idle_ms(uint32_t ms)               /* about ms of audio blocks, no MIDI */
{
    uint32_t i, ms0 = fm1_ms;
    for (i = 0; i < ms * 23u / 16u; i++) { fm1_ms = ms0 + i * 16u / 23u; events_block(CTL); }
}
/* USB-MIDI back-pressure: full 64-byte bulk packets (16 events) faster than the audio ISR drains the ring are
 * left in the endpoint (NAK) instead of overflowing it, which would panic every sounding note */
static int usb_burst_test(void)
{
    int bad = 0; uint8_t pkt[64]; uint32_t e, k, taken = 0;
    midi_test_reset();
    queued(0x90, 48, 100, 1);
    for (e = 0; e < 16; e++) {                       /* CC7 = 100 on 16 channels */
        pkt[4 * e] = 0x0B; pkt[4 * e + 1] = (uint8_t)(0xB0 | e); pkt[4 * e + 2] = 7; pkt[4 * e + 3] = 100;
    }
    for (k = 0; k < 5; k++)                          /* 5 polls at 2 kHz before the next half drains */
        taken += (uint32_t)ep1_take(pkt, sizeof pkt);
    bad += check("a USB burst fills the MIDI ring without overflowing it", taken == 3u && !midi_in_overflow && mi_w - mi_r == 48u);
    bad += check("the packet that does not fit is refused (left for the next poll)", !ep1_take(pkt, sizeof pkt) && mi_w - mi_r == 48u);
    for (e = 0; e < 8; e++)                          /* TRS MIDI meanwhile: its 8 slots are free */
        midi_in_event(0x0Bu | 0xB1u << 8 | 1u << 16 | e << 24);
    bad += check("TRS MIDI into a ring full of USB still fits (no overflow)", !midi_in_overflow && mi_w - mi_r == 56u);
    events_block(CTL);
    bad += check("the ring drains with no panic: the held note keeps sounding", gate_note(&trk[0], 48) && mi_r == mi_w);
    bad += check("the refused packet is taken once the ring has room", ep1_take(pkt, sizeof pkt) && mi_w - mi_r == 16u);
    events_block(CTL);
    bad += check("the held note survives all 88 events", gate_note(&trk[0], 48) && !midi_in_overflow);
    for (k = 0; k < 3; k++) ep1_take(pkt, sizeof pkt);
    midi_in_event(0x09u | 0x90u << 8 | 50u << 16 | 100u << 24);   /* one more event: 49 used, 15 free */
    bad += check("free space below one packet refuses a whole packet", !ep1_take(pkt, sizeof pkt) && !midi_in_overflow);
    events_block(CTL);
    return bad;
}
/* MENU > MIDI > MIDI IN (ROUT; up to 1.1.5 GLO > SYSTEM, #68): CH1-4 listens to channels 1..4 only, channels 5..16 are free for other instruments
 * (notes, bend, CCs, aftertouch, panic and reset all ignored, from USB and TRS); SEL plays the selected track
 * from every channel; a switch to CH1-4 lets go of what channels 5..16 held */
static int any_gate(void)
{
    for (uint32_t i = 0; i < NTRK * NVOICE; i++) if (trk[i / NVOICE].v[i % NVOICE].active && trk[i / NVOICE].v[i % NVOICE].gate) return 1;
    return 0;
}
static int route_test(void)
{
    int bad = 0; uint32_t ch, src, owned = 0;
    midi_test_reset(); song.sel = 2;
    bad += check("ROUT defaults to CH1-4", song.g[G_ROUTE] == 0);
    for (src = 1; src <= 2u; src++)
        for (ch = 4; ch < 16u; ch++) queued(0x90 | ch, 60 + ch, 100, src);
    for (ch = 4; ch < 16u; ch++) owned |= midi_notes[ch][60 + ch];
    bad += check("CH1-4: note-ons on channels 5..16 (USB and TRS) play nothing", !any_gate() && !owned && !midi_owners[2] && !midi_hint);
    queued(0x91, 62, 100, 1); queued(0x93, 40, 100, 2);
    bad += check("CH1-4: channels 2 and 4 still play parts 2 and 4", gate_note(&trk[1], 62) && midi_notes[1][62] == 2u && midi_notes[3][40] == 4u);
    queued(0xE4, 127, 127, 1); queued(0xEF, 0, 0, 2); queued(0xE9, 127, 127, 1);
    bad += check("CH1-4: pitch bend on channels 5, 10, 16 bends no part", !midi_bend_target[0] && !midi_bend_target[1] && !midi_bend_target[2] && !midi_ch[4].bend && !midi_ch[15].bend);
    queued(0xB4, 1, 99, 1); queued(0xB9, 11, 10, 2); queued(0xDF, 77, 0, 1);
    bad += check("CH1-4: CC1 / CC11 and aftertouch on channels 5..16 reach no MOD source", !trk[2].mw && !trk[2].ex_off && !trk[2].at && !trk[0].mw && !trk[0].at);
    queued(0xB4, 101, 0, 1); queued(0xB4, 100, 0, 1); queued(0xB4, 6, 24, 1);
    bad += check("CH1-4: RPN on channel 5 changes nothing", midi_ch[4].semis != 24u);
    queued(0xB4, 64, 127, 1); queued(0x81, 62, 0, 1);
    bad += check("CH1-4: a channel 5 sustain pedal does not hold channel 2's note", !gate_note(&trk[1], 62) && !midi_notes[1][62]);
    queued(0x91, 62, 100, 1); queued(0xB1, 1, 50, 1);
    queued(0xB4, 120, 0, 1); queued(0xB9, 123, 0, 2); queued(0xBF, 121, 0, 1);
    bad += check("CH1-4: CC120 / CC123 / CC121 on channels 5..16 leave parts 1..4 sounding", gate_note(&trk[1], 62) && gate_note(&trk[3], 40) && midi_owners[1] == 1u && trk[1].mw == 50);
    queued(0xB1, 123, 0, 1); queued(0x83, 40, 0, 2);
    bad += check("CH1-4: panic on channel 2 still works", !gate_note(&trk[1], 62) && !midi_owners[1] && !midi_owners[3]);

    midi_test_reset(); song.sel = 2; song.g[G_ROUTE] = 1; trk[2].p[P_VOICE] = V_POLY; trk[2].p[P_SUS] = 127; events_block(CTL);
    queued(0x90, 60, 100, 1); queued(0x94, 62, 100, 2); queued(0x99, 64, 100, 1); queued(0x9F, 65, 100, 2);
    bad += check("SEL: channels 1, 5, 10, 16 all play the selected track", gate_note(&trk[2], 60) && gate_note(&trk[2], 62) && gate_note(&trk[2], 64) && gate_note(&trk[2], 65) && !gate_note(&trk[0], 60) && midi_owners[2] == 4u);
    queued(0xEF, 127, 127, 1); queued(0xB9, 1, 66, 2); queued(0xD4, 33, 0, 1);
    bad += check("SEL: bend, CC1 and aftertouch from channels 5..16 reach the selected track", midi_bend_target[2] == 512 && trk[2].mw == 66 && trk[2].at == 33);
    queued(0xB4, 64, 127, 1); queued(0x84, 62, 0, 2);
    bad += check("SEL: channel 5 pedal holds its note", gate_note(&trk[2], 62) && (midi_notes[4][62] & MIDI_PEDAL_NOTE));
    song.g[G_ROUTE] = 0; events_block(CTL);
    bad += check("SEL -> CH1-4 releases channels 5..16's notes (pedal-held too), keeps channel 1's",
                 !gate_note(&trk[2], 62) && !gate_note(&trk[2], 64) && !gate_note(&trk[2], 65) && gate_note(&trk[2], 60) &&
                 !midi_notes[4][62] && !midi_notes[9][64] && !midi_notes[15][65] && midi_owners[2] == 1u && !midi_ch[4].pedal);
    bad += check(".. and resets their bend: each part follows its own channel 1..4", !midi_bend_target[2] && !midi_ch[15].bend);
    queued(0x80, 60, 0, 1);
    bad += check("channel 1's note-off still releases its note after the switch", !any_gate() && !midi_owners[2]);
    return bad;
}
/* 1.2 (Discussion #143): ROUT CH5-8, CH9-12, CH13-16: tracks 1..4 on those four channels (notes, bend, CCs, the CC map,
 * panic), every other channel ignored, as CH1-4 does with 1..4; a switch between blocks lets go of the old block's
 * notes; the clock (realtime) is not a channel's: it runs whatever ROUT says; the keys send on the block's channel */
static uint32_t out_last(void) { return mo_w != mo_r ? midi_out_q[(mo_w - 1u) % MQ] : 0u; }
static int route_block_test(void)
{
    static const char *const NAME[5] = {"CH1-4", "SEL", "CH5-8", "CH9-12", "CH13-16"};
    int bad = 0, ok = 1; uint32_t r, ch, i;
    ok = GP[G_ROUTE].max == 4 && GP[G_ROUTE].def == 0;
    for (r = 0; r < 5u; r++) ok &= str_eq(GP[G_ROUTE].names[r], NAME[r]);
    bad += check("ROUT: CH1-4 SEL (stored 0 1, as before), CH5-8 CH9-12 CH13-16 appended", ok);
    for (r = 2; r <= 4u; r++) {
        uint32_t b = (r - 1u) * 4u;
        midi_test_reset(); song.sel = 2; song.g[G_ROUTE] = (int16_t)r;
        trk[3].eng_req = trk[3].engine = 0u;               /* (a synth on track 4: its notes held as gates) */
        events_block(CTL);
        ok = midi_base() == (int32_t)b;
        for (ch = 0; ch < 16u; ch++) {                     /* a note on every channel: only the block's play */
            uint32_t n = 40u + ch;
            queued(0x90 | ch, n, 100, 1 + (ch & 1u));
            for (i = 0; i < NTRK; i++)
                ok &= gate_note(&trk[i], n) == (ch >= b && ch < b + 4u && i == ch - b);
        }
        queued(0xE0 | (b + 1u), 127, 127, 1); queued(0xE0 | (b ? 0u : 4u), 127, 127, 1);
        ok &= midi_bend_target[1] == 512 && !midi_bend_target[0] && !midi_bend_target[2];
        queued(0xB0 | (b + 2u), 7, 5, 1); queued(0xB0 | (b + 2u) % 16u, 1, 44, 1);
        queued(0xB0 | (b + 4u) % 16u, 7, 9, 1);           /* (the next block's first channel: ignored) */
        ok &= trk[2].p[P_LEVEL] == 5 && trk[2].mw == 44 && trk[0].p[P_LEVEL] != 9 && trk[3].p[P_LEVEL] != 9;
        queued(0xB0 | (b + 4u) % 16u, 123, 0, 1); queued(0xB0 | (b + 4u) % 16u, 120, 0, 1);
        ok &= gate_note(&trk[0], 40u + b) && gate_note(&trk[3], 43u + b);
        queued(0xB0 | (b + 3u), 123, 0, 1);
        ok &= !gate_note(&trk[3], 43u + b) && gate_note(&trk[0], 40u + b);
        bad += check(r == 2u ? "ROUT CH5-8: channels 5..8 play tracks 1..4 (notes, bend, CC map, CC1, panic); the rest ignored" :
                     r == 3u ? "ROUT CH9-12: channels 9..12 play tracks 1..4; the rest ignored" :
                               "ROUT CH13-16: channels 13..16 play tracks 1..4; the rest ignored", ok);
    }
    /* a switch between blocks: the old block's notes let go (pedal-held too), its bend reset */
    midi_test_reset(); song.g[G_ROUTE] = 2; events_block(CTL);
    queued(0x94, 60, 100, 1); queued(0xB5, 64, 127, 1); queued(0x95, 62, 100, 1); queued(0x85, 62, 0, 1); queued(0xE4, 127, 127, 1);
    ok = gate_note(&trk[0], 60) && gate_note(&trk[1], 62) && midi_bend_target[0] == 512;
    song.g[G_ROUTE] = 3; events_block(CTL);
    ok &= !gate_note(&trk[0], 60) && !gate_note(&trk[1], 62) && !midi_owners[0] && !midi_owners[1] && !midi_ch[4].bend &&
          !midi_ch[5].pedal && !midi_bend_target[0];
    queued(0x98, 64, 100, 1);
    ok &= gate_note(&trk[0], 64);
    song.g[G_ROUTE] = 1; events_block(CTL);             /* -> SEL: nothing let go (every channel heard) */
    ok &= gate_note(&trk[0], 64);
    queued(0x88, 64, 0, 1);
    ok &= !any_gate();
    bad += check("ROUT CH5-8 -> CH9-12 lets go of channels 5..8 (pedal-held too, bend reset); -> SEL keeps notes", ok);
    /* the clock is not routed: an external clock runs with ROUT CH13-16 as with CH1-4 */
    clock_setup(1); song.g[G_ROUTE] = 4; events_block(CTL);
    clock_packet(1, 0xFA, 2); clock_to(1, 2, 7);
    ok = song.playing && hs.row == 1u;
    clock_packet(1, 0xFC, 133);
    ok &= !song.playing;
    song.g[G_CLOCK] = 0; events_block(CTL);
    bad += check("ROUT CH13-16: the USB clock (Start, pulses, Stop) runs the transport as ever", ok);
    /* MIDI out: the keys send on the selected track's channel of the block (SEL, CH1-4: 1..4) */
    usb.config = 1;
    ok = 1;
    for (r = 0; r <= 4u; r++) {
        uint32_t want = (r >= 2u ? (r - 1u) * 4u : 0u) + 2u;
        midi_test_reset(); song.g[G_ROUTE] = (int16_t)r; song.sel = 2; mo_r = mo_w; events_block(CTL);
        fm1_in.notes = 1u << 7; events_block(CTL);
        ok &= ((out_last() >> 8) & 0xFFu) == (0x90u | want);
        fm1_in.notes = 0; events_block(CTL);
        ok &= ((out_last() >> 8) & 0xFFu) == (0x80u | want);
    }
    usb.config = 0; mo_r = mo_w;
    song.g[G_ROUTE] = 0; song.sel = 0; events_block(CTL);
    bad += check("MIDI out: the keys of track 3 send on channel 3 (CH1-4, SEL), 7 (CH5-8), 11 (CH9-12), 15 (CH13-16)", ok);
    return bad;
}
/* #103: the standard CC map, on the track the channel plays, as a knob turn (and as AUTOMATION records one) */
static int cc_map_test(void)
{
    static const struct { uint8_t cc; uint8_t id; } COMMON[] = {
        {5, P_GLIDE}, {7, P_LEVEL}, {72, P_REL}, {73, P_ATK}, {75, P_DEC}, {91, P_REV}, {93, P_CHOR}, {94, P_DLY},
    };
    int bad = 0, ok = 1; uint32_t i, e; int16_t before[P_COUNT];
    midi_test_reset();
    bad += check("power-on parts (ui_test.c UT_DEF): SID, the pad (WAVE or OBIE), SID, MODAL", trk[0].eng_req == ENGI_SID &&
                 trk[1].eng_req == ENGI_HPAD && trk[2].eng_req == ENGI_SID && trk[3].eng_req == ENGI_MODAL);
    for (i = 0; i < NELEM(COMMON); i++) {
        queued(0xB0, COMMON[i].cc, 127, 1);
        ok &= trk[0].p[COMMON[i].id] == TP[COMMON[i].id].max;
        queued(0xB0, COMMON[i].cc, 0, 1);
        ok &= trk[0].p[COMMON[i].id] == TP[COMMON[i].id].min;
        queued(0xB0, COMMON[i].cc, 37, 1);
        ok &= trk[0].p[COMMON[i].id] == 37;
    }
    bad += check("CC5 7 72 73 75 91 93 94: GLIDE LEVEL REL ATK DEC REV CHO DLY, 0..127 over their range", ok);
    queued(0xB0, 10, 0, 1); ok = trk[0].p[P_PAN] == -64;
    queued(0xB0, 10, 127, 1); ok &= trk[0].p[P_PAN] == 63;
    queued(0xB0, 10, 64, 1); ok &= trk[0].p[P_PAN] == 0;
    bad += check("CC10 PAN: 0 left, 64 centre, 127 right", ok);
    {   /* CC74 / CC71: each engine's own targets (engine_t.cc: CC74 the high nibble's EDIT value, CC71 the low's) */
        uint32_t c0 = ENGINES[trk[0].eng_req]->cc, c1 = ENGINES[trk[1].eng_req]->cc, c3 = ENGINES[trk[3].eng_req]->cc;
        queued(0xB0, 74, 127, 1); queued(0xB0, 71, 0, 1);
        ok = (!(c0 >> 4) || trk[0].p[P_E0 + (c0 >> 4) - 1u] == ENGINES[trk[0].eng_req]->edit[(c0 >> 4) - 1u].max) &&
             (!(c0 & 15u) || trk[0].p[P_E0 + (c0 & 15u) - 1u] == ENGINES[trk[0].eng_req]->edit[(c0 & 15u) - 1u].min);
        bad += check("CC74 / CC71 on track 1: its engine's targets, 127 the top, 0 the bottom", ok);
        queued(0xB1, 74, 127, 1);
        bad += check("CC74 on channel 2: track 2's target", !(c1 >> 4) ||
                     trk[1].p[P_E0 + (c1 >> 4) - 1u] == ENGINES[trk[1].eng_req]->edit[(c1 >> 4) - 1u].max);
        queued(0xB3, 74, 0, 1);
        bad += check("CC74 on KIT (channel 4): its target", !(c3 >> 4) ||
                     trk[3].p[P_E0 + (c3 >> 4) - 1u] == ENGINES[trk[3].eng_req]->edit[(c3 >> 4) - 1u].min);
    }
    for (e = 0, ok = 1; e < NENGINES; e++) {   /* the table names a continuous EDIT value of every engine */
        uint32_t m = ENGINES[e]->cc, k;
        for (k = 0; k < 2u; k++) {
            uint32_t n = (m >> (k ? 0 : 4)) & 15u;
            const param_desc_t *d = n ? &ENGINES[e]->edit[n - 1u] : 0;
            ok &= n <= 8u && (!d || (d->fmt != F_ENUM && d->max - d->min >= 24));
        }
    }
    bad += check("every engine's CC74 / CC71 target is a continuous EDIT value (or none)", ok);
    for (e = 0; e < NENGINES && ENGINES[e]->cc; e++)
        ;
    if (e < NENGINES) {                                   /* an engine of no CC74 / CC71 target: ignored */
        trk[2].eng_req = trk[2].engine = (uint8_t)e;
        memcpy(before, trk[2].p, sizeof before); queued(0xB2, 74, 3, 1); queued(0xB2, 71, 3, 1);
        bad += check("CC74 / CC71 on an engine without targets: ignored", !memcmp(before, trk[2].p, sizeof before));
    }
    memcpy(before, trk[0].p, sizeof before);
    queued(0xB0, 121, 0, 1);
    bad += check("CC121 resets no mapped parameter (RP-015)", !memcmp(before, trk[0].p, sizeof before));
    queued(0xB0, 20, 99, 1); queued(0xB0, 76, 99, 1); queued(0xB0, 95, 99, 1);
    bad += check("CCs outside the map change nothing", !memcmp(before, trk[0].p, sizeof before));
    memcpy(before, trk[2].p, sizeof before);
    queued(0xB4, 7, 5, 1); queued(0xB9, 74, 5, 2); queued(0xBF, 91, 5, 1);
    ok = !memcmp(before, trk[2].p, sizeof before) && trk[0].p[P_LEVEL] != 5 && trk[3].p[P_LEVEL] != 5;
    bad += check("ROUT CH1-4: mapped CCs on channels 5..16 reach no part", ok);
    song.g[G_ROUTE] = 1; song.sel = 2; events_block(CTL);
    queued(0xB9, 7, 5, 2);
    bad += check("ROUT SEL: channel 10's CC7 sets the selected track's LEVEL", trk[2].p[P_LEVEL] == 5 && trk[0].p[P_LEVEL] != 5);
    song.g[G_ROUTE] = 0; song.sel = 0; events_block(CTL);

    return bad;
}
int main(void)
{
    int bad = controls_test() + sustain_test() + ownership_test() + clock_test(1) + clock_test(2) + clock_source_and_boundaries() +
              usb_burst_test() + route_test() + route_block_test() + cc_map_test();
    printf("%s\n", bad ? "MIDI CONTROL/CLOCK TEST FAILED" : "MIDI control/clock integration tests passed"); return bad != 0;
}
