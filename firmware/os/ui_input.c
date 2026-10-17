/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* Blue Hare UI input: LEDs, knobs and buttons, panel setup. */
#include "ui_name.c"                                    /* NAME: naming user presets and projects */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

/* PLAY's second, green LED: not in the key matrix (no key there), found on the hardware at column 8, row PA9 (bit 1) */
#define LED_PLAY_GREEN ((8u << 3) | 1u)
static void led_clear(uint8_t *nl, uint32_t id)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF)
        nl[q >> 3] &= (uint8_t)~(1u << (q & 7u));
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_HOME};   /* MIXER: HOME's (1.2); REC is transport */

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }

static int layer_set_open(void);                       /* (ui_layer.c) */
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* (below: the knobs' acceleration) */
/* the OCT LEDs, bit 0 OCT- lit, bit 1 OCT+ lit, OCT_BREATH OCT+ breathing (can be pressed: dark .. ~60 %,
 * hal/fm1_input.h fm1_led_breath, #119). In the dialogs and on action pages OCT- (back) is lit and OCT+ breathes
 * while it would do something; the menu: on a value row OCT- and OCT+ both breathe while that way has a value to go to,
 * on CALIBRATION / ABOUT OCT+ breathing alone (opens), in ABOUT OCT- lit (back); elsewhere the octave shift */
#define OCT_BREATH 4u
#define OCT_BREATH_DN 8u                                /* OCT- breathing (the menu: the previous value) */
/* MOMENTARY (1.2, Discussions #72 / #73): LFO held, the knobs' changes are temporary. KNOB 1..4 on HOME and on the
 * sound and FX pages (a value of the selected track or a global sound / FX one, as edit_param sets it; not on the
 * action pages, the lists or TRACKS, where the knobs pick) change their value as ever, and each
 * value turned is put back as it was when LFO is let go: hold LFO, sweep the filter, let go, it snaps back. OCT+ while
 * LFO is still held KEEPS them (the changes stay, nothing is put back; OCT+ breathes while something would go back;
 * LFO has no LED). LFO's tap (its page) is dropped once any knob turned with it; LFO alone still opens LFO. Temporary
 * turns take no undo; a sound or project load meanwhile (the
 * PRESETS knob, the editor, a song's section) puts that track's values (and the global ones) back first (mom_back: the
 * undo copy holds the sound as it was, the load's sound stays after). The menu, a dialog, NAME or a layer opening put
 * them back too. Up to MOM_N values at a time ("MOMENTARY FULL" past them: the turn does nothing). LFO held did
 * nothing (it is no layer; OCT- is the octave and a layer's UNDO, so not it), so no gesture changes: LFO + a knob
 * turned opened LFO's page on release before, and edited the page under it for good. The values: ui.c mom */
static int mom_down(void)                              /* LFO held where its turns are momentary */
{
    return ((fm1_in.buttons >> panel.btn[B_LFO]) & 1u) && !ui.menu && !ui.confirm && !name_on() && !ui.layer && !ui.uboot;
}
/* KNOB k turned s with LFO held: 1 = taken (a momentary value, or refused full), 0 = not a momentary value (the turn
 * goes as ever: the column picks, steps, acts) */
static int mom_turn(uint32_t k, int32_t s)
{
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int16_t *vp = 0;
    uint32_t i, g;
    if (ui.home) {
        d = home_param(k, &vp);
    } else {
        if (act_cols() || !(pg->scope == SC_TRACK || pg->scope == SC_ENGINE || pg->scope == SC_GLOBAL) ||
            pg->graph == GR_BROWSE || pg->graph == GR_USER ||
            (pg->graph == GR_MOD && k == 0u))
            return 0;
        d = page_desc(pg, k, &vp);
    }
    g = vp && vp >= song.g && vp < song.g + G_COUNT ? (uint32_t)(vp - song.g) : G_COUNT;
    if (!d || !vp || d->max == d->min ||
        !((vp >= TSEL->p && vp < TSEL->p + P_COUNT) || (g <= G_CDEPTH && g != G_CLOCK) || g == G_RTYPE || g == G_DTYPE))
        return 0;                                      /* (only the values a sound and the FX are: no CLK, no actions) */
    for (i = 0; i < mom.n && mom.vp[i] != vp; i++)
        ;
    if (i == mom.n) {                                  /* the first turn of this value: what it was */
        if (mom.n >= MOM_N) {
            ui_message("MOMENTARY FULL");
            return 1;
        }
        mom.vp[i] = vp;
        mom.v[i] = *vp;
        mom.n++;
        if (i == 0u)
            ui_message("MOMENTARY  [OCT+] KEEP");
    }
    *vp = (int16_t)param_turn(d, *vp, accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min));
    return 1;
}

