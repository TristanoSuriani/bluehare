/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The tracker's pages (tracker.c plays the song): SEQ opens PATTERN, its taps go round PATTERN, SONG, SETTINGS, INSTR;
 * GLO opens SONG. Included at the end of ui_graph.c (its list helpers); ui_draw.c draws them (tk_draw), ui_input.c
 * sends them the knobs, PRESETS, EDIT and the keys (tk_knob, tk_presets, tk_edit_tap, tk_page_keys).
 *
 * PATTERN (LOOP: PLAY here plays the pattern over and over): the pattern's rows, 8 at a time, a column per track (the
 * selected one, ALGORITHM, is the cursor's): the note (C-4, OFF, --- none), the instrument (01..3F, .. none) and the FX
 * (a letter and its value 00..7F, ... none). KNOB 1 the row, 2 the note (from none: the last note entered; below the
 * lowest OFF, then none), 3 the instrument, 4 the FX value (a new FX starts at the track's own value), PRESETS the FX
 * command (NO FX clears it); EDIT clears the cell. With the selected track armed (REC) and stopped the keys write
 * their note (and the instrument used last) at the cursor, which then moves EDIT STEP rows on; armed and playing they
 * record into the rows as they play (tracker.c tk_rec_note). Playing, the cursor is the row playing: the rows show
 * the 8 that hold it, the next 8 once it gets there. SEQ held: KNOB 1 the pattern edited (LOOP plays it from the end
 * of the one playing), 2 its length, 3 EDIT STEP, 4 the cell's chord; the white keys the tools (tk_tool), PLAY from
 * the cursor's row. SAVE held: the last pattern edit undone / redone (tk_undo_swap).
 * SONG (PLAY here plays the song from the position picked): the order list, each position a pattern and its variation
 * ("03B"). KNOB 1 the position (past the last: + ADD), 2 its pattern (on + ADD: a new position), 3 its variation, 4
 * right: a copy of the position after it, left: the position deleted (EDIT too). (A pattern's length: SEQ held, KNOB 2.)
 * VARIATION (Fel-Tracker): the variation of the SONG page's position (stopped there: of the pattern LOOP plays), its
 * rows like SETTINGS': TEMPO (SONG or a BPM), MUTE, CHANCE, the sends and the SLICER (each KEEP: the track's own),
 * PRESETS the row, KNOB 1..4 the view's tracks (TEMPO: KNOB 1), EDIT the selected track's value back to KEEP (TEMPO:
 * SONG; on the last row, DELETE, the variation deleted after the OCT- / OCT+ dialog: not A); PLAY here plays its pattern under it (LOOP). On SONG KNOB 3 picks a position's variation A .. D (past the
 * last one the pattern has: a new one, a copy of A).
 * SETTINGS: each track's ENV, filter and FX: a row a setting, a column a track; PRESETS the row, KNOB 1..4 tracks
 * 1..4. CUT and RES hold the engine's filter over every instrument (INST: the instrument's own; EDIT puts INST back).
 * (Fel-Tracker) WIDTH (the last tracks': OFF, 1..20 ms of Haas) and LOW CUT (OFF, 40 .. 320 Hz): the track's place in
 * the mix (core.h trk_width, trk_lowcut); EDIT puts the power-on one back.
 * INSTR: the instruments 01..3F, each a factory preset of an engine or a user preset. KNOB 1 (or PRESETS) the slot,
 * 2 its engine (or USER, or none), 3 its preset; the selected track plays it at once (the keys audition it), and the
 * keys record it. EDIT empties the slot. */
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* (ui_input.c) */
static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc,
                        int32_t ratio, uint32_t icon);                /* (ui_draw.c) */

#define TK_Y_INFO 26
#define TK_H_INFO 18
#define TK_Y_HEAD 46
#define TK_H_HEAD 16
#define TK_Y_ROWS 64
#define TK_ROW_H 16
#define TK_VIEW 8u
#define TK_Y_END (TK_Y_ROWS + (int32_t)TK_VIEW * TK_ROW_H)   /* 192: the footer at Y_FOOT (202) */
#define TK_COL_X(k) (20 + 55 * (int32_t)(k))
#define TK_COL_W 54
#define TK_GW 6                                  /* the grid font: 5 x 7 glyphs, 6 px apart */

static struct {
    uint8_t row;                 /* PATTERN: the cursor's row (playing: the row playing) */
    uint8_t field;               /* the field edited last: 0 the note, 1 the instrument, 2 the FX (the cursor shows it) */
    uint8_t step;                /* EDIT STEP: rows the cursor moves after a key's note (0..16) */
    uint8_t pos;                 /* SONG: the position picked (tk.norder: + ADD) */
    uint8_t srow;                /* SETTINGS: the setting picked */
    uint8_t vrow;                /* VARIATION: the row picked */
    uint8_t slot;                /* INSTR: the instrument picked (0..TK_INSTS-1) */
    uint8_t note;                /* the note KNOB 2 starts an empty cell from: the last one entered */
    uint8_t cmd;                 /* the FX command PRESETS starts an empty FX from: the last one picked */
    uint8_t playing, mode;       /* the transport as the last frame saw it (tk_frame) */
    uint32_t sig[TK_VIEW + 2u];  /* drawn: the info strip, the column heads, the rows */
    int8_t mv_dir;               /* tk_slow: the direction of the turn moving the cursor, */
    uint8_t mv_acc;              /* .. its detents since the cursor last moved */
    uint32_t mv_t;               /* .. fm1_ms of its last detent */
} tku = {0, 0, 1, 0, 0, 0, 0, 60, TKC_LEVEL, 0, 0, {0}};

static void tk_vtarget(uint32_t *p, uint32_t *v);   /* (VARIATION's variation, below) */
static void confirm_open(uint32_t kind, uint32_t trk);   /* (ui_input.c: the OCT- / OCT+ dialog) */
static uint32_t tk_graph(void) { return ui.home ? GR_NONE : PAGES[ui.page % NPAGES].graph; }
static int tk_page(void) { uint32_t g = tk_graph(); return g >= GR_TKPAT && g <= GR_TKINST; }
/* the pattern the PATTERN page shows: the one playing while SONG plays, else the one LOOP plays (and edits) */
static uint32_t tk_view_pat(void) { return song.playing && tk_pl.mode == TK_SONG ? tk_pl.pat : tk_pl.loop_pat; }
static int tk_following(void) { return song.playing && tk_pl.pat == tk_view_pat(); }

/* a cell written from the main loop: the audio ISR reads it whole (three bytes), never half of an edit */
static void tk_put(uint32_t p, uint32_t r, uint32_t k, tk_cell_t c)
{
    fm1_irq_off();
    *tk_at(p, r, k) = c;
    fm1_irq_on();
}

/* ------------------------------------------------------------ undo --- */
/* SAVE held on a tracker page (ui_input.c): the pattern edited last, cells and length, as it was before that edit,
 * swapped with it (held again: redo). An edit is one undo; a knob's turn (the same knob on the same cell, under
 * TK_UD_GAP ms apart) shares the copy from before its first detent. The editor's writes and live recording are not
 * edits here, they end a turn (tk_ext_gen). In the pool with the clipboard (RAM's .bss is tight) */
#define TK_UD_GAP 1500u
enum { TKU_KEY, TKU_NOTE, TKU_INST, TKU_FX, TKU_CMD, TKU_CHORD, TKU_LEN };   /* the tags (TKU_KEY: never shared) */
static struct {
    tk_cell_t c[TK_ROWS][NTRK];
    uint8_t pat, len, has, tag, row, col;
    uint32_t t_ms, gen;
} tk_ud __attribute__((section(".pool")));

/* pattern p is about to change by edit tag: its copy as it is now (unless the turn under way has one) */
static void tk_undo_take(uint32_t p, uint32_t tag)
{
    uint32_t r;
    p %= TK_PATS;
    if (tag != TKU_KEY && tk_ud.has && tag == tk_ud.tag && p == tk_ud.pat && tku.row == tk_ud.row &&
        song.sel == tk_ud.col && tk_ext_gen == tk_ud.gen && fm1_ms - tk_ud.t_ms < TK_UD_GAP) {
        tk_ud.t_ms = fm1_ms;
        return;
    }
    for (r = 0; r < TK_ROWS; r++)
        memcpy(tk_ud.c[r], tk_row(p, r), sizeof tk_ud.c[r]);
    tk_ud.pat = (uint8_t)p;
    tk_ud.len = tk.len[p];
    tk_ud.has = 1;
    tk_ud.tag = (uint8_t)tag;
    tk_ud.row = tku.row;
    tk_ud.col = song.sel;
    tk_ud.t_ms = fm1_ms;
    tk_ud.gen = tk_ext_gen;
}
/* a cell edited by hand: one undo (nothing changed: none, the last edit's copy stays) */
static void tk_edit(uint32_t tag, uint32_t p, uint32_t r, uint32_t k, tk_cell_t c)
{
    if (!memcmp(&c, tk_at(p, r, k), sizeof c))
        return;
    tk_undo_take(p, tag);
    tk_put(p, r, k, c);
}
/* a pattern's length set by hand */
static void tk_edit_len(uint32_t p, uint32_t len)
{
    if (tk.len[p % TK_PATS] == len)
        return;
    tk_undo_take(p, TKU_LEN);
    tk.len[p % TK_PATS] = (uint8_t)len;
}
static void tk_undo_swap(void)
{
    uint32_t p = tk_ud.pat, r, k;
    uint8_t l;
    char b[4];
    if (!tk_ud.has) {
        ui_message("NOTHING TO UNDO");
        return;
    }
    for (r = 0; r < TK_ROWS; r++)
        for (k = 0; k < NTRK; k++) {
            tk_cell_t c = *tk_at(p, r, k);
            tk_put(p, r, k, tk_ud.c[r][k]);
            tk_ud.c[r][k] = c;
        }
    l = tk.len[p];
    tk.len[p] = tk_ud.len;
    tk_ud.len = l;
    tk_ud.tag = TKU_KEY;                            /* (the next edit takes its own copy) */
    if (!song.playing)                              /* (stopped: PATTERN shows the pattern put back) */
        tk_pl.loop_pat = (uint8_t)p;
    if (tku.row >= tk_len(tk_view_pat()))
        tku.row = 0;
    tk_hex2(b, p);
    ui_say("UNDO/REDO PATTERN ", b);
    ui.force = 1;
}

