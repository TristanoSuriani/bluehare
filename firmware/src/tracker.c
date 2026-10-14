/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The tracker: the song and how it plays (the audio ISR, events_block), the edits the UI makes (ui_tracker.c).
 *
 * The song (core.h tk_song_t): TK_PATS patterns of up to TK_ROWS rows, four columns each (one per track), and an order
 * list naming the pattern each song position plays. A row is a 1/16 of the song tempo (BPM, or the external clock),
 * swung by CLOCK's SWING. PLAY plays one of two ways (tk_pl.mode, chosen by the page PLAY is pressed on):
 *   TK_LOOP  the pattern being edited, over and over; another pattern picked while it plays starts after this one;
 *   TK_SONG  the order list from a position, round and round.
 * A column is one voice line: a note releases the column's note before it (a note on the same row of every other
 * column plays with it), OFF releases it, an empty row lets it ring. The row's instrument (a slot of the instrument
 * table) becomes the track's sound from that row on: the engine and its EDIT values, the voice mode, an FM6 patch;
 * never the track's own settings (the ENV, the filter envelope, the sends, LEVEL and PAN: the SETTINGS page), which
 * the instruments leave alone. The track's CUT / RES settings, when set, hold the engine's filter over every
 * instrument (TK_INST_VAL: the instrument's own). The row's FX sets one parameter of the track for that row only,
 * over the instrument and the settings, and puts the value back on the next row of that track (tk_fx_restore).
 *
 * A cell's chord (core.h: two intervals above its note) plays with it: up to three notes in the column, released
 * together. A track that is not POLY (the instrument's voice mode) plays the note alone.
 *
 * Patterns live in two places, so the song fits the RAM that the four project slots and the song chain of 1.4 held:
 * patterns 0..TK_LO-1 in .noinit (cleared at power-on, tk_defaults), the rest in .bss (tk_row). With 8 tracks
 * (Fel-Tracker: a distribution's TRACKS 8) a pattern is twice the size: 0..6 in .noinit (its 15.3 KB), 7..15 in .bss,
 * 16..23 in the pool (TK_MID). */
#if NTRK == 8
#define TK_LO 7u
#define TK_MID 16u
#else
#define TK_LO 15u
#define TK_MID TK_PATS
#endif
static tk_cell_t tk_lo[TK_LO][TK_ROWS][NTRK] __attribute__((section(".noinit")));
static tk_cell_t tk_hi[TK_MID - TK_LO][TK_ROWS][NTRK];
#if TK_MID < TK_PATS
static tk_cell_t tk_top[TK_PATS - TK_MID][TK_ROWS][NTRK] __attribute__((section(".pool")));
#endif
static tk_song_t tk;

/* (Fel-Tracker) variations: a pattern played under its own tempo, mutes, chance and the tracks' sends and SLICER, so one
 * pattern carries several sections of a song. Every pattern has variation A (its default) and up to TK_VARS - 1 more,
 * B .. D (tk_nvar: how many it has). An order entry names a pattern and one of its variations (TK_OPAT, TK_OVAR: the
 * pattern in bits 0..4, the variation in 5..6; a song of before has A everywhere). A variation's values are over the
 * track's own while it plays (core.h trk_vo, tempo_ovr: the audio path reads them; the track's own stay as they are):
 *   bpm      0: the song's tempo (SONG), else 40..240
 *   mute     bit k: track k plays no notes
 *   chance   per track, 0..100 %: each note of the track's cells plays with that chance (100: always)
 *   vo       per track, the sends DIST CHO DLY REV and the SLICER's mode, pattern, rate: 0 the track's own (KEEP), else the
 *            value + 1 (core.h VO_*)
 * A new one starts as everything as the tracks have it (tk_var_default): adding it changes nothing until it is edited */
#define TK_VARS 4u
#define TK_OPAT(o) ((uint32_t)(o) & 31u)
#define TK_OVAR(o) ((uint32_t)(o) >> 5 & 3u)
#define TK_OENT(p, v) ((uint8_t)((p) % TK_PATS | ((v) % TK_VARS) << 5))
typedef struct {
    uint8_t bpm, mute;
    uint8_t chance[NTRK];
    uint8_t vo[NTRK][VO_N];
} tk_var_t;
static tk_var_t tk_var[TK_PATS][TK_VARS] __attribute__((section(".pool")));
static uint8_t tk_nvar[TK_PATS];         /* the variations each pattern has, 1 .. TK_VARS (A always) */
static void tk_var_default(tk_var_t *v)
{
    memset(v, 0, sizeof *v);
    memset(v->chance, 100, sizeof v->chance);
}
static int tk_var_is_default(const tk_var_t *v)
{
    tk_var_t d;
    tk_var_default(&d);
    return !memcmp(v, &d, sizeof d);
}
static const char TK_VLET[TK_VARS] = {'A', 'B', 'C', 'D'};

static tk_cell_t *tk_row(uint32_t p, uint32_t r)
{
    p %= TK_PATS;
    r %= TK_ROWS;
#if TK_MID < TK_PATS
    if (p >= TK_MID)
        return tk_top[p - TK_MID][r];
#endif
    return p < TK_LO ? tk_lo[p][r] : tk_hi[p - TK_LO][r];
}
static tk_cell_t *tk_at(uint32_t p, uint32_t r, uint32_t k) { return &tk_row(p, r)[k % NTRK]; }
static uint32_t tk_len(uint32_t p) { return tk.len[p % TK_PATS] ? tk.len[p % TK_PATS] : 1u; }

/* the FX commands: the letter a cell shows, the name the footer shows, the parameter (TKP_CUT / TKP_RES: the engine's
 * filter, as MIDI CC74 / CC71 reach it). Append-only: a stored command keeps its meaning */
enum { TKC_NONE, TKC_LEVEL, TKC_ATK, TKC_DEC, TKC_SUS, TKC_REL, TKC_CUT, TKC_RES, TKC_FENV, TKC_PAN, TKC_DIST,
       TKC_CHOR, TKC_DLY, TKC_REV, TKC_GLIDE, TKC_PLAY, TKC_N };
#define TKP_CUT 0xFEu
#define TKP_RES 0xFDu
static const char TKC_CHAR[TKC_N] = {'.', 'V', 'A', 'D', 'S', 'R', 'C', 'Q', 'F', 'P', 'X', 'H', 'E', 'W', 'G', '*'};
static const char *const TKC_NAME[TKC_N] = {"NO FX", "LEVEL", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "CUTOFF",
                                            "RESONANCE", "FLT ENV", "PAN", "DIST", "CHORUS", "DELAY", "REVERB",
                                            "GLIDE", "PLAY"};
static const uint8_t TKC_PARAM[TKC_N] = {0xFF, P_LEVEL, P_ATK, P_DEC, P_SUS, P_REL, TKP_CUT, TKP_RES, P_ED_FLT, P_PAN,
                                         P_DIST, P_CHOR, P_DLY, P_REV, P_GLIDE, 0xFF};
/* (Fel-Tracker) PLAY (the last of the 16 commands a cell holds): how the row's note plays, its value the command (bits 4..6)
 * and its amount (bits 0..3), shown as the command's own letter and the amount: T RETRIG (the note struck again: the
 * amount's hits in the row, 2..15), Y DELAY (it starts amount/16 of the row late), J ARPEGGIO (the cell's chord played
 * a note at a time: the amount's steps in the row, 0: 3), U SLIDE UP and N SLIDE DOWN (the track's pitch moves the
 * amount's semitones over the row; it stays there until the track's next note), % CHANCE (it plays with a chance of
 * amount/15: 0 never, 15 always). Append-only, as the commands are */
enum { TKP_RETRIG, TKP_DELAY, TKP_ARP, TKP_SLUP, TKP_SLDN, TKP_CHANCE, TKP_N };
static const char TKP_CHAR[TKP_N] = {'T', 'Y', 'J', 'U', 'N', '%'};   /* (T: R is RELEASE) */
static const char *const TKP_NAME[TKP_N] = {"RETRIG", "NOTE DELAY", "ARPEGGIO", "SLIDE UP", "SLIDE DOWN", "CHANCE"};
static inline uint32_t tk_psub(const tk_cell_t *c) { return tk_cmd(c) == TKC_PLAY ? tk_val(c) >> 4 : TKP_N; }
static inline uint32_t tk_parg(const tk_cell_t *c) { return tk_val(c) & 15u; }

/* the parameter id the engine's filter is (TKP_CUT / TKP_RES on track t's engine), 0xFF: it has none */
static uint32_t tk_filter_id(const track_t *t, uint32_t which)
{
    uint32_t e = ENGINES[eng_idx(t->eng_req)]->cc >> (which == TKP_CUT ? 4 : 0) & 15u;
    return e ? P_E0 + e - 1u : 0xFFu;
}
static uint32_t tk_param_id(const track_t *t, uint32_t cmd)
{
    uint32_t id = cmd < TKC_N ? TKC_PARAM[cmd] : 0xFFu;
    return id == TKP_CUT || id == TKP_RES ? tk_filter_id(t, id) : id;
}
/* 0..127 over parameter id's range (64: the middle of a bipolar one), as a MIDI CC sets it */
static int16_t tk_scale(const track_t *t, uint32_t id, uint32_t v)
{
    const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), id);
    if (d->max <= d->min)
        return (int16_t)d->min;
    return (int16_t)(d->min + ((int32_t)(v > 127u ? 127u : v) * (d->max - d->min) + 63) / 127);
}
/* .. and back: the 0..127 a value stands at (the SETTINGS page shows CUT / RES so) */
static uint32_t tk_unscale(const track_t *t, uint32_t id, int32_t v)
{
    const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), id);
    if (d->max <= d->min)
        return 0;
    v = clamp(v, d->min, d->max);
    return (uint32_t)(((v - d->min) * 127 + (d->max - d->min) / 2) / (d->max - d->min));
}

