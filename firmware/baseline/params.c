/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
/* LFO 2 (1.2, #154). SYNC: OFF (RATE), or one cycle per note value of the tempo, longest first (LSYNC_DIV: its N_DIV);
 * TRIG: NOTE restarts it at PHS on a fresh phrase (as before), FREE never; POL: BI -1..+1 (as before), UNI 0..+1 */
static const char *const N_LSYNC[] = {"OFF", "4BAR", "2BAR", "1/1", "1/2", "1/4", "1/8", "8T", "1/16", "16T", "1/32"};
static const uint8_t LSYNC_DIV[11] = {0, 9, 8, 7, 6, 0, 1, 4, 2, 5, 3};
static const char *const N_LTRIG[] = {"NOTE", "FREE"};
static const char *const N_LPOL[] = {"BI", "UNI"};
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
static const char *const N_SCALE[] = {"CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM",
                                    "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
/* (2026-10-18) the chorus's and the EQ's presets (normal mode's knobs; EXPERT's pages hold every value): five letters */
static const char *const N_CPRE[] = {"SUBTL", "WIDE", "DEEP", "FAST", "WOBBL"};
static const char *const N_EQPRE[] = {"FLAT", "PUNCH", "WARM", "BRITE", "SCOOP", "LOFI", "SMILE", "DARK"};

/* seq.c kb_map; 1 = SNAP (stored projects: the former ON). (3, SEQ, the sequencer's notes snapped too: gone with it) */
static const char *const N_QUANT[] = {"OFF", "SNAP", "WHITE"};
/* chord keys (chord.c): OFF, the diatonic triad / seventh of the track's ROOT and SCALE on the key, fixed shapes */
static const char *const N_CHRD[] = {"OFF", "DIA3", "DIA7", "MAJ", "MIN", "DOM7", "MAJ7", "MIN7", "SUS4", "POW"};
static const char *const N_VOIC[] = {"CLOSE", "OPEN", "INV1", "INV2", "+OCT"};   /* VC_CLOSE .. VC_BASS */
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "UNI"};   /* V_POLY .. V_UNISON */
static const char *const N_GLMODE[] = {"RATE", "TIME"};
static const char *const N_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const N_ALLOC[] = {"ROT", "REUSE"};
static const char *const N_CLOCK[] = {"INT", "USB", "TRS"};
/* MIDI IN (seq.c midi_base): 1..4 -> parts, 5..16 ignored / all -> selected / (1.2, append-only) 5..8, 9..12, 13..16 -> parts */
#if NTRK == 8                                   /* (Blue Hare, TRACKS 8) blocks of 8: tracks 1 .. 8 on channels 1 .. 8 or 9 .. 16 */
static const char *const N_ROUTE[] = {"CH1-8", "SEL", "CH9-16"};
#else
static const char *const N_ROUTE[] = {"CH1-4", "SEL", "CH5-8", "CH9-12", "CH13-16"};
#endif
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
/* G_RTYPE (2026-10-18): the global reverb, DRY or one of three (fx.c fx_buses: SPRING, GATED HALL's gated, PLATE amb-1's
 * Dattorro plate); a build without the PLATE component ends at GATED */
static const char *const N_RTYPE[] = {"DRY", "SPRING", "GATED",
#if BLUEHARE_FX_PLATE
                                      "PLATE"
#endif
};
static const char *const N_DTYPE[] = {"CLEAN", "TAPE", "WORN", "PING", "DUAL",
#if BLUEHARE_FX_AMB                                               /* (AMB a component, 2026-10-17) */
                                      "AMB"
#endif
};   /* G_DTYPE: the delay bus's (fx.c
                                                                   * dly_tape; PING, DUAL: Blue Hare's, stereo) */
static const char *const N_GO[] = {"--", "GO"};
/* (2026-10-18) TECHNO's pieces (techno.c TC_PIECES, in that order: the names the knob shows, five letters at most) */
static const char *const N_TC_PIECE[] = {"WARE", "ACID", "RAVE", "DUB", "INDUS", "MINML", "PEAK", "DEEP"};
/* modulation matrix (mod.c): sources, destinations (E1..E8 = P_E0..P_E7: shown with the engine's labels).
 * Append-only (stored values keep their meaning): S&H SLEW and DEPTH (#132) came after EXPR and E8 */