static void tk_clear_pattern(uint32_t p)
{
    uint32_t r, k;
    tk_cell_t z = {{0, 0, 0}};
    tk_undo_take(p, TKU_KEY);
    for (r = 0; r < TK_ROWS; r++)
        for (k = 0; k < NTRK; k++)
            tk_put(p, r, k, z);
    ui.force = 1;
}
static void tk_clear_column(uint32_t p, uint32_t k)
{
    uint32_t r;
    tk_cell_t z = {{0, 0, 0}};
    tk_undo_take(p, TKU_KEY);
    for (r = 0; r < TK_ROWS; r++)
        tk_put(p, r, k, z);
    ui.force = 1;
}

/* ------------------------------------------------------ the SEQ tools --- */
/* SEQ held, the white keys from F3 (ui_layer.c layer_key): on the cursor's pattern (PATTERN's) and track. Each is one
 * undo (SAVE held); the layer's OCT- puts back the pattern as the layer opened. INS / DEL work in the pattern's
 * length: the rows after the cursor move down (the last one goes) or up (an empty one at the end); ALL: every track */
enum { TKT_COPY_TRK, TKT_PASTE_TRK, TKT_COPY_PAT, TKT_PASTE_PAT, TKT_INS, TKT_DEL, TKT_INS_ALL, TKT_DEL_ALL, TKT_N };
static const char *const TKT_NAME[TKT_N] = {"COPY TRK", "PASTE TRK", "COPY PAT", "PASTE PAT", "INS ROW", "DEL ROW",
                                            "INS ALL", "DEL ALL"};
static struct {
    tk_cell_t col[TK_ROWS];                         /* COPY TRK: a track's column */
    tk_cell_t pat[TK_ROWS][NTRK];                   /* COPY PAT: a pattern, */
    uint8_t len;                                    /* .. its length */
    uint8_t has;                                    /* bit 0 a column, bit 1 a pattern */
} tk_clip __attribute__((section(".pool")));

static int tk_tool_ok(uint32_t t)                   /* tool t can act now (the map: lit or dark) */
{
    return t == TKT_PASTE_TRK ? (tk_clip.has & 1u) != 0u : t == TKT_PASTE_PAT ? (tk_clip.has & 2u) != 0u : t < TKT_N;
}
static void tk_shift_col(uint32_t p, uint32_t k, uint32_t from, int del)   /* rows from.. of column k: down / up one */
{
    uint32_t len = tk_len(p), r;
    tk_cell_t z = {{0, 0, 0}};
    if (del) {
        for (r = from; r + 1u < len; r++)
            tk_put(p, r, k, *tk_at(p, r + 1u, k));
        tk_put(p, len - 1u, k, z);
    } else {
        for (r = len - 1u; r > from; r--)
            tk_put(p, r, k, *tk_at(p, r - 1u, k));
        tk_put(p, from, k, z);
    }
}
static int tk_tool(uint32_t t)                      /* 1 = the pattern changed (the layer's OCT- puts it back) */
{
    uint32_t p = tk_view_pat(), r, k;
    int changed = 0;
    char b[16];
    if (t >= TKT_N)
        return 0;
    if (!tk_tool_ok(t)) {
        ui_message("NOTHING COPIED");
        return 0;
    }
    if (t == TKT_COPY_TRK) {
        for (r = 0; r < TK_ROWS; r++)
            tk_clip.col[r] = *tk_at(p, r, song.sel);
        tk_clip.has |= 1u;
        str_cpy(b, "TRACK 1 COPIED", sizeof b);
        b[6] = (char)('1' + song.sel);
        ui_message(b);
    } else if (t == TKT_COPY_PAT) {
        for (r = 0; r < TK_ROWS; r++)
            memcpy(tk_clip.pat[r], tk_row(p, r), sizeof tk_clip.pat[r]);
        tk_clip.len = tk.len[p];
        tk_clip.has |= 2u;
        tk_hex2(b, p);
        ui_say("COPIED PATTERN ", b);
    } else {
        tk_undo_take(p, TKU_KEY);
        if (t == TKT_PASTE_TRK) {
            for (r = 0; r < TK_ROWS; r++)
                tk_put(p, r, song.sel, tk_clip.col[r]);
            str_cpy(b, "PASTED TO 1", sizeof b);
            b[10] = (char)('1' + song.sel);
            ui_message(b);
        } else if (t == TKT_PASTE_PAT) {
            for (r = 0; r < TK_ROWS; r++)
                for (k = 0; k < NTRK; k++)
                    tk_put(p, r, k, tk_clip.pat[r][k]);
            tk.len[p] = tk_clip.len ? tk_clip.len : (uint8_t)TK_ROWS;
            if (tku.row >= tk_len(p))
                tku.row = 0;
            tk_hex2(b, p);
            ui_say("PASTED TO PATTERN ", b);
        } else {
            int del = t == TKT_DEL || t == TKT_DEL_ALL;
            for (k = 0; k < NTRK; k++)
                if (t >= TKT_INS_ALL || k == song.sel)
                    tk_shift_col(p, k, tku.row % tk_len(p), del);
            ui_message(del ? "ROW DELETED" : "ROW INSERTED");
        }
        changed = 1;
    }
    ui.force = 1;
    return changed;
}

/* SEQ held + PLAY, stopped on PATTERN: LOOP from the cursor's row (ui_layer.c layer_play). 1 = done */
static int tk_play_from_row(void)
{
    if (tk_graph() != GR_TKPAT || song.playing)
        return 0;
    tk_pl.mode = TK_LOOP;
    tk_pl.start_row = tku.row;
    transport_req = 1;
    return 1;
}

/* PLAY pressed (ui_input.c, stopped): SONG from its position on the SONG page, LOOP on PATTERN; elsewhere as last time */
static void tk_play_mode(void)
{
    if (tk_graph() == GR_TKSONG) {
        tk_pl.mode = TK_SONG;
        tk_pl.start_pos = (uint8_t)(tku.pos < tk.norder ? tku.pos : 0u);
    } else if (tk_graph() == GR_TKPAT) {
        tk_pl.mode = TK_LOOP;
    } else if (tk_graph() == GR_TKVAR) {             /* its pattern, under it */
        uint32_t p, v;
        tk_vtarget(&p, &v);
        tk_pl.mode = TK_LOOP;
        tk_pl.loop_pat = (uint8_t)p;
        tk_pl.loop_var = (uint8_t)v;
    }
}

/* every frame: the cursor follows the row playing; a SONG stopped leaves its pattern on the PATTERN page */
static void tk_frame(void)
{
    uint32_t len;
    if (song.playing) {
        tku.mode = tk_pl.mode;
        if (tk_following())
            tku.row = tk_pl.row;
    } else if (tku.playing && tku.mode == TK_SONG) {
        tk_pl.loop_pat = tk_pl.pat;
        tku.pos = tk_pl.pos;
    }
    tku.playing = song.playing;
    len = tk_len(tk_view_pat());
    if (tku.row >= len)
        tku.row = (uint8_t)(len - 1u);
    if (tku.pos > tk.norder || tku.pos >= TK_ORDER)
        tku.pos = (uint8_t)(tk.norder < TK_ORDER ? tk.norder : TK_ORDER - 1u);
}

/* ------------------------------------------------------------- the grid font --- */
/* 5 x 7, a row a byte (bit 4 the left pixel): what the cells show (hex digits, notes, OFF, the FX letters, - # .) */
static const uint8_t TKF_DIG[10][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14}, {14, 17, 1, 2, 4, 8, 31}, {31, 2, 4, 2, 1, 17, 14},
    {2, 6, 10, 18, 31, 2, 2}, {31, 16, 30, 1, 1, 17, 14}, {6, 8, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12}};
