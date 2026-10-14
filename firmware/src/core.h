/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md */
/* FELUCCA core types: tracks, voices, engines, parameters.
 * Four tracks, each a synth part: its own engine, preset, parameters, voices and 64-step
 * pattern. The parts share one budget of NVOICE sounding voices (voice.c). Drums are the DRUM
 * engine (General MIDI map) on any part.
 * Sections: sizes, parameters, voices and engines, tracks and the song, system. */
#include <stdint.h>

#ifndef FELUCCA_DIST             /* (not given: a host test's build, the primary distribution with 4 tracks, the shape
                                  * the shared tests were written for: their 8-track builds set FELUCCA_NTRK; every
                                  * image and the emulator name their distribution) */
#define FELUCCA_DIST idm
#ifndef FELUCCA_NTRK
#define FELUCCA_NTRK 4
#endif
#endif
#define DIST_H2(f) #f
#define DIST_H1(n) DIST_H2(dist_##n.h)
#define DIST_H(n) DIST_H1(n)
#include DIST_H(FELUCCA_DIST)    /* build/gen/dist_<name>.h (tools/gen_dist.py): FELUCCA_ENG_<NAME>, FELUCCA_DIST_ENGINES,
                                  * FELUCCA_TRACKS */

/* ------------------------------------------------------------ sizes --- */
#define NVOICE 12                /* voices per part, and the most of the budget shared by all parts (Fel-Tracker: 12) */
#define VOICE_BASE 8             /* (Fel-Tracker) the budget always there; NVOICE while the audio's load allows
                                  * (voice.c voice_limit) */
#define VOICE_LOAD_Q8 192        /* (Fel-Tracker) the load under which voices 9 .. 12 may start: 75 % (song.cpu_q8) */
#define UNISON_MAX 8             /* UNISON's voices (its level and spread made for 8) */
#ifndef FELUCCA_NTRK             /* (a test build may set it: tests/run_tests.sh's 8-track one) */
#define FELUCCA_NTRK FELUCCA_TRACKS
#endif
#define NPART FELUCCA_NTRK       /* synth parts: tracks 1..4, or 1..8 (Fel-Tracker: a distribution's TRACKS) */
#define NTRK NPART               /* tracks (the formats and the protocol count these): every track is a part */
typedef char ntrk_ok[NTRK == 4 || NTRK == 8 ? 1 : -1];
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz (a key waits 0..1 half, then plays 1 half later) */
/* ---- the engines. The tree holds every engine (engine_ids.h); a distribution (distributions/<name>.txt, at most 16) is
 * the set one build holds, FELUCCA_DIST=<name> (build.py; default: "default"). Two numbers name an engine:
 * its id (EID_*): fixed for good, what everything stored and the editor protocol use (projects, user presets, the
 *   song's instruments, favourites; the protocol's engine byte, G_ENGSEL). Append-only (engine_ids.h).
 * its index (ENGI_*): its place in this build's engines.c ENGINES[] (0 .. NENGINES - 1), the distribution's order,
 *   what a track holds (eng_req) and the code compares.
 * The engines the distribution leaves out are not linked (nothing refers to them but the table) and their ENGI_ is
 * ENG_ABSENT, which no track's engine equals (a compare with it folds to false). A stored sound of an engine the build
 * lacks loads as its substitute (engines.c eng_load_id) */
enum {                           /* the ids */
#define ENGINE(N, id, sub) EID_##N = id,
#include "engine_ids.h"
#undef ENGINE
};
#define EID_COUNT 57u            /* ids 0 .. EID_COUNT - 1 (the protocol's NENGINES; EID_COUNT: a user preset) */
/* retired: 1 DIGITAL, 4 SAMPLE, 8 GRAIN, 13 SLICE (Felucca 1.5), 27 ACID; 0 ANALOG, 2 PHASE, 3 LOFI, 5 VOICE, 6 TRIO,
 * 7 WHEEL, 9 PHYS, 10 DRUM, 11 NOISE, 12 FM6 (Felucca's own engines: Fel-Tracker removed them): ids 0 .. 13 but 14,
 * and 27 */