/* the track's CUT / RES settings over its sound (after an instrument, or a setting changed) */
static void tk_filter_hold(track_t *t)
{
    uint32_t k = trk_index(t), id;
    if (tk.cut[k] != TK_INST_VAL && (id = tk_filter_id(t, TKP_CUT)) != 0xFFu)
        t->p[id] = tk_scale(t, id, tk.cut[k]);
    if (tk.res[k] != TK_INST_VAL && (id = tk_filter_id(t, TKP_RES)) != 0xFFu)
        t->p[id] = tk_scale(t, id, tk.res[k]);
}

/* a user preset's sound into track t (upreset.c sets it at boot: 0 = none, a user instrument plays nothing new) */
static void (*tk_user_sound)(track_t *t, uint32_t slot);
static uint8_t tk_trk_inst[NTRK];       /* the instrument each track took last (0 none; tk_track_inst checks it still is) */

/* instrument i (1..TK_INSTS) becomes track t's sound: the engine (switched after its voices fade, voice.c
 * engine_block), its EDIT values, the voice mode, FM6's patch; the track's settings stay (see the top). Audio ISR (a
 * row) or main loop with the audio IRQ off (the INSTR page). Not inlined: once a row at most, out of the ISR's loop */
/* instrument in's factory sound in this build: the engine (its src is an engine id: the build's own, or the substitute
 * of one it lacks), *pi its preset (a substitute: its first); ENG_NO for none (a user preset, none, a retired id) */