static const uint8_t TKF_ABC[26][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
    {28, 18, 17, 17, 17, 18, 28}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {14, 4, 4, 4, 4, 4, 14},
    {7, 2, 2, 2, 2, 18, 12}, {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 17, 25, 21, 19, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
    {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4}, {17, 17, 17, 17, 17, 17, 14},
    {17, 17, 17, 17, 17, 10, 4}, {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 17, 10, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31}};
static const uint8_t TKF_DASH[7] = {0, 0, 0, 31, 0, 0, 0};
static const uint8_t TKF_SHARP[7] = {10, 10, 31, 10, 31, 10, 10};
static const uint8_t TKF_DOT[7] = {0, 0, 0, 0, 0, 0, 4};
static const uint8_t *tkf_glyph(char c)
{
    if (c >= '0' && c <= '9')
        return TKF_DIG[c - '0'];
    if (c >= 'A' && c <= 'Z')
        return TKF_ABC[c - 'A'];
    return c == '-' ? TKF_DASH : c == '#' ? TKF_SHARP : c == '.' ? TKF_DOT : 0;
}
/* s at x, y of the canvas (glyph tops), TK_GW px a character */
static int32_t tkf_text(int32_t x, int32_t y, const char *s, uint16_t fg)
{
    uint16_t c = swap16(fg);
    for (; *s; s++, x += TK_GW) {
        const uint8_t *g = tkf_glyph(*s);
        uint32_t r, b;
        if (!g)
            continue;
        for (r = 0; r < 7u; r++)
            for (b = 0; b < 5u; b++)
                if ((g[r] >> (4u - b)) & 1u) {
                    int32_t px = x + (int32_t)b, py = y + (int32_t)r + cv_oy;
                    if ((uint32_t)px < cv_w && (uint32_t)py < cv_h)
                        cv_px[(uint32_t)py * cv_w + (uint32_t)px] = c;
                }
    }
    return x;
}

/* a note as the cells show it: C-4, C#4 (MIDI 60 C4), OFF; none "---" */
static void tk_note_str(char *b, uint32_t n)
{
    static const char NM[12][3] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    int32_t o = (int32_t)n / 12 - 1;
    if (!n) {
        str_cpy(b, "---", 4);
        return;
    }
    if (n == TK_OFF) {
        str_cpy(b, "OFF", 4);
        return;
    }
    b[0] = NM[n % 12u][0];
    b[1] = NM[n % 12u][1];
    b[2] = o < 0 ? '-' : (char)('0' + o);
    b[3] = 0;
}
static void tk_fx_str(char *b, const tk_cell_t *c)
{
    if (!tk_cmd(c)) {
        str_cpy(b, "...", 4);
        return;
    }
    if (tk_cmd(c) == TKC_PLAY) {                    /* (Fel-Tracker) PLAY: its command's letter, its amount */
        b[0] = TKP_CHAR[tk_psub(c) % TKP_N];
        tk_hex2(b + 1, tk_parg(c));
        return;
    }
    b[0] = TKC_CHAR[tk_cmd(c) % TKC_N];
    tk_hex2(b + 1, tk_val(c));
}

/* (Fel-Tracker) the FX commands as PRESETS steps through them: the parameter ones, then PLAY's six as six */
#define TKV_N (TKC_PLAY + TKP_N)
static uint32_t tk_vcmd(const tk_cell_t *c)
{
    uint32_t sub = tk_psub(c);
    return tk_cmd(c) < TKC_PLAY ? tk_cmd(c) : TKC_PLAY + (sub < TKP_N ? sub : TKP_N - 1u);
}
static const uint8_t TKP_DEF[TKP_N] = {4, 8, 3, 2, 2, 8};   /* (a new one's amount: 4 hits, half a row, 3 steps, ..) */

/* ------------------------------------------------------------ drawing --- */
static void tk_gaps(void)                        /* full redraw: the BG between the strips (header, info, heads, rows) */
{
    lcd_fill(0, H_HEAD, 240, TK_Y_INFO - H_HEAD, T_BG);
    lcd_fill(0, TK_Y_INFO + TK_H_INFO, 240, TK_Y_HEAD - TK_Y_INFO - TK_H_INFO, T_BG);
    lcd_fill(0, TK_Y_HEAD + TK_H_HEAD, 240, TK_Y_ROWS - TK_Y_HEAD - TK_H_HEAD, T_BG);
    lcd_fill(0, (uint32_t)TK_Y_END, 240, (uint32_t)(Y_FOOT - TK_Y_END), T_BG);
}

/* the strip over the rows: what the page shows (S, on a surface) */
static void tk_info(void)
{
    char a[16], b[24], c[16], h[3];
    uint32_t g = tk_graph(), sig, p = tk_view_pat();
    a[0] = b[0] = c[0] = 0;
    if (g == GR_TKPAT) {
        str_cpy(a, "PATTERN ", sizeof a);
        tk_hex2(h, p);
        str_cpy(a + str_len(a), h, sizeof a - str_len(a));
        str_cpy(b, "LEN ", sizeof b);
        tk_hex2(h, tk_len(p));
        str_cpy(b + 4, h, sizeof b - 4);
        str_cpy(b + str_len(b), "  STEP ", sizeof b - str_len(b));
        fmt_int(b + str_len(b), tku.step);
        if (song.playing && tk_pl.mode == TK_SONG) {
            str_cpy(c, "SONG ", sizeof c);
            tk_hex2(h, tk_pl.pos);
            str_cpy(c + 5, h, sizeof c - 5);
        } else {
            str_cpy(c, song.playing && tk_pl.pat != p ? "CUED" : "LOOP", sizeof c);
        }
    } else if (g == GR_TKSONG) {
        str_cpy(a, "SONG", sizeof a);
        fmt_int(b, tk.norder);
        str_cpy(b + str_len(b), tk.norder == 1u ? " POSITION" : " POSITIONS", sizeof b - str_len(b));
        str_cpy(c, song.playing && tk_pl.mode == TK_SONG ? "PLAYING" : "", sizeof c);
    } else if (g == GR_TKVAR) {
        uint32_t vp, vv;
        tk_vtarget(&vp, &vv);
        (void)vv;
        str_cpy(a, "PAT SET ", sizeof a);
        tk_hex2(h, vp);
        str_cpy(a + str_len(a), h, sizeof a - str_len(a));
        str_cpy(b, tku.pos < tk.norder ? "POS " : "LOOP", sizeof b);
        if (tku.pos < tk.norder) {
            tk_hex2(h, tku.pos);
            str_cpy(b + str_len(b), h, sizeof b - str_len(b));
        }
        str_cpy(c, song.playing && tk_pl.pat == vp && tk_pl.var == vv ? "PLAYING" : "[PRESETS] ROW", sizeof c);
    } else if (g == GR_TKSET) {
        str_cpy(a, "TRACK SETTINGS", sizeof a);
        str_cpy(c, "[PRESETS] ROW", sizeof c);
    } else {
        str_cpy(a, "INSTRUMENT ", sizeof a);
        tk_hex2(h, tku.slot + 1u);
        str_cpy(a + str_len(a), h, sizeof a - str_len(a));
    }
    sig = str_hash(str_hash(str_hash(g * 31u + 7u, a), b), c);
    if (!ui.force && sig == tku.sig[0])
        return;
    tku.sig[0] = sig;
    cv_begin(240, TK_H_INFO, T_BG);
    cv_rrect(3, 0, 234, TK_H_INFO, 4, T_SURF, T_BG);
    cv_bg = T_SURF;
    cv_text(10, 2, &AF_S, a, T_THEME);
    if (b[0])
        cv_text(10 + text_w(&AF_S, a) + 12, 2, &AF_S, b, T_MID);
    if (c[0])
        cv_text_r(230, 2, &AF_S, c, T_ACCENT, T_SURF);
    cv_blit(0, TK_Y_INFO);
}

/* PATTERN's column heads: the track, the instrument it sounds now (none: its sound is not one) and its engine, the
 * selected one in the accent; a track silent (GLO's MUTE, or another one's SOLO held) dim, "M" for its number */
static int tk_silent(uint32_t k) { return trk[k].p[P_MUTE] || (perf_solo && !((perf_solo >> k) & 1u)); }
static void tk_heads(void)
{
    uint32_t k, j, v = trk_view(), sig = song.sel * 7u + 1u;
    for (k = 0; k < NTRK; k++)
        sig = ((sig * 31u + trk[k].eng_req) * 131u + tk_track_inst(k)) * 3u + (uint32_t)tk_silent(k);
    if (!ui.force && sig == tku.sig[1])
        return;
    tku.sig[1] = sig;
    cv_begin(240, TK_H_HEAD, T_BG);
    for (j = 0; j < VIEW_N; j++) {                  /* (the grid font: "1 01 ANLG" in the 54 px of a column) */
        char b[8];
        uint32_t i, off;
        int32_t x = TK_COL_X(j) + 1;
        k = v + j;                                  /* (8 tracks: the view's four, 1 .. 4 or 5 .. 8) */
        i = tk_track_inst(k);
        off = (uint32_t)tk_silent(k);
        uint16_t fg = off ? T_DIM : k == song.sel ? T_ACCENT : T_MID;
        b[0] = off ? 'M' : (char)('1' + k);
        b[1] = 0;
        tkf_text(x, 5, b, off ? T_MID : fg);
        if (i) {
            tk_hex2(b, i);
            tkf_text(x + 9, 5, b, off ? T_DIM : k == song.sel ? T_ACCENT : T_THEME);
        }
        str_cpy(b, eng_abbr(ENGINES[trk[k].eng_req % NENGINES]->name), 5);
        tkf_text(i ? x + 26 : x + 9, 5, b, fg);
    }
    cv_blit(0, TK_Y_HEAD);
}

/* row r of pattern p on screen row i (r past the length: an empty strip) */
static void tk_grid_row(uint32_t i, uint32_t p, uint32_t r)
{
    uint32_t len = tk_len(p), k, sig, cur = r == tku.row, play = song.playing && tk_pl.pat == p && tk_pl.row == r;
    uint16_t bg, fg;
    char b[4];
    sig = ((0x811C9DC5u ^ (p << 8 | r)) * 16777619u ^ ((r < len ? 0u : 1u) | cur << 1 | play << 2 | (uint32_t)song.sel << 3 |
          (uint32_t)tku.field << 6)) * 16777619u;   /* (each its own bits: a cursor row never hashes as another pattern's) */
    if (r < len)
        for (k = 0; k < NTRK; k++)
            sig = (sig ^ tk_bits(tk_at(p, r, k)) ^ tk_chord(tk_at(p, r, k)) << 24) * 16777619u;
    if (!ui.force && sig == tku.sig[2u + i])
        return;
    tku.sig[2u + i] = sig;
    bg = play ? T_SEL : cur ? T_TINT : (r % 4u) ? T_SURF : T_RAISE;
    cv_begin(240, TK_ROW_H, T_BG);
    if (r >= len) {                                 /* (past the pattern's end) */
        cv_blit(0, (uint32_t)(TK_Y_ROWS + (int32_t)i * TK_ROW_H));
        return;
    }
    cv_rect(3, 0, 234, TK_ROW_H, bg);
    for (k = 0; k < VIEW_N; k++)                    /* (a 1 px line of BG left of each column) */
        cv_rect(TK_COL_X(k) - 1, 0, 1, TK_ROW_H, T_BG);
    fg = play ? T_INK : T_TEXT;
    tk_hex2(b, r);
    tkf_text(5, 4, b, play ? T_INK : (r % 4u) ? T_MID : T_THEME);
    for (k = trk_view(); k < trk_view() + VIEW_N; k++) {   /* (8 tracks: the view's four) */
        const tk_cell_t *c = tk_at(p, r, k);
        int32_t x = TK_COL_X(k - trk_view()) + 1;
        uint32_t n = tk_note(c), f;
        char s[3][4];
        int32_t fx[3] = {x, x + 21, x + 36}, fw[3] = {17, 11, 17};   /* (3 px between the fields) */
        tk_note_str(s[0], n);
        if (tk_inst(c))
            tk_hex2(s[1], tk_inst(c));
        else
            str_cpy(s[1], "..", 4);
        tk_fx_str(s[2], c);
        for (f = 0; f < 3u; f++) {
            uint16_t col = f == 0u ? (!n ? T_DIM : n == TK_OFF ? T_MID : fg)
                         : f == 1u ? (tk_inst(c) ? (play ? T_INK : T_THEME) : T_DIM)
                         : (tk_cmd(c) ? fg : T_DIM);
            if (cur && k == song.sel && f == tku.field) {   /* the cursor: the field edited last */
                cv_rect(fx[f] - 1, 2, fw[f] + 2, TK_ROW_H - 4, T_ACCENT);
                col = T_INK;
            } else if (play && col == T_DIM) {
                col = T_INK;
            }
            tkf_text(fx[f], 4, s[f], col);
        }
        if (tk_chord(c) && n && n != TK_OFF)        /* a chord: a mark between the note and the instrument */
            cv_rect(x + 18, 4, 2, 7, play ? T_INK : T_ACCENT);
    }
    cv_blit(0, (uint32_t)(TK_Y_ROWS + (int32_t)i * TK_ROW_H));
}

static void tk_draw_pattern(void)
{
    uint32_t i, p = tk_view_pat(), top = tku.row / TK_VIEW * TK_VIEW;   /* the page of 8 the cursor is on */
    tk_heads();
    for (i = 0; i < TK_VIEW; i++)
        tk_grid_row(i, p, top + i);
}

/* a list row of the S face on screen row i (SONG, SETTINGS, INSTR), selected: SEL; text drawn by the caller */
static void tk_list_begin(uint32_t sel, uint32_t mark)
{
    cv_begin(240, TK_ROW_H, T_BG);
    cv_rrect(3, 0, 234, TK_ROW_H, 3, sel ? T_SEL : mark ? T_RAISE : T_SURF, T_BG);
    cv_bg = sel ? T_SEL : mark ? T_RAISE : T_SURF;
}

static void tk_draw_song(void)
{
    uint32_t i, top = tku.pos / TK_VIEW * TK_VIEW;
    for (i = 0; i < TK_VIEW; i++) {
        uint32_t q = top + i, sel = q == tku.pos, play = song.playing && tk_pl.mode == TK_SONG && tk_pl.pos == q, sig;
        char b[24], h[3];
        sig = 0x9E37u ^ (q << 4) ^ sel * 0x100000u ^ play * 0x200000u ^ (q < tk.norder ? tk.order[q] * 0x1000u : 0x7F000u);
        if (q < tk.norder)
            sig ^= tk_len(TK_OPAT(tk.order[q])) * 0x1000000u;
        if (q == tk.norder)
            sig ^= 0xADD00000u;
        if (!ui.force && sig == tku.sig[2u + i])
            continue;
        tku.sig[2u + i] = sig;
        if (q > tk.norder || q >= TK_ORDER) {
            cv_begin(240, TK_ROW_H, T_BG);
            cv_blit(0, (uint32_t)(TK_Y_ROWS + (int32_t)i * TK_ROW_H));
            continue;
        }
        tk_list_begin(sel, play);
        if (q == tk.norder) {
            cv_text(10, 1, &AF_S, "+ ADD", sel ? T_INK : T_MID);
        } else {
            uint16_t fg = sel ? T_INK : T_TEXT;
            tk_hex2(h, q);
            cv_text(10, 1, &AF_S, h, sel ? T_INK : T_MID);
            str_cpy(b, "PATTERN ", sizeof b);
            tk_hex2(h, TK_OPAT(tk.order[q]));
            str_cpy(b + 8, h, sizeof b - 8);
            b[10] = 0;
            cv_text(40, 1, &AF_S, b, fg);
            str_cpy(b, "LEN ", sizeof b);
            tk_hex2(h, tk_len(TK_OPAT(tk.order[q])));
            str_cpy(b + 4, h, sizeof b - 4);
            cv_text(130, 1, &AF_S, b, sel ? T_INK : T_MID);
            if (play)
                cv_text_r(230, 1, &AF_S, "PLAY", sel ? T_INK : T_ACCENT, cv_bg);
            else if (tk_pat_empty(TK_OPAT(tk.order[q])))
                cv_text_r(230, 1, &AF_S, "EMPTY", sel ? T_INK : T_DIM, cv_bg);
        }
        cv_blit(0, (uint32_t)(TK_Y_ROWS + (int32_t)i * TK_ROW_H));
    }
}

/* SETTINGS' rows: a track parameter, or the track's CUT / RES over the instruments (TKS_CUT, TKS_RES) */
#define TKS_CUT 0xFEu
#define TKS_RES 0xFDu
#define TKS_WIDTH 0xFCu                  /* (Fel-Tracker) core.h trk_width, trk_lowcut: the track's place in the mix */
#define TKS_LOWCUT 0xFBu
#define TKS_DUCK 0xFAu                   /* (Fel-Tracker) core.h trk_duck: OFF, 1..15 ducked that deep, KEY the one that ducks */
#define TKS_PMODE 0xF9u                  /* (Fel-Tracker 2026-10) core.h trk_pmode: ALT, RND */
static const struct { const char *label; uint8_t id; } TK_SET[] = {
    {"ATTACK", P_ATK}, {"DECAY", P_DEC}, {"SUSTAIN", P_SUS}, {"RELEASE", P_REL},
    {"CUTOFF", TKS_CUT}, {"RESO", TKS_RES}, {"FLT ENV", P_ED_FLT},
    {"DIST", P_DIST}, {"CHORUS", P_CHOR}, {"DELAY", P_DLY}, {"REVERB", P_REV},
    {"LEVEL", P_LEVEL}, {"PAN", P_PAN}, {"WIDTH", TKS_WIDTH}, {"LOW CUT", TKS_LOWCUT},
    {"DUCK", TKS_DUCK}, {"PAN MODE", TKS_PMODE},
};
#define TK_NSET ((uint32_t)NELEM(TK_SET))
static void tk_set_value(uint32_t row, uint32_t k, char *val)     /* track k's value of setting row (val: 8) */
{
    uint32_t id = TK_SET[row % TK_NSET].id;
    const char *unit;
    if (id == TKS_WIDTH) {                          /* "12MS", OFF; "-": a track of the rhythm section (no WIDTH) */
        if (!tw_has(k % NTRK))
            str_cpy(val, "-", 8);
        else if (!trk_width[k % NTRK])
            str_cpy(val, "OFF", 8);
        else {
            fmt_int(val, trk_width[k % NTRK]);
            str_cpy(val + str_len(val), "MS", 8 - str_len(val));
        }
        return;
    }
    if (id == TKS_PMODE) {                          /* ALT, RND */
        str_cpy(val, trk_pmode[k % NTRK] == TP_RND ? "RND" : "ALT", 8);
        return;
    }
    if (id == TKS_DUCK) {                           /* OFF, "1" .. "15" (how deep), KEY */
        uint32_t d = trk_duck[k % NTRK];
        if (!d)
            str_cpy(val, "OFF", 8);
        else if (d == TD_KEY)
            str_cpy(val, "KEY", 8);
        else
            fmt_int(val, d);
        return;
    }
    if (id == TKS_LOWCUT) {                         /* OFF, "120" (Hz) */
        if (!trk_lowcut[k % NTRK])
            str_cpy(val, "OFF", 8);
        else
            fmt_int(val, TL_HZ[trk_lowcut[k % NTRK] % TL_N]);
        return;
    }
    if (id == TKS_CUT || id == TKS_RES) {
        uint8_t v = (id == TKS_CUT ? tk.cut : tk.res)[k % NTRK];
        if (v == TK_INST_VAL)
            str_cpy(val, "INST", 8);
        else
            fmt_int(val, v);
        if (tk_filter_id(&trk[k % NTRK], id == TKS_CUT ? TKP_CUT : TKP_RES) == 0xFFu)
            str_cpy(val, "-", 8);
        return;
    }
    param_format(track_desc(&trk[k % NTRK], id), trk[k % NTRK].p[id], val, &unit);
}
static void tk_draw_settings(void)
{
    uint32_t i, k, top = tku.srow / TK_VIEW * TK_VIEW;
    for (i = 0; i < TK_VIEW; i++) {
        uint32_t q = top + i, sel = q == tku.srow, sig = 0x5E7u ^ q * 0x100u ^ (uint32_t)sel << 16 ^ (uint32_t)song.sel << 17;
        char val[VIEW_N][8];
        if (q < TK_NSET)
            for (k = 0; k < VIEW_N; k++) {          /* (8 tracks: the view's four) */
                tk_set_value(q, trk_view() + k, val[k]);
                sig = str_hash(sig, val[k]);
            }
        sig ^= (uint32_t)(ui.hot_t != 0) * (ui.hot_col + 1u) * 0x1000000u;
        if (!ui.force && sig == tku.sig[2u + i])
            continue;
        tku.sig[2u + i] = sig;
        if (q >= TK_NSET) {
            cv_begin(240, TK_ROW_H, T_BG);
            cv_blit(0, (uint32_t)(TK_Y_ROWS + (int32_t)i * TK_ROW_H));
            continue;
        }
        tk_list_begin(sel, 0);
        cv_text_fit(10, 1, &AF_S, TK_SET[q].label, sel ? T_INK : T_MID, cv_bg, 60);
        for (k = 0; k < VIEW_N; k++) {
            uint16_t fg = sel ? T_INK : trk_view() + k == song.sel ? T_ACCENT : T_TEXT;
            cv_text_fit(72 + 41 * (int32_t)k, 1, &AF_S, val[k], fg, cv_bg, 39);
        }
        cv_blit(0, (uint32_t)(TK_Y_ROWS + (int32_t)i * TK_ROW_H));
    }
    {   /* the heads: the four tracks over their columns */
        uint32_t sig = 0x7E7u + song.sel;
        if (ui.force || sig != tku.sig[1]) {
            tku.sig[1] = sig;
            cv_begin(240, TK_H_HEAD, T_BG);
            for (k = 0; k < VIEW_N; k++) {
                char b[8] = "TRACK 1";
                b[6] = (char)('1' + trk_view() + k);
                cv_text_fit(72 + 41 * (int32_t)k, 1, &AF_S, b + 6, trk_view() + k == song.sel ? T_ACCENT : T_MID, T_BG, 39);
            }
            cv_blit(0, TK_Y_HEAD);
        }
    }
}

/* ------------------------------------------------------------ VARIATION --- */
/* the variation the page edits: the SONG page's position's (past the last: the one LOOP plays) */
static void tk_vtarget(uint32_t *p, uint32_t *v)
{
    if (tku.pos < tk.norder) {
        *p = TK_OPAT(tk.order[tku.pos]) % TK_PATS;
        *v = TK_OVAR(tk.order[tku.pos]);
    } else {
        *p = tk_pl.loop_pat % TK_PATS;
        *v = tk_pl.loop_var % TK_VARS;
    }
}
enum { TV_TEMPO, TV_MUTE, TV_CHANCE, TV_VO };   /* the rows: then one a VO_* (the sends, the SLICER) */
#define TK_NVROW (TV_VO + VO_N)
static const char *const TV_LABEL[TK_NVROW] = {"TEMPO", "MUTE", "CHANCE", "DIST", "CHORUS", "DELAY", "REVERB",
                                               "SLICER", "SL PAT", "SL RATE"};
static void tk_var_value(const tk_var_t *x, uint32_t row, uint32_t k, char *val)   /* (val: 8) */
{
    uint32_t v;
    const char *unit;
    if (row == TV_TEMPO) {
        if (x->bpm)
            fmt_int(val, x->bpm);
        else
            str_cpy(val, "SONG", 8);
    } else if (row == TV_MUTE) {
        str_cpy(val, (x->mute >> k) & 1u ? "MUTE" : "PLAY", 8);
    } else if (row == TV_CHANCE) {
        fmt_int(val, x->chance[k % NTRK]);
        str_cpy(val + str_len(val), "%", 8 - str_len(val));
    } else if (!(v = x->vo[k % NTRK][row - TV_VO])) {
        str_cpy(val, "KEEP", 8);
    } else {
        param_format(&TP[VO_PARAM[row - TV_VO]], (int32_t)v - 1, val, &unit);
    }
}
static void tk_draw_var(void)
{
    uint32_t i, k, p, v, top = tku.vrow / TK_VIEW * TK_VIEW;
    const tk_var_t *x;
    tk_vtarget(&p, &v);
    x = &tk_var[p][v];
    for (i = 0; i < TK_VIEW; i++) {
        uint32_t q = top + i, sel = q == tku.vrow, sig = 0xA7u ^ q * 0x100u ^ (uint32_t)sel << 16 ^ (uint32_t)song.sel << 17 ^ p << 20 ^ v << 26;
        char val[VIEW_N][8];
        if (q < TK_NVROW)
            for (k = 0; k < VIEW_N; k++) {          /* (8 tracks: the view's four; TEMPO: one) */
                if (q == TV_TEMPO && k)
                    val[k][0] = 0;
                else
                    tk_var_value(x, q, trk_view() + k, val[k]);
                sig = str_hash(sig, val[k]);
            }
        if (!ui.force && sig == tku.sig[2u + i])
            continue;
        tku.sig[2u + i] = sig;
        if (q >= TK_NVROW) {
            cv_begin(240, TK_ROW_H, T_BG);
            cv_blit(0, (uint32_t)(TK_Y_ROWS + (int32_t)i * TK_ROW_H));
            continue;
        }
        tk_list_begin(sel, 0);
        cv_text_fit(10, 1, &AF_S, TV_LABEL[q], sel ? T_INK : T_MID, cv_bg, 60);
        for (k = 0; k < VIEW_N; k++) {
            uint16_t fg = sel ? T_INK : q != TV_TEMPO && trk_view() + k == song.sel ? T_ACCENT : T_TEXT;
            cv_text_fit(72 + 41 * (int32_t)k, 1, &AF_S, val[k], fg, cv_bg, 39);
        }
        cv_blit(0, (uint32_t)(TK_Y_ROWS + (int32_t)i * TK_ROW_H));
    }
    {   /* the heads: the four tracks over their columns */
        uint32_t sig = 0xA7E7u + song.sel;
        if (ui.force || sig != tku.sig[1]) {
            tku.sig[1] = sig;
            cv_begin(240, TK_H_HEAD, T_BG);
            for (k = 0; k < VIEW_N; k++) {
                char b[2] = {(char)('1' + trk_view() + k), 0};
                cv_text_fit(72 + 41 * (int32_t)k, 1, &AF_S, b, trk_view() + k == song.sel ? T_ACCENT : T_MID, T_BG, 39);
            }
            cv_blit(0, TK_Y_HEAD);
        }
    }
}
/* a variation edited: if it is the one playing, its values now (the audio ISR's) */
static void tk_var_live(uint32_t p, uint32_t v)
{
    if (!song.playing || tk_pl.pat != p || tk_pl.var != v)
        return;
    fm1_irq_off();
    tk_var_enter(p, v);
    fm1_irq_on();
}
static void tk_knob_var(uint32_t k, int32_t s)
{
    uint32_t p, v, t = trk_view() + k, row = tku.vrow % TK_NVROW;
    tk_var_t *x;
    tk_vtarget(&p, &v);
    x = &tk_var[p][v];
    if (row == TV_TEMPO) {                          /* SONG, then 40 .. 240 (from the song's tempo) */
        int32_t b = x->bpm ? (int32_t)x->bpm + accel(EN_K1 + k, s, 200) : s > 0 ? song.g[G_BPM] : 0;
        if (k)
            return;
        x->bpm = (uint8_t)(b < 40 ? 0 : b > 240 ? 240 : b);
    } else if (t >= NTRK) {
        return;
    } else if (row == TV_MUTE) {
        if (s > 0)
            x->mute = (uint8_t)(x->mute | 1u << t);
        else
            x->mute = (uint8_t)(x->mute & ~(1u << t));
    } else if (row == TV_CHANCE) {
        x->chance[t] = (uint8_t)clamp((int32_t)x->chance[t] + accel(EN_K1 + k, s, 100), 0, 100);
    } else {                                        /* KEEP, then the parameter's range */
        const param_desc_t *d = &TP[VO_PARAM[row - TV_VO]];
        uint8_t *c = &x->vo[t][row - TV_VO];
        int32_t nv;
        if (!*c)
            nv = s > 0 ? trk[t].p[VO_PARAM[row - TV_VO]] : d->min - 1;   /* (from KEEP: the track's own value) */
        else
            nv = (int32_t)*c - 1 + accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min);
        *c = (uint8_t)(nv < d->min ? 0 : (nv > d->max ? d->max : nv) + 1);
    }
    tk_var_live(p, v);
}