#define EID_RETIRED(e) ((e) <= 13u || (e) == 27u)
#define ENG_ABSENT 0x100u        /* the ENGI_ of an engine the build leaves out: no uint8_t equals it */
#define FELUCCA_VERSION_DIST FELUCCA_VERSION FELUCCA_DIST_TAG   /* "v1.5", "v1.5 IDM": the version as shown */
enum {                           /* the indices: the distribution's engines in its order (engines.c ENGINES[]) */
#define X(N) ENGI_##N,
    FELUCCA_DIST_ENGINES(X)
#undef X
    NENGINES                     /* engines in this build (1 .. 16) */
};                               /* (the ones left out: ENGI_<NAME> ENG_ABSENT, in the generated header) */
typedef char eng_count_ok[NENGINES > 0 && NENGINES <= 16 ? 1 : -1];
#define NENG_SHOWN NENGINES      /* (the engines one can pick: all of the build's, in their order) */
/* engine index e is engine X (a uint8_t compared without the warning that an engine left out would raise: still false,
 * folded away) */
#define ENG_IS(e, X) ((uint32_t)(e) == (uint32_t)ENGI_##X)
#define UP_SLOTS 32u             /* user presets (upreset.c) */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_OCT,       /* P_OCT (Fel-Tracker): the sound's octave, -4 .. 4 (VOICE 3; a preset sets it,
                                                * voice.c adds it to the pitch). Was P_ED_FX, never read: a stored value
                                                * outside -4 .. 4 (ENV DEST FX in the web editor) loads as 0 (param_oct) */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER insert (slicer.c) */
    P_M1SRC, P_M1DST, P_M1AMT,                 /* modulation matrix (mod.c): 4 slots of SRC, DST, AMT; */
    P_M2SRC, P_M2DST, P_M2AMT,                 /* new common parameters go just before P_E0 (user presets */
    P_M3SRC, P_M3DST, P_M3AMT,                 /* and projects map by count) */
    P_M4SRC, P_M4DST, P_M4AMT,
    P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL, P_FM1_LEVEL,
    P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL, P_FM2_LEVEL,
    P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL, P_FM3_LEVEL,
    P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL, P_FM4_LEVEL,
    P_CHRD, P_VOIC,                            /* chord keys (chord.c): one key plays a chord; its voicing */
    P_LN0, P_LN1, P_LN2, P_LN3, P_LN4, P_LN5, P_LN6, P_LN7,   /* DRUM lane levels (KIT; once DRUM, #97): KICK .. BELL */
    P_LSYNC, P_LTRIG, P_LPOL,                  /* 1.2 (#154): LFO SYNC (OFF, a note value), TRIG (NOTE, FREE), POL (BI, UNI) */
    P_SQNT,                                    /* 1.2 (#162): QUANTIZE, the steps' recorded / nudged timing (ON: none) */
    P_SPRD,                                    /* (#148): SPREAD, the voices of a POLY / UNISON track around PAN (fx.c) */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,   /* G_ROUTE: MIDI IN, 0 CH1-4 (channels 1..4 -> parts 1..4, 5..16 ignored), 1 SEL;
                                        * 1.2: 2..4 CH5-8 CH9-12 CH13-16 (seq.c midi_base) */
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,          /* no page: the editor switches the engine with a SET of G_ENGSEL; G_ENGGO is
                                 * unused (ids are fixed by the formats and the protocol) */
    G_CLRSEQ, G_INITSND,
    G_RTYPE,                    /* REVERB TYPE: 0 ROOM, 1 SPRING, 2 HALL (1.2, the power-on default) (fx.c). Was
                                 * G_DRCH, the GM drum part's MIDI channel (inert since 1.0, never read); projects
                                 * of formats before FUN7 load it as ROOM (project.c proj_rtype_room) */
    G_DTYPE,                    /* DELAY TYPE (Fel-Tracker 1.5): 0 CLEAN, 1 TAPE, 2 WORN (fx.c). Was G_DRLVL, the GM
                                 * drum part's level (inert since 1.0: range 0..0, so every song stored 0, CLEAN) */
    G_DRREV,                    /* inert (label "-", on no page): the GM drum part it set is gone; kept because the
                                 * ids and G_COUNT are fixed by the formats and the protocol */
    G_COUNT
};