static uint32_t tk_inst_engine(const tk_inst_t *in, uint32_t *pi)
{
    int sub;
    uint32_t e = in->src < EID_COUNT ? eng_load_id(in->src, &sub) : ENG_NO;
    if (e != ENG_NO)
        *pi = sub || !ENGINES[e]->npresets ? 0u : (uint32_t)in->preset % ENGINES[e]->npresets;
    return e;
}
static __attribute__((noinline)) void tk_inst_apply(track_t *t, uint32_t i)
{
    const tk_inst_t *in;
    const engine_t *e;
    uint32_t k, pi;
    if (!i || i > TK_INSTS)
        return;
    in = &tk.instr[i - 1u];
    if (in->src >= TK_SRC_USER && in->src != TK_SRC_NONE) {
        if (tk_user_sound)
            tk_user_sound(t, (uint32_t)(in->src - TK_SRC_USER) % UP_SLOTS);
    } else if ((k = tk_inst_engine(in, &pi)) != ENG_NO) {
        k = eng_for_track(t, k);                     /* (DUBSIREN off the last track: DUBSIREN8, the same preset) */
        e = ENGINES[k];
        t->eng_req = (uint8_t)k;
        t->user = 0;
        if (e->npresets) {
            t->preset = (uint8_t)pi;
            for (k = 0; k < 8u; k++)
                t->p[P_E0 + k] = (int16_t)e->presets[pi].e[k];
            t->p[P_VOICE] = e->presets[pi].mono ? V_LEGATO : V_POLY;
            t->p[P_OCT] = e->presets[pi].oct;            /* (the sound's octave goes with it) */
        } else {
            for (k = 0; k < 8u; k++)
                t->p[P_E0 + k] = e->edit[k].def;
        }
    } else {
        return;
    }
    if (t >= trk && t < trk + NTRK)
        tk_trk_inst[t - trk] = (uint8_t)i;
    tk_filter_hold(t);
}

/* the instrument track k sounds now (the column heads), 0: its sound is not one (none taken yet, or PRESETS / a
 * sound load since, or the instrument changed on INSTR) */
static uint32_t tk_track_inst(uint32_t k)
{
    const track_t *t = &trk[k % NTRK];
    uint32_t i = tk_trk_inst[k % NTRK];
    const tk_inst_t *in;
    uint32_t e, pi;
    if (!i || i > TK_INSTS)
        return 0;
    in = &tk.instr[i - 1u];
    if (in->src == TK_SRC_NONE)
        return 0;
    if (in->src >= TK_SRC_USER)
        return t->user == in->src - TK_SRC_USER + 1u ? i : 0u;
    if ((e = tk_inst_engine(in, &pi)) == ENG_NO || t->user || t->eng_req != e)
        return 0;
    return !ENGINES[e]->npresets || t->preset == pi ? i : 0u;
}

/* -------------------------------------------------------------- playback --- */
enum { TK_LOOP, TK_SONG };
static struct {
    volatile uint8_t mode;       /* TK_LOOP / TK_SONG: what the next PLAY plays (the UI sets it) */
    volatile uint8_t pat, pos, row;   /* playing now: the pattern, the song position (SONG), the row */
    volatile uint8_t loop_pat;   /* LOOP: the pattern the UI edits (played from the end of the one playing) */
    volatile uint8_t start_pos;  /* SONG: the position PLAY starts from */
    volatile uint8_t start_row;  /* LOOP: the row the next PLAY starts from (SEQ + PLAY: the cursor's; then 0 again) */
    volatile uint32_t rows;      /* rows played since PLAY (the UI follows them) */
    uint8_t fresh;               /* PLAY: row 0 plays on the next tick */
    uint32_t row_pos;            /* samples into the row */
    uint8_t note[NTRK];          /* the note each column sounds, 0 none */
    uint8_t ch[NTRK][TK_CHORD - 1u];   /* .. and its chord's other notes, 0 none */
    uint8_t ovr[NTRK];           /* the parameter a row's FX holds (id + 1), 0 none */
    int16_t ovr_v[NTRK];         /* .. the track's own value, back on its next row */
    uint8_t skip[NTRK];          /* live recording: the row (+ 1) whose note sounds already (it does not play again) */
    uint8_t rec_row[NTRK], rec_note[NTRK];   /* live recording: the row (+ 1) and the note recorded last (its OFF) */
    volatile uint8_t var;        /* the variation playing now (of tk_pl.pat) */
    volatile uint8_t loop_var;   /* LOOP: the variation the UI edits (the pattern plays under it) */
    uint8_t vmute;               /* .. its mutes */
    uint32_t rnd;                /* the chance's random numbers (from the same seed at every PLAY: renders repeat) */
    /* (Fel-Tracker) a PLAY command at work in its row: the command + 1 (0 none), its amount, the row's sample of its
     * next step, the steps done; DELAY's cell; ARPEGGIO's notes; SLIDE's start (the track's tk_slide_q8) */
    uint8_t pcmd[NTRK], parg[NTRK], pstep[NTRK], parp[NTRK][TK_CHORD], pn[NTRK];
    uint32_t pnext[NTRK];
    tk_cell_t pcell[NTRK];
    int32_t pslide[NTRK];
} tk_pl;
static volatile uint8_t tk_cur_inst;   /* the instrument the keys record with (the UI's last one), 0 none */
static volatile uint32_t tk_ext_gen;   /* writes not by hand: live recording, the editor (they end an undo's turn) */

