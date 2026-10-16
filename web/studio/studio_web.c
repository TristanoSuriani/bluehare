/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani, Blue Hare: see AUTHORS.md */
/* The sound design studio's WebAssembly: the browser emulator (web/emu/bluehare_web.c, the whole firmware against a
 * simulated FM-1) and, beside the panel's exports, the studio's: the engines and their presets, every parameter of
 * the selected track and of the FX chain by id (described, read, set, formatted as the device shows it), the user
 * preset slots (stored in the emulated flash as the device stores them). The page calls them through the worklet
 * (web/emu/worklet.js, "call"). Nothing of the firmware changes for this file: each export goes the way a device
 * action or the editor protocol (editor.c) goes.
 *   web/studio/build.sh  ->  build/studio/{index.html, studio.js, worklet.js, bluehare.wasm} */
#define BLUEHARE_AMBFX 1                 /* amb-1's processing beside Blue Hare's (fx_amb.c): EFFECTS > PROCESSING */
#include "../emu/bluehare_web.c"

#define STU_EXPORT __attribute__((used, visibility("default")))

static char stu_text[256];                         /* strings both ways: the page writes a name here, reads labels */
STU_EXPORT char *stu_buf(void) { return stu_text; }
static char *stu_say(const char *s)
{
    str_cpy(stu_text, s ? s : "", sizeof stu_text);
    return stu_text;
}

/* -------------------------------------------------------- engines, presets --- */
STU_EXPORT uint32_t stu_nengines(void) { return NENGINES; }                 /* this build's, in display order */
STU_EXPORT char *stu_engine_name(uint32_t e) { return stu_say(ENGINES[eng_idx(e)]->name); }
STU_EXPORT uint32_t stu_engine_id(uint32_t e) { return eng_id(e); }          /* the stored id (user presets, files) */
STU_EXPORT uint32_t stu_engine_of_id(uint32_t id) { return id < EID_COUNT ? eng_of_id(id) : ENG_NO; }
STU_EXPORT uint32_t stu_engine_none(void) { return ENG_NO; }
STU_EXPORT uint32_t stu_npresets(uint32_t e) { return ENGINES[eng_idx(e)]->npresets; }
STU_EXPORT char *stu_preset_name(uint32_t e, uint32_t k)
{
    const engine_t *en = ENGINES[eng_idx(e)];
    return stu_say(k < en->npresets ? en->presets[k].name : "");
}
STU_EXPORT uint32_t stu_preset_cat(uint32_t e, uint32_t k) { return preset_cat(eng_idx(e), k); }
STU_EXPORT char *stu_page_title(uint32_t e, uint32_t k) { return stu_say(ENGINES[eng_idx(e)]->page_title[k & 1u]); }
STU_EXPORT uint32_t stu_ncat(void) { return CAT_N; }
STU_EXPORT char *stu_cat_name(uint32_t c) { return stu_say(c < CAT_N ? CAT_NAME[c] : ""); }

/* ----------------------------------------------------------------- tracks --- */
STU_EXPORT uint32_t stu_ntracks(void) { return NTRK; }
STU_EXPORT uint32_t stu_sel(void) { return song.sel; }
STU_EXPORT void stu_select(uint32_t t) { track_select(t); }
STU_EXPORT uint32_t stu_engine(void) { return eng_idx(TSEL->eng_req); }   /* the selected track's (as asked for) */
STU_EXPORT uint32_t stu_preset(void) { return TSEL->preset; }
STU_EXPORT uint32_t stu_user(void) { return TSEL->user; }                  /* user slot + 1, 0 a factory preset */
STU_EXPORT uint32_t stu_track_engine(uint32_t t) { return eng_idx(trk[t % NTRK].eng_req); }
/* an engine on the selected track: its defaults and first preset (the device's engine jump, editor.c G_ENGSEL) */
STU_EXPORT void stu_set_engine(uint32_t e)
{
    if (e < NENGINES)
        select_engine(e);
}
STU_EXPORT void stu_load_preset(uint32_t k)        /* factory preset k of the track's engine (editor.c PRESET) */
{
    apply_preset(k);
    ui.force = 1;
}
STU_EXPORT void stu_undo(void) { undo_swap(); }    /* SAVE held: the sound before the last load, and back */