static const char *const N_MSRC[] = {"OFF", "LFO", "ENV", "VEL", "KEY", "RAND", "MODW", "AT", "EXPR", "S&H", "SLEW"};
static const char *const N_MDST[] = {"OFF", "PITCH", "CUT", "SHP", "AMP", "PAN", "DIST", "CHO", "DLY", "REV", "RATE",
                                     "VIB", "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "DEPTH"};
static const char *const N_ENGNAME[EID_COUNT] = {   /* by id (G_ENGSEL); "-": a retired id or an engine left out */
    [0] = "-", [1] = "-", [2] = "-", [3] = "-", [4] = "-", [5] = "-", [6] = "-", [7] = "-", [8] = "-", [9] = "-",
    [10] = "-", [11] = "-", [12] = "-", [13] = "-", [27] = "-",   /* (retired: core.h EID_RETIRED) */
#define ENG_NAME8(N) (sizeof #N > 9u ? (EID_##N == EID_DUBSIREN8 ? "SIREN8" : "SANDSTRM") : #N)   /* (2026-10-18: the device's
                                                     * names, 8 letters at most, as the engines' own .name) */
#define ENGINE(N, id, sub) [EID_##N] = BLUEHARE_ENG_##N ? ENG_NAME8(N) : "-",
#define ARCHIVED(N, id) [id] = "-",
#include "engine_ids.h"
#undef ARCHIVED
#undef ENGINE
#undef ENG_NAME8
};   /* (engines.c ENGINES[]; "-": not built, or archived) */

#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

static const param_desc_t TP[P_COUNT] = {
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_ATK] = PD("ATK", F_TIME, 0, 127, 10),
    [P_DEC] = PD("DEC", F_TIME, 0, 127, 70),
    [P_SUS] = PD("SUS", F_PCT, 0, 127, 90),
    [P_REL] = PD("REL", F_TIME, 0, 127, 60),
    [P_ED_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_ED_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_ED_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_OCT] = PD("OCT", F_INT, -4, 4, 0),     /* (Blue Hare: was ENV DEST FX, read by nothing) */
    [P_LRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHS", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_LD_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_LD_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_LD_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_LD_AMP] = PD("AMP", F_PCT, 0, 127, 0),
    [P_RSV17] = PE("TIME", N_DIV, 1),                 /* (2026-10-18) P_DTIME: the patch delay's time (17 .. 24: the */
    [P_RSV18] = PD("FEED", F_PCT, 0, 120, 45),        /* arpeggiator's until 2026-10) P_DFDBK: its feedback */
    [P_RSV19] = PD("-", F_INT, 0, 0, 0),
    [P_RSV20] = PD("-", F_INT, 0, 0, 0),
    [P_RSV21] = PD("-", F_INT, 0, 0, 0),
    [P_RSV22] = PD("-", F_INT, 0, 0, 0),
    [P_RSV23] = PD("-", F_INT, 0, 0, 0),
    [P_RSV24] = PD("-", F_INT, 0, 0, 0),
    [P_ROOT] = PD("ROOT", F_NOTE, 0, 11, 0),
    [P_SCALE] = PE("SCL", N_SCALE, 0),
    [P_QUANT] = PE("QNT", N_QUANT, 0),
    [P_TRANS] = PD("TRN", F_SEMI, -24, 24, 0),
    [P_SLEN] = PD("LEN", F_STEPS, 1, 64, 16),         /* (LEN DIV GATE QNTZ: the old step sequencer's, unused since 1.5; kept
                                                       * in the stored formats and the protocol's ids) */
    [P_SDIV] = PE("DIV", N_DIV, 2),
    [P_SSWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_SGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_DIST] = PD("DST", F_PCT, 0, 127, 0),
    [P_CHOR] = PD("CHO", F_PCT, 0, 127, 0),
    [P_DLY] = PD("MIX", F_PCT, 0, 127, 0),            /* (2026-10-18: the patch delay's MIX; 0 out, the default) */
    [P_REV] = PD("REV", F_PCT, 0, 127, 0),
    [P_VOICE] = PE("VCE", N_VOICE, 0),
    [P_GLIDE] = PD("GLD", F_TIME, 0, 127, 0),
    [P_GLMODE] = PE("GLMOD", N_GLMODE, 0),
    [P_PRIO] = PE("PRIO", N_PRIO, 0),
    [P_ALLOC] = PE("ALLOC", N_ALLOC, 0),
    [P_DETUNE] = PD("DTUNE", F_INT, 0, 127, 40),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_RSV33] = PD("-", F_INT, 0, 0, 0),              /* (33 .. 36: the SLICER's until 2026-10-16; inert) */
    [P_RSV34] = PD("-", F_INT, 0, 0, 0),
    [P_RSV35] = PD("-", F_INT, 0, 0, 0),
    [P_RSV36] = PD("-", F_INT, 0, 0, 0),
#define MSLOT(k) [P_M##k##SRC] = PE("SRC" #k, N_MSRC, 0), [P_M##k##DST] = PE("DST" #k, N_MDST, 0), \
                 [P_M##k##AMT] = PD("AMT" #k, F_BIPCT, -64, 63, 0)
    MSLOT(1), MSLOT(2), MSLOT(3), MSLOT(4),
#undef MSLOT
/* ids 61..80: the operator envelopes and levels of DIGITAL (removed in 1.5): inert, on no page, in no editor layout,
 * read by nothing; kept so the stored layouts (projects, user presets, the editor's) stay as they are. Their labels
 * stay: the editor's library files key parameters by label ("ATK#2" ..), so an old file's values still find them */
#define FMOP(k) [P_FM##k##_ATK] = PD("ATK", F_TIME, 0, 127, 0), \
                [P_FM##k##_DEC] = PD("DEC", F_TIME, 0, 127, 0), \
                [P_FM##k##_SUS] = PD("SUS", F_PCT, 0, 127, 127), \
                [P_FM##k##_REL] = PD("REL", F_TIME, 0, 127, 0), \
                [P_FM##k##_LEVEL] = PD("LVL", F_PCT, 0, 127, 127)
    FMOP(1), FMOP(2), FMOP(3), FMOP(4),
#undef FMOP
    [P_CHRD] = PE("CHRD", N_CHRD, 0),
    [P_VOIC] = PE("VOIC", N_VOIC, 0),
    [P_RSV25] = PD("-", F_INT, 0, 0, 0),              /* (25 .. 32: the kits' lane levels until 2026-10-16; inert) */
    [P_RSV26] = PD("-", F_INT, 0, 0, 0),
    [P_RSV27] = PD("-", F_INT, 0, 0, 0),
    [P_RSV28] = PD("-", F_INT, 0, 0, 0),
    [P_RSV29] = PD("-", F_INT, 0, 0, 0),
    [P_RSV30] = PD("-", F_INT, 0, 0, 0),
    [P_RSV31] = PD("-", F_INT, 0, 0, 0),
    [P_RSV32] = PD("-", F_INT, 0, 0, 0),
    [P_LSYNC] = PE("SYNC", N_LSYNC, 0),
    [P_LTRIG] = PE("TRIG", N_LTRIG, 0),
    [P_LPOL] = PE("POL", N_LPOL, 0),
    [P_SQNT] = PE("QNTZ", N_ONOFF, 1),            /* ON: every step on its start, as before 1.2 */
    [P_SPRD] = PD("SPRD", F_PCT, 0, 127, 0),      /* #148: 0 = every voice on PAN, as before (fx.c mix_spread) */
};

static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 120),
    [G_SWING] = PD("SWG", F_PCT, 0, 100, 0),
    [G_CLOCK] = PE("CLK", N_CLOCK, 0),
    [G_TUNE] = PD("TUNE", F_INT, -50, 50, 0),     /* cents; edited on MENU > AUDIO TUNE (1.2: menu_items.c MI_TUNE) */
    [G_DTIME] = PE("TIME", N_DIV, 1),
    [G_DFDBK] = PD("FDBK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLR", F_PCT, 0, 127, 70),
    [G_DMIX] = PD("MIX", F_PCT, 0, 127, 90),
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("CRT", F_LFOHZ, 0, 127, 40),
    [G_CDEPTH] = PD("CDP", F_PCT, 0, 127, 60),
    /* (Blue Hare, 2026-10) the GEN page's (gen.c): the preset, the scale, the root, MOTION; ids 12, 13, 15 and 26 were
     * the old SYSTEM page's MIDI, SYNC and CPU (on no page since 1.2) and the GM drum part's reverb send (inert since
     * 1.0). Their defaults: the first piece's (GARDEN: D dorian) and MOTION at its middle */
    [G_GPRE] = PE("PIECE", N_TC_PIECE, 0),         /* (2026-10-18: TECHNO's, in GEN's ids) */
    [G_GSCL] = PE("SCL", N_SCALE, 2),
    /* MIDI IN's routing, edited on MENU > MIDI > MIDI IN (1.2: menu_items.c MI_MIDIIN; up to 1.1.5 GLO > SYSTEM ROUT),
     * stored with the project as before. (was "--": stored 0 = CH1-4, as MIDI IN always was) */
    [G_ROUTE] = PE("ROUT", N_ROUTE, 0),
    [G_GROOT] = PD("ROOT", F_NOTE, 0, 11, 2),
    [G_SLOT] = PD("SLOT", F_INT, 1, 1, 1),   /* (1.5: one song; 1.4 had four projects) */
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_ENGSEL] = PE("ENG", N_ENGNAME, 0),
    [G_ENGGO] = PE("SET", N_GO, 0),
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),          /* (unused: the sequencer is a placeholder; the id stays, as G_ENGGO) */
    [G_INITSND] = PE("INIT", N_GO, 0),
    /* the reverb's model on the REVERB page: the id of the old GM drum channel (G_DRCH, inert since 1.0) */
    [G_RTYPE] = PE("TYPE", N_RTYPE, BLUEHARE_FX_PLATE ? 3 : 2),   /* (2026-10-18) PLATE (GATED without it) */
    /* the delay's type on DLY 2: the id of the old GM drum part's level (G_DRLVL, inert since 1.0: every song holds
     * 0, CLEAN) */
    [G_DTYPE] = PE("TYPE", N_DTYPE, 0),
    [G_GMOT] = PD("MOTN", F_PCT, 0, 127, 64),
    /* (2026-10-17) GEN's densities, a role each (the GEN MIX page, while it plays) */
    [G_GDPAD] = PD("DRUM", F_PCT, 0, 127, 64),   /* (2026-10-18) TECHNO's roles' DENSITY: DRUMS BASS STAB LEAD */
    [G_GDBASS] = PD("BASS", F_PCT, 0, 127, 64),
    [G_GDBELL] = PD("STAB", F_PCT, 0, 127, 64),
    [G_GDTEX] = PD("LEAD", F_PCT, 0, 127, 64),
    /* (2026-10-17) the patch's EQ (fx.c eq_block) */
    [G_EQLO] = {"LOW", F_INT, -12, 12, 0, 0, "dB"},
    [G_EQLM] = {"LMID", F_INT, -12, 12, 0, 0, "dB"},
    [G_EQHM] = {"HMID", F_INT, -12, 12, 0, 0, "dB"},
    [G_EQHI] = {"HIGH", F_INT, -12, 12, 0, 0, "dB"},
    /* (2026-10-18) the global reverb's MIX (REVERB's third knob) */
    [G_RMIX] = PD("MIX", F_PCT, 0, 127, 30),
    [G_CPRE] = PE("CHO", N_CPRE, 1),               /* WIDE: CRT 40 CDP 60, the power-on chorus */
    [G_EQPRE] = PE("EQ", N_EQPRE, 0),              /* FLAT: the power-on EQ */
};

static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (id >= P_E0 && id <= P_E7) {                   /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[eng_idx(t->eng_req)];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    return &TP[id];
}

/* the descriptor of parameter id for engine e without the engine's desc hook (the device display
 * only; range and default are the same): what the editor protocol, user presets and projects use */
static const param_desc_t *param_desc_of(uint32_t e, uint32_t id)
{
    return id >= P_E0 ? &ENGINES[e]->edit[id - P_E0] : &TP[id];
}

/* a stored value of track parameter id: OCT's old values (ENV DEST FX) outside -4 .. 4 are 0, not 4 octaves away */
static int32_t param_oct(uint32_t id, int32_t v) { return id == P_OCT && (v < -4 || v > 4) ? 0 : v; }
/* a stored value as the parameter takes it (projects, user presets, motion): inside d's range. (Felucca kept retired
 * values as aliases here, DRUM's kits and FM6's bank slots; their engines are gone) */
static int32_t param_fit(const param_desc_t *d, int32_t v) { return clamp(v, d->min, d->max); }

/* #48: the note divisions in the order of their length, longest first (triplets between their neighbours), on the
 * knobs and the gauges; the stored values (N_DIV indices: projects, presets, the editor protocol) stay.
 * -> the shown order of d's values (index: position, entry: value), 0 = the values' own order */
static const uint8_t DIV_ORDER[10] = {9, 8, 7, 6, 0, 1, 4, 2, 5, 3};   /* 4BAR 2BAR 1/1 1/2 1/4 1/8 8T 1/16 16T 1/32 */
static const uint8_t *enum_order(const param_desc_t *d)
{
    return d->names == N_DIV ? DIV_ORDER : 0;
}
static int32_t enum_rank(const param_desc_t *d, int32_t v)   /* v's place in the shown order (+ min): the gauges */
{
    const uint8_t *o = enum_order(d);
    int32_t r;
    for (r = 0; o && r < d->max - d->min; r++)
        if (o[r] == v - d->min)
            break;
    return o ? r + d->min : v;
}
/* a knob turned `steps` (signed) from v: clamped, over the shown order */
static int32_t param_turn(const param_desc_t *d, int32_t v, int32_t steps)
{
    const uint8_t *o = enum_order(d);
    if (o)
        return o[clamp(enum_rank(d, v) - d->min + steps, 0, d->max - d->min)] + d->min;
    return clamp(v + steps, d->min, d->max);
}

static uint8_t fmt_named;        /* the last param_format was a name (F_ENUM, even "4"): ui_draw.c's digits do not roll it */

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    fmt_named = d->fmt == F_ENUM;
    switch (d->fmt) {
    case F_PCT:                                     /* a 0..100 range (SWG) is its value, others a share of 127 */
        fmt_int(val, d->max == 100 ? v : (v * 100 + 63) / 127);
        *unit = "%";
        break;
    case F_BIPCT:
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 64);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME: {
        uint32_t ms10 = TIME_MS_X10[v & 127];
        if (ms10 < 100u) {
            fmt_fix(val, (int32_t)ms10, 1);
            *unit = "ms";
        } else if (ms10 < 10000u) {
            fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
            *unit = "ms";
        } else {
            fmt_fix(val, (int32_t)(ms10 / 100u), 2);
            if (ms10 >= 100000u)
                fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
            *unit = "s";
        }
        break;
    }
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        str_cpy(val, d->names[v < d->min ? d->min : v > d->max ? d->max : v], 6);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* ------------------------------------------------------------ pages --- */
enum { FAM_HOME, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_EDIT, FAM_GLO, FAM_SAVE, FAM_ARP, FAM_SEQ, FAM_TRK,
       FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_ENGINE, SC_TRK };   /* SC_TRK: the TRACKS page (ui_input.c tracks_edit) */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_SCALE, GR_FX, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_MOD, GR_TOOLS, GR_CHORD,
       GR_TODO,                    /* (GR_TODO: SEQ's placeholder, ui_draw.c draw_todo) */
       GR_GEN };                   /* (Blue Hare, 2026-10) the GEN page: playing or not, the chord, the bar */