static uint32_t tk_row_len(uint32_t row) { return swing_step_len(&trk[0], div_samples(2), row); }   /* (core.h: the
                                                                                                 * SLICER's too) */

static void tk_fx_restore(uint32_t k)
{
    if (tk_pl.ovr[k]) {
        trk[k].p[tk_pl.ovr[k] - 1u] = tk_pl.ovr_v[k];
        tk_pl.ovr[k] = 0;
    }
}

static void tk_release(uint32_t k)
{
    uint32_t j;
    if (tk_pl.note[k]) {
        trk_note_off(&trk[k], tk_pl.note[k]);
        tk_pl.note[k] = 0;
    }
    for (j = 0; j < TK_CHORD - 1u; j++)
        if (tk_pl.ch[k][j]) {
            trk_note_off(&trk[k], tk_pl.ch[k][j]);
            tk_pl.ch[k][j] = 0;
        }
}
/* the notes cell c plays on track t: its chord (a track not POLY: the note alone), SCL QNT SEQ snapping each onto the
 * scale (the cell as written; never a kit's or the slices' notes). out: TK_CHORD */
static uint32_t tk_row_notes(const track_t *t, const tk_cell_t *c, uint8_t *out)
{
    uint32_t n = tk_chord_notes(c, out), j;
    if (n > 1u && t->p[P_VOICE] != V_POLY)
        n = 1;
    if (n && t->p[P_QUANT] == QN_SEQ) {
        const engine_t *e = ENGINES[eng_idx(t->eng_req)];
        if (!(e->keys && e->keys(t, 0) >= 0))
            for (j = 0; j < n; j++)
                out[j] = (uint8_t)clamp(scale_snap(t, (int32_t)out[j]), 1, TK_OFF - 1);
    }
    return n;
}

static int tk_var_plays(uint32_t k);
static uint32_t tk_rand(void)                       /* (the chance's random numbers: tk_pl.rnd) */
{
    tk_pl.rnd ^= tk_pl.rnd << 13;
    tk_pl.rnd ^= tk_pl.rnd >> 17;
    tk_pl.rnd ^= tk_pl.rnd << 5;
    return tk_pl.rnd;
}
static int tk_chance(uint32_t a) { return a >= 15u || tk_rand() % 15u < a; }   /* PLAY CHANCE: a / 15 */
/* the notes of cell c (its note n: OFF releases the column) on column k; arp: only the chord's first, the others kept
 * for ARPEGGIO (tk_pl.parp). The notes it plays */
static uint32_t tk_col_play(uint32_t k, const tk_cell_t *c, uint32_t n, int arp)
{
    track_t *t = &trk[k];
    uint8_t nw[TK_CHORD], old[TK_CHORD] = {tk_pl.note[k], tk_pl.ch[k][0], tk_pl.ch[k][1]};
    uint32_t m, i, j;
    if (n == TK_OFF) {
        tk_release(k);
        return 0;
    }
    m = tk_row_notes(t, c, nw);
    if (arp && m > 1u) {                            /* ARPEGGIO: the first note now, the others in turn */
        for (i = 0; i < m; i++)
            tk_pl.parp[k][i] = nw[i];
        tk_pl.pn[k] = (uint8_t)m;
        m = 1;
    }
    for (i = 0; i < TK_CHORD; i++)                  /* (a note sounding again: released first, so it starts again) */
        for (j = 0; old[i] && j < m; j++)
            if (old[i] == nw[j]) {
                trk_note_off(t, old[i]);
                old[i] = 0;
            }
    for (j = 0; j < m; j++)
        trk_note_on(t, nw[j], 100);
    for (i = 0; i < TK_CHORD; i++)                  /* (after the new ones: MONO / LEGATO glide into it) */
        if (old[i])
            trk_note_off(t, old[i]);
    tk_pl.note[k] = nw[0];
    tk_pl.ch[k][0] = m > 1u ? nw[1] : 0u;
    tk_pl.ch[k][1] = m > 2u ? nw[2] : 0u;
    tk_slide_q8[k] = 0;                             /* (a note: the slide starts again from its pitch) */
    return m;
}

/* the row tk_pl.row of tk_pl.pat starts: each column's FX ends, its instrument, its FX, its note (not inlined: out
 * of the audio ISR's loop over the blocks) */
