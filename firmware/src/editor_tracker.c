/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* Editor protocol 1.5: the tracker's song (tracker.c) for the web editor (web/EDITOR_PROTOCOL.md "1.5: the tracker").
 * INFO advertises 58 02 pats rows insts ncmd order-lo order-hi (02: 6-byte cells with the chord; 01, before the chords,
 * was never released). One command, TRACKER (78): op, args -> op, rc, ...
 *   0 STATUS                                   -> playing, mode (0 LOOP, 1 SONG), pattern, position, row, the pattern
 *                                                 LOOP plays (the device's PATTERN page), positions
 *   1 PATTERN  pat, row, count (1..16)         -> pat, len, row, count, count x NTRK cells
 *   2 CELLS    pat, row, col, count (1..64), count cells   (row-major from row / col) -> pat, row, col, count
 *   3 LENGTH   pat [, len 1..TK_ROWS]          -> pat, len
 *   4 ORDER    [count lo, count hi (1..TK_ORDER), count x pat]  -> count lo, count hi, count x pat
 *   5 INSTR    first, count [, count x (kind, a, b)]  -> first, count, count x (kind, a, b): kind 0 none, 1 engine a's
 *                                                 factory preset b, 2 user preset slot a (b 0)
 *   6 FILTER   [track, cut v14, res v14]       -> NTRK x (cut v14, res v14): 0..127, -1 the instrument's own (INST)
 *   7 PLAY     action [, arg]: 0 stop; 1 LOOP pattern arg (playing: cued, after the one playing); 2 SONG from position
 *              arg (stopped only: rc 2 while playing)  -> as STATUS
 *   8 CLEAR    pat                             -> pat
 * A cell is 6 bytes: note (0 none, 1..126 a MIDI note, 127 OFF), instrument (0 none, 1..TK_INSTS), FX command
 * (0..TKC_N-1), FX value (0..127), the chord's two intervals above the note (0 none, 1..15 semitones each). rc: 0 ok, 1 arguments (nothing written), 2 busy. A write is the editor's own: no
 * TRACKER_CHANGED for it (the shadow takes it). TRACKER_CHANGED (79, pushed with WATCH bit 2): kind (0 a pattern, 1
 * the song: order, instruments, CUT / RES), index. */
enum { EDT_STATUS, EDT_PATTERN, EDT_CELLS, EDT_LENGTH, EDT_ORDER, EDT_INSTR, EDT_FILTER, EDT_PLAY, EDT_CLEAR };
#define EDT_CELL 6u                              /* bytes a cell on the wire */
#define EDT_ROWS_MAX 16u                         /* PATTERN: 16 rows x 4 cells x 6 bytes = 384 reply bytes */
#define EDT_CELLS_MAX 64u                        /* CELLS: 384 request bytes */

/* a cell written from the main loop: whole for the audio ISR (as ui_tracker.c tk_put; the editor builds without the UI
 * on the host) */
static void edt_put(uint32_t p, uint32_t r, uint32_t k, tk_cell_t c)
{
    fm1_irq_off();
    *tk_at(p, r, k) = c;
    fm1_irq_on();
    tk_ext_gen++;                                /* (not an edit on the device: no undo of it) */
}
static void edt_known(uint32_t i) { ed_w.ts[i] = ed_tk_sig(i); }   /* the editor's own write: no push */

static void edt_status(void)
{
    ed_b(song.playing != 0);
    ed_b(tk_pl.mode);
    ed_b(tk_pl.pat);
    ed_b(tk_pl.pos);
    ed_b(tk_pl.row);
    ed_b(tk_pl.loop_pat);
    ed_b(tk.norder);
    ed_b((uint32_t)tk.norder >> 7);
}
static int edt_cell_ok(const uint8_t *c) { return c[1] <= TK_INSTS && c[2] < TKC_N && c[4] <= 15u && c[5] <= 15u; }
static void edt_inst(const tk_inst_t *in)
{
    if (in->src == TK_SRC_NONE) {
        ed_b(0); ed_b(0); ed_b(0);
    } else if (in->src >= TK_SRC_USER) {
        ed_b(2); ed_b(in->src - TK_SRC_USER); ed_b(0);
    } else {
        ed_b(1); ed_b(in->src); ed_b(in->preset);
    }
}
static int edt_inst_put(tk_inst_t *in, const uint8_t *e)
{
    if (e[0] == 0u) {
        in->src = TK_SRC_NONE;
        in->preset = 0;
    } else if (e[0] == 1u && e[1] < EID_COUNT && eng_load_id(e[1], 0) != ENG_NO) {   /* an engine id (one the build lacks:
                                                           * kept, it plays its substitute: tracker.c tk_inst_engine) */
        const engine_t *en = eng_by_id(e[1]);
        in->src = e[1];
        in->preset = (uint8_t)(en->npresets ? e[2] % en->npresets : e[2]);
    } else if (e[0] == 2u && e[1] < UP_SLOTS) {
        in->src = (uint8_t)(TK_SRC_USER + e[1]);
        in->preset = 0;
    } else {
        return 1;
    }
    return 0;
}

static int ed_tracker_handle(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op, rc = 0, i;
    if (cmd != ED_TRACKER)
        return 0;
    if (!na)
        return 0;
    op = a[0];
    a++;
    na--;
    switch (op) {
    case EDT_STATUS:
        if (na)
            return 0;
        ed_b(op); ed_b(0);
        edt_status();
        return 1;
    case EDT_PATTERN: {
        uint32_t p, r0, n, r, k;
        if (na != 3u || a[0] >= TK_PATS || a[1] >= TK_ROWS || !a[2] || a[2] > EDT_ROWS_MAX)
            return 0;
        p = a[0]; r0 = a[1]; n = a[2];
        if (n > EDT_ROWS_MAX * 4u / NTRK)
            n = EDT_ROWS_MAX * 4u / NTRK;           /* (8 tracks: 8 rows a reply, the reply's count says so) */
        if (r0 + n > TK_ROWS)
            n = TK_ROWS - r0;
        ed_b(op); ed_b(0); ed_b(p); ed_b(tk_len(p)); ed_b(r0); ed_b(n);
        for (r = r0; r < r0 + n; r++)
            for (k = 0; k < NTRK; k++) {
                const tk_cell_t *c = tk_at(p, r, k);
                ed_b(tk_note(c)); ed_b(tk_inst(c)); ed_b(tk_cmd(c)); ed_b(tk_val(c)); ed_b(tk_iv(c, 0)); ed_b(tk_iv(c, 1));
            }
        return 1;
    }
    case EDT_CELLS: {
        uint32_t p, r0, c0, n;
        if (na < 4u || a[0] >= TK_PATS || a[1] >= TK_ROWS || a[2] >= NTRK || !a[3] || a[3] > EDT_CELLS_MAX ||
            na != 4u + EDT_CELL * a[3])
            return 0;
        p = a[0]; r0 = a[1]; c0 = a[2]; n = a[3];
        if (r0 * NTRK + c0 + n > TK_ROWS * NTRK)
            rc = 1;
        for (i = 0; !rc && i < n; i++)
            if (!edt_cell_ok(a + 4 + EDT_CELL * i))
                rc = 1;
        for (i = 0; !rc && i < n; i++) {
            const uint8_t *c = a + 4 + EDT_CELL * i;
            uint32_t at = r0 * NTRK + c0 + i;
            edt_put(p, at / NTRK, at % NTRK, tk_pack_ch(c[0], c[1], c[2], c[3], c[4] | c[5] << 4));
        }
        if (!rc) {
            edt_known(p);
            ui.force = 1;
        }
        ed_b(op); ed_b(rc); ed_b(p); ed_b(r0); ed_b(c0); ed_b(n);
        return 1;
    }
    case EDT_LENGTH:
        if ((na != 1u && na != 2u) || a[0] >= TK_PATS)
            return 0;
        if (na == 2u) {
            if (a[1] < 1u || a[1] > TK_ROWS) {
                rc = 1;
            } else {
                tk.len[a[0]] = a[1];
                tk_ext_gen++;
                edt_known(a[0]);
                ui.force = 1;
            }
        }
        ed_b(op); ed_b(rc); ed_b(a[0]); ed_b(tk_len(a[0]));
        return 1;
    case EDT_ORDER:
        if (na) {
            uint32_t n = na >= 2u ? (uint32_t)a[0] | (uint32_t)a[1] << 7 : 0u;
            if (!n || n > TK_ORDER || na != 2u + n)
                return 0;
            for (i = 0; i < n; i++)
                if (TK_OPAT(a[2 + i]) >= TK_PATS)       /* (an entry: the pattern, its variation in bits 5..6) */
                    rc = 1;
            if (!rc) {
                fm1_irq_off();
                memcpy(tk.order, a + 2, n);
                tk.norder = (uint8_t)n;
                for (i = 0; i < n; i++)                 /* (a variation the pattern lacks: it has it now) */
                    if (TK_OVAR(a[2 + i]) >= tk_nvar[TK_OPAT(a[2 + i])])
                        tk_nvar[TK_OPAT(a[2 + i])] = (uint8_t)(TK_OVAR(a[2 + i]) + 1u);
                if (tk_pl.pos >= n)
                    tk_pl.pos = 0;
                fm1_irq_on();
                edt_known(TK_PATS);
                ui.force = 1;
            }
        }
        ed_b(op); ed_b(rc); ed_b(tk.norder); ed_b((uint32_t)tk.norder >> 7);
        for (i = 0; i < tk.norder; i++)
            ed_b(tk.order[i]);
        return 1;
    case EDT_INSTR: {
        uint32_t f, n;
        if (na < 2u || a[0] >= TK_INSTS || !a[1] || a[0] + a[1] > TK_INSTS || (na != 2u && na != 2u + 3u * a[1]))
            return 0;
        f = a[0]; n = a[1];
        if (na > 2u) {
            tk_inst_t in[TK_INSTS];
            for (i = 0; i < n && !rc; i++)
                rc = (uint32_t)edt_inst_put(&in[i], a + 2 + 3u * i);
            if (!rc) {
                fm1_irq_off();
                memcpy(&tk.instr[f], in, n * sizeof in[0]);
                fm1_irq_on();
                edt_known(TK_PATS);
                ui.force = 1;
            }
        }
        ed_b(op); ed_b(rc); ed_b(f); ed_b(n);
        for (i = f; i < f + n; i++)
            edt_inst(&tk.instr[i]);
        return 1;
    }
    case EDT_FILTER:
        if (na && (na != 5u || a[0] >= NTRK))
            return 0;
        if (na) {
            int32_t c = ed_rv(a + 1), q = ed_rv(a + 3);
            if (c < -1 || c > 127 || q < -1 || q > 127) {
                rc = 1;
            } else {
                tk.cut[a[0]] = (uint8_t)(c < 0 ? TK_INST_VAL : c);
                tk.res[a[0]] = (uint8_t)(q < 0 ? TK_INST_VAL : q);
                fm1_irq_off();
                tk_filter_hold(&trk[a[0]]);
                fm1_irq_on();
                edt_known(TK_PATS);
                ui.force = 1;
            }
        }
        ed_b(op); ed_b(rc);
        for (i = 0; i < NTRK; i++) {
            ed_v(tk.cut[i] == TK_INST_VAL ? -1 : tk.cut[i]);
            ed_v(tk.res[i] == TK_INST_VAL ? -1 : tk.res[i]);
        }
        return 1;
    case EDT_PLAY:
        if (!na || na > 2u || a[0] > 2u || (a[0] && na != 2u))
            return 0;
        if (a[0] == 0u) {
            if (song.playing)
                transport_req = 2;
        } else if (a[0] == 1u) {                   /* LOOP: the pattern (playing: next, after the one playing) */
            if (a[1] >= TK_PATS) {
                rc = 1;
            } else {
                tk_pl.loop_pat = a[1];
                if (!song.playing) {
                    tk_pl.mode = TK_LOOP;
                    transport_req = 1;
                }
            }
        } else if (song.playing) {
            rc = 2;                                /* SONG: from a stop (STOP first) */
        } else if (a[1] >= tk.norder) {
            rc = 1;
        } else {
            tk_pl.mode = TK_SONG;
            tk_pl.start_pos = a[1];
            transport_req = 1;
        }
        ui.force = 1;
        ed_b(op); ed_b(rc);
        edt_status();
        return 1;
    case EDT_CLEAR:
        if (na != 1u || a[0] >= TK_PATS)
            return 0;
        for (i = 0; i < TK_ROWS * NTRK; i++)
            edt_put(a[0], i / NTRK, i % NTRK, tk_pack(0, 0, 0, 0));
        edt_known(a[0]);
        ui.force = 1;
        ed_b(op); ed_b(0); ed_b(a[0]);
        return 1;
    default:
        return 0;
    }
}