/* stored parameters of an older layout -> today's P_* order. A store keeps np = the P_COUNT it was
 * written with; common parameters are only ever added just before P_E0, so the first np - 8 are
 * P_LEVEL.. in order and the last 8 are P_E0..P_E7; the parameters a store does not have take def[]
 * (user presets, projects of formats 1 and 2) */
static void params_by_count(int16_t *out, const int16_t *in, uint32_t np, const int16_t *def)
{
    uint32_t i, nc = np - 8u;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? in[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = in[nc + i];
}

/* ------------------------------------------------- voices, engines --- */
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release */
    uint8_t side;                /* SPREAD (fx.c mix_spread): 0 left of PAN, 1 right; set as the voice starts */
    int32_t env;                 /* Q24 */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide TIME mode: 1/16 st per control tick, 0 = RATE mode */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
    uint8_t mvel;                /* mod.c: the note-on velocity (before UNISON scaling) and RAND of the note */
    int16_t mrnd;
} voice_t;

typedef struct {                 /* per-voice control-rate modulation, computed in voice.c */
    uint32_t inc;                /* phase increment of the base pitch */
    int32_t pitch16;
    int32_t amp0, amp1;          /* Q15 ramp over the block */
    int32_t cutoff;              /* 0..127 << 8 */
    int32_t shape;               /* 0..127 << 8 */
    int32_t envq15;              /* env value (for engines that use it as a mod source) */
    int32_t fine;                /* the residual below 1/16 semitone in inc: unison detune, TUNE, bend (1/4096) */
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[8];                 /* P_E0..P_E7 (signed: an interval below the note; every value fits) */
    uint8_t env[4];              /* ATK DEC SUS REL */
    int8_t fenv;                 /* ENV -> FILTER amount (-64..63) */
    uint8_t mono;                /* 1 = MONO (bass / lead), 0 = POLY */
    /* the rest of the patch; each value is stored + 1, 0 = the default */
    uint8_t fx[4];               /* DIST, CHORUS, DELAY, REVERB sends (the arp is the track's: a preset does not set it) */
    int8_t oct;                  /* (Fel-Tracker) OCT, the sound's octave (P_OCT; 0, the default, for most) */
    int8_t eshp, lshp;           /* (Fel-Tracker) ENV -> SHAPE and LFO -> SHAPE (P_ED_SHP, P_LD_SHP; -64 .. 63, 0 none):
                                  * a preset that moves its shape (WAVE: a scan through the table) */
    uint8_t lrate, lwave;        /* (Fel-Tracker) the LFO's RATE and WAVE, + 1 (0: the default) */
} preset_t;
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}