static __attribute__((noinline)) void tk_play_row(void)
{
    uint32_t k, id, n, sub, arg, len = tk_row_len(tk_pl.row);
    tk_cell_t c;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        c = *tk_at(tk_pl.pat, tk_pl.row, k);
        tk_fx_restore(k);
        if (tk_pl.pcmd[k] == TKP_SLUP + 1u || tk_pl.pcmd[k] == TKP_SLDN + 1u)   /* (the last row's slide: all the way) */
            tk_slide_q8[k] = tk_pl.pslide[k] + (tk_pl.pcmd[k] == TKP_SLUP + 1u ? 256 : -256) * (int32_t)tk_pl.parg[k];
        tk_pl.pcmd[k] = 0;
        if (tk_inst(&c))
            tk_inst_apply(t, tk_inst(&c));
        if (tk_cmd(&c) && (id = tk_param_id(t, tk_cmd(&c))) < P_COUNT) {
            tk_pl.ovr_v[k] = t->p[id];
            tk_pl.ovr[k] = (uint8_t)(id + 1u);
            t->p[id] = tk_scale(t, id, tk_val(&c));
        }
        n = tk_note(&c);
        sub = tk_psub(&c);
        arg = tk_parg(&c);
        if (n && n != TK_OFF && !tk_var_plays(k))
            n = 0;                                  /* (the variation: muted, or its chance said no: as an empty row) */
        if (n && n != TK_OFF && sub == TKP_CHANCE && !tk_chance(arg))
            n = 0;                                  /* (PLAY CHANCE said no) */
        if (tk_pl.skip[k] == tk_pl.row + 1u) {      /* (live recording: it sounds from the keys already) */
            tk_pl.skip[k] = 0;
            if (n && n != TK_OFF) {
                if (tk_pl.note[k] && tk_pl.note[k] != n)
                    tk_release(k);
                continue;
            }
        }
        if (sub == TKP_DELAY && n && arg) {         /* NOTE DELAY: the note later in the row (tk_play_tick) */
            tk_pl.pcmd[k] = TKP_DELAY + 1u;
            tk_pl.pcell[k] = c;
            tk_pl.pnext[k] = len * arg / 16u;
            continue;
        }
        if (n)
            tk_col_play(k, &c, n, sub == TKP_ARP);
        tk_pl.parg[k] = (uint8_t)arg;
        tk_pl.pstep[k] = 1;
        if (sub == TKP_RETRIG && n && n != TK_OFF && arg >= 2u) {
            tk_pl.pcmd[k] = TKP_RETRIG + 1u;
            tk_pl.pnext[k] = len / arg;
        } else if (sub == TKP_ARP && n && n != TK_OFF && tk_pl.pn[k] > 1u) {
            tk_pl.parg[k] = (uint8_t)(arg ? arg : 3u);
            tk_pl.pcmd[k] = TKP_ARP + 1u;
            tk_pl.pnext[k] = len / tk_pl.parg[k];
        } else if ((sub == TKP_SLUP || sub == TKP_SLDN) && arg) {
            tk_pl.pcmd[k] = (uint8_t)(sub + 1u);
            tk_pl.pslide[k] = tk_slide_q8[k];
        }
    }
    tk_pl.rows++;
}

/* (Fel-Tracker) the PLAY commands inside their row, row_pos samples into it (tk_tick, every block) */
static __attribute__((noinline)) void tk_play_tick(void)
{
    uint32_t k, len = tk_row_len(tk_pl.row), pos = tk_pl.row_pos;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        uint32_t cmd = tk_pl.pcmd[k];
        if (!cmd--)
            continue;
        if (cmd == TKP_RETRIG) {                    /* the notes sounding, struck again */
            while (tk_pl.pstep[k] < tk_pl.parg[k] && pos >= tk_pl.pnext[k]) {
                uint8_t ns[TK_CHORD] = {tk_pl.note[k], tk_pl.ch[k][0], tk_pl.ch[k][1]};
                uint32_t j;
                for (j = 0; j < TK_CHORD; j++)
                    if (ns[j])
                        trk_note_off(t, ns[j]);
                for (j = 0; j < TK_CHORD; j++)
                    if (ns[j])
                        trk_note_on(t, ns[j], 100);
                tk_pl.pstep[k]++;
                tk_pl.pnext[k] = len * tk_pl.pstep[k] / tk_pl.parg[k];
            }
        } else if (cmd == TKP_DELAY) {              /* the note, now */
            if (pos >= tk_pl.pnext[k]) {
                tk_pl.pcmd[k] = 0;
                tk_col_play(k, &tk_pl.pcell[k], tk_note(&tk_pl.pcell[k]), 0);
            }
        } else if (cmd == TKP_ARP) {                /* the chord's next note */
            while (tk_pl.pstep[k] < tk_pl.parg[k] && pos >= tk_pl.pnext[k]) {
                uint8_t nx = tk_pl.parp[k][tk_pl.pstep[k] % tk_pl.pn[k]];
                if (tk_pl.note[k])
                    trk_note_off(t, tk_pl.note[k]);
                trk_note_on(t, nx, 100);
                tk_pl.note[k] = nx;
                tk_pl.pstep[k]++;
                tk_pl.pnext[k] = len * tk_pl.pstep[k] / tk_pl.parg[k];
            }
        } else {                                    /* SLIDE UP / DOWN: on through the row */
            int32_t d = 256 * (int32_t)tk_pl.parg[k] * (int32_t)(pos < len ? pos : len) / (int32_t)(len ? len : 1u);
            tk_slide_q8[k] = tk_pl.pslide[k] + (cmd == TKP_SLUP ? d : -d);
        }
    }
}

/* variation v of pattern p starts playing (an order entry, LOOP's pattern): its tempo, mutes, sends and SLICER over the
 * tracks; a track it mutes lets go of its notes */
static void tk_var_enter(uint32_t p, uint32_t v)
{
    const tk_var_t *x = &tk_var[p % TK_PATS][v % TK_VARS];
    uint32_t k;
    tk_pl.var = (uint8_t)(v % TK_VARS);
    tempo_ovr = x->bpm;
    memcpy(trk_vo, x->vo, sizeof trk_vo);
    tk_pl.vmute = x->mute;
    for (k = 0; k < NTRK; k++)
        if ((x->mute >> k) & 1u)
            tk_release(k);
}
/* .. and none plays any more (STOP): the tracks as they are */
static void tk_var_leave(void)
{
    tempo_ovr = 0;
    memset(trk_vo, 0, sizeof trk_vo);
    tk_pl.vmute = 0;
    tk_pl.var = 0;
}
/* pattern p now has n variations (1 .. TK_VARS): a new one a copy of A; one gone back to the default, the order's
 * entries of it on A, and if it is the one playing, A plays (the caller holds the audio ISR off) */