#define PX_PLAIN 0xF0u           /* (a page's column: the engine's plain[] column k is PX_PLAIN + k) */
#define PX_GLOBAL 0x80u          /* (2026-10-18: a global parameter on a track page: PX_GLOBAL + its id) */


typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;

static const page_t PAGES[] = {
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_ED_FLT, P_ED_PIT, P_ED_SHP, 0xFF}},
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO 2", FAM_LFO, SC_TRACK, GR_LFO, {P_LSYNC, P_LTRIG, P_LPOL, 0xFF}},   /* 1.2: SYNC TRIG POL */
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP}},
    {"MOD", FAM_LFO, SC_TRACK, GR_MOD, {0xFF, P_M1SRC, P_M1DST, P_M1AMT}},   /* KNOB 1: the slot (mod_ui_slot) */
    /* (2026-10-18) FX: the global reverb and the patch's delay (EXPERT adds the sends, the chorus, the EQ) */
    {"REVERB", FAM_FX, SC_GLOBAL, GR_NONE, {G_RTYPE, G_RSIZE, G_RMIX, 0xFF}},   /* TYPE: DRY / SPRING / GATED / PLATE */
    {"DELAY", FAM_FX, SC_TRACK, GR_NONE, {P_DLY, P_DFDBK, P_DTIME, 0xFF}},       /* the patch's own: MIX FEED TIME */
    {"CHORUS", FAM_FX, SC_TRACK, GR_NONE, {P_CHOR, PX_GLOBAL + G_CPRE, 0xFF, 0xFF}},   /* the patch's amount, the preset */