static uint32_t oct_leds(void)
{
    if (name_on() && !ui.confirm && !ui.menu)           /* NAME: OCT- cancels, OCT+ (breathing) writes */
        return 1u | OCT_BREATH;
    if (layer_set_open())                               /* a SET layer: OCT- puts back (UNDO), OCT+ nothing */
        return 1u;
    if (ui.menu && !ui.confirm)
        return ui.menu >= 2u ? 1u :                    /* (INFO, ABOUT: OCT- back, lit) */
               ui.menu_sel >= MI_VALUES ? OCT_BREATH :  /* CALIBRATION / INFO / ABOUT: OCT+ opens */
               (menu_step(ui.menu_sel, -1) != menu_get(ui.menu_sel) ? OCT_BREATH_DN : 0u) |   /* a value: each */
               (menu_step(ui.menu_sel, 1) != menu_get(ui.menu_sel) ? OCT_BREATH : 0u);         /* way it can go */
    if (ui.confirm || act_cols())
        return 1u | (ui.confirm || act_ready() ? OCT_BREATH : 0u);
    if (mom.n && mom_down())                            /* MOMENTARY: OCT+ keeps the changes */
        return (song.octave < 0 ? 1u : 0u) | OCT_BREATH;
    return (song.octave < 0 ? 1u : 0u) | (song.octave > 0 ? 2u : 0u);
}

/* Discussion #81: the keys of the notes MIDI IN (USB and TRS, routed by ROUT) holds on track t, bit k = key k (#38):
 * where the keys play that note at the octave now (kb_map: TRN, QNT, an engine's own map), the lowest key that gives it; a note no key plays is
 * not shown. Read from midi_control.c's own state (midi_note_held: the notes each channel holds for the track, the
 * pedal's too, and the tones of MIDI chords): no state here; nothing to do while MIDI holds none of the track's */
static uint32_t midi_leds(const track_t *t)
{
    uint32_t k, note, m = 0, seen[4] = {0, 0, 0, 0};
    if (!midi_owners[trk_index(t)])
        return 0;
    for (k = 0; k < 27u; k++) {
        note = kb_map(t, k);
        if (note > 127u || ((seen[note >> 5] >> (note & 31u)) & 1u))
            continue;                                   /* (silent, or a lower key gives it) */
        seen[note >> 5] |= 1u << (note & 31u);
        m |= (uint32_t)midi_note_held(t, note) << k;
    }
    return m;
}

/* 1: the keys show a map of their own (NAME, a layer's map: SCL's scale, FX; SLICES), lit or
 * dark; 0: the keys held and the notes playing, over the idle glow */
static int keys_own(void)
{
    return (name_on() && !ui.menu) || ui.layer
        ;
}

/* Discussion #127 (1.2): MENU > SCALE LEDS ON, the keys of the selected track's scale (ROOT, SCALE), bit k = key k, as
 * the keys play now: the note a key gives with TRN (QNT SNAP / SEQ: the key's own note; the others round down onto
 * the scale); QNT WHITE every white key (they walk the scale; the black ones are silent). *root: the keys of the
 * scale's root. None where the engine maps the keys itself (DRUM's GM map, the slices): 0 there, and OFF */
static uint32_t scale_leds(uint32_t *root)
{
    const track_t *t = TSEL;
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t k, m = 0, r = 0, mask = scale_mask(t), note;
    int32_t pc;
    *root = 0;
    if (!(ui_rec_prefs & (PREF_SCALE_LEDS >> 8)))
        return 0;
    for (k = 0; k < 27u; k++) {
        if (e->keys && e->keys(t, k) >= 0)
            continue;
        if (t->p[P_QUANT] == QN_WHITE) {
            if ((note = kb_map(t, k)) > 127u)
                continue;                               /* (a black key: silent) */
            pc = (int32_t)note - t->p[P_TRANS];         /* (WHITE walks the scale before TRN) */
        } else {
            pc = 53 + (int32_t)k + t->p[P_TRANS];
        }
        pc = ((pc - t->p[P_ROOT]) % 12 + 12) % 12;
        m |= ((mask >> pc) & 1u) << k;
        r |= (uint32_t)(pc == 0) << k;
    }
    *root = r & m;
    return m;
}

/* MENU > ANIM IDLE (1.2, Discussion #135): after IDLE_LEDS_MS without panel input (ui.c scrn.idle: a button or key
 * pressed or held, a knob turned; what SCREEN OFF counts) the LEDs play the idle animation (hal/fm1_led_anim.h
 * fm1_idle_level: a soft light drifting over the keys and back, the buttons breathing with it; the scan draws it,
 * hal/fm1_input.h fm1_led_idle_start). Only with nothing going on: stopped (no count-in, no song, nothing armed or
 * waiting to start), no layer (held or locked), menu, dialog, NAME or UPDATE countdown, no key held or lit by the
 * selected track (MIDI IN notes), not with ANIM OFF. Any input ends it at once and is not swallowed (as SCREEN OFF's
 * wake would be): a key or a button down ends it in the scan before its next frame (the press acts as ever), a knob
 * here (the turn acts as ever); so does the transport starting (MIDI Start, the editor) or anything above. 1 = it has
 * the LEDs (ui_leds writes none). A dialog or the count-in count as input, as for SCREEN OFF (ui.c scr_frame) */