/* ------------------------------------------------------------- parameters --- */
/* scope 2: the effects that are no track or song parameter (docs/SoundAndDSP.md section 3): the inserts LOW CUT, WIDTH
 * (the Haas effect) and PAN MODE (core.h trk_lowcut, trk_width, trk_pmode: per track on the device, its SETTINGS'; the
 * studio treats every effect as shared: a value set is every track's, the one shown track 1's), the
 * performance layer's macros FILTER CRUSH THROW DEPTH (perform.c perf_k, as KNOB 1..4 with FX turn them), MENU > SPEAKER
 * EQ (settings.lowcut, fx.c fx_lowcut) and MASTER (the pot). Described as the others, values through stu_x */
enum { SX_LOWCUT, SX_WIDTH, SX_PMODE, SX_SPKEQ, SX_FILTER, SX_CRUSH, SX_THROW, SX_DEPTH, SX_MASTER, SX_PROC, SX_COUNT };
static const char *const N_SX_LOWCUT[TL_N] = {"OFF", "40Hz", "60Hz", "80Hz", "120Hz", "160Hz", "240Hz", "320Hz"};
static const char *const N_SX_PMODE[] = {"ALT", "RND"};
static const char *const N_SX_SPKEQ[] = {"FLAT", "LOWCUT", "BASS+"};
static const char *const N_SX_PROC[] = {"BLUE HARE", "AMB-1"};   /* fx.c fx_proc: the buses and master, or amb-1's */
static const param_desc_t SX[SX_COUNT] = {
    [SX_LOWCUT] = {"LOWCUT", F_ENUM, 0, TL_N - 1, 0, N_SX_LOWCUT, 0},
    [SX_WIDTH] = {"WIDTH", F_INT, 0, TW_MAX, TW_DEF, 0, "ms"},
    [SX_PMODE] = {"PANMODE", F_ENUM, 0, 1, 0, N_SX_PMODE, 0},
    [SX_SPKEQ] = {"SPK EQ", F_ENUM, 0, 2, 0, N_SX_SPKEQ, 0},
    [SX_FILTER] = {"FILTER", F_INT, -100, 100, 0, 0, 0},
    [SX_CRUSH] = {"CRUSH", F_INT, 0, 100, 0, 0, 0},
    [SX_THROW] = {"THROW", F_INT, 0, 100, 0, 0, 0},
    [SX_DEPTH] = {"DEPTH", F_INT, 0, 100, 0, 0, 0},
    [SX_MASTER] = {"MASTER", F_PCT, 0, 127, 87, 0, 0},
    [SX_PROC] = {"PROC", F_ENUM, 0, 1, 0, N_SX_PROC, 0},
};
static int16_t stu_xv;                            /* (the value read, as the others' pointers give theirs) */
static int32_t stu_x(uint32_t id)
{
    uint32_t k = song.sel;
    switch (id) {
    case SX_LOWCUT: return trk_lowcut[0];
    case SX_WIDTH: return trk_width[0];
    case SX_PMODE: return trk_pmode[0];
    case SX_SPKEQ: return settings.lowcut % 3u;
    case SX_FILTER: case SX_CRUSH: case SX_THROW: return perf_k[id - SX_FILTER];
    case SX_DEPTH: return 100 - perf_k[3];        /* (perf_k[3] is the cut: 0 the full level) */
    case SX_PROC: return fx_proc;
    default: return (int32_t)(web_master_adc * 127u + 511u) / 1023;
    }
}
static void stu_x_put(uint32_t id, int32_t v)
{
    uint32_t k = song.sel;
    switch (id) {
    case SX_LOWCUT: for (k = 0; k < NTRK; k++) trk_lowcut[k] = (uint8_t)v; break;   /* (shared: every track) */
    case SX_WIDTH: for (k = 0; k < NTRK; k++) trk_width[k] = (uint8_t)v; break;
    case SX_PMODE: for (k = 0; k < NTRK; k++) trk_pmode[k] = (uint8_t)v; break;
    case SX_SPKEQ: settings.lowcut = (uint8_t)v; fx_lowcut = (uint8_t)v; break;
    case SX_FILTER: case SX_CRUSH: case SX_THROW: perf_k[id - SX_FILTER] = (int8_t)v; break;
    case SX_DEPTH: perf_k[3] = (int8_t)(100 - v); break;
    case SX_PROC: fx_proc = (uint8_t)(v != 0); break;
    default: web_master((uint32_t)v * 1023u / 127u); break;
    }
}
STU_EXPORT uint32_t stu_xcount(void) { return SX_COUNT; }

