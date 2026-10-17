/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* (Blue Hare, 2026-10-17: out of seq.c, Blue Hare OS's) The instrument's 27 keys: scanned once a block (os_keys_block,
 * from the baseline's events_block), mapped to notes by the selected track's SCL (kb_map: QNT SNAP / WHITE), played
 * through the baseline's input_on / input_off (as chords where CHRD is on); the FX layer's keys (perf_key*); the
 * splash's and the dark screen's silence. MIDI echoes the keys out (midi_out_event). */
static uint8_t kb_chord[27][CHORD_MAX], kb_chn[27];   /* the notes key k started (kb_chn 0: none; chord.c) */
static uint32_t kb_prev;
static volatile uint8_t kb_boot_hold;    /* #137: set from power-on to the end of the splash (main.c): the keys sound nothing,
                                          * and one held across its end stays silent until it is let go and pressed again */
static uint8_t kb_note[27], kb_trk[27];  /* per key: the note it started and on which track */
static volatile uint8_t kb_asleep;       /* MENU > SCREEN OFF (ui.c): the screen dark or waking, a key pressed now only
                                          * wakes it (silent, no layer, its release nothing) */

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

static void os_keys_block(void) { keyboard_block(); }
static int os_keys_hold_track(uint32_t track)        /* a key holds a note on track (midi_control.c midi_track_held) */
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if ((fm1_in.notes & (1u << k)) && kb_trk[k] == track)
            return 1;
    return 0;
}
static int os_keys_hold_note(uint32_t track, uint32_t note)
{
    uint32_t k, i;
    for (k = 0; k < 27u; k++)
        if (kb_chn[k] && kb_trk[k] == track)
            for (i = 0; i < kb_chn[k]; i++)
                if (kb_chord[k][i] == note)
                    return 1;
    return 0;
}