/* an instrument's names: its source (an engine's, USER, "--") and its preset's (16 bytes each) */
static void tk_inst_names(uint32_t i, char *src, char *pre)
{
    const tk_inst_t *in = &tk.instr[i % TK_INSTS];
    src[0] = pre[0] = 0;
    if (in->src == TK_SRC_NONE) {
        str_cpy(src, "--", 16);
    } else if (in->src >= TK_SRC_USER) {
        uint32_t k = (uint32_t)(in->src - TK_SRC_USER) % UP_SLOTS;
        up_slot_label(src, k);
        if (up_used(k))
            up_name(k, pre);
        else
            str_cpy(pre, "EMPTY", 16);
    } else {                                        /* (the engine it plays here: an id the build lacks, its */
        uint32_t pi, k = tk_inst_engine(in, &pi);   /* substitute) */
        const engine_t *e = k == ENG_NO ? &ENG_NONE : ENGINES[k];
        str_cpy(src, e->name, 16);
        if (e->npresets)
            str_cpy(pre, e->presets[pi].name, 16);
    }
}
static void tk_draw_inst(void)
{
    uint32_t i, top = tku.slot / TK_VIEW * TK_VIEW;
    for (i = 0; i < TK_VIEW; i++) {
        uint32_t q = top + i, sel = q == tku.slot, sig;
        char src[16], pre[16], h[3];
        if (q < TK_INSTS) {
            tk_inst_names(q, src, pre);
            sig = str_hash(str_hash(0x1257u ^ q << 8 ^ (uint32_t)sel << 16, src), pre);
        } else {
            sig = 0xE0Fu ^ q;
        }
        if (!ui.force && sig == tku.sig[2u + i])
            continue;
        tku.sig[2u + i] = sig;
        if (q >= TK_INSTS) {
            cv_begin(240, TK_ROW_H, T_BG);
            cv_blit(0, (uint32_t)(TK_Y_ROWS + (int32_t)i * TK_ROW_H));
            continue;
        }
        tk_list_begin(sel, 0);
        tk_hex2(h, q + 1u);
        cv_text(10, 1, &AF_S, h, sel ? T_INK : T_MID);
        cv_text_fit(36, 1, &AF_S, src, sel ? T_INK : T_THEME, cv_bg, 70);
        cv_free_text(112, 1, &AF_S, pre, sel ? T_INK : T_TEXT, cv_bg, 118);
        cv_blit(0, (uint32_t)(TK_Y_ROWS + (int32_t)i * TK_ROW_H));
    }
    if (ui.force) {                                 /* the heads: what the columns are */
        cv_begin(240, TK_H_HEAD, T_BG);
        cv_text(36, 1, &AF_S, "SOURCE", T_MID);
        cv_text(112, 1, &AF_S, "PRESET", T_MID);
        cv_blit(0, TK_Y_HEAD);
        tku.sig[1] = 0;
    }
}