#define IDLE_LEDS_MS 60000u
static int idle_leds(void)
{
    int due = (ui_idle & 1u) && !(ui_prefs & PREF_ANIM_OFF) && !song.playing &&
              !transport_req && !ui.layer && !ui.lock && !ui.menu && !ui.confirm && !ui.uboot && !name_on() &&
              !(fm1_in.notes | fm1_in.buttons) && !panel_moved && !midi_leds(TSEL) && fm1_ms - scrn.idle >= IDLE_LEDS_MS;
    if (!due) {
        if (fm1_led_idle_on())
            fm1_led_anim_stop();
        return 0;
    }
    if (!fm1_led_anim_on())                            /* (not over the power-on sweep) */
        fm1_led_idle_start(settings_leds == LEDS_DIM || settings_leds == LEDS_DIM_LO);
    return fm1_led_idle_on();
}
/* the key LEDs, bit k = key k: NAME's keys, the layer's map, else the keys held and the notes the selected track's
 * tracker, ARP and MIDI IN play (and on SLICES the keys of the selected slice) */
static uint32_t key_leds(uint32_t *br)                 /* (*br: a layer's keys that breathe, layer_leds) */
{
    uint32_t c;
    if (br)
        *br = 0;
    c = name_on() && !ui.menu ? name_leds() : ui.layer ? layer_leds(br) : (fm1_in.notes & ~kb_layer) | midi_leds(TSEL);
    return c;
}

/* The LEDs: lit = active (the page's family, PLAY / REC running, the keys held or playing, a map's keys), the ARP
 * beat flashes, the ones that can be pressed breathe (#119: the open layer's button, OCT+ when it would act, a
 * layer's keys; dark .. ~60 % of lit and back, hal/fm1_input.h fm1_led_breath, in every mode: never a hard blink in
 * a dark room; DIM LO up to ~30 %, DIM HI OFF INV ~60 %; with LEDS OFF it is the only glow; INV leaves it as it is,
 * between the dark active ones and the lit idle ones), every other button and key glows dim (#35: the
 * buttons of the black FM-1 can be found in the dark; hal/fm1_input.h fm1_led_dim, a short pulse each frame).
 * MENU > LEDS: DIM HI (default) that glow, DIM LO a darker one (fm1_led_dim_level), OFF no glow (as 1.0); INV
 * turns it around, as the stock firmware: the idle ones fully lit, the active ones dark, no glow (the ARP beat:
 * lit / dark). The keys' own maps (keys_own) stay lit or dark in every mode: their dark keys read as dark, nothing
 * glows under them. 1.2, MENU > SCALE LEDS ON (scale_leds; not over a map of the keys' own, nor on a DRUM
 * track): the keys of the selected track's scale glow and the others are dark, the root's at the mid level
 * (hal/fm1_input.h fm1_led_mid: lit 1 frame in 8, DIM LO in 12, over the glow), in
 * DIM HI, DIM LO and OFF alike (asked for: OFF only takes the other glow away); INV turns it around as it does the
 * rest: the root's keys lit, the scale's at the mid level, the others glowing; the keys held and playing as ever
 * (lit, INV dark). Each picture is built off-line and copied one byte per column, the
 * glow first: an LED going from lit to dim never has a dark frame */
static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, own[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0};
    uint8_t nm[FM1_NCOL] = {0}, sg[FM1_NCOL] = {0}, sm[FM1_NCOL] = {0};
    uint32_t k, c, br, sc, sr = 0;
    uint32_t fam = cur_fam(), mode = settings_leds;
    int keys_map = keys_own();
    static uint8_t ready;
    if (idle_leds() || fm1_led_anim_on())              /* the idle animation, the power-on sweep have them (main.c */
        return;                                         /* boot_leds) */
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    if (!ui.layer || FAM_BTN[fam] != layer_btn())
        led_put(nl, panel.btn[FAM_BTN[fam]], 1);
    if (!ui.layer && (perf_latched || perf_k[0] || perf_k[1] || perf_k[2] || perf_k[3]))
        led_put(nl, panel.btn[B_FX], 1);                /* FX LATCH: lit while an effect or a macro is on */
    if (gen.on && FAM_BTN[fam] != B_ARP)                /* (Blue Hare) GEN playing: ARP breathes, elsewhere */
        led_put(nb, panel.btn[B_ARP], 1);
    if (ui.layer)                                       /* the layer's button breathes while its map is up */
        led_put(nb, panel.btn[layer_btn()], 1);
    k = oct_leds();
    led_put(nl, panel.btn[B_OCTDN], (int)(k & 1u));
    led_put(nl, panel.btn[B_OCTUP], (int)((k >> 1) & 1u));
    led_put(nb, panel.btn[B_OCTUP], (int)(k & OCT_BREATH));
    led_put(nb, panel.btn[B_OCTDN], (k & OCT_BREATH_DN) != 0u);
    c = key_leds(&br);
    sc = keys_map ? 0u : scale_leds(&sr);               /* SCALE LEDS: the scale's keys glow, the root's brighter */
    for (k = 0; k < 27u; k++) {
        uint32_t in = (sc >> k) & 1u, rt = (sr >> k) & 1u, idle = !((c >> k) & 1u);
        led_put(keys_map ? own : nl, 14u + k, (int)((c >> k) & 1u));
        if (!keys_map)
            led_put(nd, 14u + k, !sc || (mode == LEDS_INV && rt));
        led_put(nb, 14u + k, (int)((br >> k) & 1u));
        if (!sc)
            continue;
        led_put(sg, 14u + k, mode == LEDS_INV ? !in && idle : (int)in);   /* (INV: the active ones dark) */
        led_put(sm, 14u + k, mode == LEDS_INV ? in && !rt && idle : (int)rt);
    }
    for (k = 0; k < NB; k++)
        led_put(nd, panel.btn[k], 1);
    if (song.playing)                                   /* playing: PLAY's green, its own LED dark in every mode */
        led_clear(nd, panel.btn[B_PLAY]);
    for (c = 0; c < FM1_NCOL; c++) {
        if (mode == LEDS_INV)                           /* INV: the active ones dark, the rest lit */
            nl[c] = (uint8_t)(nd[c] & ~nl[c]);
        if (mode != LEDS_DIM && mode != LEDS_DIM_LO)    /* OFF, INV: no glow */
            nd[c] = 0;
        nd[c] |= sg[c];                                 /* SCALE LEDS: in every mode (INV: inverted above) */
        nm[c] |= sm[c];
        nm[c] &= (uint8_t)~(nb[c] | own[c]);
        nl[c] |= own[c];
        nl[c] &= (uint8_t)~nb[c];                       /* breathing: neither lit (INV's idle) nor the glow */
        nd[c] &= (uint8_t)~nb[c];
    }
    if (song.playing)
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    fm1_led_dim_level(mode == LEDS_DIM_LO);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[c] = nd[c];
        fm1_led_breath[c] = nb[c];
        fm1_led_mid[c] = nm[c];
    }
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = nl[c];
}