struct track;
typedef struct {                 /* an engine (engines.c ENGINES[]; the eng_*.c files) */
    const char *name;            /* "ANALOG" (PRESETS, the editor) */
    uint8_t id;                  /* its id (EID_*: what the stores and the protocol name it by) */
    uint8_t cc;                  /* the standard CC map (midi_control.c): CC74's E index + 1 in the high nibble, CC71's
                                  * in the low one, 0 = none */
    const char *cats;            /* its factory presets' categories, a letter each in preset order (category.c) */
    const char *page_title[2];   /* EDIT 1 and EDIT 2 */
    param_desc_t edit[8];        /* P_E0..P_E7 */
    const preset_t *presets;
    uint8_t npresets;
    uint8_t knob[4];             /* HOME: the four parameters on KNOB 1..4 */
    uint8_t poly;                /* voice cap for POLY and UNISON, 0 = NVOICE */
    uint8_t sampled;             /* 1 = plays recorded material (a position, not a phase): voice.c keeps
                                  * no phases over a retrigger, spreads none for UNISON, renders at SUS 0 */
    uint8_t keep;                /* bit k: s[k] is kept when a sounding voice is retriggered (filters) */
    uint8_t oneshot;             /* 1 = hits (KIT, WAR): always POLY, no glide; amp ends its voices (KIT, WAR) */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    /* optional (0 = none): the voice amplitude instead of the ADSR curve, once per control tick;
     * gets the ADSR value (Q15, env_tick already ran: it still gates the voice), returns Q15 */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* optional: once per block and part, before its voices (also with no voice sounding) */
    void (*block)(struct track *t);
    /* (Fel-Tracker) optional: once per block and part, after its voices, on their sum (STRINGS: the ensemble); not
     * while SPREAD splits the voices (voice.c track_render) */
    void (*post)(struct track *t, int32_t *out, uint32_t n);
    /* optional: the note of key k (0 = the lowest) when the engine maps the keys itself, else -1
     * (then the scale keyboard of seq.c kb_map) */
    int32_t (*keys)(const struct track *t, uint32_t k);
    /* 1 = the engine's own envelopes are the voice's amplitude (FM6): voice.c renders it at 1.0 (no ADSR, no
     * velocity; LFO -> AMP, the matrix's AMP and the fades still apply; ENV is 0) and the voice ends when
     * done() says so (once per control tick, before the render), not at the end of the ADSR's release */
    uint8_t ownenv;
    int (*done)(struct track *t, voice_t *v);
    /* (Fel-Tracker) 1 = on the last track alone (DUBSIREN: its echo is that track's): a sound of it loaded on another
     * track becomes its substitute (engines.c eng_for_track) */
    uint8_t last_track;
} engine_t;

/* ------------------------------------------------- tracks, the song --- */
#define NLANE 8                  /* the DRUM engine's lanes (shared/drum_map.c) */

/* The tracker (tracker.c): the song is TK_PATS patterns of up to TK_ROWS rows, a cell per track and row, and an order
 * list of TK_ORDER positions naming the pattern each plays. A cell is 4 bytes: 24 bits of the note (bits 0..6: 0 none,
 * 1..126 a MIDI note, TK_OFF its release), the instrument (bits 7..12: 0 none, 1..TK_INSTS a slot of the instrument
 * table), the FX command (bits 13..16: 0 none, TKC_*) and its value (bits 17..23, 0..127); then the chord: two
 * intervals above the note (low nibble, high nibble: 0 none, 1..15 semitones), so a row of a track plays up to three
 * notes. A row's FX sets one parameter of the track for that row only: the track's own value is back on the next row.
 * 24 patterns of 4-byte cells take the RAM 32 of 3 bytes took before the chords */