/* ui_draw.c ui_draw_page: the page between the header and the footer */
static void tk_draw(void)
{
    uint32_t g = tk_graph(), c;
    tk_frame();
    for (c = 0; c < 4u; c++)                         /* (no cards here: a card's rolling digits are over) */
        ui.roll[c].from[0] = 0;
    if (ui.force) {
        tk_gaps();
        if (g == GR_TKSONG) {                       /* (SONG has no heads: BG there) */
            lcd_fill(0, TK_Y_HEAD, 240, TK_H_HEAD, T_BG);
        }
    }
    tk_info();
    if (g == GR_TKPAT)
        tk_draw_pattern();
    else if (g == GR_TKSONG)
        tk_draw_song();
    else if (g == GR_TKVAR)
        tk_draw_var();
    else if (g == GR_TKSET)
        tk_draw_settings();
    else
        tk_draw_inst();
}

/* the footer's first row on these pages (ui_draw.c draw_foot): the cursor's cell spelled out, or what the knobs do */
/* chord shapes (the SEQ layer's KNOB 4): the two intervals above the note; a cell's chord is any two (the keys, the
 * web editor), shown by its name when it is one of these, else as its intervals */
static const struct { const char *name; uint8_t ch; } TK_SHAPES[] = {
    {"NONE", 0x00}, {"MAJ", 0x74}, {"MIN", 0x73}, {"SUS2", 0x72}, {"SUS4", 0x75}, {"DIM", 0x63}, {"AUG", 0x84},
    {"MAJ6", 0x94}, {"MAJ7", 0xB4}, {"DOM7", 0xA4}, {"MIN7", 0xA3}, {"5TH", 0x07}, {"5+OCT", 0xC7}, {"OCT", 0x0C},
};
#define TK_NSHAPE ((uint32_t)NELEM(TK_SHAPES))
static uint32_t tk_shape_of(uint32_t ch)        /* its shape, TK_NSHAPE: none of them */
{
    uint32_t i, sw = (ch >> 4 | ch << 4) & 0xFFu;
    for (i = 0; i < TK_NSHAPE; i++)
        if (TK_SHAPES[i].ch == ch || (sw && TK_SHAPES[i].ch == sw && (ch & 15u)))
            return i;
    return TK_NSHAPE;
}
static void tk_chord_str(char *b, uint32_t n, uint32_t ch)   /* "MAJ", or "+2+9" */
{
    uint32_t sh = tk_shape_of(ch), j;
    if (sh < TK_NSHAPE) {
        str_cpy(b, TK_SHAPES[sh].name, n);
        return;
    }
    b[0] = 0;
    for (j = 0; j < 2u; j++) {
        uint32_t i = ch >> (j ? 4 : 0) & 15u;
        if (!i)
            continue;
        str_cpy(b + str_len(b), "+", n - str_len(b));
        fmt_int(b + str_len(b), (int32_t)i);
    }
}