/* ---------------------------------------------------------- input --- */
/* Predictable hardware response (#23): each decoded detent is one value step; a fast turn keeps its full signed
 * detent count. MENU > KNOB ACCEL ON (#52, OFF by default) multiplies a fast turn of a wide value (range > 32, not a
 * list of names; the FX and GLO layers' knobs too, #126) by 2..4, by up to 8 over a range above 64. The main loop
 * reads the knobs many times a frame (main.c), so a read holds one detent as a rule: the speed is the time per
 * detent, ACC_RATE / ms -> 25 ms x2, 16 ms x3, 12 ms x4, 10 ms x5 .. 6 ms or less x8 (a flick: 12 detents in 120 ms
 * move a -100..100 FX macro ~50). Only the longer of this read's and the previous read's time counts, and only while
 * the turn goes on (both under ACC_GAP ms) in one direction: a slow turn, the first two detents of a turn, a single
 * quick detent (a bounce) and a reversal are one step per detent, and the sign is always the detents'.
 * ui.enc_t[role]: bits 0..23 the ms of its last read, bit 24 its direction (+1), 25..31 its ms per detent (127 slow) */
#define ACC_GAP 40u
#define ACC_RATE 50u
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ms & 0xFFFFFFu, st = ui.enc_t[role], up = s > 0, pi = st >> 25, a, i, m = 1;
    if (!(ui_prefs & PREF_ACCEL) || range <= 32 || !s)
        return s;
    a = (uint32_t)(s < 0 ? -s : s);
    i = ((now - st) & 0xFFFFFFu) / a;                   /* ms per detent of this read */
    if (!st || ((st >> 24) & 1u) != up || i >= ACC_GAP)
        i = 127u;                                       /* a new turn, or reversed */
    else if (pi < ACC_GAP) {
        m = ACC_RATE / (i > pi ? i : pi ? pi : 1u);
        a = range > 64 ? 8u : 4u;                       /* (the cap) */
        m = m < 1u ? 1u : m > a ? a : m;
    }
    ui.enc_t[role] = now | up << 24 | (i ? i : 1u) << 25;
    return s * (int32_t)m;
}

/* MIXER page: KNOB 1 LEVEL, 2 PAN, 3 REV send, 4 MUTE of the selected track (right = ON, left = OFF: the
 * track itself is ALGORITHM's, on every page). Pattern length stays on SEQ. */
static void tracks_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    int16_t *vp;
    const param_desc_t *d;
    switch (slot) {
    case 3:
        t->p[P_MUTE] = (int16_t)(steps > 0);
        return;
    case 0:
        vp = &t->p[P_LEVEL];
        d = &TP[P_LEVEL];
        break;
    case 2:
        vp = &t->p[P_REV];
        d = &TP[P_REV];
        break;
    default:
        vp = &t->p[P_PAN];
        d = &TP[P_PAN];
        break;
    }
    *vp = (int16_t)clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
}

/* the OCT- / OCT+ dialog (ui_draw.c draws it); trk: the track or the slot it is about */
static void confirm_open(uint32_t kind, uint32_t trk)
{
    ui.confirm = (uint8_t)kind;
    ui.confirm_trk = (uint8_t)trk;
    ui.act = 0;
    ui.force = 1;
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int32_t v;
    if (pg->scope == SC_TRK) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_BROWSE) {                         /* KNOB 1: one preset, KNOB 2: the next / previous engine */
        if (slot == 0u) {
            preset_step(steps);
        } else if (slot == 1u) {
            select_engine(eng_step(TSEL->eng_req, steps));
        } else if (slot == 2u) {
            preset_mark(steps > 0);
        } else if (slot == 3u) {                          /* LIST: ALL FAV BASS .. OTHER (ui.c list_mode), no wrap */
            uint32_t m = (uint32_t)clamp((int32_t)list_mode() + steps, 0, (int32_t)LIST_N - 1);
            if (m != list_mode()) {
                list_set(m);
                ui.force = 1;
                settings_save();
            }
        }
        return;
    }
    if ((act_cols() >> slot) & 1u) {                      /* an action's knob picks it (right) or drops it (left); */
        ui.act = steps > 0 ? (uint8_t)(slot + 1u) : ui.act == slot + 1u ? 0u : ui.act;   /* (OCT+ does it: act_do) */
        return;
    }
    if (pg->graph == GR_USER) {                           /* KNOB 1 the slot */
        if (slot == 0u)
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, UP_SLOTS - 1);
        return;
    }
    if (pg->graph == GR_MOD && slot == 0u) {             /* MOD: KNOB 1 the slot, 2..4 its SRC DST AMT */
        mod_ui_slot = (uint8_t)clamp((int32_t)mod_ui_slot + (steps > 0 ? 1 : -1), 0, 3);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = param_turn(d, *vp, accel(EN_K1 + slot, steps, d->fmt == F_ENUM ? 0 : d->max - d->min));
    *vp = (int16_t)v;
}