static void tk_var_count(uint32_t p, uint32_t n)
{
    uint32_t v, i;
    p %= TK_PATS;
    n = n < 1u ? 1u : n > TK_VARS ? TK_VARS : n;
    for (v = tk_nvar[p]; v < n; v++)
        tk_var[p][v] = tk_var[p][0];
    for (v = n; v < TK_VARS; v++)
        tk_var_default(&tk_var[p][v]);
    for (i = 0; i < tk.norder; i++)
        if (TK_OPAT(tk.order[i]) == p && TK_OVAR(tk.order[i]) >= n)
            tk.order[i] = TK_OENT(p, 0u);
    if (tk_pl.loop_pat == p && tk_pl.loop_var >= n)
        tk_pl.loop_var = 0;
    tk_nvar[p] = (uint8_t)n;
    if (song.playing && tk_pl.pat == p && tk_pl.var >= n)
        tk_var_enter(p, 0);
}
/* variation v of pattern p deleted (not A): the ones after it move up a letter, as do the order's entries of them;
 * the order's entries of v (and LOOP's) play A; the one playing, if it moved, plays on as it is now lettered (the
 * caller holds the audio ISR off). 1 = deleted */
static int tk_var_delete(uint32_t p, uint32_t v)
{
    uint32_t i, n;
    p %= TK_PATS;
    n = tk_nvar[p];
    if (!v || v >= n)
        return 0;
    for (i = v; i + 1u < n; i++)
        tk_var[p][i] = tk_var[p][i + 1u];
    tk_var_default(&tk_var[p][n - 1u]);
    for (i = 0; i < tk.norder; i++) {
        uint32_t w = TK_OVAR(tk.order[i]);
        if (TK_OPAT(tk.order[i]) == p && w >= v)
            tk.order[i] = TK_OENT(p, w == v ? 0u : w - 1u);
    }
    if (tk_pl.loop_pat == p && tk_pl.loop_var >= v)
        tk_pl.loop_var = (uint8_t)(tk_pl.loop_var == v ? 0u : tk_pl.loop_var - 1u);
    tk_nvar[p] = (uint8_t)(n - 1u);
    if (song.playing && tk_pl.pat == p && tk_pl.var >= v)
        tk_var_enter(p, tk_pl.var == v ? 0u : tk_pl.var - 1u);
    return 1;
}
/* a note of track k on this row plays: not muted by the variation, and its chance */
static int tk_var_plays(uint32_t k)
{
    uint32_t c = tk_var[tk_pl.pat % TK_PATS][tk_pl.var % TK_VARS].chance[k % NTRK];
    if ((tk_pl.vmute >> k) & 1u)
        return 0;
    if (c >= 100u)
        return 1;
    tk_pl.rnd ^= tk_pl.rnd << 13;
    tk_pl.rnd ^= tk_pl.rnd >> 17;
    tk_pl.rnd ^= tk_pl.rnd << 5;
    return tk_pl.rnd % 100u < c;
}

/* PLAY (seq.c seq_start, the audio ISR): LOOP from row 0 of the pattern edited, SONG from the position picked */
static void tk_start(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        tk_fx_restore(k);
        tk_release(k);
        tk_pl.skip[k] = tk_pl.rec_row[k] = 0;
        tk_pl.pcmd[k] = 0;                          /* (Fel-Tracker: no PLAY command at work, no slide) */
        tk_slide_q8[k] = 0;
    }
    if (tk_pl.mode == TK_SONG) {
        tk_pl.pos = (uint8_t)(tk_pl.start_pos < tk.norder ? tk_pl.start_pos : 0u);
        tk_pl.pat = (uint8_t)(TK_OPAT(tk.order[tk_pl.pos]) % TK_PATS);
        tk_var_enter(tk_pl.pat, TK_OVAR(tk.order[tk_pl.pos]));
    } else {
        tk_pl.pat = (uint8_t)(tk_pl.loop_pat % TK_PATS);
        tk_var_enter(tk_pl.pat, tk_pl.loop_var);
    }
    tk_pl.rnd = 0x2545F491u;
    tk_pl.row = (uint8_t)(tk_pl.mode == TK_LOOP && tk_pl.start_row < tk_len(tk_pl.pat) ? tk_pl.start_row : 0u);
    tk_pl.start_row = 0;
    tk_pl.row_pos = 0;
    tk_pl.rows = 0;
    tk_pl.fresh = 1;
}

/* STOP (seq.c seq_stop): the notes end, the FX values go back */
static void tk_stop(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        tk_fx_restore(k);
        tk_release(k);
        tk_pl.skip[k] = tk_pl.rec_row[k] = 0;
        tk_pl.pcmd[k] = 0;                          /* (Fel-Tracker: no PLAY command at work, no slide) */
        tk_slide_q8[k] = 0;
    }
    tk_pl.fresh = 0;
    tk_var_leave();
}

/* the pattern after the one that ended: LOOP the one the UI edits now, SONG the next position (round) */
static void tk_next_pattern(void)
{
    if (tk_pl.mode == TK_SONG) {
        tk_pl.pos = (uint8_t)(tk_pl.pos + 1u < tk.norder ? tk_pl.pos + 1u : 0u);
        tk_pl.pat = (uint8_t)(TK_OPAT(tk.order[tk_pl.pos]) % TK_PATS);
        tk_var_enter(tk_pl.pat, TK_OVAR(tk.order[tk_pl.pos]));
    } else {
        tk_pl.pat = (uint8_t)(tk_pl.loop_pat % TK_PATS);
        tk_var_enter(tk_pl.pat, tk_pl.loop_var);
    }
}