static void tk_foot(char *b, uint32_t n)
{
    uint32_t g = tk_graph();
    char h[4], s[16], p[16];
    b[0] = 0;
    if (g == GR_TKPAT) {
        const tk_cell_t *c = tk_at(tk_view_pat(), tku.row, song.sel);
        tk_note_str(s, tk_note(c));
        str_cpy(b, tk_note(c) ? s : "NO NOTE", n);
        if (tk_chord(c) && tk_note(c) && tk_note(c) != TK_OFF) {   /* "C-4 MAJ" */
            str_cpy(b + str_len(b), " ", n - str_len(b));
            tk_chord_str(b + str_len(b), n - str_len(b), tk_chord(c));
        }
        if (tk_inst(c)) {
            tk_inst_names(tk_inst(c) - 1u, s, p);
            str_cpy(b + str_len(b), "  ", n - str_len(b));
            str_cpy(b + str_len(b), p[0] ? p : s, n - str_len(b));
        }
        if (tk_cmd(c) == TKC_PLAY) {                /* (Fel-Tracker) "RETRIG 4", "NOTE DELAY 8/16", "CHANCE 53 %" */
            uint32_t sub = tk_psub(c) % TKP_N, a = tk_parg(c);
            str_cpy(b + str_len(b), "  ", n - str_len(b));
            str_cpy(b + str_len(b), TKP_NAME[sub], n - str_len(b));
            str_cpy(b + str_len(b), " ", n - str_len(b));
            if (sub == TKP_CHANCE)
                a = a * 100u / 15u;
            else if (sub == TKP_ARP && !a)
                a = 3;
            h[0] = a >= 100u ? '1' : (char)('0' + a / 10u % 10u);
            h[1] = (char)('0' + (a >= 100u ? 0u : a % 10u));
            h[2] = a >= 100u ? '0' : 0;
            h[3] = 0;
            str_cpy(b + str_len(b), h[0] == '0' ? h + 1 : h, n - str_len(b));
            str_cpy(b + str_len(b), sub == TKP_DELAY ? "/16" : sub == TKP_CHANCE ? " %" : sub == TKP_SLUP || sub == TKP_SLDN ? " ST" : sub == TKP_ARP ? " STEPS" : "", n - str_len(b));
        } else if (tk_cmd(c)) {
            str_cpy(b + str_len(b), "  ", n - str_len(b));
            str_cpy(b + str_len(b), TKC_NAME[tk_cmd(c) % TKC_N], n - str_len(b));
            str_cpy(b + str_len(b), " ", n - str_len(b));
            tk_hex2(h, tk_val(c));
            str_cpy(b + str_len(b), h, n - str_len(b));
        }
    } else if (g == GR_TKSONG) {
        str_cpy(b, "K2 PATTERN  K4 INS/DEL", n);
    } else if (g == GR_TKVAR) {
        str_cpy(b, tku.vrow == TV_TEMPO ? "K1 TEMPO  EDIT SONG" : "K1-K4 TRACKS  EDIT KEEP", n);
    } else if (g == GR_TKSET) {
        str_cpy(b, "K1-K4 TRACK 1-4  EDIT INST", n);
    } else {
        str_cpy(b, "K2 SOURCE  K3 PRESET  EDIT CLEAR", n);
    }
}

/* ----------------------------------------------------------- the knobs --- */
static void tk_cell_set(uint32_t tag, uint32_t note, uint32_t ins, uint32_t cmd, uint32_t val)   /* (the chord stays;
                                                                     * no note or OFF: it goes; tag: the undo's) */
{
    uint32_t ch = note && note != TK_OFF ? tk_chord(tk_at(tk_view_pat(), tku.row, song.sel)) : 0u;
    tk_edit(tag, tk_view_pat(), tku.row, song.sel, tk_pack_ch(note, ins, cmd, val, ch));
}
/* stopped: the cell under the cursor sounds once (its instrument, its note: tracker.c tk_audition) */
static void tk_preview(void)
{
    if (!song.playing)
        tk_aud_req = tk_view_pat() << 16 | (uint32_t)tku.row << 8 | (song.sel + 1u);
}

/* the cursor across the grid at half the knob's speed: a row every second detent of a turn, its first detent at once
 * (a single click still moves one row; a reversal or a pause of TK_MV_GAP ms starts a new turn) */
#define TK_MV_GAP 250u
static int32_t tk_slow(int32_t s)
{
    int32_t st = s > 0 ? 1 : -1, n = s > 0 ? s : -s, out = 0;
    if (fm1_ms - tku.mv_t >= TK_MV_GAP || tku.mv_dir != st) {
        tku.mv_dir = (int8_t)st;
        tku.mv_acc = 1;
    }
    tku.mv_t = fm1_ms;
    while (n--)
        if (++tku.mv_acc >= 2u) {
            tku.mv_acc = 0;
            out += st;
        }
    return out;
}

static void tk_knob_pattern(uint32_t k, int32_t s)
{
    uint32_t p = tk_view_pat(), len = tk_len(p);
    tk_cell_t c = *tk_at(p, tku.row, song.sel);
    uint32_t n = tk_note(&c), in = tk_inst(&c), cmd = tk_cmd(&c), v = tk_val(&c);
    if (k == 0u) {                                  /* the row (playing: the row playing, it moves on its own) */
        if (tk_following() || (s = tk_slow(s)) == 0)
            return;
        tku.row = (uint8_t)(((int32_t)tku.row + s % (int32_t)len + (int32_t)len) % (int32_t)len);
        if (ui_prefs2 & (PREF_PREVIEW >> 16))
            tk_preview();
        return;
    }
    tku.field = (uint8_t)(k - 1u);
    if (k == 1u) {                                  /* the note: none, OFF, 1..126 */
        if (!n || n == TK_OFF) {
            if (s > 0)
                n = tku.note;
            else
                n = n ? 0u : TK_OFF;
        } else {
            int32_t m = (int32_t)n + accel(EN_K2, s, 126);
            n = m < 1 ? TK_OFF : m > 126 ? 126u : (uint32_t)m;
        }
        if (n && n != TK_OFF) {
            tku.note = (uint8_t)n;
            if (!in && tk_cur_inst)                 /* (a new note takes the instrument used last) */
                in = tk_cur_inst;
        }
        tk_cell_set(TKU_NOTE, n, in, cmd, v);
        if (n && n != TK_OFF)
            tk_preview();
    } else if (k == 2u) {                           /* the instrument: none, 01..3F */
        in = (uint32_t)clamp((int32_t)in + accel(EN_K3, s, TK_INSTS), 0, TK_INSTS);
        if (in)
            tk_cur_inst = (uint8_t)in;
        tk_cell_set(TKU_INST, n, in, cmd, v);
    } else {                                        /* the FX value (a new FX: the command used last, from the value */
        if (!cmd && tku.cmd >= TKC_PLAY) {          /* the track has now; PLAY's: its amount by default) */
            cmd = TKC_PLAY;
            v = (tku.cmd - TKC_PLAY) << 4 | TKP_DEF[(tku.cmd - TKC_PLAY) % TKP_N];
        } else if (!cmd) {
            uint32_t id = tk_param_id(TSEL, tku.cmd);
            cmd = tku.cmd;
            v = id < P_COUNT ? tk_unscale(TSEL, id, TSEL->p[id]) : 64u;
        } else if (cmd == TKC_PLAY) {               /* (PLAY: its amount, 0..15; the command stays) */
            v = (v & 0x70u) | (uint32_t)clamp((int32_t)(v & 15u) + accel(EN_K4, s, 15), 0, 15);
        } else {
            v = (uint32_t)clamp((int32_t)v + accel(EN_K4, s, 127), 0, 127);
        }
        tk_cell_set(TKU_FX, n, in, cmd, v);
    }
}