/* OCT+ on an action page: the picked action. A load stays picked (browse and load again); the others
 * are dropped once done. Flash writes only while stopped; over the user's data: the dialog */
static void act_do(void)
{
    uint32_t c = act_col(), id, k = (uint32_t)song.g[G_SLOT] - 1u;
    if (!c--)
        return;
    if (cur_page()->graph == GR_USER) {                   /* 1 LOAD, 2 ERASE, 3 SAVE (the NAME screen first) */
        if (c > 1u)
            ui.act = 0;
        if (c == 2u && up_used(ui.uslot) && !transport_busy())
            confirm_open(CF_ERASE_USER, ui.uslot);        /* ERASE: the dialog first */
        else if (c != 3u)
            up_ui(c - 1u, ui.uslot);
        else if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (up_used(ui.uslot))
            confirm_open(CF_OVR_USER, ui.uslot);
        else
            name_open(NK_USER_SAVE, ui.uslot);
        return;
    }
    id = cur_page()->id[c & 3u];
    if (id != G_LOAD)
        ui.act = 0;
    switch (id) {
    case G_LOAD:
        project_load(k);
        break;
    case G_SAVE:                                          /* the NAME screen writes it */
        if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (project_used(k))
            confirm_open(CF_OVR_PROJ, k);
        else
            name_open(NK_PROJ_SAVE, k);
        break;
    default:                                              /* G_INITSND (TOOLS): the dialog first */
        confirm_open(CF_INIT_SOUND, song.sel);
        break;
    }
}

/* OCT- / OCT+ where they answer (the dialogs, the menu, action pages): on release, and only a press
 * that began there; both down together (UPDATE MODE, main.c) is no tap. Bit 0 OCT-, bit 1 OCT+ */
static uint32_t oct_taps(uint32_t pressed, int here)
{
    static uint8_t down, chord;
    uint32_t dn = panel.btn[B_OCTDN], up = panel.btn[B_OCTUP];
    uint32_t now = ((fm1_in.buttons >> dn) & 1u) | ((fm1_in.buttons >> up) & 1u) << 1, tap;
    if (here)
        down |= (uint8_t)(((pressed >> dn) & 1u) | ((pressed >> up) & 1u) << 1);
    if (now == 3u)
        chord = 1;
    tap = down & ~now;
    down &= (uint8_t)now;
    if (chord) {
        tap = 0;
        chord = now != 0u;
    }
    return tap;
}

/* Discussions #92 / #94: the PRESETS knob by page (the menu, a dialog, NAME and the layers: ui_input before this).
 *   the list pages USER and PROJECT: as their KNOB 1; TOOLS and SLICES: nothing
 *   HOME, PRESETS and every other page (EDIT, ENV, LFO, FX, SCL, ARP, MIXER, CLOCK, ...): the selected track's
 *     sound, one list over every engine, then the used user presets (preset_step; SAVE held undoes it) */
static void presets_turn(int32_t s)
{
    const page_t *pg = cur_page();
    uint32_t g = pg->graph;
    if (ui.home || g == GR_BROWSE) {
        preset_step(s);
    } else if (g == GR_USER || g == GR_SLOTS) {
        edit_param(0, s);                                 /* KNOB 1's */
        ui.hot_col = 0;
        ui.hot_t = 40;
    } else if (g != GR_TOOLS) {
        preset_step(s);
    }
}

/* HOME / SAVE: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

/* the quick layers: ui_layer.c (included after this file) */
static void layer_masks(void);
static void layer_arm(uint32_t pressed, uint32_t now);
static uint32_t layer_held(void);
static int layer_knobs_quiet(void);
static uint32_t layer_gesture(uint32_t now, uint32_t combo);
static void layer_show(void);
static void layer_tap(uint32_t l);
static void layer_keys(uint32_t keys);
static void layer_knob(uint32_t k, int32_t s);
static void fx_map_sync(void);
static void fx_assign(int32_t s);
static void fx_default(void);
static int layer_play(void);
static int layer_set_open(void);
static uint32_t layer_oct(uint32_t pressed, uint32_t oct);
static void layer_oct_open(uint32_t pressed);
static int layer_allowed(void);
static uint32_t ly_bit(uint32_t l);
static void layer_lock_input(uint32_t pressed);

/* a page button let go (they act on release; a layer's own button: layer_gesture). 1: it acted (EDIT on USER,
 * PROJECT) instead of opening a page */