#ifdef FELUCCA_NATIVE
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
#endif

/* MENU > CONTROL > STEP PREVIEW (ui_tracker.c): with the transport stopped, the cell the cursor lands on sounds once,
 * as its row would play it (its instrument, its note) for a 1/16. tk_aud_req: pattern << 16 | row << 8 | track + 1 */
static volatile uint32_t tk_aud_req;
static uint8_t tk_aud_trk, tk_aud_note, tk_aud_n, tk_aud_notes[TK_CHORD];   /* (tk_aud_note: one sounds) */
static uint32_t tk_aud_left;
static __attribute__((noinline)) void tk_audition(uint32_t n)
{
    uint32_t r = tk_aud_req, k = (r & 0xFFu) - 1u, note;
    const tk_cell_t *c;
    if (tk_aud_note) {                              /* the one sounding: let go after its 1/16 (or a new one) */
        if (tk_aud_left > n && !r && !song.playing) {
            tk_aud_left -= n;
            return;
        }
        while (tk_aud_n)
            trk_note_off(&trk[tk_aud_trk], tk_aud_notes[--tk_aud_n]);
        tk_aud_note = 0;
    }
    if (!r)
        return;
    tk_aud_req = 0;
    if (song.playing || k >= NTRK)
        return;
    c = tk_at(r >> 16, r >> 8 & 0xFFu, k);
    if (tk_inst(c))
        tk_inst_apply(&trk[k], tk_inst(c));
    tk_aud_n = (uint8_t)tk_row_notes(&trk[k], c, tk_aud_notes);
    if (!tk_aud_n)
        return;
    for (note = 0; note < tk_aud_n; note++)
        trk_note_on(&trk[k], tk_aud_notes[note], 100);
    tk_aud_trk = (uint8_t)k;
    tk_aud_note = tk_aud_notes[0];
    tk_aud_left = div_samples(2);
}

/* n samples on (events_block, the transport running; the external clock's samples while it runs) */
static void tk_tick(uint32_t n)
{
    uint32_t len;
    if (!song.playing)
        return;
    if (tk_pl.fresh) {                              /* row 0 with this block (as seq_tick's step 0) */
        tk_pl.fresh = 0;
        tk_play_row();
        return;
    }
    tk_pl.row_pos += n;
    while (tk_pl.row_pos >= (len = tk_row_len(tk_pl.row))) {
        tk_pl.row_pos -= len;
        if (tk_pl.row + 1u >= tk_len(tk_pl.pat)) {
            tk_pl.row = 0;
            tk_next_pattern();
        } else {
            tk_pl.row++;
        }
        tk_play_row();
    }
    tk_play_tick();                                 /* (Fel-Tracker: the PLAY commands inside the row) */
}

/* ------------------------------------------------------- live recording --- */
/* a note into armed track t while PLAY runs (seq.c input_on, the ARP's notes, the count-in's): written into the row
 * nearest to now of the pattern playing, with the instrument the UI last used (tk_cur_inst; none: the cell's stays).
 * A row still to come does not play it again (tk_pl.skip): the key sounds it already. Keys pressed while the first one
 * of the row is held add to its chord (1..15 semitones above it, two at most; others are not recorded). Audio ISR */
static void tk_rec_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t k = trk_index(t), r = tk_pl.row, next = tk_pl.row_pos > tk_row_len(tk_pl.row) / 2u;
    tk_cell_t *c;
    (void)vel;
    if (!song.playing || tk_pl.fresh || !note || note >= TK_OFF)
        return;
    if (next && r + 1u < tk_len(tk_pl.pat))
        r++;
    else
        next = 0;
    if (tk_pl.rec_row[k] == r + 1u) {               /* (a note recorded into this row already, still held: a chord) */
        uint32_t root = tk_pl.rec_note[k], i = note - root, ch;
        c = tk_at(tk_pl.pat, r, k);
        ch = tk_chord(c);
        if (note > root && i <= 15u && tk_note(c) == root && (ch & 15u) != i && (ch >> 4) != i && (ch >> 4) == 0u) {
            c->b[3] = (uint8_t)((ch & 15u) ? ch | i << 4 : i);
            tk_ext_gen++;
        }
        return;
    }
    c = tk_at(tk_pl.pat, r, k);
    *c = tk_pack(note, tk_cur_inst ? tk_cur_inst : tk_inst(c), tk_cmd(c), tk_val(c));   /* (a new note: no chord yet) */
    tk_ext_gen++;
    if (next) {
        tk_pl.skip[k] = (uint8_t)(r + 1u);
        if (tk_pl.note[k] && tk_pl.note[k] != note)
            tk_release(k);                          /* (the row it goes into would have released it) */
    } else if (tk_pl.note[k] && tk_pl.note[k] != note) {
        tk_release(k);                              /* (as the row would have, had it held the note) */
    }
    tk_pl.rec_row[k] = (uint8_t)(r + 1u);
    tk_pl.rec_note[k] = (uint8_t)note;
}

/* its key let go: OFF into the nearest row after the one it went into, when that holds no note (a hit of a one-shot
 * engine needs none) */
static void tk_rec_release(track_t *t, uint32_t note)
{
    uint32_t k = trk_index(t), r = tk_pl.row;
    tk_cell_t *c;
    if (!song.playing || !tk_pl.rec_row[k] || tk_pl.rec_note[k] != note)
        return;
    if (tk_pl.row_pos > tk_row_len(tk_pl.row) / 2u && r + 1u < tk_len(tk_pl.pat))
        r++;
    if (r + 1u != tk_pl.rec_row[k] && !ENGINES[eng_idx(t->eng_req)]->oneshot) {
        c = tk_at(tk_pl.pat, r, k);
        if (!tk_note(c)) {
            *c = tk_pack(TK_OFF, tk_inst(c), tk_cmd(c), tk_val(c));
            tk_ext_gen++;
        }
    }
    tk_pl.rec_row[k] = 0;
}

