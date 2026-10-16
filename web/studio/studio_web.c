/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani, Blue Hare: see AUTHORS.md */
/* The sound design studio's WebAssembly: the browser emulator (web/emu/bluehare_web.c, the whole firmware against a
 * simulated FM-1) and, beside the panel's exports, the studio's: the engines and their presets, every parameter of
 * the selected track and of the FX chain by id (described, read, set, formatted as the device shows it), the user
 * preset slots (stored in the emulated flash as the device stores them). The page calls them through the worklet
 * (web/emu/worklet.js, "call"). Nothing of the firmware changes for this file: each export goes the way a device
 * action or the editor protocol (editor.c) goes.
 *   web/studio/build.sh  ->  build/studio/{index.html, studio.js, worklet.js, bluehare.wasm} */
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
/* scope 0: the selected track's p[id] (P_E0..P_E7 described by its engine, mode-dependent labels included: ui.c
 * track_desc); scope 1: song.g[id], the FX chain's and the clock's (params.c GP) */
static const param_desc_t *stu_d(uint32_t scope, uint32_t id, int16_t **vp)
{
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
/* every value at once: the selected track's P_COUNT, then G_COUNT, then engine, preset, user, sel (the page's poll) */
static int16_t stu_all[P_COUNT + G_COUNT + 4];
STU_EXPORT int16_t *stu_dump(void)
{
    memcpy(stu_all, TSEL->p, sizeof TSEL->p);
    memcpy(stu_all + P_COUNT, song.g, sizeof song.g);
    stu_all[P_COUNT + G_COUNT] = (int16_t)eng_idx(TSEL->eng_req);
    stu_all[P_COUNT + G_COUNT + 1] = TSEL->preset;
    stu_all[P_COUNT + G_COUNT + 2] = TSEL->user;
    stu_all[P_COUNT + G_COUNT + 3] = song.sel;
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
static const uint8_t STU_LAYOUT[] = {
    'E', 0, P_E0, 0, P_E1, 0, P_E2, 0, P_E3, 0, P_E4, 0, P_E5, 0, P_E6, 0, P_E7, 0xFF,
    'A', 0, P_ATK, 0, P_DEC, 0, P_SUS, 0, P_REL, 0, P_LEVEL, 0, P_OCT, 0xFF,
    'N', 0, P_ED_FLT, 0, P_ED_PIT, 0, P_ED_SHP, 0xFF,
    'L', 0, P_LRATE, 0, P_LWAVE, 0, P_LPHASE, 0, P_LFADE, 0, P_LSYNC, 0, P_LTRIG, 0, P_LPOL, 0xFF,
    'D', 0, P_LD_PIT, 0, P_LD_FLT, 0, P_LD_SHP, 0, P_LD_AMP, 0xFF,
    'M', 0, P_M1SRC, 0, P_M1DST, 0, P_M1AMT, 0, P_M2SRC, 0, P_M2DST, 0, P_M2AMT,
         0, P_M3SRC, 0, P_M3DST, 0, P_M3AMT, 0, P_M4SRC, 0, P_M4DST, 0, P_M4AMT, 0xFF,
    'V', 0, P_VOICE, 0, P_GLIDE, 0, P_GLMODE, 0, P_PRIO, 0, P_ALLOC, 0, P_DETUNE, 0, P_SPRD, 0, P_PAN, 0xFF,
    'S', 0, P_DIST, 0, P_CHOR, 0, P_DLY, 0, P_REV, 0xFF,
    'Y', G1(G_DTYPE), G1(G_DTIME), G1(G_DFDBK), G1(G_DCOLOR), G1(G_DMIX), 0xFF,
    'R', G1(G_RTYPE), G1(G_RSIZE), G1(G_RDAMP), 0xFF,
    'C', G1(G_CRATE), G1(G_CDEPTH), 0xFF,
    'K', G1(G_BPM), 0, P_ROOT, 0, P_SCALE, 0, P_QUANT, 0, P_TRANS, 0, P_CHRD, 0, P_VOIC, 0xFF,
    0,
};
#undef G1
STU_EXPORT const uint8_t *stu_layout(void) { return STU_LAYOUT; }
STU_EXPORT uint32_t stu_layout_size(void) { return sizeof STU_LAYOUT; }

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