static int page_tap(uint32_t b)
{
    uint32_t f;
    if (b == B_EDIT && !ui.home && (cur_page()->graph == GR_USER || cur_page()->graph == GR_SLOTS)) {
        name_rename();                                  /* SAVE > USER / PROJECT: EDIT renames the slot */
        return 1;
    }
    if (b == B_ARP && BLUEHARE_PLUGIN_GEN) {            /* (Blue Hare) ARP: GEN starts or stops; starting it shows */
        uint32_t i, mix = !gen.on;                      /* GEN MIX (the roles' densities), stopping it GEN */
        gen_toggle();
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].fam == FAM_ARP && (PAGES[i].id[0] == G_GDPAD) == mix)
                break;
        ui.page = (uint8_t)(i < NPAGES ? i : page_first(FAM_ARP));
        ui.fam_last[FAM_ARP] = ui.page;
        ui.home = 0;
        page_entered();
        return 0;
    }
    for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)        /* (GLO SEQ SAVE ..: HOME is ui_input's, home_tap) */
        if (FAM_BTN[f] == b && b != B_HOME) {
            open_family(f);
            return 0;
        }
    return 0;
}

/* messages of things that happened elsewhere (a load, the editor, MIDI in): after this frame's own */
static void ui_notices(void)
{
    static uint32_t midi_t, midi_last;
    if (midi_hint) {                                    /* MIDI notes into a track that is not selected */
        uint32_t h = midi_hint;
        midi_hint = 0;
        if (!ui.msg_t && (h != midi_last || fm1_ms - midi_t > 4000u)) {
            char b[4] = {'T', (char)('0' + h), 0, 0};
            ui_say("MIDI IN -> ", b);
            midi_last = h;
            midi_t = fm1_ms;
        }
    }
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    uint32_t home, save;
    uint32_t oct;
    uint32_t lay, combo = 0, lytap, lkeys, glo, kq = 0;
    int32_t s, sel = 0, ks[4] = {0, 0, 0, 0};
    static uint32_t lock_ms;                            /* BPM LOCK: the last locked SELECT turn (fm1_ms | 1; 0 none) */
    for (k = 0; k < NTRK; k++)                          /* an engine index past the build's any other way (every */
        if (trk[k].eng_req >= NENGINES)                 /* load maps ids): its first engine */
            set_engine_of(&trk[k], 0);
    gen_poll();                                         /* (Blue Hare) GEN: a preset picked loads its sounds (ui.c) */
    perf_latch_on = fx_latch & 1u;                      /* (MENU > FX LATCH; a settings load sets it too) */
    fx_map_sync();                                      /* (the FX layer's key map: ui_layer.c, a settings load too) */
    fx_usb_fixed = (ui_prefs & PREF_USB_FIXED) != 0u;   /* (MENU > USB LEVEL: fx.c, audio.c) */
    scope_mix = (ui_prefs2 & (PREF_SCOPE_MIX >> 16)) != 0u;   /* (MENU > SCOPE: fx.c, audio.c) */
    rp_apply();                                         /* (MENU > CLICK, CLICK LEVEL: click.c) */
    if (!ui.menu)
        usb_serial_apply();                             /* (MENU > USB SERIAL: when the menu has closed) */
    if (scr_input(pressed, notes))                      /* MENU > SCREEN OFF: dark, or the wake gesture: swallowed */
        return;                                         /* (before the buttons' timers: no tap or hold after it) */
    if (mom.n && !mom_down()) {                         /* MOMENTARY: LFO let go (or the menu, a layer ..): back */
        mom_restore();
        ui_message("BACK");
    }
    if (mom.n && ((pressed >> panel.btn[B_OCTUP]) & 1u)) {   /* .. OCT+ with LFO held: kept (no octave) */
        mom.n = 0;
        pressed &= ~(1u << panel.btn[B_OCTUP]);
        ui_message("KEPT");
    }
    home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    save = btn_hold(&ui.save_t0, B_SAVE, now, !ui.menu && !ui.confirm);   /* held: UNDO (ui.c undo_swap) */
    layer_lock_input(pressed);                          /* (#83: a button closes a locked layer) */
    layer_arm(pressed, now);
    layer_oct_open(pressed);                            /* (OCT± with a SET layer's button down: it opens now) */
    oct = oct_taps(pressed, ui.menu || ui.confirm || act_cols() || name_on() || layer_set_open());
    oct = layer_oct(pressed, oct);                      /* (a SET layer's OCT-: put back) */
    layer_masks();                                      /* seq.c: keys pressed with a layer's button are its own */
    lay = layer_held();
    glo = lay && ui.ly == LAYER_GLO;                    /* GLO held: SELECT is the tempo, BPM LOCK or not (#58) */
    if (!layer_allowed()) {
        perf_kill = 1;                                  /* (effects off until their keys are let go) */
        perf_latched = 0;                               /* (FX LATCH: the latched ones and the macros off) */
        perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    }
    else if (!kb_layer)
        perf_kill = 0;
    {   /* a key pressed with the button: a combo (its edge, or the ISR already took it); then not the grid's or a step's */
        static uint32_t kb_seen;
        lkeys = kb_layer & ~kb_seen;
        combo = lay && (notes || lkeys);
        kb_seen = kb_layer;
    }
    if (lay)
        lkeys |= notes & ~fm1_in.notes;                 /* (tapped and let go already) */
    notes &= ~kb_layer;
    if (lay) {                                          /* a layer's button held: keys, knobs and buttons are combos */
        combo |= (pressed & ~ly_bit(ui.ly)) != 0u;
        notes = 0;                                      /* (the keys are the layer's, not the grid's or a step's) */
        if (pressed & (1u << panel.btn[B_SAVE]))       /* no UNDO, no page */
            ui.save_t0 |= 2u;
        if (pressed & (1u << panel.btn[B_HOME]))       /* no menu, no HOME */
            ui.home_t0 |= 2u;
        for (k = 0; k < 4u; k++)                        /* KNOB 1..4: the layer's (ui_layer.c layer_knob) */
            if ((ks[k] = panel_enc(EN_K1 + k)) != 0)
                combo = 1;
        if ((s = panel_enc(EN_PRESET)) != 0 && ui.ly == LAYER_FX) {   /* (a stray turn would load another sound) */
            fx_assign(s);                               /* FX: the white key held takes the next effect */
            combo = 1;
        }
        if (ui.ly == LAYER_FX && ((pressed >> panel.btn[B_EDIT]) & 1u))
            fx_default();                               /* .. EDIT: its default back */
        panel_enc(EN_ALGO);                             /* (another track: OCT- puts back the layer's track only) */
        if (glo && (sel = panel_enc(EN_SELECT)) != 0)   /* GLO + SELECT: the tempo, a combo (OCT- puts it back) */
            combo = 1;
    } else if ((kq = (uint32_t)layer_knobs_quiet()) != 0) {   /* a layer letting go: KNOB 1..4 are nobody's (#39) */
        for (k = 0; k < 4u; k++)
            if (panel_enc(EN_K1 + k) != 0)
                combo = 1;                              /* (with the button let go this frame: no tap) */
    }
    lytap = layer_gesture(now, combo);
    layer_show();
    layer_keys(lkeys);
    for (k = 0; k < 4u; k++)
        if (ks[k]) {
            layer_knob(k, ks[k]);
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
    song.keys_mute = (uint8_t)keys_mode();            /* (the menu, a dialog: the keys play again; NAME: silent) */
    if (home == BT_TAP && ui.menu) {                    /* HOME (pressed, or held) closes the menu, from ABOUT too; */
        menu_close();                                   /* the release of the hold that opened it is no tap */
        home = BT_NONE;                                 /* (menu_close went HOME already) */
    } else if (home == BT_HOLD) {                       /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            ui.menu = 1;                                /* (at the tab and row it was left at) */
            ui.confirm = 0;                             /* (a clear dialog is cancelled, NAME too) */
            name_close();
            ui.force = 1;
        }
    }
    if (ui.menu) {                                      /* SAVE taps do nothing here (HOME: above) */
        if (ui.save_t0)
            ui.save_t0 |= 2u;
        ui.pg_down = 0;
        if (!ui.home_t0)
            menu_input(oct);
        return;
    }
    if (name_on() && !ui.confirm) {                     /* NAME: the keys type, KNOB 1 / 2, OCT+ / OCT- (ui_name.c); */
        if (ui.save_t0)                                 /* SAVE does nothing */
            ui.save_t0 |= 2u;
        if (((pressed >> panel.btn[B_PLAY]) & 1u) && song.playing)
            transport_req = 2;                          /* PLAY stops a transport started meanwhile (MIDI Start, the
                                                         * editor) so the name can be saved; it never starts one */
        ui.pg_down = 0;
        name_input(notes, oct);
        return;
    }
    if (save == BT_HOLD)                                /* SAVE held: undo the last sound load */
        undo_swap();
    else if (save == BT_TAP && !ui.confirm)             /* SAVE acts on release (a hold is the undo) */
        open_family(FAM_SAVE);
    if (ui.confirm) {                                   /* OCT- cancels, OCT+ does it; nothing else reacts */
        if (oct & 2u) {
            uint32_t kind = ui.confirm;
            ui.confirm = 0;
            ui.force = 1;
            if (kind == CF_OVR_PROJ) {                  /* overwrite: the NAME screen writes it */
                name_open(NK_PROJ_SAVE, ui.confirm_trk & 3u);
            } else if (kind == CF_OVR_USER) {
                name_open(NK_USER_SAVE, ui.confirm_trk);
            } else if (kind == CF_ERASE_USER) {
                up_ui(1u, ui.confirm_trk);
            } else if (kind == CF_INIT_SOUND) {      /* engine defaults + its first preset */
                set_engine(TSEL->eng_req); ui_message("SOUND INIT");
            }
        } else if (oct & 1u) {
            ui.confirm = 0;
            ui.force = 1;
        }
        ui.pg_down = 0;
        enc_drop();
        return;
    }
    if (lytap)                                          /* a layer's button acts on release (held: the layer) */
        layer_tap(lytap);
    if (home == BT_TAP)                                 /* HOME acts on release: a hold opens the menu */
        home_tap();
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            if (layer_play())                           /* (GLO held: RESTART) */
                break;
            transport_req = song.playing ? 2u : 1u;
            break;
        case B_REC:                                     /* (nothing: there is no recording yet) */
        case B_SAVE:
        case B_FX:                                      /* the layers' buttons: ui_layer.c */
        case B_GLO:
        case B_SCL:
        case B_EDIT:
        case B_HOME:
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if (act_cols() || layer_set_open())         /* action pages: enter / back (below); SET layers: OCT- */
                break;
            if ((fm1_in.buttons & both) == both)
                song.octave = 0;
            else
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            break;
        }
        default:                                        /* page buttons (GLO SCL ENV LFO EDIT ARP SEQ): when let go */
            if (!lay)                                   /* (with FX held: swallowed) */
                ui.pg_down |= (uint16_t)(1u << id);
            break;
        }
    }
    b = ui.pg_down & ~fm1_in.buttons;
    ui.pg_down &= (uint16_t)~b;
    for (id = 0; b; id++, b >>= 1)
        if (b & 1u)
            page_tap(panel_btn_of(id));
    if (act_cols() && (oct & 2u)) {                     /* action pages: OCT+ does the picked action, */
        act_do();
    } else if (act_cols() && (oct & 1u)) {              /* OCT- drops it, or (none picked) goes HOME */
        if (ui.act)
            ui.act = 0;
        else
            go_home();
    }
    song.keys_mute = (uint8_t)keys_mode();            /* (seq.c: NAME's keys are silent) */

    /* #102: the knobs a layer took above are not read again here. The TIMER5 scan (10 kHz) counts detents while this
     * pass runs, so one landing after the layer's read went to the page as well: KNOB 1..4 edited the layer's control
     * and the page under it (HOME's sound), PRESETS could load a sound. They stay for the next pass */
    kq |= lay;
    if (!lay && (s = panel_enc(EN_PRESET)) != 0)
        presets_turn(s);
    if (!lay && (s = panel_enc(EN_ALGO)) != 0)     /* ALGORITHM: the selected track, on every page */
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
    if ((s = glo ? sel : panel_enc(EN_SELECT)) != 0) {   /* SELECT knob = global tempo; */
        if (glo || !(ui_prefs & PREF_BPM_LOCK)) {
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
            ui.bpm_t = 40;                              /* the header's BPM lights up; no message over the header */
        } else {                                        /* MENU > BPM LOCK ON (#58): only with GLO held (and on HOME >
                                                         * CLOCK, GLO's F4 TAP); a turn burst says so once */
            if (!lock_ms || fm1_ms - lock_ms > 1000u)
                ui_message("BPM LOCKED");
            lock_ms = fm1_ms | 1u;
        }
    }
    for (k = 0; k < 4u && !kq; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (mom_down())                                 /* (LFO + a knob, MOMENTARY or not: LFO's release opens no page) */
            ui.pg_down &= (uint16_t)~(1u << panel.btn[B_LFO]);
        if (mom_down() && mom_turn(k, s)) {             /* MOMENTARY: LFO held (no undo) */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
            continue;
        }
        if (ui.home || pg->scope == SC_TRK || page_desc(pg, k, &hv) ||
            ((pg->graph == GR_USER || pg->graph == GR_MOD) && k == 0u)) {   /* (not an empty column) */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            *vp = (int16_t)param_turn(d, *vp, accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min));
        } else {
            edit_param(k, s);
        }
    }
    ui_notices();
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot). A piano key cancels at
 * any time: the keys are not part of what is taught, so one works whatever the buttons' table says */