#if BLUEHARE_FX_EQ
    {"EQ", FAM_FX, SC_GLOBAL, GR_NONE, {G_EQPRE, 0xFF, 0xFF, 0xFF}},                   /* the preset */
#endif
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, 0xFF, 0xFF}},
    {"CHORUS EDIT", FAM_FX, SC_GLOBAL, GR_NONE, {G_CRATE, G_CDEPTH, 0xFF, 0xFF}},   /* (EXPERT) */
#if BLUEHARE_FX_EQ
    {"EQ BANDS", FAM_FX, SC_GLOBAL, GR_NONE, {G_EQLO, G_EQLM, G_EQHM, G_EQHI}},   /* (EXPERT) the four bands */
#endif
    {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_QUANT, P_TRANS}},
    {"CHORD", FAM_SCL, SC_TRACK, GR_CHORD, {P_CHRD, P_VOIC, 0xFF, 0xFF}},   /* SCL again: the chord keys (chord.c) */
    /* (2026-10-17) the plain page (MENU > EXPERT OFF; EDIT 1 and 2 with it ON): the engine's four (engine_t plain) */
    {"SOUND", FAM_EDIT, SC_ENGINE, GR_NONE, {PX_PLAIN, PX_PLAIN + 1, PX_PLAIN + 2, PX_PLAIN + 3}},
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E4, P_E5, P_E6, P_E7}},
    {"VOICE", FAM_EDIT, SC_TRACK, GR_NONE, {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO}},
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_PAN, P_MUTE}},
    {"VOICE 3", FAM_EDIT, SC_TRACK, GR_NONE, {P_SPRD, P_OCT, 0xFF, 0xFF}},   /* #148: SPREAD; OCT (Blue Hare) */
    /* 1.2 (Discussion #153): a family's pages go round in this order, a tap of its button each (ui.c open_family) */
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* browser: PRESETS knob / KNOB 1 */
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, {0xFF, 0xFF, 0xFF, 0xFF}},       /* user presets: SLOT LOAD ERASE SAVE */
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {0xFF, 0xFF, G_LOAD, G_SAVE}},   /* the song store (1.5: one song) */
    {"TOOLS", FAM_SAVE, SC_GLOBAL, GR_TOOLS, {G_INITSND, 0xFF, 0xFF, 0xFF}},
    /* (Blue Hare, 2026-10) ARP: the generative engine (gen.c); ARP starts and stops it (ui_input.c page_tap) */