/* scope 0: the selected track's p[id] (P_E0..P_E7 described by its engine, mode-dependent labels included: ui.c
 * track_desc); scope 1: song.g[id], the FX chain's and the clock's (params.c GP); scope 2: the effects above (SX) */
static const param_desc_t *stu_d(uint32_t scope, uint32_t id, int16_t **vp)
{
    if (scope == 2u && id < SX_COUNT) {
        stu_xv = (int16_t)stu_x(id);
        *vp = &stu_xv;
        return &SX[id];
    }
    if (scope == 0u && id < P_COUNT) {
        *vp = &TSEL->p[id];
        return track_desc(TSEL, id);
    }
    if (scope == 1u && id < G_COUNT) {
        *vp = &song.g[id];
        return &GP[id];
    }
    *vp = 0;
    return 0;
}
/* the description: {fmt, min, max, def, number of names}; stu_desc_label / stu_desc_unit / stu_desc_value_name */
static int32_t stu_dsc[5];
STU_EXPORT int32_t *stu_desc(uint32_t scope, uint32_t id)
{
    int16_t *vp;
    const param_desc_t *d = stu_d(scope, id, &vp);
    uint32_t n = 0;
    memset(stu_dsc, 0, sizeof stu_dsc);
    if (!d)
        return stu_dsc;
    stu_dsc[0] = d->fmt;
    stu_dsc[1] = d->min;
    stu_dsc[2] = d->max;
    stu_dsc[3] = d->def;
    if (d->fmt == F_ENUM && d->names)
        n = (uint32_t)(d->max - d->min + 1);
    stu_dsc[4] = (int32_t)n;
    return stu_dsc;
}
STU_EXPORT char *stu_desc_label(uint32_t scope, uint32_t id)
{
    int16_t *vp;
    const param_desc_t *d = stu_d(scope, id, &vp);
    return stu_say(d ? d->label : "");
}
STU_EXPORT int32_t stu_get(uint32_t scope, uint32_t id)
{
    int16_t *vp;
    return stu_d(scope, id, &vp) ? *vp : 0;
}
/* set as the editor protocol's SET: inside its range (a fixed value stays), the undo copy taken as a knob turn takes
 * it (one gesture: the same tag) */
STU_EXPORT int32_t stu_set(uint32_t scope, uint32_t id, int32_t v, uint32_t tag)
{
    int16_t *vp;
    const param_desc_t *d = stu_d(scope, id, &vp);
    if (!d)
        return 0;
    if (d->max > d->min) {
        if (scope == 0u)
            edit_undo_take(TSEL, tag ? tag : 1u + id);
        *vp = (int16_t)clamp(v, d->min, d->max);
        if (scope == 0u)
            edit_undo_done(TSEL);
        if (scope == 2u)
            stu_x_put(id, *vp);
    }
    ui.force = 1;
    return *vp;
}
/* name k of an enum parameter, whole (the device's value text cuts it to 5 letters) */
STU_EXPORT char *stu_enum_name(uint32_t scope, uint32_t id, uint32_t k)
{
    int16_t *vp;
    const param_desc_t *d = stu_d(scope, id, &vp);
    if (!d || d->fmt != F_ENUM || !d->names || k > (uint32_t)(d->max - d->min))
        return stu_say("");
    return stu_say(d->names[k]);
}
/* value v of the parameter as the device shows it: "value unit" */
STU_EXPORT char *stu_format(uint32_t scope, uint32_t id, int32_t v)
{
    int16_t *vp;
    const param_desc_t *d = stu_d(scope, id, &vp);
    char val[16] = {0};
    const char *unit = "";
    if (!d)
        return stu_say("");
    param_format(d, clamp(v, d->min, d->max), val, &unit);
    str_cpy(stu_text, val, sizeof stu_text);
    if (unit && *unit) {
        uint32_t n = (uint32_t)strlen(stu_text);
        stu_text[n] = ' ';
        str_cpy(stu_text + n + 1u, unit, sizeof stu_text - n - 1u);
    }
    return stu_text;
}
/* every value at once: the selected track's P_COUNT, then G_COUNT, then engine, preset, user, sel, then scope 2's
 * SX_COUNT (the page's poll) */