#define SETUP_IDLE_MS 30000u
#define SETUP_GIVE_UP(t0) (fm1_ms - (t0) > SETUP_IDLE_MS || fm1_in.notes != 0u)
static void setup_title(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    {   /* the title with its icon (the menu row's), centred together; M from y 8 as before */
        const char *t = "HARDWARE CALIBRATION";
        int32_t x = (240 - (16 + 6 + text_w(&AF_M, t))) / 2;
        cv_begin(240, 24, T_BG);
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(2), "calibration title centred");
        GFX_HOOK_ALIGN(0, HEAD_MY + AF_M_CAP_Y, 0, HEAD_MY + AF_M_CAP_Y + AF_M_CAP_H, AL_V | AL_PASS,
                       "header icon on its title's line");
        cv_icon_mid(x, 12, 16, ICON_X_DOCTOR, T_THEME, T_BG);
        cv_text(x + 22, HEAD_MY, &AF_M, t, T_TEXT);
        cv_blit(0, 5);
    }
    draw_text_box(0, 32, 240, &AF_S, "TEACH EACH BUTTON AND KNOB", T_MID, 1);
    lcd_fill(16, 56, 208, 1, T_LINE);
    draw_text_box(0, 206, 240, &AF_S, "ANY KEY: CANCEL", T_DIM, 1);
}
static void setup_show(const char *what, const char *name)     /* "PRESS" / "TURN RIGHT", the control */
{
    draw_text_box(0, 80, 240, &AF_S, what, T_MID, 1);
    draw_text_box(0, 100, 240, &AF_L, name, T_THEME, 1);
}
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    setup_title();
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (SETUP_GIVE_UP(t0))
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        setup_show("PRESS", B_NAME[i]);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (SETUP_GIVE_UP(t0))
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        setup_show("TURN RIGHT", E_NAME[i]);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (SETUP_GIVE_UP(t0))
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}
