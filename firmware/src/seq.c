/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md */
/* Keyboard, scale and transport (the sequencer: a placeholder).
 * Everything here runs in the audio ISR, once per CTL-sample block, and
 * ends in trk_note_on / trk_note_off: engines never see where a note came from.
 * Four tracks, one transport: every track's pattern loops on its own LEN / DIV /
 * SWING / GATE (polymeter). The keys play the selected track; MIDI IN by MENU > MIDI > MIDI IN (G_ROUTE):
 * CH1-4 (0) channels 1..4 play parts 1..4, channels 5..16 are ignored (midi_control.c midi_event); SEL (1)
 * every channel the selected track; 1.2: CH5-8, CH9-12, CH13-16 as CH1-4 on those four channels (midi_base). Their CC1 (mod wheel), CC11 (expression) and channel aftertouch go to
 * the same track's modulation matrix (mod.c; the selected track: the one selected then).
 * The sequencer is a placeholder (SEQ shows TODO): PLAY runs the transport alone (the click, the SLICER, the FX
 * layer's grid, the synced LFOs). A key or MIDI note plays a
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
static uint32_t kb_prev;
static volatile uint8_t kb_boot_hold;    /* #137: set from power-on to the end of the splash (main.c): the keys sound nothing,
                                          * and one held across its end stays silent until it is let go and pressed again */
static uint8_t kb_note[27], kb_trk[27];  /* per key: the note it started and on which track */
static volatile uint8_t kb_asleep;       /* MENU > SCREEN OFF (ui.c): the screen dark or waking, a key pressed now only
                                          * wakes it (silent, no layer, its release nothing) */
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

static int drum_track(const track_t *t) { return eng_idx(t->eng_req) == ENGI_KIT; }

/* the 27 keys from F: black or white, and the key's place among the keys of its colour (white 0..15, black
 * 0..10) */
static int key_black(uint32_t k) { return (int)((0x54Au >> ((k + 5u) % 12u)) & 1u); }
static uint32_t key_place(uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++)
        n += (uint32_t)(key_black(i) == key_black(k));
    return n;
}

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

static uint32_t kb_map(const track_t *t, uint32_t k)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];   /* (the engine it switches to) */
    int32_t n;
    if (e->keys && (n = e->keys(t, k)) >= 0)           /* the engine's own key map (DRUM's GM map, slices) */
        return (uint32_t)n;
    n = 53 + (int32_t)k;
    if (t->p[P_QUANT] == QN_SNAP)                      /* SNAP: every key, rounded down */
        return (uint32_t)clamp(scale_snap(t, n + 12 * song.octave + t->p[P_TRANS]), 0, 127);
    if (t->p[P_QUANT] == QN_WHITE) {                    /* WHITE: white keys walk the scale, black keys are silent */
        uint32_t mask = scale_mask(t), i;
        int32_t count = 0, degree = DEGREE[n % 12], oct;
        if (degree < 0)
            return KB_SILENT;
        /* C4 is the root. Walk scale degrees on successive white keys, including
         * below C4; scales with 5, 6, 8 or 12 notes still have no duplicated degrees. */
        degree += (n / 12 - 5) * 7;
        for (i = 0; i < 12u; i++)
            count += (mask >> i) & 1u;
        oct = degree / count;
        degree %= count;
        if (degree < 0) {
            degree += count;
            oct--;
        }
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (!degree)
                    break;
                degree--;
            }
        n = 60 + t->p[P_ROOT] + 12 * oct + (int32_t)i;
    }
    return (uint32_t)clamp(n + 12 * song.octave + t->p[P_TRANS], 0, 127);
}

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
static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    last_note = (uint8_t)note;
    trk_note_on(t, note, vel);
}

static int midi_note_held(const track_t *t, uint32_t note);
static int midi_local_held(const track_t *t, uint32_t note);
static void input_off(track_t *t, uint32_t note)
{
    if (midi_note_held(t, note))
        return;
    trk_note_off(t, note);
}

/* the effect of key k in the FX layer (PF_N none): the white keys F3 .. G5 the map's (perf_map: the settings,
 * ui_layer.c; by default 1.1.5's ten), black keys 1..4 (F#3 G#3 A#3 C#4) the track mutes */
static uint32_t perf_key(uint32_t k)
{
    uint32_t p = key_place(k);
    return !key_black(k) ? (p < PF_KEYS ? perf_map[p] : PF_N) : p < NTRK ? PF_M1 + p : PF_N;
}

/* a key of the FX layer down (FX held) / up: the effect it holds. kb_fx: the one it took (+ 1, 0 none), so a key
 * remapped while held lets go of what it holds; an effect two keys hold ends with the last of them. noinline: out
 * of keyboard_block's loop (the audio ISR) */
static uint8_t kb_fx[27];
static __attribute__((noinline)) void perf_key_press(uint32_t k, int down)
{
    uint32_t e, j;
    if (down) {
        e = perf_key(k);
        kb_fx[k] = (uint8_t)(e + 1u);
        perf_press(e, 1);
        return;
    }
    if (!kb_fx[k])
        return;
    e = kb_fx[k] - 1u;
    kb_fx[k] = 0;
    for (j = 0; j < 27u; j++)
        if (kb_fx[j] == e + 1u)
            return;
    perf_press(e, 0);
}

/* the map changed (ui_layer.c: PRESETS with a key held, EDIT, a settings load): a white key held takes its new
 * effect at once (the old one let go); FX LATCH: a latched effect of the key's becomes the new one */