static int16_t stu_all[P_COUNT + G_COUNT + 4 + SX_COUNT];
STU_EXPORT int16_t *stu_dump(void)
{
    memcpy(stu_all, TSEL->p, sizeof TSEL->p);
    memcpy(stu_all + P_COUNT, song.g, sizeof song.g);
    stu_all[P_COUNT + G_COUNT] = (int16_t)eng_idx(TSEL->eng_req);
    stu_all[P_COUNT + G_COUNT + 1] = TSEL->preset;
    stu_all[P_COUNT + G_COUNT + 2] = TSEL->user;
    stu_all[P_COUNT + G_COUNT + 3] = song.sel;
    for (uint32_t i = 0; i < SX_COUNT; i++)                    /* then the effects of scope 2 */
        stu_all[P_COUNT + G_COUNT + 4 + i] = (int16_t)stu_x(i);
    return stu_all;
}
STU_EXPORT uint32_t stu_pcount(void) { return P_COUNT; }
STU_EXPORT uint32_t stu_gcount(void) { return G_COUNT; }
STU_EXPORT uint32_t stu_pe0(void) { return P_E0; }
/* a parameter a sound load leaves alone (the mix, the scale: ui.c param_kept): not part of a sound */
STU_EXPORT uint32_t stu_kept(uint32_t id) { return id < P_COUNT && param_kept(id); }

/* the studio's layout: groups of parameters, by scope and id (the names the page shows are its own; the labels,
 * ranges and values are the firmware's). A group: its title, then {scope, id} pairs, ended by 0xFF */
#define G1(x) 1, x
#define X2(x) 2, x
static const uint8_t STU_LAYOUT[] = {
    'E', 0, P_E0, 0, P_E1, 0, P_E2, 0, P_E3, 0, P_E4, 0, P_E5, 0, P_E6, 0, P_E7, 0xFF,
    'A', 0, P_ATK, 0, P_DEC, 0, P_SUS, 0, P_REL, 0, P_LEVEL, 0, P_OCT, 0xFF,
    'N', 0, P_ED_FLT, 0, P_ED_PIT, 0, P_ED_SHP, 0xFF,
    'L', 0, P_LRATE, 0, P_LWAVE, 0, P_LPHASE, 0, P_LFADE, 0, P_LSYNC, 0, P_LTRIG, 0, P_LPOL, 0xFF,
    'D', 0, P_LD_PIT, 0, P_LD_FLT, 0, P_LD_SHP, 0, P_LD_AMP, 0xFF,
    'M', 0, P_M1SRC, 0, P_M1DST, 0, P_M1AMT, 0, P_M2SRC, 0, P_M2DST, 0, P_M2AMT,
         0, P_M3SRC, 0, P_M3DST, 0, P_M3AMT, 0, P_M4SRC, 0, P_M4DST, 0, P_M4AMT, 0xFF,
    'V', 0, P_VOICE, 0, P_GLIDE, 0, P_GLMODE, 0, P_PRIO, 0, P_ALLOC, 0, P_DETUNE, 0, P_PAN, 0xFF,
    /* the effects, as docs/SoundAndDSP.md section 3 lists them: the track's inserts, its sends, the shared buses, the
     * performance layer, the master */
    'S', 0, P_DIST, 0, P_SPRD, 0, P_CHOR, 0, P_DLY, 0, P_REV, 0xFF,   /* (the sound's: drive, spread, sends) */
    'X', X2(SX_PROC), 0xFF,                                           /* Blue Hare's processing or amb-1's */
    'I', X2(SX_LOWCUT), X2(SX_WIDTH), X2(SX_PMODE), 0xFF,
    'Y', G1(G_DTYPE), G1(G_DTIME), G1(G_DFDBK), G1(G_DCOLOR), G1(G_DMIX), 0xFF,
    'R', G1(G_RTYPE), G1(G_RSIZE), G1(G_RDAMP), 0xFF,
    'C', G1(G_CRATE), G1(G_CDEPTH), 0xFF,
    'P', X2(SX_FILTER), X2(SX_CRUSH), X2(SX_THROW), X2(SX_DEPTH), 0xFF,
    'Q', X2(SX_SPKEQ), X2(SX_MASTER), 0xFF,
    'K', 0, P_ROOT, 0, P_SCALE, 0, P_QUANT, 0, P_TRANS, 0, P_CHRD, 0, P_VOIC, 0xFF,
    'G', G1(G_GPRE), G1(G_GSCL), G1(G_GROOT), G1(G_GMOT), G1(G_BPM), 0xFF,   /* GEN (gen.c), the tempo */
    0,
};
#undef G1
#undef X2
STU_EXPORT const uint8_t *stu_layout(void) { return STU_LAYOUT; }
STU_EXPORT uint32_t stu_layout_size(void) { return sizeof STU_LAYOUT; }