#define TK_PATS 24u
#define TK_ROWS 64u
#define TK_ORDER 128u
#define TK_INSTS 63u
#define TK_OFF 127u
#define TK_CHORD 3u              /* notes a cell plays at most: the note and its two intervals */
typedef struct { uint8_t b[4]; } tk_cell_t;
static inline uint32_t tk_bits(const tk_cell_t *c) { return (uint32_t)c->b[0] | (uint32_t)c->b[1] << 8 | (uint32_t)c->b[2] << 16; }
static inline uint32_t tk_note(const tk_cell_t *c) { return tk_bits(c) & 127u; }
static inline uint32_t tk_inst(const tk_cell_t *c) { return tk_bits(c) >> 7 & 63u; }
static inline uint32_t tk_cmd(const tk_cell_t *c) { return tk_bits(c) >> 13 & 15u; }
static inline uint32_t tk_val(const tk_cell_t *c) { return tk_bits(c) >> 17 & 127u; }
static inline uint32_t tk_chord(const tk_cell_t *c) { return c->b[3]; }   /* i1 | i2 << 4 */
static inline uint32_t tk_iv(const tk_cell_t *c, uint32_t j) { return (uint32_t)c->b[3] >> (j ? 4 : 0) & 15u; }
static inline tk_cell_t tk_pack_ch(uint32_t note, uint32_t inst, uint32_t cmd, uint32_t val, uint32_t chord)
{
    uint32_t v = (note & 127u) | (inst & 63u) << 7 | (cmd & 15u) << 13 | (val & 127u) << 17;
    tk_cell_t c = {{(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)chord}};
    return c;
}
static inline tk_cell_t tk_pack(uint32_t note, uint32_t inst, uint32_t cmd, uint32_t val)
{
    return tk_pack_ch(note, inst, cmd, val, 0);
}
static inline int tk_empty(const tk_cell_t *c) { return !(c->b[0] | c->b[1] | c->b[2] | c->b[3]); }
/* the notes cell c plays (out: TK_CHORD): its note, then each interval above it that stays a note (1..126); 0: none
 * (no note, or OFF) */
static inline uint32_t tk_chord_notes(const tk_cell_t *c, uint8_t *out)
{
    uint32_t n = tk_note(c), k = 0, j;
    if (!n || n == 127u)
        return 0;
    out[k++] = (uint8_t)n;
    for (j = 0; j < 2u; j++) {
        uint32_t i = tk_iv(c, j);
        if (i && n + i <= 126u)
            out[k++] = (uint8_t)(n + i);
    }
    return k;
}
/* an instrument: a factory preset of an engine (src: its id, < EID_COUNT), a user preset (TK_SRC_USER + its slot), or
 * none */
#define TK_SRC_NONE 0xFFu
#define TK_SRC_USER 0x80u
typedef struct { uint8_t src, preset; } tk_inst_t;
#define TK_INST_VAL 0xFFu        /* a track's CUT / RES setting: the instrument's own value */
typedef struct {
    uint8_t order[TK_ORDER];     /* the pattern of each song position */
    uint8_t norder;              /* positions in use, 1..TK_ORDER */
    uint8_t len[TK_PATS];        /* rows of each pattern, 1..TK_ROWS */
    tk_inst_t instr[TK_INSTS];    /* instruments 01..3F */
    uint8_t cut[NTRK], res[NTRK];   /* each track's filter over the instruments (0..127, TK_INST_VAL: theirs) */
} tk_song_t;

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    int16_t lfo_prev;            /* the S&H value of the cycle before (mod.c SLEW glides from it to lfo_rnd's) */
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime */
    uint32_t arp_pos;            /* q8 samples into the current arp step */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint8_t arp_walk;            /* WALK: the place in the note list it stands on */
    uint8_t arp_ch[3];           /* CHORD: the other notes sounding with arp_note, 0 = none */
    uint32_t arp_off;            /* q8 sample time of its note-off */
    /* mono */
    uint8_t mono_stack[8];
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state (fx.c) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST tail) */
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
    /* modulation matrix (mod.c): MIDI performance controllers of the track's channel, 0..127 */
    uint8_t mw, at;              /* CC1 mod wheel, channel aftertouch */
    uint8_t ex_off;              /* 127 - CC11 expression (0: full, the MIDI default) */
    uint8_t m_vel, m_key, m_vi;  /* the latest note-on: velocity, note, voice index (per-block destinations) */
    uint8_t sp_alt;              /* SPREAD: the side the next voice starts on (alternating, voice.c voice_start) */
    int16_t m_rnd;               /* .. its RAND */
    int32_t m_env;               /* the amp envelope of voice m_vi, last block (Q15) */
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t keys_mute;           /* 1: NAME (ui_name.c): the keys type, none plays (seq.c keyboard_block) */
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];        /* the instrument: four parts */
static song_t song;
#define TSEL (&trk[song.sel])    /* the selected track */