static void tk_order_insert(uint32_t at, uint32_t ent)   /* a position before position at (ent: an order entry) */
{
    uint32_t i;
    if (tk.norder >= TK_ORDER)
        return;
    fm1_irq_off();
    for (i = tk.norder; i > at; i--)
        tk.order[i] = tk.order[i - 1u];
    tk.order[at] = TK_OENT(TK_OPAT(ent), TK_OVAR(ent));
    tk.norder++;
    if (tk_pl.pos >= at && song.playing && tk_pl.mode == TK_SONG && tk_pl.pos + 1u < tk.norder)
        tk_pl.pos++;                                /* (the position playing stays the one playing) */
    fm1_irq_on();
}
static void tk_order_delete(uint32_t at)
{
    uint32_t i;
    if (tk.norder <= 1u || at >= tk.norder)
        return;
    fm1_irq_off();
    for (i = at; i + 1u < tk.norder; i++)
        tk.order[i] = tk.order[i + 1u];
    tk.norder--;
    if (tk_pl.pos > at || tk_pl.pos >= tk.norder)
        tk_pl.pos = (uint8_t)(tk_pl.pos ? tk_pl.pos - 1u : 0u);
    fm1_irq_on();
    if (tku.pos >= tk.norder)                       /* (the last one went: the cursor on the one before it) */
        tku.pos = (uint8_t)(tk.norder - 1u);
}

static void tk_knob_song(uint32_t k, int32_t s)
{
    uint32_t q = tku.pos;
    if (k == 0u) {
        tku.pos = (uint8_t)clamp((int32_t)q + s, 0, tk.norder < TK_ORDER ? tk.norder : TK_ORDER - 1);
    } else if (k == 1u) {                           /* the pattern (+ ADD: a position, the one before's pattern) */
        if (q >= tk.norder) {
            if (q >= TK_ORDER)
                return;
            tk_order_insert(q, q ? tk.order[q - 1u] : 0u);
            return;
        }
        {
            uint32_t p = (uint32_t)clamp((int32_t)TK_OPAT(tk.order[q]) + s, 0, TK_PATS - 1), v = TK_OVAR(tk.order[q]);
            tk.order[q] = TK_OENT(p, v < tk_nvar[p] ? v : 0u);   /* (its variation if the pattern has it, else A) */
        }
    } else if (k == 3u && q < tk.norder) {          /* right: a copy after it; left: it goes */
        if (s > 0) {
            tk_order_insert(q + 1u, tk.order[q]);
            if (tku.pos + 1u < tk.norder)
                tku.pos++;
        } else {
            tk_order_delete(q);
        }
    }
}

static void tk_knob_set(uint32_t k, int32_t s)
{
    uint32_t id = TK_SET[tku.srow % TK_NSET].id;
    track_t *t = &trk[k % NTRK];
    if (id == TKS_PMODE) {                          /* (Fel-Tracker 2026-10) ALT, RND */
        trk_pmode[k % NTRK] = s > 0 ? TP_RND : TP_ALT;
        return;
    }
    if (id == TKS_DUCK) {                           /* (Fel-Tracker) OFF, 1..15, then KEY */
        trk_duck[k % NTRK] = (uint8_t)clamp((int32_t)trk_duck[k % NTRK] + (s > 0 ? 1 : -1), 0, TD_KEY);
        return;
    }
    if (id == TKS_WIDTH || id == TKS_LOWCUT) {      /* (Fel-Tracker) OFF, then 1..20 ms / the cutoffs */
        uint32_t tk_ = k % NTRK;
        if (id == TKS_WIDTH && tw_has(tk_))
            trk_width[tk_] = (uint8_t)clamp((int32_t)trk_width[tk_] + accel(EN_K1 + k, s, TW_MAX), 0, TW_MAX);
        else if (id == TKS_LOWCUT)
            trk_lowcut[tk_] = (uint8_t)clamp((int32_t)trk_lowcut[tk_] + (s > 0 ? 1 : -1), 0, TL_N - 1);
        return;
    }
    if (id == TKS_CUT || id == TKS_RES) {           /* INST, then 0..127 */
        uint8_t *v = &(id == TKS_CUT ? tk.cut : tk.res)[k % NTRK];
        uint32_t fid = tk_filter_id(t, id == TKS_CUT ? TKP_CUT : TKP_RES);
        int32_t x;
        if (fid == 0xFFu)
            return;
        x = *v == TK_INST_VAL ? (s > 0 ? (int32_t)tk_unscale(t, fid, t->p[fid]) : -1) : (int32_t)*v + accel(EN_K1 + k, s, 127);
        *v = (uint8_t)(x < 0 ? TK_INST_VAL : x > 127 ? 127 : x);
        fm1_irq_off();
        tk_filter_hold(t);
        fm1_irq_on();
        return;
    }
    {
        const param_desc_t *d = track_desc(t, id);
        t->p[id] = (int16_t)param_turn(d, t->p[id], accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min));
    }
}

/* instrument i's source as INSTR's KNOB 2 steps it: 0 none, 1..NENG_SHOWN the engines in display order, then USER */
static uint32_t tk_src_rank(const tk_inst_t *in)
{
    uint32_t pi, e;
    if (in->src == TK_SRC_NONE)
        return 0u;
    if (in->src >= TK_SRC_USER)
        return NENG_SHOWN + 1u;
    e = tk_inst_engine(in, &pi);
    return (e == ENG_NO ? 0u : eng_rank(e)) + 1u;
}
static void tk_inst_audition(void)              /* the instrument picked onto the selected track (the keys play it) */
{
    tk_cur_inst = (uint8_t)(tku.slot + 1u);
    if (tk.instr[tku.slot].src == TK_SRC_NONE)
        return;
    panic_req |= (uint8_t)(1u << song.sel);
    fm1_irq_off();
    tk_inst_apply(TSEL, tku.slot + 1u);
    fm1_irq_on();
    sync_reload = 1;
}
static void tk_knob_inst(uint32_t k, int32_t s)
{
    tk_inst_t *in = &tk.instr[tku.slot % TK_INSTS];
    if (k == 0u) {
        tku.slot = (uint8_t)clamp((int32_t)tku.slot + s, 0, TK_INSTS - 1);
        tk_cur_inst = (uint8_t)(tku.slot + 1u);
        return;
    }
    if (k == 1u) {
        uint32_t r = (uint32_t)clamp((int32_t)tk_src_rank(in) + s, 0, NENG_SHOWN + 1);
        in->src = (uint8_t)(!r ? TK_SRC_NONE : r > NENG_SHOWN ? TK_SRC_USER : eng_id(eng_vis(r - 1u)));   /* (by id) */
        in->preset = 0;
        if (in->src == TK_SRC_USER) {               /* (the first used user preset) */
            uint32_t u;
            for (u = 0; u < UP_SLOTS && !up_used(u); u++)
                ;
            in->src = (uint8_t)(TK_SRC_USER + (u < UP_SLOTS ? u : 0u));
        }
    } else if (k == 2u) {
        if (in->src == TK_SRC_NONE)
            return;
        if (in->src >= TK_SRC_USER) {               /* the used user presets */
            uint32_t u = (uint32_t)(in->src - TK_SRC_USER), n;
            for (n = 0; n < UP_SLOTS; n++) {
                u = (u + (s > 0 ? 1u : UP_SLOTS - 1u)) % UP_SLOTS;
                if (up_used(u))
                    break;
            }
            in->src = (uint8_t)(TK_SRC_USER + u);
        } else {                                    /* the engine's presets (of the one it plays here) */
            uint32_t pi, ei = tk_inst_engine(in, &pi);
            const engine_t *e = ei == ENG_NO ? &ENG_NONE : ENGINES[ei];
            int32_t p = (int32_t)pi, st = s > 0 ? 1 : -1, n = s > 0 ? s : -s;
            if (!e->npresets)
                return;
            in->src = (uint8_t)eng_id(ei);
            while (n-- && p + st >= 0 && p + st < (int32_t)e->npresets)
                p += st;
            in->preset = (uint8_t)p;
        }
    } else {
        return;
    }
    tk_inst_audition();
}

/* KNOB k turned s on a tracker page (ui_input.c) */
static void tk_knob(uint32_t k, int32_t s)
{
    uint32_t g = tk_graph();
    if (g == GR_TKPAT)
        tk_knob_pattern(k, s);
    else if (g == GR_TKSONG)
        tk_knob_song(k, s);
    else if (g == GR_TKVAR)
        tk_knob_var(k, s);
    else if (g == GR_TKSET)
        tk_knob_set(k, s);
    else if (g == GR_TKINST)
        tk_knob_inst(k, s);
}

/* PRESETS turned on a tracker page: PATTERN the FX command, SONG the position, SETTINGS the row, INSTR the slot. 1 =
 * taken (not a sound load) */