/* -------------------------------------------------- the performance layer --- */
/* an effect of the FX hold layer held (down 1) or let go (perform.c perf_press, as a white key with FX): e PF_LPF,
 * PF_HPF, PF_FRZ, PF_OUP, PF_ODN (the archived ones hold nothing). latch 1: each down turns it on or off instead
 * (MENU > FX LATCH) */
STU_EXPORT void stu_perf(uint32_t e, uint32_t down, uint32_t latch)
{
    perf_latch_on = (uint8_t)(latch != 0u);
    perf_kill = 0;
    perf_press(e, (int)down);
}
STU_EXPORT uint32_t stu_perf_on(void) { return (perf_held | perf_latched) & perf_avail() & ((1u << PF_M1) - 1u); }
STU_EXPORT void stu_perf_off(void)               /* every effect off, the macros back to 0 (FX + OCT-) */
{
    perf_held = perf_latched = 0;
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
}
/* the ids of the effects a key can hold, in the layer's order (PF_DEF), then PF_N: LPF HPF FREEZE OCT UP OCT DN */
STU_EXPORT const uint8_t *stu_perf_ids(void) { return PF_DEF; }

/* -------------------------------------------------------------------- GEN --- */
/* GEN (gen.c): start (1) or stop (0), as ARP does; its state {on, bar, step, chords, notes played by the four roles} */
STU_EXPORT void stu_gen(uint32_t on)
{
    if (on != gen.on)
        gen_toggle();
}
static int32_t stu_gs[8];
STU_EXPORT int32_t *stu_gen_state(void)
{
    uint32_t r;
    stu_gs[0] = gen.on;
    stu_gs[1] = (int32_t)gen.s.bar;
    stu_gs[2] = (int32_t)gen.s.step;
    stu_gs[3] = (int32_t)gen.s.nchord;
    for (r = 0; r < 4u; r++)
        stu_gs[4 + r] = (int32_t)gen.s.played[r];
    return stu_gs;
}
STU_EXPORT char *stu_gen_chord(void)
{
    char b[8];
    gen_chord_name(b);
    return stu_say(b);
}
STU_EXPORT uint32_t stu_gen_npresets(void) { return GEN_NPRESETS; }
STU_EXPORT char *stu_gen_preset_name(uint32_t k) { return stu_say(GEN_PRESETS[k % GEN_NPRESETS].name); }

/* ------------------------------------------------------------ the master --- */
/* the master's always-on stages, now: the bus compressor's gain (Q15), the limiter's level over its threshold (Q15:
 * 32768 = none) */
static int32_t stu_mtr[2];
STU_EXPORT int32_t *stu_meters(void)
{
    stu_mtr[0] = fx_proc ? 32768 : bc_g;          /* (amb-1's processing: no compressor, no limiter) */
    stu_mtr[1] = fx_proc || lim_env <= LIM_T ? 32768 : (int32_t)(((int64_t)LIM_T << 15) / lim_env);
    return stu_mtr;
}