static __attribute__((noinline)) void perf_key_remap(void)
{
    uint32_t k, e, o;
    perf_remap = 0;
    for (k = 0; k < 27u; k++) {
        if (!kb_fx[k] || key_black(k) || (o = kb_fx[k] - 1u) == (e = perf_key(k)))
            continue;
        if (!perf_latch_on) {
            perf_key_press(k, 0);
            perf_key_press(k, 1);
            continue;
        }
        kb_fx[k] = (uint8_t)(e + 1u);
        if (o < PF_N && ((perf_latched >> o) & 1u)) {
            perf_latched &= ~PF_BIT(o);
            if (e < PF_N) {
                perf_latched |= PF_BIT(e);
                perf_ord[e] = ++perf_seq;
            }
        }
    }
}

/* key k plays kb_note[k] on track t: its chord (chord.c; the note alone with CHRD OFF). A note another key
 * holds already sounds: it is not started again (nor sent to MIDI OUT); the key keeps its notes */
static void key_on(uint32_t k, track_t *t)
{
    uint32_t n = chord_build(t, kb_note[k], kb_chord[k]), i, mc = trk_midi_ch(trk_index(t));
    kb_chn[k] = 0;
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_on(t, x, 100);
        midi_out_event(0x09u | (0x90u | mc) << 8 | x << 16 | 100u << 24);
    }
    kb_chn[k] = (uint8_t)n;
    last_note = kb_note[k];                         /* (step entry: the key's note) */
}

/* key k is up: the notes it started end, but those another key still holds */
static void key_off(uint32_t k, track_t *t)
{
    uint32_t n = kb_chn[k], i, mc = trk_midi_ch(trk_index(t));
    kb_chn[k] = 0;
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_off(t, x);
        midi_out_event(0x08u | (0x80u | mc) << 8 | x << 16);
    }
}

#ifdef FM1_INPUT_LAT
/* the press latency stats (hal/fm1_input.h fm1_in_stat, console `inp`): from the first scan that saw
 * key k closed to its note-on here, and to the note's first sample leaving the DMA (kb_out_tick:
 * audio.c, the TIMER4 tick at which the block being rendered starts to play) */
static uint32_t kb_out_tick;
static __attribute__((noinline)) void kb_lat(uint32_t k)
{
    uint32_t t0 = fm1_in.note_t0[k], d = fm1_ticks() - t0, o = kb_out_tick - t0;
    fm1_in_stat.kb_n++;
    fm1_in_stat.kb_sum += d;
    fm1_in_stat.dac_sum += o;
    if (d > fm1_in_stat.kb_max)
        fm1_in_stat.kb_max = d;
    if (o > fm1_in_stat.dac_max)
        fm1_in_stat.dac_max = o;
}
#endif

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch, k;
    uint32_t lay, fx;
    ch = cur ^ kb_prev;                          /* keys also sound while entering steps */
    if (perf_remap)
        perf_key_remap();
    if (!ch)
        return;
    if (kb_boot_hold) {                           /* #137: under the splash; their release is silent too */
        for (k = 0; k < 27u; k++)
            if ((ch & cur) >> k & 1u)
                kb_note[k] = KB_SILENT;
        kb_prev = cur;
        return;
    }
    lay = (fm1_in.buttons & kb_mask) || kb_lock;  /* (#83: kb_lock, a layer locked open with no button held) */
    fx = (fm1_in.buttons & perf_mask) || (kb_lock & 2u);
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {                    /* the selected track; the key-up goes to the same one */
            kb_trk[k] = song.sel;
            if (kb_asleep) {                      /* (SCREEN OFF: the wake press is swallowed) */
                kb_note[k] = KB_SILENT;
                continue;
            }
            if (lay) {                            /* a layer's button held (or locked): the key is the layer's */
                kb_note[k] = KB_SILENT;           /* (ui_layer.c), with FX an effect (perform.c) */
                kb_layer |= 1u << k;
                if (fx)
                    perf_key_press(k, 1);
                continue;
            }
            if (song.keys_mute)                   /* NAME (ui_name.c): every key types, none sounds */
                kb_note[k] = KB_SILENT;
            else
                kb_note[k] = (uint8_t)kb_map(&trk[kb_trk[k]], k);
            if (kb_note[k] == KB_SILENT)
                continue;
            key_on(k, &trk[kb_trk[k]]);
#ifdef FM1_INPUT_LAT
            kb_lat(k);
#endif
        } else {
            if ((kb_layer >> k) & 1u) {
                kb_layer &= ~(1u << k);
                perf_key_press(k, 0);
                continue;
            }
            if (kb_note[k] == KB_SILENT)
                continue;
            key_off(k, &trk[kb_trk[k] % NTRK]);
        }
    }
    kb_prev = cur;
}

/* -------------------------------------------------------- sequencer --- */
/* the sequencer: a placeholder for now (SEQ shows TODO). seq_hook, when set, gets every block the samples the
 * transport moved on (events_block, after the keys and MIDI IN; 0 while stopped): where the sequencer will play. The
 * host tests play their songs through it (tests/hostsim.c host_seq) */
static void (*seq_hook)(uint32_t n);

/* PLAY: the transport from its start: the metronome, the SLICER and the FX layer's grid with it */
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
    slicer_start();                                /* slicer.c: its step 0 now */
    perf_start();                                  /* perform.c: its 1/16 grid too */
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
    keyboard_block();
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
    if (seq_hook)
        seq_hook(song.playing ? seq_n : 0u);
    if (song.playing)
        click_tick(seq_n);
    if (song.playing)
        song.tick++;
    tgt_block_end(n);
}

#ifdef FELUCCA_NATIVE
#include "target_host.c"                /* (Fel-Tracker) the host targets' side of target.h */
#endif