#if BLUEHARE_PLUGIN_TECHNO
    {"TECHNO", FAM_ARP, SC_GLOBAL, GR_GEN, {G_GPRE, G_GSCL, G_GROOT, G_GMOT}},
    /* (2026-10-17) while GEN plays: how busy each role is (ARP starting it opens this page, stopping it GEN's) */
    {"TECHNO MIX", FAM_ARP, SC_GLOBAL, GR_GEN, {G_GDPAD, G_GDBASS, G_GDBELL, G_GDTEX}},
#endif
    /* (Blue Hare) the sequencer's placeholder: SEQ (and GLO, which opened the song) shows TODO */
    {"SEQ", FAM_SEQ, SC_GLOBAL, GR_TODO, {0xFF, 0xFF, 0xFF, 0xFF}},
    /* HOME's pages (1.2): HOME tapped on HOME the MIXER (up to 1.1.5 GLO's; LEVEL PAN REV MUTE), again CLOCK (up to 1.1.5
     * GLO > GLOBAL; its TUNE is MENU > AUDIO's now), again HOME (ui.c home_tap) */
    {"MIXER", FAM_TRK, SC_TRK, GR_TRK, {0, 1, 2, 3}},
    {"CLOCK", FAM_TRK, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, 0xFF}},
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))
static uint8_t mod_ui_slot;      /* the MOD page: the matrix slot (0..3) KNOB 2..4 edit */