/* ------------------------------------------------------------ user presets --- */
STU_EXPORT uint32_t stu_up_slots(void) { return UP_SLOTS; }
STU_EXPORT uint32_t stu_up_used(uint32_t k) { return (uint32_t)up_used(k); }
STU_EXPORT char *stu_up_name(uint32_t k)
{
    char b[13];
    if (!up_used(k))
        return stu_say("");
    up_name(k, b);
    return stu_say(b);
}
STU_EXPORT uint32_t stu_up_engine(uint32_t k) { return up_used(k) ? up_engine(k) : ENG_NO; }   /* the index (ENG_NO) */
STU_EXPORT uint32_t stu_up_cat(uint32_t k) { return up_used(k) ? up_cat(k) : 0u; }
/* the selected track's sound -> slot k, named stu_buf (1..12 of ASCII 32..126), category cat (0: the sound's own).
 * 0 ok, 1 a bad name or slot, 2 the flash (as up_put) */
STU_EXPORT uint32_t stu_up_store(uint32_t k, uint32_t cat)
{
    uint32_t n = (uint32_t)strlen(stu_text);
    if (k >= UP_SLOTS || !n || n > 12u || !up_name_ok((const uint8_t *)stu_text, n) || cat >= CAT_N)
        return 1;
    if (song.playing)                            /* (as the device: no flash write while the transport runs) */
        seq_stop();
    return up_store_cat(k, stu_text, cat) == 2 ? 2u : 0u;
}
STU_EXPORT uint32_t stu_up_load(uint32_t k) { return k < UP_SLOTS && !up_load(k) ? 0u : 1u; }
STU_EXPORT uint32_t stu_up_erase(uint32_t k)
{
    if (k >= UP_SLOTS)
        return 1;
    if (song.playing)
        seq_stop();
    return up_put(k, 0) == 2 ? 2u : 0u;
}
/* a sound from a file into the selected track: engine (index), then the values the page wrote with stu_set_raw,
 * as a user preset loads (up_load: the track's own parameters stay). stu_load_begin, stu_set_raw .., stu_load_end */
static int16_t stu_stage[P_COUNT];
STU_EXPORT void stu_stage_begin(void)
{
    memcpy(stu_stage, TSEL->p, sizeof stu_stage);
}
STU_EXPORT void stu_stage_set(uint32_t id, int32_t v)
{
    if (id < P_COUNT)
        stu_stage[id] = (int16_t)v;
}
STU_EXPORT void stu_stage_load(uint32_t e)
{
    track_t *t = TSEL;
    uint32_t i;
    if (e >= NENGINES)
        return;
    load_begin(t);
    panic_req |= (uint8_t)(1u << song.sel);
    fm1_irq_off();
    t->eng_req = (uint8_t)eng_for_track(t, e);
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i))
            t->p[i] = (int16_t)param_fit(param_desc_of(t->eng_req, i), param_oct(i, stu_stage[i]));
    t->preset = 0;
    t->user = 0;
    fm1_irq_on();
    load_end(t);
    sync_reload = 1;
    ui.force = 1;
}

/* ------------------------------------------------------------------- notes --- */
/* the computer keyboard and the page's keys: a note on the selected track, as MIDI IN's channel of it would play it
 * (MENU > MIDI > MIDI IN: CH1-4, channel k + 1 plays track k + 1) */
STU_EXPORT void stu_note(uint32_t note, uint32_t vel)
{
    uint32_t ch = song.sel & 15u;
    web_midi(vel ? 0x90u | ch : 0x80u | ch, note & 127u, vel & 127u);
}
STU_EXPORT void stu_all_off(void) { panic_req |= (uint8_t)((1u << NTRK) - 1u); }

/* --------------------------------------------------------------- the tests --- */
/* web/studio/studio_test.mjs: the page's UP_PUT arguments (web/studio/sysex.js) against the firmware's own parser
 * (upreset.c up_parse, the one editor.c's UP_PUT runs on the device): -1 refused, else slot | engine << 8 | category << 16 */
static uint8_t stu_frm[640];
STU_EXPORT uint8_t *stu_frame(void) { return stu_frm; }
STU_EXPORT int32_t stu_up_parse(uint32_t n)
{
    static up_rec_t r;
    uint32_t slot = 0;
    if (n > sizeof stu_frm || up_parse(stu_frm, n, &r, &slot))
        return -1;
    return (int32_t)(slot | (uint32_t)r.engine << 8 | up_cat_of(&r) << 16);
}