static int tk_presets(int32_t s)
{
    uint32_t g = tk_graph();
    if (!tk_page())
        return 0;
    if (g == GR_TKPAT) {
        tk_cell_t c = *tk_at(tk_view_pat(), tku.row, song.sel);
        int32_t from = tk_cmd(&c) ? (int32_t)tk_vcmd(&c) : (int32_t)tku.cmd - (s > 0 ? 1 : -1);   /* (none: the last */
        uint32_t vc = (uint32_t)clamp(from + s, 0, TKV_N - 1), cmd = vc < TKC_PLAY ? vc : TKC_PLAY, id, v = tk_val(&c);
        tku.field = 2;                                                                            /* used first) */
        if (vc >= TKC_PLAY) {                       /* (Fel-Tracker) PLAY's: its command, its amount by default */
            tku.cmd = (uint8_t)vc;
            v = (vc - TKC_PLAY) << 4 | (tk_cmd(&c) == TKC_PLAY ? tk_parg(&c) : TKP_DEF[vc - TKC_PLAY]);
        } else if (cmd) {
            tku.cmd = (uint8_t)cmd;
            if (!tk_cmd(&c) || tk_cmd(&c) == TKC_PLAY) {   /* (a new FX: from the value the track has now) */
                id = tk_param_id(TSEL, cmd);
                v = id < P_COUNT ? tk_unscale(TSEL, id, TSEL->p[id]) : 64u;
            }
        } else {
            v = 0;
        }
        tk_cell_set(TKU_CMD, tk_note(&c), tk_inst(&c), cmd, v);
    } else if (g == GR_TKSONG) {
        tk_knob_song(0, s);
    } else if (g == GR_TKVAR) {
        tku.vrow = (uint8_t)clamp((int32_t)tku.vrow + s, 0, TK_NVROW - 1);
    } else if (g == GR_TKSET) {
        tku.srow = (uint8_t)clamp((int32_t)tku.srow + s, 0, TK_NSET - 1);
    } else {
        tk_knob_inst(0, s);
    }
    return 1;
}

/* EDIT tapped on a tracker page: PATTERN the cell cleared (stopped: the cursor on by EDIT STEP), SONG the position
 * deleted, SETTINGS the selected track's value back (CUT / RES: INST), INSTR the slot emptied. 1 = taken */
static int tk_edit_tap(void)
{
    uint32_t g = tk_graph();
    if (!tk_page())
        return 0;
    if (g == GR_TKPAT) {
        tk_cell_set(TKU_KEY, 0, 0, 0, 0);
        if (!tk_following() && tku.step)
            tku.row = (uint8_t)((tku.row + tku.step) % tk_len(tk_view_pat()));
        ui_message("CELL CLEARED");
    } else if (g == GR_TKSONG) {
        if (tku.pos < tk.norder && tk.norder > 1u) {
            tk_order_delete(tku.pos);
            ui_message("POSITION DELETED");
        }
    } else if (g == GR_TKVAR) {
        uint32_t p, v, row = tku.vrow % TK_NVROW;
        tk_var_t *x;
        tk_vtarget(&p, &v);
        x = &tk_var[p][v];
        if (row == TV_TEMPO)
            x->bpm = 0;
        else if (row == TV_MUTE)
            x->mute = (uint8_t)(x->mute & ~(1u << song.sel));
        else if (row == TV_CHANCE)
            x->chance[song.sel] = 100;
        else
            x->vo[song.sel][row - TV_VO] = 0;
        tk_var_live(p, v);
    } else if (g == GR_TKSET) {
        uint32_t id = TK_SET[tku.srow % TK_NSET].id;
        if (id == TKS_WIDTH)                        /* (Fel-Tracker) back as at power-on */
            trk_width[song.sel] = (uint8_t)(tw_has(song.sel) ? TW_DEF : 0u);
        else if (id == TKS_LOWCUT)
            trk_lowcut[song.sel] = (uint8_t)(tw_has(song.sel) ? TL_DEF : 0u);
        else if (id == TKS_DUCK)
            trk_duck[song.sel] = 0;
        else if (id == TKS_PMODE)
            trk_pmode[song.sel] = TP_ALT;
        else if (id == TKS_CUT || id == TKS_RES)
            (id == TKS_CUT ? tk.cut : tk.res)[song.sel] = TK_INST_VAL;
        else
            TSEL->p[id] = track_desc(TSEL, id)->def;
    } else {
        tk.instr[tku.slot % TK_INSTS].src = TK_SRC_NONE;
        tk.instr[tku.slot % TK_INSTS].preset = 0;
        ui_message("INSTRUMENT CLEARED");
    }
    ui.force = 1;
    return 1;
}

/* the keys on PATTERN, the selected track armed and stopped: each note pressed into the cursor's cell (with the
 * instrument used last, no chord), the cursor on by EDIT STEP. A key pressed while the one that wrote a cell is still
 * held adds to that cell's chord instead (1..15 semitones above its note, two at most). 1 = the keys were PATTERN's
 * (ui_input.c) */
static struct { uint8_t row, pat, root, key; } tk_entry = {0xFF, 0, 0, 0};   /* the cell the held key wrote */
static int tk_page_keys(uint32_t pressed)
{
    uint32_t k;
    if (tk_graph() != GR_TKPAT || !((song.rec >> song.sel) & 1u) || song.playing || song.keys_mute)
        return 0;
    if (tk_entry.row != 0xFFu && !((fm1_in.notes >> tk_entry.key) & 1u))
        tk_entry.row = 0xFF;                        /* (its key let go: the next key writes a new cell) */
    for (k = 0; k < 27u; k++) {
        uint32_t note;
        tk_cell_t c;
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(TSEL, k);
        if (note == KB_SILENT || !note || note >= TK_OFF)
            continue;
        if (tk_entry.row != 0xFFu && tk_entry.pat == tk_view_pat()) {   /* a chord's note */
            tk_cell_t *e = tk_at(tk_entry.pat, tk_entry.row, song.sel);
            uint32_t i = note - tk_entry.root, ch = tk_chord(e);
            if (note > tk_entry.root && i <= 15u && tk_note(e) == tk_entry.root && (ch & 15u) != i && !(ch >> 4)) {
                tk_cell_t x = *e;
                x.b[3] = (uint8_t)((ch & 15u) ? ch | i << 4 : i);
                tk_edit(TKU_KEY, tk_entry.pat, tk_entry.row, song.sel, x);
            }
            last_note = (uint8_t)note;
            continue;
        }
        c = *tk_at(tk_view_pat(), tku.row, song.sel);
        tku.note = (uint8_t)note;
        tku.field = 0;
        tk_edit(TKU_KEY, tk_view_pat(), tku.row, song.sel,
                tk_pack(note, tk_cur_inst ? tk_cur_inst : tk_inst(&c), tk_cmd(&c), tk_val(&c)));
        tk_entry.row = tku.row;
        tk_entry.pat = (uint8_t)tk_view_pat();
        tk_entry.root = (uint8_t)note;
        tk_entry.key = (uint8_t)k;
        tku.row = (uint8_t)((tku.row + tku.step) % tk_len(tk_view_pat()));
        last_note = (uint8_t)note;
    }
    return 1;
}

/* ------------------------------------------------------- the SEQ layer --- */
/* SEQ held: KNOB 1 the pattern edited (LOOP plays it from the end of the one playing), 2 its length, 3 EDIT STEP,
 * 4 the chord of the cursor's cell (TK_SHAPES; a cell without a note takes none) */
static void tk_layer_knob(uint32_t k, int32_t s)
{
    uint32_t p = tk_pl.loop_pat;
    if (k == 0u) {
        tk_pl.loop_pat = (uint8_t)clamp((int32_t)p + s, 0, TK_PATS - 1);
        ui.force = 1;
    } else if (k == 1u) {
        tk_edit_len(p, (uint32_t)clamp((int32_t)tk.len[p] + accel(EN_K2, s, TK_ROWS), 1, TK_ROWS));
    } else if (k == 2u) {
        tku.step = (uint8_t)clamp((int32_t)tku.step + s, 0, 16);
    } else if (k == 3u) {
        tk_cell_t c = *tk_at(tk_view_pat(), tku.row, song.sel);
        uint32_t sh = tk_shape_of(tk_chord(&c));
        int32_t to;
        if (!tk_note(&c) || tk_note(&c) == TK_OFF) {
            ui_message("NO NOTE HERE");
            return;
        }
        to = sh < TK_NSHAPE ? (int32_t)sh + s : s > 0 ? 1 : 0;   /* (another chord: from the first shape) */
        c.b[3] = TK_SHAPES[clamp(to, 0, (int32_t)TK_NSHAPE - 1)].ch;
        tk_edit(TKU_CHORD, tk_view_pat(), tku.row, song.sel, c);
        if (ui_prefs2 & (PREF_PREVIEW >> 16))
            tk_preview();
    }
}
/* .. its cards (ui_draw.c draw_columns) */
static void tk_layer_columns(void)
{
    char v[8];
    tk_hex2(v, tk_pl.loop_pat);
    draw_column(0, "PATTERN", v, "", VAL(0u), (int32_t)tk_pl.loop_pat * 1000 / (TK_PATS - 1), ICON_X_PATTERN);
    tk_hex2(v, tk_len(tk_pl.loop_pat));
    draw_column(1, "LENGTH", v, "", VAL(1u), (int32_t)(tk_len(tk_pl.loop_pat) - 1u) * 1000 / (TK_ROWS - 1), ICON_AUTO);
    fmt_int(v, tku.step);
    draw_column(2, "STEP", v, "", VAL(2u), (int32_t)tku.step * 1000 / 16, ICON_AUTO);
    {
        const tk_cell_t *c = tk_at(tk_view_pat(), tku.row, song.sel);
        uint32_t sh = tk_shape_of(tk_chord(c));
        char ch[10];
        if (!tk_note(c) || tk_note(c) == TK_OFF)
            str_cpy(ch, "-", sizeof ch);
        else
            tk_chord_str(ch, sizeof ch, tk_chord(c));
        draw_column(3, "CHORD", ch, "", VAL(3u), sh < TK_NSHAPE ? (int32_t)sh * 1000 / (int32_t)(TK_NSHAPE - 1) : -1, ICON_AUTO);
    }
}