/* (2026-10-18) the chorus's presets {CRT, CDP} and the EQ's {LOW, LMID, HMID, HIGH} (dB), in N_CPRE's and N_EQPRE's order;
 * picked (the knob, the editor, the studio: page_turned), they write those values */
static const uint8_t CHO_PRE[][2] = {{20, 30}, {40, 60}, {25, 110}, {90, 45}, {8, 127}};
static const int8_t EQ_PRE[][4] = {{0, 0, 0, 0}, {4, -2, 1, 2}, {3, 1, -2, -4}, {-1, 0, 3, 5}, {4, -4, -1, 3},
                                   {-6, 3, -2, -9}, {3, -2, 0, 3}, {2, 0, -4, -8}};
typedef char pre_named[NELEM(CHO_PRE) == NELEM(N_CPRE) && NELEM(EQ_PRE) == NELEM(N_EQPRE) ? 1 : -1];
static void color_preset(uint32_t id)
{
    uint32_t i, k;
    if (id == G_CPRE) {
        k = (uint32_t)song.g[G_CPRE] % NELEM(CHO_PRE);
        song.g[G_CRATE] = (int16_t)param_fit(&GP[G_CRATE], CHO_PRE[k][0]);
        song.g[G_CDEPTH] = (int16_t)param_fit(&GP[G_CDEPTH], CHO_PRE[k][1]);
    } else if (id == G_EQPRE) {
        k = (uint32_t)song.g[G_EQPRE] % NELEM(EQ_PRE);
        for (i = 0; i < 4u; i++)
            song.g[G_EQLO + i] = (int16_t)param_fit(&GP[G_EQLO + i], EQ_PRE[k][i]);
    }
}