/* ------------------------------------------------------------ the song --- */
/* a fresh song (power-on, a new project): empty patterns of TK_ROWS rows, one position (pattern 00), instruments 01..04
 * the four tracks' power-on sounds (engines.c TRK_DEF), the filter the instruments' own */
static void tk_defaults(void)
{
    uint32_t i;
    memset(tk_lo, 0, sizeof tk_lo);
    memset(tk_hi, 0, sizeof tk_hi);
#if TK_MID < TK_PATS
    memset(tk_top, 0, sizeof tk_top);
#endif
    memset(&tk, 0, sizeof tk);
    tk.norder = 1;
    for (i = 0; i < TK_PATS; i++)
        tk.len[i] = TK_ROWS;
    for (i = 0; i < TK_INSTS; i++)
        tk.instr[i].src = TK_SRC_NONE;
    for (i = 0; i < TK_PATS; i++) {
        uint32_t v;
        for (v = 0; v < TK_VARS; v++)
            tk_var_default(&tk_var[i][v]);
        tk_nvar[i] = 1;
    }
    for (i = 0; i < NPART; i++) {
        tk.instr[i].src = (uint8_t)eng_id(TRK_DEF[i][0]);   /* (an instrument names its engine by id) */
        tk.instr[i].preset = TRK_DEF[i][1];
    }
    memset(tk.cut, TK_INST_VAL, sizeof tk.cut);
    memset(tk.res, TK_INST_VAL, sizeof tk.res);
    trk_mix_defaults();                           /* (Fel-Tracker: WIDTH, LOW CUT) */
    memset(&tk_pl, 0, sizeof tk_pl);
    memset(tk_trk_inst, 0, sizeof tk_trk_inst);
    tk_cur_inst = 0;
}

/* a song read from a store kept inside its fields (project.c, after a load) */
static void tk_bound(void)
{
    uint32_t i;
    if (!tk.norder || tk.norder > TK_ORDER)
        tk.norder = 1;
    for (i = 0; i < TK_PATS; i++)
        tk_nvar[i] = (uint8_t)clamp(tk_nvar[i], 1, TK_VARS);
    for (i = 0; i < TK_ORDER; i++) {
        uint32_t p = TK_OPAT(tk.order[i]) % TK_PATS, v = TK_OVAR(tk.order[i]);
        if (v >= tk_nvar[p])                        /* (an entry of a variation the pattern lacks: it has it now) */
            tk_nvar[p] = (uint8_t)(v + 1u);
        tk.order[i] = TK_OENT(p, v);
    }
    for (i = 0; i < TK_PATS; i++) {                 /* (a variation's values inside their fields) */
        uint32_t v, k, j;
        for (v = 0; v < TK_VARS; v++) {
            tk_var_t *x = &tk_var[i][v];
            static const uint8_t VMAX[VO_N] = {127, 127, 127, 127, 2, 16, 5};
            if (x->bpm && (x->bpm < 40u || x->bpm > 240u))
                x->bpm = 0;
            for (k = 0; k < NTRK; k++) {
                if (x->chance[k] > 100u)
                    x->chance[k] = 100;
                for (j = 0; j < VO_N; j++)
                    if (x->vo[k][j] > VMAX[j] + 1u || (j == VO_SLPAT && x->vo[k][j] == 1u))
                        x->vo[k][j] = 0;
            }
        }
    }
    for (i = 0; i < TK_PATS; i++)
        if (!tk.len[i] || tk.len[i] > TK_ROWS)
            tk.len[i] = TK_ROWS;
    for (i = 0; i < TK_INSTS; i++) {
        uint8_t s = tk.instr[i].src;
        if (s != TK_SRC_NONE && (s >= TK_SRC_USER ? s - TK_SRC_USER >= UP_SLOTS : eng_load_id(s, 0) == ENG_NO))
            tk.instr[i].src = TK_SRC_NONE;
    }
    for (i = 0; i < NTRK; i++) {
        if (tk.cut[i] > 127u)
            tk.cut[i] = TK_INST_VAL;
        if (tk.res[i] > 127u)
            tk.res[i] = TK_INST_VAL;
        if (trk_width[i] > TW_MAX || !tw_has(i))   /* (WIDTH: the last tracks' alone) */
            trk_width[i] = (uint8_t)(tw_has(i) && trk_width[i] <= TW_MAX ? trk_width[i] : 0u);
        if (trk_lowcut[i] >= TL_N)
            trk_lowcut[i] = 0;
        if (trk_duck[i] > TD_KEY)
            trk_duck[i] = 0;
    }
}

/* n (0..255) as two hex digits, 0-terminated (b: 3 bytes): rows, patterns, positions, instruments, FX values */
static void tk_hex2(char *b, uint32_t n)
{
    static const char H[] = "0123456789ABCDEF";
    b[0] = H[n >> 4 & 15u];
    b[1] = H[n & 15u];
    b[2] = 0;
}

static int tk_pat_empty(uint32_t p)
{
    uint32_t r, k;
    for (r = 0; r < TK_ROWS; r++)
        for (k = 0; k < NTRK; k++)
            if (!tk_empty(tk_at(p, r, k)))
                return 0;
    return 1;
}