/* (Fel-Tracker) a variation's layer over the tracks (tracker.c: set as an order entry with its variation starts, cleared
 * at STOP): what it gives a track's sends and SLICER while it plays, read by the audio path through tp_vo (the track's
 * own values, what the pages and the stores hold, never written), and the tempo it plays at (song_bpm) */
enum { VO_DIST, VO_CHOR, VO_DLY, VO_REV, VO_SLCR, VO_SLPAT, VO_SLRATE, VO_N };
static uint8_t trk_vo[NTRK][VO_N];      /* 0: the track's own; else the value + 1 */
static uint8_t tempo_ovr;               /* 0: the song's BPM (G_BPM); else the BPM the variation playing has */
static const uint8_t VO_PARAM[VO_N] = {P_DIST, P_CHOR, P_DLY, P_REV, P_SLCR, P_SLPAT, P_SLRATE};
/* track t's parameter id (one of VO_PARAM: vo) as it plays now */
static inline int32_t tp_vo(const track_t *t, uint32_t vo)
{
    uint32_t k = (uint32_t)(t - trk);
    return k < NTRK && trk_vo[k][vo] ? (int32_t)trk_vo[k][vo] - 1 : t->p[VO_PARAM[vo]];
}
static inline uint32_t song_bpm(void) { return tempo_ovr ? tempo_ovr : (uint32_t)song.g[G_BPM]; }

/* (Fel-Tracker) each track's place in the mix, the song's (SETTINGS' WIDTH and LOW CUT; project.c, version 5):
 *   trk_width   0 off, 1..TW_MAX ms: the Haas effect, on the last TW_N tracks only (5..8, or 3..4 on 4 tracks: the
 *               rhythm section stays in the middle): the track's highs (over ~250 Hz) late on one side, the side
 *               changing track by track (the first late on the right, so it sits left: as its PAN), its lows as they
 *               are on both (fx.c mix_haas)
 *   trk_lowcut  0 off, 1..TL_N - 1: a 12 dB/oct high-pass at TL_HZ[] on the track, after DIST and the SLICER (fx.c
 *               track_lowcut) */
#define TW_N (NTRK / 2u)
#define TW_FIRST (NTRK - TW_N)
#define TW_MAX 20u
#define TW_DEF 12u
#define TL_N 8u
#define TL_DEF 4u                         /* (120 Hz: the wide tracks' default; the rhythm section's OFF) */
static const uint16_t TL_HZ[TL_N] = {0, 40, 60, 80, 120, 160, 240, 320};
static uint8_t trk_width[NTRK], trk_lowcut[NTRK];
static inline int tw_has(uint32_t k) { return k >= TW_FIRST && k < NTRK; }
static void trk_mix_defaults(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        trk_width[k] = (uint8_t)(tw_has(k) ? TW_DEF : 0u);
        trk_lowcut[k] = (uint8_t)(tw_has(k) ? TL_DEF : 0u);
    }
}

/* SWING of a track's step clock (#31): CLOCK's SWING, at most 100. 1.5: the tracker's rows (tracker.c tk_row_len) and
 * the SLICER (slicer.c sl_enter) both swing by it, so the SLICER stays on the rows; a track's own SWG (P_SSWING, the
 * step sequencer's, up to 1.4) is not read any more. At 100 the even steps are 1.4 x the straight length and the odd
 * ones 0.6 x. */
#define SWING_MAX 100
static inline int32_t track_swing(const track_t *t)
{
    int32_t sw = song.g[G_SWING];
    (void)t;
    return sw < 0 ? 0 : sw > SWING_MAX ? SWING_MAX : sw;
}
/* the length of step idx of a straight length `base`, swung: even steps longer, odd ones shorter */
static inline uint32_t swing_step_len(const track_t *t, uint32_t base, uint32_t idx)
{
    int32_t sw = track_swing(t) * (int32_t)base / 250;
    return base + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* ----------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