/* (2026-10-17) the plain page: what column k is for the selected track's engine (a parameter id, PX_MACRO + k, 0xFF) */
static uint32_t plain_id(const track_t *t, uint32_t k)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    return (e->plain[0] | e->plain[1] | e->plain[2] | e->plain[3]) ? e->plain[k & 3u] : e->knob[k & 3u];
}
static int16_t plain_mval;       /* a macro's value as page_desc read it (the knob turns this, then page_turned) */
static uint8_t plain_mk;
/* after a knob changed *vp: a macro's value goes to its parameters */
static void page_turned(const int16_t *vp)
{
    if (vp == &song.g[G_CPRE] || vp == &song.g[G_EQPRE])   /* (a chorus or EQ preset picked: its values) */
        color_preset((uint32_t)(vp - song.g));
    if (vp == &plain_mval) {
        const engine_t *e = ENGINES[eng_idx(TSEL->eng_req)];
        if (e->macro_set)
            e->macro_set(TSEL, plain_mk, plain_mval);
    }
}

static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot];
    if (id >= PX_PLAIN && id != 0xFFu) {              /* the plain page: the engine's column */
        const engine_t *e = ENGINES[eng_idx(TSEL->eng_req)];
        id = plain_id(TSEL, id - PX_PLAIN);
        if (id >= PX_MACRO && id != 0xFFu) {
            if (!e->macro || !e->macro_get) {
                *valp = 0;
                return 0;
            }
            plain_mk = (uint8_t)(id - PX_MACRO);
            plain_mval = (int16_t)e->macro_get(TSEL, plain_mk);
            *valp = &plain_mval;
            return &e->macro[plain_mk];
        }
    }
    if (id == 0xFFu) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_TRK) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_GLOBAL || (pg->scope == SC_TRACK && id >= PX_GLOBAL && id < PX_MACRO)) {
        id = pg->scope == SC_GLOBAL ? id : id - PX_GLOBAL;   /* (2026-10-18: a global on a track page: PX_GLOBAL + id) */
        *valp = &song.g[id];
        return &GP[id];
    }
    if (pg->graph == GR_MOD)                          /* SRC DST AMT of the slot shown */
        id += 3u * mod_ui_slot;
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
