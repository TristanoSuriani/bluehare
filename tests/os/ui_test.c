/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* Host test of the UI (firmware/os/ui*.c with the sound sources of hostsim.c;
 * the display and the HAL are stubs, the buttons and knobs are driven through them):
 *   SOUNDS     a sound load (PRESETS knob / page, TOOLS INIT, user preset) changes the sound only: SCL,
 *              the SLICER and the mix stay the track's.
 *   UNDO       a load keeps a copy of the track's sound; SAVE held swaps it back (again: redo); browsing keeps
 *              the copy from before the first load; power-on and projects: none.
 *   REC        nothing for now (no recording): a tap on every screen changes nothing, never starts PLAY.
 *   MIDI IN    MENU > MIDI > MIDI IN (ROUT): CH1-4 (channels 5..16 ignored) / SEL, note-offs follow their note-ons.
 *   SAVE       refused while playing ("STOP TO SAVE"); a used slot asks OVERWRITE? (OCT+ / OCT-).
 *   ACTIONS    USER, PROJECT, TOOLS: the knobs pick, OCT+ does it, OCT- cancels or goes
 *              HOME, on release, never both together; the OCT LEDs (OCT- lit, OCT+ blinks).
 *   TRACKS     the PRESETS knob does nothing there; KNOB 1 is MUTE.
 *   ROLL       the rolling digits of the card values and the header BPM: strips only, the last frame static,
 *              direction, retarget, the snaps (fast turn, names, shape, page, track, palette, force).
 *   SOAK       #63: 150000 random frames (turns, pages, transport, REC, wrapping clocks): no roll
 *              outlives ROLL_FRAMES, the cards always equal a forced redraw once the rolls are over.
 *   FM6 OPS    (1.2, #76) EDIT > OPERATOR, OP ENV, OPERATOR 2 on an FM6 track: KNOB 1 the operator, each of the 84 bytes
 *              the pages edit written alone, SLOT OWN, the sound following, one undo per gesture, the macros on top,
 *              projects and user presets keep the patch; FM6 only.
 *   USB SERIAL #67: MENU > USB SERIAL ON (default, the descriptors as before) / OFF (audio + MIDI only, class 0):
 *              the menu, the settings, the descriptor at boot (built with the console, as the firmware: BLUEHARE_CDC).
 *   SCREEN OFF (1.1.5): MENU > DISPLAY > SCREEN OFF against a simulated clock and the panel's commands (lcd_power):
 *              dark after the time, nothing drawn, any input wakes it (SLPOUT, 120 ms, redraw, DISPON) and is
 *              swallowed; dialogs / UPDATE MODE keep it on; saved; the sound bit for bit while dark.
 * Built and run by tests/run_tests.sh. */
#ifndef BLUEHARE_CDC
#define BLUEHARE_CDC 1                                    /* (as src/bluehare.c: the console built in, MENU > USB SERIAL) */
#endif
#include <stddef.h>
#include <stdint.h>
#define main hostsim_main
#include "../lib/hostsim.c"
#undef main

/* ------------------------------------------------------------ HAL stubs --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL];
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL], fm1_led_mid[FM1_NCOL];
static uint32_t host_dim_lo;                      /* hal/fm1_input.h: the glow MENU > LEDS asked for (1: DIM LO) */
static void fm1_led_dim_level(uint32_t lo) { host_dim_lo = lo; }
static int host_anim;                             /* hal/fm1_input.h: the power-on sweep running (it has the LEDs) */
static int host_idle, host_idle_glow;             /* .. the idle animation (1.2: MENU > ANIM IDLE) running, its glow */
static int fm1_led_anim_on(void) { return host_anim || host_idle; }
static void fm1_led_idle_start(uint32_t glow) { host_idle = 1; host_idle_glow = (int)glow; }
static int fm1_led_idle_on(void) { return host_idle; }
static void fm1_led_anim_stop(void) { host_anim = host_idle = 0; }
#define FM1_TICKS_PER_US 1u
static uint32_t host_ticks, host_pressed, host_notes;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return host_ticks; }
static uint32_t fm1_input_edges(int x) { uint32_t p = host_pressed; (void)x; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_notes; host_notes = 0; return n; }
/* host_enc_late: detents the TIMER5 scan counts just after a read (#102: while ui_input runs); the next read has them */
static int32_t host_enc_late[7];
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = host_enc[e % 7u];
    host_enc[e % 7u] = host_enc_late[e % 7u];
    host_enc_late[e % 7u] = 0;
    return s;
}
static void (*host_wdt_hook)(void);                /* (a test's: time passing in a blocking loop, as panel_setup's) */
static void fm1_wdt_feed(void) { if (host_wdt_hook) host_wdt_hook(); }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static uint16_t host_screen[240 * 240];
/* #181 a blank frame: while host_fl_on, every write of a frame is watched (host_fl_begin .. host_fl_end); a pixel that
 * shows, between two writes, a value that is neither the one before the frame nor the one after (a fill of the
 * background before a canvas lands over it) flashed */
static uint8_t host_fl_on, host_fl_n[240 * 240];
static uint16_t host_fl_was[240 * 240], host_fl_mid[240 * 240];
static void host_put(uint32_t k, uint16_t v)
{
    if (host_fl_on) {
        if (!host_fl_n[k])
            host_fl_was[k] = host_screen[k];
        else if (host_screen[k] != host_fl_was[k] && host_fl_n[k] < 2u)
            host_fl_mid[k] = host_screen[k], host_fl_n[k] = 2;   /* (2: an in-between value kept) */
        if (!host_fl_n[k])
            host_fl_n[k] = 1;
    }
    host_screen[k] = v;
}
static void host_fl_begin(void) { memset(host_fl_n, 0, sizeof host_fl_n); host_fl_on = 1; }
static uint32_t host_fl_end(uint32_t y0, uint32_t y1)   /* the rows y0..y1-1: pixels that flashed */
{
    uint32_t k, n = 0;
    host_fl_on = 0;
    for (k = y0 * 240u; k < y1 * 240u; k++)
        n += host_fl_n[k] == 2u && host_fl_mid[k] != host_screen[k];
    return n;
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
#ifdef UI_FILL_HOOK
    UI_FILL_HOOK(x, y, w, h);                     /* (tests/os/ui_render.c: what a fill covers is gone) */
#endif
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_put((y + j) * 240u + x + i, (uint16_t)((c >> 8) | (c << 8)));
}
static void lcd_sync(void) {}
/* MENU > SCREEN OFF (lcd.c lcd_power / lcd_wake_now): what the panel was told, in order (0 off: DISPOFF, SLPIN;
 * 1 SLPOUT; 2 DISPON; 3 lcd_wake_now), with the time; host_bl the panel showing (the backlight itself stays on: PA2 is
 * the key matrix's /OE too, lcd.c lcd_power) */
static uint32_t host_pw_n, host_pw[256], host_pw_ms[256];
static int host_bl = 1;
static void host_pw_log(uint32_t s)
{
    if (host_pw_n < 256u) { host_pw[host_pw_n] = s; host_pw_ms[host_pw_n] = fm1_ms; }
    host_pw_n++;
    host_bl = s != 0u;
}
static void lcd_power(uint32_t s) { host_pw_log(s); }
static void lcd_wake_now(void) { host_pw_log(3u); }
/* (the HOME oscilloscope's buffer, scope_buf: fx.c, with hostsim.c) */
static uint32_t host_blit_rows;                    /* rows blitted (what a frame draws) */
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    host_blit_rows += h;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_put((y + j) * 240u + x + i, p[j * w + i]);
}
static struct { uint32_t stage; } bluehare_dbg;
#define BLUEHARE_FLASH 0
#ifndef BLUEHARE_VERSION
#define BLUEHARE_VERSION "TEST"
#endif
#include "../../firmware/os/gfx.c"
#include "../../firmware/os/panel.c"
#include "../../firmware/os/ui.c"
#include "../../firmware/os/menu_items.c"
#include "../../firmware/os/icons.c"
#include "../../firmware/os/ui_graph.c"
#include "../../firmware/os/ui_draw.c"
#include "../../firmware/os/ui_menu.c"
#include "../../firmware/os/ui_input.c"
#include "../../firmware/os/ui_layer.c"
#include "../../firmware/os/upreset.c"
#include "../../firmware/os/project.c"

/* the bottom of track parameter id on the selected track (an EDIT value: its engine's) */
#define TP_MIN_OF(id) ((id) >= P_E0 ? ENGINES[TSEL->eng_req]->edit[(id) - P_E0].min : TP[id].min)
static int check(const char *what, int ok)
{
    printf("ui: %-74s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* (Blue Hare) the sounds these tests power on with: the layout they were written against (a bass on track 1, the
 * keys on 2, a lead on 3, the drums on 4: MODAL's WOOD DRUM since the kits were archived; 5..8 a chord, a pluck, a bell,
 * strings), every pan centred. The device's own power-on (engines.c TRK_DEF, TRK_PAN) is engines_test.c's to check */
static const uint8_t UT_DEF[NPART][2] = {TRK_PICK(SID, 0), TRK_PICK(WAVE, 0), TRK_PICK(SID, 2), TRK_PICK(MODAL, 7)
#if NPART == 8
                                         , TRK_PICK(WAVE, 0), TRK_PICK(VOSIM, 14), TRK_PICK(MODAL, 2), TRK_PICK(WAVE, 3)
#endif
};
/* main.c bluehare_init, with these tests' sounds (UT_DEF) */
static void ui_power_on(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    trk_mix_defaults();                           /* (nothing playing) */
    seq_hook = 0;                                 /* (the sequencer: a placeholder; no test song) */
    memset(&tc, 0, sizeof tc);                    /* (TECHNO stopped, as at power-on: ARP started it in a test before) */
    memset(&ui, 0, sizeof ui);
    page_over = 0;                                /* (no layer's knobs; roll_settle's test page gone) */
    memset(&nm, 0, sizeof nm);                    /* NAME closed, no project name */
    memset(&mom, 0, sizeof mom);                  /* MOMENTARY: no value held (the pool at power-on) */
    proj_name[0] = 0;
    proj_cur = PROJ_NO_SLOT;
    memset(&favorites, 0, sizeof favorites);
    ui_prefs2 |= (uint8_t)(PREF_EXPERT >> 16);    /* (2026-10-17) EXPERT ON: every page, the layout these tests were
                                                   * written against; the plain pages' tests turn it off */
    memset(&undo, 0, sizeof undo);
    memset(proj_ram_len, 0, sizeof proj_ram_len);       /* (the song store: empty, RAM in these builds) */
    memset(&proj_good, 0, sizeof proj_good);
    memset(up_bank, 0, sizeof up_bank);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;
    rng_state = 0x1234567u;                       /* (libc.c: the random numbers from their power-on seed) */
    lim_env = LIM_T; bc_env = 0; bc_g = 32768;    /* (fx.c: the master limiter at rest, the effects silent, as at */
    rev_clear();                                  /* power-on: no tail of the test before sounds into this one) */
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(wside, 0, sizeof wside);
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, UT_DEF[i][0]);
        apply_preset_to(t, UT_DEF[i][1]);
        t->engine = t->eng_req;
    }
    undo_depth--;
    song.sel = 0;
    ui.home = 1;
    ui.force = 1;
    panel = PANEL_DEFAULT;
    settings.palette = UI_GREY_INDEX;
    palette_set(UI_GREY_INDEX);
    transport_req = 0;
    panic_req = 0;
    mi_r = mi_w = midi_in_overflow = 0;
    memset(midi_sel_on, 0, sizeof midi_sel_on);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(kb_chn, 0, sizeof kb_chn);                   /* chord.c: no key, MIDI chord or last chord */
    memset(mchord, 0, sizeof mchord);
    memset(chord_last, 0, sizeof chord_last);
    perf_held = perf_act = kb_layer = perf_mask = 0;      /* the FX layer: nothing held */
    perf_latched = 0;
    memcpy((void *)perf_map, PF_DEF, sizeof PF_DEF);    /* .. its default key map (the settings' 0), no key's effect */
    perf_remap = 0;
    memset(kb_fx, 0, sizeof kb_fx);
    perf_kill = 0;
    perf_mask = 1u << panel.btn[B_FX];                  /* (ui_input sets them every frame) */
    kb_mask = layer_bits();
    perf_solo = 0;
    memset(&lys, 0, sizeof lys);
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    settings_hold = HOLD_DEF;
    fm1_in.buttons = fm1_in.notes = 0;
    memset(&scrn, 0, sizeof scrn);                        /* MENU > SCREEN OFF: on, idle from now (main.c) */
    scrn.idle = fm1_ms;
    kb_asleep = 0;
    panel_moved = 0;
}

/* one UI frame (input, then the drawing on the stub display) */
static void frame(void)
{
    host_ticks += 16000u;
    fm1_ms += 16u;
    ui_input();
    ui_draw();
}
static void press(uint32_t label)                 /* a tap: down, a frame, up, a frame */
{
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    frame();
}
static void frames(uint32_t ms) { uint32_t i; for (i = 0; i < ms / 16u; i++) frame(); }
static void btn_down(uint32_t label) { fm1_in.buttons |= 1u << panel.btn[label]; host_pressed |= 1u << panel.btn[label]; }
static void btn_up(uint32_t label) { fm1_in.buttons &= ~(1u << panel.btn[label]); }
static void hold(uint32_t label)                  /* held 0.8 s, then let go */
{
    uint32_t k;
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    for (k = 0; k < 52u; k++)
        frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    frame();
}
static void turn(uint32_t role, int32_t s)
{
    host_enc[panel.enc[role]] += s * panel.dir[role];
    frame();
    host_ticks += 200000u;                        /* (no knob acceleration between the turns) */
}
static void stop_transport(void) { transport_req = 0; song.playing = 0; }
/* parameter id of track `track` as the stored song holds it (project.c's stream, walked; -32768: none stored) */
static int16_t stored_param(uint32_t slot, uint32_t track, uint32_t id)
{
    pr_t r;
    uint32_t i, k, ng, e = 0, n, ver;
    int16_t v = -32768;
    (void)slot;
    if (!proj_find())
        return -32768;
    pr_open(&r, proj_get, proj_good.count);
    if (pr_next(&r))
        return -32768;
    ver = pr_u8(&r);
    (void)pr_u8(&r);
    ng = pr_u8(&r);
    for (i = 0; i < ng; i++)
        (void)pr_i16(&r);
    (void)pr_u8(&r);
    for (i = 0; i < PROJ_NAME_LEN; i++)
        (void)pr_u8(&r);
    if (ver >= 3u)
        (void)pr_u8(&r);                                /* (version 3: the track count) */
    for (k = 0; k <= track; k++) {
        e = pr_u8(&r);
        (void)pr_u8(&r);
        v = param_desc_of(e % NENGINES, id)->def;
        n = pr_u8(&r);
        for (i = 0; i < n; i++) {
            uint32_t pid = pr_u8(&r);
            int16_t pv = (int16_t)pr_i16(&r);
            if (pid == id)
                v = pv;
        }
        (void)pr_u8(&r);                         /* (the FM6 patch flag: 0) */
    }
    return r.err ? -32768 : v;
}
static int msg_is(const char *s) { return ui.msg_t && str_eq(ui.msg, s); }
static void go_page(uint32_t graph)               /* the page that draws graph */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == graph)
            break;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
/* the sound of a track: engine, preset and the parameters that are not the track's own (param_kept) */
static int same_sound(const track_t *a, const track_t *b)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i) && a->p[i] != b->p[i])
            return 0;
    return a->eng_req == b->eng_req && a->preset == b->preset;
}


/* a sound load: the sound only; the scale, the SLICER and the mix are the track's */
static int test_sound_loads(void)
{
    int bad = 0;
    uint32_t i;
    track_t *t = &trk[0], before;
    ui_power_on();
    bad += check("power-on: the four sounds, no undo copy", undo.trk == 0 && undo_depth == 0 &&
                 trk[3].eng_req == ENGI_MODAL && trk[3].preset == UT_DEF[3][1] && trk[0].preset == UT_DEF[0][1]);
    t->p[P_E0 + 1] = 77;                          /* a sound edit */
    t->p[P_SCALE] = 2;                            /* and the track's own settings */
    t->p[P_ROOT] = 3;
    t->p[P_TRANS] = 5;
    t->p[P_SDIV] = 1;
    t->p[P_LEVEL] = 90;
    before = *t;
    turn(EN_PRESET, 1);                           /* HOME: the next preset */
    bad += check("PRESETS on HOME loads the sound (DIV, kept in the formats, untouched)",
                 t->preset == (uint8_t)((before.preset + 1u) % ENGINES[0]->npresets) && t->p[P_SDIV] == 1);
    bad += check("..SCL and the mix stay the track's", t->p[P_ROOT] == 3 && t->p[P_SCALE] == 2 &&
                 t->p[P_TRANS] == 5 && t->p[P_LEVEL] == 90);
    i = ENGINES[0]->npresets - 2u;                /* (ANALOG's RAVE until Blue Hare: a preset near the end) */
    t->preset = (uint8_t)(i - 1u);
    turn(EN_PRESET, 1);
    bad += check("a factory preset loads", t->preset == i);
    t->p[P_TRANS] = 7;
    before = *t;
    for (i = 0; i < 6u; i++)                      /* several loads, into the next engine */
        turn(EN_PRESET, 1);
    bad += check("browsing on, into another engine: TRN still the track's", t->eng_req != before.eng_req &&
                 t->p[P_TRANS] == 7);
    bad += check("browsing keeps the copy from before the first load", undo.trk == 1u &&
                 undo.p[P_E0 + 1] == before.p[P_E0 + 1] && undo.eng == before.eng_req && undo.preset == before.preset);
    hold(B_SAVE);
    bad += check("SAVE held: the sound back (UNDO/REDO T1)", same_sound(t, &before) && msg_is("UNDO/REDO T1"));
    bad += check("SAVE held does not open the SAVE pages", ui.home);
    hold(B_SAVE);
    bad += check("SAVE held again: the loads again (redo)", t->eng_req != before.eng_req);
    hold(B_SAVE);
    t->p[P_LEVEL] = 50;                           /* a mix change after the undo */
    turn(EN_PRESET, 1);                           /* a load after an undo copies the track as it is now */
    hold(B_SAVE);
    bad += check("a load after the undo: its own copy; the mix (LEVEL) is never swapped", same_sound(t, &before) && t->p[P_LEVEL] == 50);
    press(B_SAVE);
    bad += check("SAVE tap opens the SAVE pages (on release)", !ui.home && cur_page()->fam == FAM_SAVE);
    /* TOOLS INIT, user preset load: the sound only, undoable; one copy for all tracks */
    ui_power_on();
    trk[1].p[P_ROOT] = 4;
    track_select(1);
    set_engine(trk[1].eng_req);                   /* TOOLS INIT */
    bad += check("TOOLS INIT: the sound only (ROOT kept), a copy of T2", undo.trk == 2u && trk[1].p[P_ROOT] == 4);
    track_select(0);
    trk[0].p[P_SDIV] = 2;
    up_store(3, "MINE");
    {
        uint32_t k, none = 1;
        for (k = 0; k < 16u; k++)
            none &= !up_rec(3)->note[k];
        bad += check("a user preset stores an empty pattern (the record's format unchanged)", up_used(3) && none);
    }
    trk[0].p[P_SDIV] = 0;
    before = trk[0];
    up_load(3);
    bad += check("a user preset load: the sound only (DIV kept); the copy is now T1's", trk[0].user == 4u && undo.trk == 1u &&
                 trk[0].p[P_SDIV] == 0);
    undo_swap();
    bad += check("undo of a user preset load", trk[0].user == 0);
    /* project load: no copy, and the old one is gone */
    project_save(1);
    up_load(3);
    project_load(1);
    bad += check("a project load drops the copy and takes none", undo.trk == 0 && undo_depth == 0);
    ui_power_on();


    printf("ui: undo copy %u bytes\n", (unsigned)sizeof undo);
    return bad;
}


static uint32_t gated_notes(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) n += t->v[i].gate != 0;
    return n;
}

static int test_midi(void)
{
    int bad = 0;
    track_t *a, *b;
    ui_power_on();
    song.sel = 2;
    midi_hint = 0;
    midi_event(0x90, 0, 60, 100); midi_event(0x90, 9, 61, 100);
    bad += check("ROUT CH1-4 (default): channel 1 -> part 1, channel 10 ignored",
                 song.g[G_ROUTE] == 0 && midi_sel_on[0][60] == 1u && !midi_sel_on[9][61] && !gated_notes(&trk[2]));
    bad += check("a note into another track tells the UI", midi_hint == 1u);
    frame(); bad += check("MIDI IN -> T1 shown", msg_is("MIDI IN -> T1"));
    song.g[G_ROUTE] = 1;
    midi_event(0x80, 0, 60, 0); midi_event(0x90, 0, 62, 100);
    bad += check("ROUT changes: note-off releases original owner, new note uses selection",
                 !gated_notes(&trk[0]) && midi_sel_on[0][62] == 3u && midi_track(3) == &trk[2]);
    song.sel = 1; midi_event(0x80, 0, 62, 0);
    bad += check("note-off after selection follows the note-on's track", !midi_sel_on[0][62]);
    midi_event(0x80, 9, 61, 0);
    ui_power_on();
    song.g[G_ROUTE] = 1;
    midi_event(0x90, 0, 60, 100);
    song.sel = 1;
    midi_event(0x90, 0, 60, 100);
    bad += check("same MIDI channel/pitch on a new selected track releases its old owner",
                 !gated_notes(&trk[0]) && gated_notes(&trk[1]) && midi_sel_on[0][60] == 2u);
    midi_event(0x80, 0, 60, 0);
    midi_event(0x80, 0, 60, 0);
    bad += check("both note-offs after a MIDI ownership change leave neither track held",
                 !gated_notes(&trk[0]) && !gated_notes(&trk[1]) && !midi_sel_on[0][60]);
    ui_power_on();
    song.sel = 2; song.g[G_ROUTE] = 1;
    midi_event(0x90, 0, 61, 100);
    song.g[G_ROUTE] = 0;
    midi_event(0x90, 0, 61, 100);
    midi_event(0x80, 0, 61, 0);
    bad += check("same channel/pitch across a ROUT change cannot leave its previous part held",
                 !gated_notes(&trk[2]) && !gated_notes(&trk[0]) && !midi_sel_on[0][61]);
    ui_power_on();
    midi_event(0x90, 0, 60, 100);
    midi_event(0x90, 1, 64, 100);
    song.playing = 1;
    {
        uint32_t i, held = 0;
        for (i = 0; i < MQ; i++) midi_in_event(0x643C9909u);
        midi_in_event(0x003C8908u);                      /* note-off lost to a full ring */
        events_block(CTL);
        for (i = 0; i < NTRK * NVOICE; i++) held += trk[i / NVOICE].v[i % NVOICE].gate;
        bad += check("MIDI overflow releases held notes, keeps transport running",
                     !held && !midi_sel_on[0][60] &&
                     !midi_sel_on[1][64] && !midi_in_overflow && mi_r == mi_w && song.playing);
        midi_in_event(0x643D9009u);
        events_block(CTL);
        bad += check("MIDI accepts a fresh note after overflow recovery", midi_sel_on[0][61] == 1u && trk[0].v[0].gate);
    }
    {   /* 1.2: MENU > MIDI > MIDI IN (up to 1.1.5 GLO > SYSTEM K3 ROUT): the project's G_ROUTE, its names ROUT's */
        uint32_t k;
        int16_t r0 = song.g[G_ROUTE];
        int ok = MI_TAB[MI_MIDIIN] == MTAB_MIDI && str_eq(MI_NAME[MI_MIDIIN], "MIDI IN") &&
                 menu_n(MI_MIDIIN) == (uint32_t)GP[G_ROUTE].max + 1u;
        for (k = 0; k < menu_n(MI_MIDIIN); k++) {
            menu_put(MI_MIDIIN, k);
            ok &= song.g[G_ROUTE] == (int16_t)k && menu_get(MI_MIDIIN) == k && str_eq(menu_vname(MI_MIDIIN, k), GP[G_ROUTE].names[k]);
        }
        menu_put(MI_MIDIIN, 1u);
        ok &= str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), "SEL");
        song.g[G_ROUTE] = r0;
        bad += check("MENU > MIDI > MIDI IN: CH1-4 / SEL / CH5-8 .. CH13-16, the project's ROUT (G_ROUTE)", ok);
    }
    return bad;
}

static int test_save(void)
{
    int bad = 0;
    uint32_t i;
    ui_power_on();
    trk[0].p[P_E0] = 5;
    press(B_SAVE);
    bad += check("SAVE opens USER with SAVE selected, without writing a slot",
                 !ui.home && cur_page()->graph == GR_USER && ui.act == 4u && !up_used(0));
    press(B_OCTUP);
    bad += check("SAVE then OCT+ opens NAME (the automatic name), writes nothing yet",
                 name_on() && nm.kind == NK_USER_SAVE && str_eq(nm.s, nm.ph) && nm.cur == nm.len && !up_used(0));
    press(B_OCTUP);
    {
        char nmb[16], au[16];
        up_name(0, nmb);
        up_auto_name(au, trk[0].eng_req, 0);
        bad += check("..OCT+ again stores the edited sound with that name (SAVE, OCT+, OCT+)",
                     !name_on() && up_used(0) && up_value(up_rec(0), P_E0) == 5 && ui.act == 0u && str_eq(nmb, au));
    }
    press(B_SAVE);
    bad += check("1.5: SAVE again: PROJECT (the song; PHRASES gone with the steps)", cur_page()->graph == GR_SLOTS);
    press(B_SAVE);
    bad += check("SAVE again still cycles to TOOLS", cur_page()->graph == GR_TOOLS);
    go_home();
    press(B_SAVE);
    bad += check("SAVE from HOME returns directly to USER, even after visiting PROJECT",
                 cur_page()->graph == GR_USER && ui.act == 4u);
    press(B_OCTUP);
    bad += check("direct SAVE still asks before overwriting an occupied slot",
                 ui.confirm == CF_OVR_USER && up_value(up_rec(0), P_E0) == 5);
    press(B_OCTDN);
    ui_power_on();
    song.playing = 1;
    project_save(0);
    bad += check("project save while playing: refused, STOP TO SAVE", !project_used(0) && msg_is("STOP TO SAVE"));
    bad += check("user preset save while playing: refused", up_store(0, "X") == 2 && !up_used(0));
    song.playing = 0;
    transport_req = 1;
    project_save(0);
    bad += check("a pending PLAY cannot save a project", !project_used(0) && msg_is("STOP TO SAVE"));
    bad += check("a pending PLAY cannot save a user preset", up_store(0, "X") == 2 && !up_used(0));
    transport_req = 0;
    song.playing = 1;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_SLOTS)
            break;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
    ui.msg_t = 0;
    turn(EN_K4, 1);                               /* SAVE picked */
    bad += check("PROJECT KNOB 4 picks SAVE, saves nothing", ui.act == 4u && !project_used(0) && !ui.msg_t);
    press(B_OCTUP);
    bad += check("OCT+ while playing: STOP TO SAVE, no NAME", msg_is("STOP TO SAVE") && !project_used(0) &&
                 song.g[G_SAVE] == 0 && !name_on());
    stop_transport();
    turn(EN_K4, 1);
    press(B_OCTUP);
    bad += check("stopped, no song stored: OCT+ opens NAME (no name yet: SONG)", name_on() && nm.kind == NK_PROJ_SAVE &&
                 nm.len == 0u && str_eq(nm.ph, "SONG") && !project_used(0));
    press(B_OCTUP);
    bad += check("..OCT+ saves it unnamed, SAVE dropped", !name_on() && project_used(0) && msg_is("SAVED (RAM)") &&
                 ui.act == 0u);
    turn(EN_K4, 1);
    press(B_OCTUP);
    bad += check("a used slot: the OVERWRITE? dialog", ui.confirm == CF_OVR_PROJ && ui.confirm_trk == 0);
    trk[0].p[P_E0] = 5;
    press(B_OCTDN);
    bad += check("OCT- keeps the slot", ui.confirm == CF_NONE && stored_param(0, 0, P_E0) != 5);
    turn(EN_K4, 1);
    press(B_OCTUP);
    press(B_OCTUP);
    bad += check("OCT+ (YES): NAME, the slot not written yet", name_on() && stored_param(0, 0, P_E0) != 5);
    press(B_OCTUP);
    bad += check("OCT+ overwrites it", ui.confirm == CF_NONE && !name_on() && stored_param(0, 0, P_E0) == 5);
    trk[0].p[P_LEVEL] = 11;
    turn(EN_K3, 1);
    press(B_OCTUP);
    bad += check("KNOB 3 LOAD, OCT+: the project back; LOAD stays picked", trk[0].p[P_LEVEL] != 11 &&
                 trk[0].p[P_LEVEL] == stored_param(0, 0, P_LEVEL) && msg_is("LOADED") && ui.act == 3u);
    /* user presets */
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_USER)
            break;
    ui.page = (uint8_t)i;
    page_entered();
    up_store(4, "OLD");
    ui.uslot = 4;
    turn(EN_K4, 1);
    press(B_OCTUP);
    bad += check("USER SAVE over a used slot: the OVERWRITE? dialog", ui.confirm == CF_OVR_USER && ui.confirm_trk == 4u);
    press(B_OCTUP);
    bad += check("YES: NAME with the sound's name (not the old slot's)", name_on() && !str_eq(nm.s, "OLD"));
    press(B_OCTUP);
    {
        char nm[16];
        up_name(4, nm);
        bad += check("OCT+ stores the sound there", ui.confirm == CF_NONE && !str_eq(nm, "OLD"));
    }
    song.playing = 1;
    ui.msg_t = 0;
    turn(EN_K3, 1);                               /* ERASE */
    press(B_OCTUP);
    bad += check("USER ERASE while playing: STOP TO SAVE", msg_is("STOP TO SAVE") && up_used(4));
    stop_transport();
    return bad;
}

static void go_title(const char *title)
{
    uint32_t i;
    for (i = 0; i < NPAGES && !str_eq(PAGES[i].title, title); i++)
        ;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
static uint32_t oct_leds_seen(int all)            /* over 1 s: the OCT LED bits lit at some point (all: throughout) */
{
    uint32_t k, any = 0, every = 3u | OCT_BREATH;
    for (k = 0; k < 64u; k++) {
        frame();
        any |= oct_leds();
        every &= oct_leds();
    }
    return all ? every : any;
}

/* the action pages: the knobs pick, OCT+ does it, OCT- cancels / goes HOME; the LEDs; chords */
static int test_actions(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    ui_power_on();
    press(B_OCTUP);
    bad += check("HOME: OCT+ shifts the octave (lit)", song.octave == 1 && oct_leds() == 2u);
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];   /* pressed on HOME, let go on TOOLS: nothing done there */
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    go_title("TOOLS");
    turn(EN_K1, 1);
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    bad += check("a press that began elsewhere does nothing on an action page", !ui.confirm && song.octave == 2);
    ui.act = 0;
    go_home();
    /* TOOLS: INIT is picked, not done, by its knob */
    go_title("TOOLS");
    bad += check("TOOLS: nothing picked, OCT+ dark", act_cols() == 1u && act_col() == 0u && oct_leds_seen(0) == 1u);
    turn(EN_K1, 3);
    bad += check("KNOB 1 picks INIT; OCT+ breathes", ui.act == 1u && oct_leds_seen(0) == (1u | OCT_BREATH));
    {
        char a[16], b[16];
        foot_hint(a, b);
        bad += check("..the footer: OCT+ INIT / OCT- CANCEL", str_eq(a, "OCT+ INIT") && str_eq(b, "OCT- CANCEL"));
    }
    turn(EN_K1, -1);
    bad += check("KNOB 1 left drops it", ui.act == 0u);
    turn(EN_K1, 1);
    press(B_OCTDN);
    bad += check("OCT- cancels the pick (still on TOOLS)", ui.act == 0u && !ui.home);
    turn(EN_K1, 1);
    t->p[P_E0] = (int16_t)(t->p[P_E0] + 1);
    press(B_OCTUP);
    bad += check("TOOLS INIT first asks confirmation", ui.confirm == CF_INIT_SOUND);
    press(B_OCTUP);
    bad += check("KNOB 1 INIT, OCT+: SOUND INIT", msg_is("SOUND INIT") && ui.act == 0u);
    press(B_OCTDN);
    bad += check("OCT- with nothing picked: HOME", ui.home);
    /* the dialogs and the menu */
    open_family(FAM_SEQ);
    t->p[P_E0] = (int16_t)(t->p[P_E0] + 1);
    ui.msg_t = 0;                                       /* (the INIT above said SOUND INIT) */
    confirm_open(CF_INIT_SOUND, 0);
    frame();
    bad += check("a dialog: OCT+ breathes, OCT- lit", ui.confirm == CF_INIT_SOUND && oct_leds_seen(0) == (1u | OCT_BREATH) &&
                 oct_leds_seen(1) == (1u | OCT_BREATH));
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    bad += check("..it answers on release, not on the press", ui.confirm == CF_INIT_SOUND && !msg_is("SOUND INIT"));
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    bad += check("..OCT+ let go: done", ui.confirm == CF_NONE && msg_is("SOUND INIT"));
    bad += check("back on SEQ: the octave LEDs again", oct_leds() == 2u);
    return bad;
}

static int test_tracks(void)
{
    int bad = 0;
    track_t before;
    ui_power_on();
    open_family(FAM_TRK);
    before = trk[0];
    turn(EN_PRESET, 1);
    bad += check("#94 MIXER: the PRESETS knob loads the next sound; the mix stays",
                 trk[0].preset != before.preset && trk[0].p[P_LEVEL] == before.p[P_LEVEL] && cur_page()->graph == GR_TRK);
    hold(B_SAVE);
    bad += check("#94 MIXER: SAVE held undoes it", trk[0].preset == before.preset && !memcmp(trk[0].p, before.p, sizeof before.p));
    turn(EN_K4, 1);
    bad += check("MIXER KNOB 4 right: MUTE ON (the selected track stays)", trk[0].p[P_MUTE] == 1 && song.sel == 0);
    turn(EN_K1, -3);
    bad += check("KNOB 1 changes LEVEL only (MUTE stays)", trk[0].p[P_MUTE] == 1 && trk[0].p[P_LEVEL] == before.p[P_LEVEL] - 3);
    turn(EN_K2, 2);
    bad += check("KNOB 2 changes PAN", trk[0].p[P_PAN] == before.p[P_PAN] + 2);
    turn(EN_K4, -1);
    bad += check("MIXER KNOB 4 left: MUTE OFF", trk[0].p[P_MUTE] == 0);
    turn(EN_ALGO, 1);
    bad += check("ALGORITHM selects the track", song.sel == 1);
    return bad;
}

/* key k of colour black (1) / white (0) at place p (seq.c key_place) */
static uint32_t key_at(int black, uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) == black && key_place(k) == p)
            return k;
    return 0;
}
static void tap_key(uint32_t k)                   /* a key down for a frame, then up */
{
    fm1_in.notes |= 1u << k;
    host_notes |= 1u << k;
    frame();
    fm1_in.notes &= ~(1u << k);
    frame();
}


static int test_screen(void)
{
    int bad = 0;
    ui_power_on();
    ui_say("STOP TO SAVE", "");
    ui.force = 1;
    ui_draw();
    bad += check("a message keeps the header's transport and BPM (drawn right of x 80)", ui.msg_t != 0);
    ui.uboot = 3;
    ui.menu = 1;
    ui.force = 1;
    ui_draw();
    bad += check("UPDATE MODE countdown drawn over the menu", ui.force == 0);
    ui.uboot = 0;
    ui.menu = 0;
    return bad;
}

static int test_favorites(void)
{
    int bad = 0;
    uint32_t total, pos;
    track_t before;
    ui_power_on();
    TSEL->p[P_TRANS] = 2;
    go_page(GR_BROWSE);
    before = *TSEL;
    turn(EN_K3, 1);
    turn(EN_K4, 1);
    pos = preset_pos(&total);
    bad += check("FAV and LIST mark/filter the current sound without loading it",
                 preset_favorite() && favorites.filter && total == 1 && pos == 0 &&
                 !memcmp(TSEL, &before, sizeof before));
    bad += check("one favorite occupies one display row, without repeated copies",
                 preset_visible(pos, total, 0) == 0 && preset_visible(pos, total, 1) == total);
    select_engine(ENGI_MODAL);
    before = *TSEL;
    turn(EN_K3, 1);
    bad += check("another engine's sound can be a favorite on any track", preset_favorite() &&
                 favorite_has(EID_MODAL, 0) && !memcmp(TSEL, &before, sizeof before));
    turn(EN_PRESET, 1);
    bad += check("filtered browsing crosses engines while retaining the track's TRN",
                 TSEL->eng_req == 0 && preset_favorite() &&
                 TSEL->p[P_TRANS] == 2);
    turn(EN_K3, -1);
    pos = preset_pos(&total);
    bad += check("unmarking the current sound leaves it loaded even when outside the filtered list",
                 !preset_favorite() && total == 1 && pos == total && TSEL->eng_req == 0);
    turn(EN_PRESET, -1);
    bad += check("browsing from a nonfavorite selects the last favorite", TSEL->eng_req == ENGI_MODAL);
    turn(EN_K3, -1);
    before = *TSEL;
    turn(EN_PRESET, 1);
    bad += check("empty favorites explains LIST ALL and retains the current sound and steps",
                 msg_is("NO FAVORITES") && !memcmp(TSEL, &before, sizeof before));
    turn(EN_K4, -1);
    preset_pos(&total);
    bad += check("LIST ALL restores the full browser without an implicit load", !favorites.filter && total > 1 &&
                 !memcmp(TSEL, &before, sizeof before));
    up_store(31, "Favorite");
    up_load(31);
    turn(EN_K3, 1);
    bad += check("user slot 32 has its own favorite reference", favorite_has(EID_COUNT, 31) && preset_favorite());
    up_store(31, "Renamed");
    bad += check("overwriting or renaming a user slot retains its star", favorite_has(EID_COUNT, 31));
    up_put(31, 0);
    bad += check("successful erasure removes the user slot star and source label",
                 !favorite_has(EID_COUNT, 31) && !up_used(31) && !TSEL->user);
    up_store(31, "New sound");
    bad += check("reusing the erased slot does not restore its old star", !favorite_has(EID_COUNT, 31));
    return bad;
}

/* 1.2 (Discussion #90): sound categories. The factory table covers every preset; PRESETS > LIST (KNOB 4) steps ALL FAV
 * BASS .. OTHER without a wrap and browses only that category (factory and user presets), kept with the settings in
 * favorites.factory[15][11] with favorites.filter 0 / 1 as before; the NAME screen's KNOB 3 sets a user preset's
 * category (from the sound's own), written with the name and kept by a rename; a record of before (version 2) gets
 * one by a rename, its sound unchanged; PROJECT's KNOB 3 a random name */
static int test_categories(void)
{
    int bad = 0, ok = 1;
    uint32_t e, k, total, pos, n, i;
    char b[16], s1[16];
    ui_power_on();
    memset(&favorites, 0, sizeof favorites);
    memset(up_bank, 0, sizeof up_bank);
    for (e = 0; e < NENGINES; e++) {
        const char *c = ENGINES[e]->cats ? ENGINES[e]->cats : "";
        ok &= str_len(c) == ENGINES[e]->npresets;
        for (k = 0; k < ENGINES[e]->npresets; k++)
            ok &= preset_cat(e, k) >= CAT_BASS && preset_cat(e, k) <= CAT_OTHER;
    }
    bad += check("categories: every factory preset of every engine has one (the table's strings == the preset counts)",
                 ok && preset_cat(0, 0) == CAT_BASS && preset_cat(ENGI_MODAL, 7) == CAT_DRUM && preset_cat(ENGI_WAVE, 1) == CAT_PAD &&
                 preset_cat(ENGI_SID, 12) == CAT_DRUM && preset_cat(0, 99) == CAT_OTHER);
    {   /* (2026-10-17) archived engines' favourites refused: WEST (16) and SH101 (61) with SPEECH (18) and BYTE (15) */
        favorites_t before = favorites;
        ok = !favorite_set(EID_WEST, 0, 1) && !favorite_has(EID_WEST, 0) && !favorite_set(EID_SH101, 0, 1) &&
             !favorite_set(EID_SPEECH, 2, 1) && !favorite_has(EID_SPEECH, 2) && !favorite_set(EID_BYTE, 0, 1) &&
             !favorite_set(EID_COUNT + 1u, 0, 1) && !memcmp(&before, &favorites, sizeof favorites);
        bad += check("  archived engines' favourites (WEST, SH101, SPEECH, BYTE) refused, nothing else touched", ok);
        memset(&favorites, 0, sizeof favorites);
    }
    go_page(GR_BROWSE);
    select_engine(0);
    apply_preset(1);                                       /* SID LEAD (not a bass) */
    turn(EN_K4, 1);
    turn(EN_K4, 1);
    pos = preset_pos(&total);
    for (n = 0, e = 0; e < NENGINES; e++)
        for (k = 0; eng_ok(e) && k < preset_shown(e); k++)
            n += preset_cat(e, k) == CAT_BASS;
    bad += check("  KNOB 4: ALL, FAV, then BASS: the list holds the factory basses only, the sound not loaded",
                 list_mode() == 2u && str_eq(list_name(list_mode()), "BASS") && !favorites.filter && ui_lcat == CAT_BASS &&
                 total == n && n >= 6u && pos == total && TSEL->eng_req == 0u && TSEL->preset == 1u);
    ok = 1;
    for (i = 0; i < total; i++) {
        turn(EN_PRESET, 1);
        ok &= track_cat(TSEL) == CAT_BASS;
    }
    bad += check("  the PRESETS knob steps through basses only (every engine), and round", ok && preset_pos(&total) == total - 1u);
    turn(EN_K4, 20);
    bad += check("  .. right to OTHER and no further (no wrap)", list_mode() == LIST_N - 1u && str_eq(list_name(list_mode()), "OTHER"));
    {
        track_t before = *TSEL;
        ui.msg_t = 0;
        turn(EN_PRESET, 1);
        bad += check("  OTHER is empty (no factory sound): NO SOUNDS IN LIST, the sound stays",
                     msg_is("NO SOUNDS IN LIST") && !memcmp(TSEL, &before, sizeof before));
    }
    {   /* the settings: the category's byte, unknown = none; FAV clears it */
        persist_t p;
        list_set(1u + CAT_PAD);
        settings_export(&p);
        ok = p.favorites.factory[15][11] == CAT_PAD && p.favorites.filter == 0u;
        p.favorites.factory[15][11] = 9;
        ok &= settings_import(&p, sizeof p) == 1 && list_mode() == 0u;
        p.favorites.factory[15][11] = CAT_FX;
        ok &= settings_import(&p, sizeof p) == 1 && list_mode() == 1u + CAT_FX;
        turn(EN_K4, -20);
        turn(EN_K4, 1);
        bad += check("  kept with the settings ([15][11], an unknown value: none); FAV clears it, ALL at the left end",
                     ok && list_mode() == 1u && favorites.filter == 1u && ui_lcat == 0u);
        turn(EN_K4, -1);
    }
    /* NAME: USER SAVE takes the sound's category; KNOB 3 changes it; written with the name */
    select_engine(0);
    apply_preset(0);                                       /* SID BASS */
    song.playing = 0;
    go_page(GR_USER);
    ui.uslot = 4;
    press(B_OCTUP);
    bad += check("NAME: USER SAVE starts at the sound's category (a factory BASS)", name_on() && nm.cat == CAT_BASS);
    turn(EN_K3, 1);
    ok = nm.cat == CAT_LEAD;
    turn(EN_K3, -5);
    ok &= nm.cat == CAT_BASS;
    turn(EN_K3, 1);
    press(B_OCTUP);
    bad += check("  KNOB 3 steps the category (no wrap below BASS); OCT+ writes it with the name",
                 ok && !name_on() && up_used(4) && up_cat(4) == CAT_LEAD && up_rec(4)->packed[UP_CAT_AT] == CAT_LEAD);
    list_set(1u + CAT_LEAD);
    up_load(4);
    pos = preset_pos(&total);
    bad += check("  the user preset is in the LEAD list (after the factory leads), not in BASS",
                 pos == total - 1u && preset_at(pos, &k) == NENGINES && k == 4u &&
                 (list_set(1u + CAT_BASS), preset_pos(&total) == total));
    list_set(0);
    go_page(GR_USER);
    ui.uslot = 4;
    press(B_EDIT);
    ok = name_on() && nm.kind == NK_USER_RENAME && nm.cat == CAT_LEAD;
    turn(EN_K3, 3);                                        /* LEAD -> KEYS */
    press(B_OCTUP);
    up_name(4, b);
    bad += check("  a rename starts at the slot's own category and writes the new one, the sound as it was",
                 ok && up_cat(4) == CAT_KEYS && up_engine(4) == 0u);
    up_load(4);
    name_open(NK_USER_SAVE, 5);
    ok = name_on() && nm.cat == CAT_KEYS;
    press(B_OCTDN);
    bad += check("  saving a sound loaded from a user preset starts at that preset's category", ok && !name_on() && !up_used(5));
    {   /* a record of before categories (version 2: 16-bit values) */
        up_rec_t *r = up_rec(9);
        int16_t v0[P_COUNT], v1[P_COUNT];
        uint32_t c0;
        memset(r, 0, sizeof *r);
        r->used = UP_USED; r->ver = 2; r->engine = EID_VOSIM; r->np = 72;
        str_cpy(r->name, "OLD VOSIM", 12);
        for (i = 0; i < 72u; i++) r->p[i] = (int16_t)(i % 100u);
        up_bank[0].magic = UP_BANK_MAGIC; up_bank[0].rsize = sizeof(up_rec_t); up_bank[0].nslot = UP_PER_BANK;
        c0 = up_cat(9);
        up_values(r, v0);
        ok = up_rename_cat(9, "OLD VOSIM", CAT_FX) != 1;
        up_values(r, v1);
        bad += check("  a version 2 record: listed by its engine (OTHER); a rename with a category makes it version 4, same sound",
                     c0 == CAT_OTHER && ok && r->ver == UP_VER && up_cat(9) == CAT_FX && !memcmp(v0, v1, sizeof v0));
        r->ver = 1; r->engine = EID_MODAL;
        bad += check("  a version 1 PHYS record keeps its version (MODEL 2 = DUST): no category",
                     (up_set_cat(r, CAT_PAD), r->ver == 1u && up_cat(9) == cat_of_engine(EID_MODAL)));
        memset(r, 0, sizeof *r);
    }
    /* PROJECT: KNOB 3 a random name */
    go_page(GR_SLOTS);
    name_open(NK_PROJ_SAVE, 1);
    turn(EN_K3, 1);
    str_cpy(s1, nm.s, sizeof s1);
    ok = nm.len >= 8u && nm.len <= 12u && nm.cur == nm.len && str_len(s1) == nm.len;
    for (i = 0, k = 0; i < 16u; i++)
        k |= (uint32_t)(str_len(NM_RA[i]) > 5u || str_len(NM_RB[i]) > 6u);
    turn(EN_K3, 1);
    bad += check("NAME: PROJECT's KNOB 3 types a random name (adjective + noun, at most 12), another one each detent",
                 ok && !k && !str_eq(s1, nm.s) && nm.len <= 12u && strchr(nm.s, ' '));
    name_close();
    memset(up_bank, 0, sizeof up_bank);
    memset(&favorites, 0, sizeof favorites);
    return bad;
}

static int test_display_preferences(void)
{
    int bad = 0, k;
    track_t sounds[NTRK];
    uint16_t before[240 * 240];
    ui_power_on();
    memcpy(sounds, trk, sizeof sounds);
    hold(B_HOME);
    ui.menu_sel = MI_COLOR;
    ui.force = 1; ui_draw();
    memcpy(before, host_screen, sizeof before);
    settings.palette = 5; palette_set(5);
    press(B_OCTUP);
    bad += check("COLOR OCT+ previews the next palette (PAPER, light) without closing the menu",
                 settings.palette == 6 && T_BG == UI_PALETTES[6].bg && ux.light && ui.menu == 1 && !song.octave &&
                 memcmp(before, host_screen, sizeof before) && !memcmp(sounds, trk, sizeof sounds));
    turn(EN_K1, 1);
    bad += check("COLOR KNOB 1 steps on to HI-CON, NIGHT (#50: true black), MONO (1.0.2: black and white), IDM, SIGNAL, "
                 "FOUNDRY, RAVE, CHIP (Blue Hare: the distributions'), then wraps to GREY",
                 settings.palette == 7 && (turn(EN_K1, 1), settings.palette == 8u) && T_BG == 0u &&
                 str_eq(UI_PALETTES[8].name, "NIGHT") && (turn(EN_K1, 1), settings.palette == UI_BW_INDEX) &&
                 str_eq(UI_PALETTES[UI_BW_INDEX].name, "MONO") && T_BG == 0u && T_TEXT == 0xFFFFu &&
                 (turn(EN_K1, 1), settings.palette == UI_PAL_IDM) && (turn(EN_K1, 1), settings.palette == UI_PAL_SIGNAL) &&
                 (turn(EN_K1, 1), settings.palette == UI_PAL_FOUNDRY) &&
                 (turn(EN_K1, 1), settings.palette == UI_PAL_RAVE) &&
                 (turn(EN_K1, 1), settings.palette == UI_PAL_CHIP) &&
                 (turn(EN_K1, 1), settings.palette == UI_GREY_INDEX) && str_eq(UI_PALETTES[UI_GREY_INDEX].name, "GREY"));
    settings.lowcut = 2;
    ui.menu_sel = MI_LOWCUT;
    press(B_OCTUP);
    k = settings.lowcut == 2u;                         /* (OCT+ stops at the end, as the knobs) */
    press(B_OCTDN);
    k &= settings.lowcut == 1u && fx_lowcut == 1u;
    press(B_OCTDN);
    bad += check("the expanded menu retains all three SPEAKER modes (OCT+ / OCT- step them, stopping at the ends)",
                 k && settings.lowcut == 0 && !fx_lowcut && ui.menu == 1);
    {   /* #42: SPEAKER EQ FLAT (the old OFF, stored 0) LOWCUT BASS+; KNOB 1 steps them, the audio follows at once */
        int ok = str_eq(MI_NAME[MI_LOWCUT], "SPEAKER EQ") && str_eq(SPK_EQ[0], "FLAT") && str_eq(SPK_EQ[1], "LOWCUT") &&
                 str_eq(SPK_EQ[2], "BASS+");
        turn(EN_K1, 1); ok &= settings.lowcut == 1u && fx_lowcut == 1u;
        turn(EN_K1, 1); ok &= settings.lowcut == 2u && fx_lowcut == 2u;
        turn(EN_K1, 1); ok &= settings.lowcut == 2u;                  /* (KNOB 1 stops at the ends) */
        turn(EN_K1, -1); turn(EN_K1, -1); ok &= settings.lowcut == 0u && !fx_lowcut;
        bad += check("#42 SPEAKER EQ: FLAT (old OFF) / LOWCUT / BASS+ on KNOB 1, fx_lowcut follows", ok);
    }
    press(B_OCTDN);
    k = ui.menu == 1 && settings.lowcut == 0u;         /* (OCT- at the first value: nothing, the menu stays) */
    press(B_HOME);
    bad += check("HOME leaves display preferences without changing musical state (OCT- no longer closes)",
                 k && !ui.menu && settings.palette == UI_GREY_INDEX && !memcmp(sounds, trk, sizeof sounds));
    return bad;
}

/* 1.0.5: the MENU in tabs (ui_menu.c, menu_items.c MI_TAB): every tab 1..MTAB_ROWS rows in a run, each fitting the
 * page in S and LARGE; ALGORITHM between the tabs (wraps, back at a tab's last row), PRESETS within one (wraps), any of
 * KNOB 1..4 and OCT+ / OCT- change a value, OCT+ opens CALIBRATION and ABOUT, HOME closes (OCT- not); reopened at the
 * same tab and row; the tabs slide with ANIM ON (only their strip drawn again), snap with ANIM OFF */
static void wdt_ms(void) { fm1_ms++; }
static void wdt_key(void) { fm1_ms++; if (fm1_ms % 50u == 0u) fm1_in.notes = 1u; }   /* a key pressed after a moment */
static int test_menu_tabs(void)
{
    int bad = 0, ok = 1;
    uint32_t t, i, n;
    for (t = 0; t < MTAB_COUNT; t++) {
        uint32_t f = mtab_first(t);
        n = mtab_rows(t);
        ok &= n >= 1u && n <= MTAB_ROWS && f < MI_COUNT;
        for (i = 0; i < MI_COUNT; i++)                  /* its rows in one run */
            ok &= (MI_TAB[i] == t) == (i >= f && i < f + n);
        ui.menu_sel = (uint8_t)f;
        ui_prefs = 0;
        ok &= mr_y(n) - mr_gap() + MP_PAD <= MK_Y - 6;
        ui_prefs = PREF_LARGE;
        ok &= mr_y(n) - mr_gap() + MP_PAD <= MK_Y - 6;
    }
    ok &= MP_Y + MP_PAD + (int32_t)MTAB_ROWS * 19 - 1 + MP_PAD <= MK_Y - 6;   /* (room for MTAB_ROWS: 18 px rows 1 px apart) */
    for (i = 0; i < MI_COUNT; i++) ok &= MI_TAB[i] < MTAB_COUNT;
    {   /* the tab bar: MT_GAP between every two tabs at every place of the slide, inside MT_X0..MT_X1 (S and LARGE) */
        int32_t a[MTAB_COUNT], x[MTAB_COUNT], w[MTAB_COUNT], p;
        for (n = 0; n < 2u; n++)
            for (p = 0; p <= (int32_t)(MTAB_COUNT - 1u) * MT_ONE; p++) {
                ui_prefs = n ? PREF_LARGE : 0u;
                mt.pos = (int16_t)p;
                mt_layout(a, x, w);
                ok &= x[0] >= MT_X0 && x[MTAB_COUNT - 1u] + w[MTAB_COUNT - 1u] <= MT_X1;
                for (t = 1; t < MTAB_COUNT; t++) ok &= x[t] - (x[t - 1u] + w[t - 1u]) == MT_GAP;
            }
    }
    ui_prefs = 0;
    bad += check("MENU tabs: DISPLAY CONTROL AUDIO MIDI SYSTEM (8 6 5 1 5 rows), 1..8 each in a run, fit the page; tab gaps "
                 "constant (S, LARGE)",
                 ok && MTAB_COUNT == 5u && str_eq(MTAB_NAME[0], "DISPLAY") && str_eq(MTAB_NAME[MTAB_MIDI], "MIDI") &&
                 mtab_rows(MTAB_DISPLAY) == 8u && MI_TAB[MI_HOME] == MTAB_DISPLAY && mtab_rows(MTAB_CONTROL) == 6u && MI_TAB[MI_EXPERT] == MTAB_CONTROL && mtab_rows(MTAB_AUDIO) == 5u &&
                 mtab_rows(MTAB_MIDI) == 1u && mtab_rows(MTAB_SYSTEM) == 5u && MI_TAB[MI_TUNE] == MTAB_AUDIO &&
                 MI_TAB[MI_MIDIIN] == MTAB_MIDI && MI_TAB[MI_INFO] == MTAB_SYSTEM && MI_INFO >= MI_VALUES);

    ui_power_on();
    hold(B_HOME);
    ok = ui.menu == 1 && ui.menu_sel == MI_COLOR && menu_tab() == MTAB_DISPLAY;
    for (i = 0; i < 7u; i++) turn(EN_PRESET, 1);
    ok &= ui.menu_sel == MI_HOME;
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_COLOR;  /* (wraps within the tab) */
    turn(EN_PRESET, -1); ok &= ui.menu_sel == MI_HOME;
    turn(EN_PRESET, -1); turn(EN_PRESET, -1); turn(EN_PRESET, -1); ok &= ui.menu_sel == MI_LEDS;
    bad += check("MENU: PRESETS moves within the tab and wraps there (COLOR .. HOME)", ok);
    turn(EN_ALGO, 1); ok = ui.menu_sel == MI_HOLD && menu_tab() == MTAB_CONTROL;
    turn(EN_PRESET, 1); turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_LATCH;
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_LOWCUT && menu_tab() == MTAB_AUDIO;
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_MIDIIN && menu_tab() == MTAB_MIDI;   /* (1.2) */
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_MIDIIN;  /* (its only row) */
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_SERIAL && menu_tab() == MTAB_SYSTEM;
    turn(EN_ALGO, -1); turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_LOWCUT;
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_LATCH;   /* (back at the row left there) */
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_LEDS;
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_SERIAL && menu_tab() == MTAB_SYSTEM;   /* (wraps) */
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_LEDS;
    bad += check("MENU: ALGORITHM steps between the tabs and wraps, each tab back at its last row", ok && ui.menu == 1);
    turn(EN_ALGO, 1); turn(EN_PRESET, -1);            /* CONTROL: from FX LATCH up to KNOB ACCEL */
    ok = ui.menu_sel == MI_ACCEL && !(ui_prefs & PREF_ACCEL);
    for (i = 0; i < 4u; i++) {                         /* any of KNOB 1..4: right ON, left OFF */
        turn(EN_K1 + i, 1); ok &= (ui_prefs & PREF_ACCEL) != 0u;
        turn(EN_K1 + i, -1); ok &= !(ui_prefs & PREF_ACCEL);
    }
    turn(EN_ALGO, 1); turn(EN_PRESET, 1);             /* AUDIO: USB LEVEL */
    ok &= ui.menu_sel == MI_USB;
    turn(EN_K4, 1); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed;
    turn(EN_K2, -1); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed;
    bad += check("MENU: any of KNOB 1..4 changes the selected row's value in any tab, the menu stays", ok && ui.menu == 1);

    /* OCT+ / OCT-: the next / previous value on every kind of row (as the knobs: stopping at the ends; COLOR wraps) */
    ok = 1;
    for (i = 0; i < MI_VALUES; i++) {
        uint32_t n = menu_n(i), v, j, want;
        uint8_t seen[128] = {0};                       /* (1.2: TUNE has 101 values) */
        ui.menu_sel = (uint8_t)i;
        for (j = 0; j <= n; j++)                       /* down to the first value (COLOR: round) */
            press(B_OCTDN);
        seen[menu_get(i) & 127u] = 1;
        for (j = 0; j <= n; j++) {                     /* up through every value */
            want = menu_step(i, 1);
            press(B_OCTUP);
            ok &= menu_get(i) == want && ui.menu == 1;
            seen[menu_get(i) & 127u] = 1;
        }
        for (j = 0; j < 128u; j++)
            ok &= seen[j] == (j < n);
        for (j = 0; j <= n; j++) {                     /* and down again */
            want = menu_step(i, -1);
            press(B_OCTDN);
            ok &= menu_get(i) == want && ui.menu == 1;
        }
        v = menu_get(i);                               /* the ends: COLOR wraps, the rest stop */
        if (i == MI_COLOR) {
            menu_put(i, n - 1u); press(B_OCTUP); ok &= menu_get(i) == 0u;
            press(B_OCTDN); ok &= menu_get(i) == n - 1u;
        } else {
            press(B_OCTDN); ok &= menu_get(i) == v;
            for (j = 0; j < n; j++) press(B_OCTUP);
            v = menu_get(i);
            press(B_OCTUP); ok &= menu_get(i) == v && v != menu_step(i, -1);
        }
        want = menu_step(i, -1);                       /* a knob steps as OCT does */
        turn(EN_K3, -1); ok &= menu_get(i) == want && ui.menu == 1;
    }
    ui_prefs = 0; settings_hold = HOLD_DEF; settings_leds = LEDS_DIM; ui_style = ST_FLAT;
    settings.lowcut = 0; fx_lowcut = 0; menu_put(MI_COLOR, UI_GREY_INDEX);
    song.g[G_TUNE] = 0; song.g[G_ROUTE] = 0;
    bad += check("MENU: OCT+ / OCT- step every value row up / down (COLOR wraps, the rest stop), the menu stays", ok);
    {   /* the ON / OFF rows: up (a knob right, OCT+) is ON, the switch's knob to the right, whichever bit it is */
        static const uint8_t SW[] = {MI_LARGE, MI_ANIM, MI_ACCEL, MI_LATCH, MI_BPMLOCK, MI_SERIAL, MI_RESTORE};
        ok = 1;
        for (i = 0; i < NELEM(SW); i++) {
            ui.menu_sel = SW[i];
            press(B_OCTDN); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "OFF");
            press(B_OCTUP); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "ON");
            turn(EN_K2, -1); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "OFF");
            turn(EN_K2, 1); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "ON");
        }
        ui.menu_sel = MI_STYLE;
        press(B_OCTUP); ok &= ui_style == ST_LINE;     /* (the others: up the second, LINE) */
        press(B_OCTDN); ok &= ui_style == ST_FLAT;
        ui_prefs = 0;
        bad += check("MENU: on an ON / OFF row OCT+ / a knob right is ON, OCT- / left OFF (ANIM, USB SERIAL, RESTORE LAST too)", ok);
    }
    ui.menu_sel = MI_USB;
    press(B_HOME);
    ok = !ui.menu;
    hold(B_HOME);
    ok &= ui.menu == 1 && ui.menu_sel == MI_USB && menu_tab() == MTAB_AUDIO;
    frames(500);
    ok &= ui.menu == 1;                                 /* (the hold's release: no tap, the menu stays) */
    hold(B_HOME);
    ok &= !ui.menu;
    frames(1000);
    ok &= !ui.menu;                                     /* (the hold that closed it: no reopen) */
    hold(B_HOME);
    bad += check("MENU: HOME pressed closes it, held too (no reopen); held opens it again at the same tab and row",
                 ok && ui.menu == 1 && ui.menu_sel == MI_USB);
    turn(EN_ALGO, 1); turn(EN_ALGO, 1); turn(EN_PRESET, -1);   /* (MIDI,) SYSTEM: ABOUT */
    ok = ui.menu_sel == MI_ABOUT;
    frame();
    ok &= oct_leds() == OCT_BREATH;                     /* (OCT+ opens, OCT- does nothing: dark) */
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;   /* (no close, no back) */
    turn(EN_K1, 1); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;
    press(B_OCTUP); ok &= ui.menu == 2;
    ok &= oct_leds() == 1u;                             /* (ABOUT: OCT- back, lit) */
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;
    turn(EN_PRESET, -1); ok &= ui.menu_sel == MI_INFO;   /* (1.2) INFO: as ABOUT, no scroll */
    frame();
    ok &= oct_leds() == OCT_BREATH;
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_INFO;
    press(B_OCTUP); ok &= ui.menu == 3 && oct_leds() == 1u;
    turn(EN_PRESET, 3); turn(EN_K1, 1); turn(EN_ALGO, 1);
    ok &= ui.menu == 3 && !ui.menu_scroll && ui.menu_sel == MI_INFO;
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_INFO;
    turn(EN_PRESET, -1); ok &= ui.menu_sel == MI_PANEL;
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_PANEL;
    {   /* CALIBRATION: OCT+ starts the setup (nothing pressed here: it times out and keeps the table) */
        panel_t p0 = panel;
        host_wdt_hook = wdt_ms;
        press(B_OCTUP);
        host_wdt_hook = 0;
        ok &= !memcmp(&p0, &panel, sizeof panel) && msg_is("SETUP CANCELLED") && ui.menu == 1;
    }
    {   /* CALIBRATION: a piano key cancels at once (no 30 s wait), the table kept */
        panel_t p0 = panel;
        uint32_t t = fm1_ms;
        host_wdt_hook = wdt_key;
        press(B_OCTUP);
        host_wdt_hook = 0;
        fm1_in.notes = 0;
        ok &= !memcmp(&p0, &panel, sizeof panel) && msg_is("SETUP CANCELLED") && ui.menu == 1 && fm1_ms - t < 1000u;
    }
    bad += check("MENU SYSTEM: OCT+ opens ABOUT, INFO (OCT- back to the tab) and CALIBRATION; OCT- does nothing on them", ok);
    turn(EN_PRESET, 1); turn(EN_PRESET, 1); press(B_OCTUP);   /* (INFO,) ABOUT, HOME: closed, from the document */
    ok = ui.menu == 2;
    press(B_HOME);
    ok &= !ui.menu && ui.home;
    hold(B_HOME); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;   /* (opened at the tabs, ABOUT's row) */
    turn(EN_ALGO, -1); frame();
    {   /* a value row: OCT- and OCT+ both breathe, each while that way has a value to go to; none lit */
        uint32_t r = ui.menu_sel, want = (menu_step(r, -1) != menu_get(r) ? OCT_BREATH_DN : 0u) |
                                         (menu_step(r, 1) != menu_get(r) ? OCT_BREATH : 0u);
        ok &= r < MI_VALUES && oct_leds() == want && want != 0u;
        menu_put(MI_HOLD, 0u); ui.menu_sel = MI_HOLD;   /* HOLD at its first value: OCT- dark, OCT+ breathing */
        ok &= oct_leds() == OCT_BREATH;
        menu_put(MI_HOLD, 1u);                          /* in between: both breathe */
        ok &= oct_leds() == (OCT_BREATH | OCT_BREATH_DN);
    }
    menu_close();
    bad += check("MENU: HOME closes ABOUT too; reopened at the tabs; a value row breathes OCT- / OCT+ where they can go", ok);

    /* the slide: ANIM ON, a few frames from DISPLAY to CONTROL drawing only the tab strip; ANIM OFF, at once */
    ui_power_on();
    hold(B_HOME);
    frame();
    turn(EN_ALGO, 1);
    ok = mt.pos > 0 && mt.pos < MT_ONE;                 /* (a step on the way) */
    host_blit_rows = 0; frame();
    ok &= host_blit_rows == (uint32_t)(MP_Y - H_HEAD);  /* (only the strip) */
    for (i = 0; i < 8u && mt.pos != MT_ONE; i++) frame();
    ok &= mt.pos == MT_ONE && i <= 6u;
    host_blit_rows = 0; frame();
    ok &= host_blit_rows == 0u;                         /* (settled: nothing drawn) */
    ui_prefs |= PREF_ANIM_OFF;
    turn(EN_ALGO, 1);
    ok &= mt.pos == 2 * MT_ONE;
    ui_prefs &= ~PREF_ANIM_OFF;
    menu_close();
    bad += check("MENU tabs slide with ANIM ON (their strip only, settled within 6 frames), snap with ANIM OFF", ok);
    return bad;
}

/* 1.2: MENU > AUDIO > TUNE and MENU > MIDI > MIDI IN edit the project's G_TUNE and G_ROUTE (up to 1.1.5 GLO > GLOBAL K4
 * and GLO > SYSTEM K3): the same values, stored with the project as before (a PROJECT save / load, the autosave), never
 * in the settings record */
static int test_menu_project_rows(void)
{
    int bad = 0, ok;
    persist_t p0, p1;
    uint32_t sig;
    ui_power_on();
    memset(&p0, 0, sizeof p0); memset(&p1, 0, sizeof p1);   /* (export keeps some fields as they were) */
    settings_export(&p0);
    sig = autosave_sig();
    hold(B_HOME);
    ui.menu_sel = MI_TUNE;
    ok = ui.menu == 1 && menu_tab() == MTAB_AUDIO && song.g[G_TUNE] == 0 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "0");
    press(B_OCTUP); ok &= song.g[G_TUNE] == 1 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "+1");
    turn(EN_K2, 11); ok &= song.g[G_TUNE] == 12;         /* (a fast turn: as far as the knob went) */
    turn(EN_K1, -40); ok &= song.g[G_TUNE] == -28 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "-28");
    turn(EN_K3, -100); ok &= song.g[G_TUNE] == -50;      /* (the ends: -50 .. +50 cents) */
    press(B_OCTDN); ok &= song.g[G_TUNE] == -50;
    turn(EN_K4, 200); ok &= song.g[G_TUNE] == 50 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "+50");
    turn(EN_K4, -31); ok &= song.g[G_TUNE] == 19;
    bad += check("MENU > AUDIO > TUNE: the project's G_TUNE, -50 .. +50 cents (OCT one, a knob as far as it turned)", ok);
    ui.menu_sel = MI_MIDIIN;
    ok = menu_tab() == MTAB_MIDI && song.g[G_ROUTE] == 0 && str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), NTRK == 8 ? "CH1-8" : "CH1-4");
    press(B_OCTUP); ok &= song.g[G_ROUTE] == 1;
    if (NTRK == 8) {                                    /* (TRACKS 8: blocks of 8, CH1-8 SEL CH9-16) */
        turn(EN_K1, 1); turn(EN_K1, 1); ok &= song.g[G_ROUTE] == 2 && str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), "CH9-16");
    } else {
        turn(EN_K1, 1); turn(EN_K1, 1); ok &= song.g[G_ROUTE] == 3 && str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), "CH9-12");
    }
    bad += check("MENU > MIDI > MIDI IN: the project's G_ROUTE (ROUT), its names", ok);
    menu_close();
    settings_export(&p1);
    ok = !memcmp(&p0, &p1, sizeof p0) && autosave_sig() != sig;
    bad += check("TUNE / MIDI IN: nothing in the settings record; the autosave sees the change (the music's)", ok);
    project_save(2);                                     /* (PROJECT C) */
    song.g[G_TUNE] = 0; song.g[G_ROUTE] = 0;
    project_load(2);
    ok = song.g[G_TUNE] == 19 && song.g[G_ROUTE] == (NTRK == 8 ? 2 : 3) && menu_get(MI_TUNE) == 69u &&
         menu_get(MI_MIDIIN) == (NTRK == 8 ? 2u : 3u);
    song.g[G_TUNE] = -7; song.g[G_ROUTE] = NTRK == 8 ? 2 : 4;   /* (another song's values: the rows follow the load) */
    project_save(0);
    song.g[G_TUNE] = 0; song.g[G_ROUTE] = 0;
    project_load(0);
    ok &= song.g[G_TUNE] == -7 && song.g[G_ROUTE] == (NTRK == 8 ? 2 : 4) && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "-7") &&
          str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), NTRK == 8 ? "CH9-16" : "CH13-16");
    bad += check("TUNE / MIDI IN: saved with the song and loaded back (round trip; saved again, the new values)", ok);
    ui_power_on();
    return bad;
}

static int test_information(void)
{
    uint16_t header[240 * H_HEAD], footer[240 * 19], battery[240 * H_HEAD];
    track_t sounds[NTRK];
    int bad = 0, quiet = 1;
    uint32_t x, y;
    ui_power_on();
    memcpy(sounds, trk, sizeof sounds);
    hold(B_HOME);
    ui.menu_sel = MI_ABOUT;
    press(B_OCTUP);
    ui_draw();
    bad += check("ABOUT opens one document with scrollable license text and credits", ui.menu == 2 && !ui.menu_scroll && menu_scroll_max() > MENU_DOC_H);
    for (y = 146; y < 220; y++)
        for (x = 158; x < 232; x++)
            if (x < 166 || x >= 224 || y < 154 || y >= 212)
                quiet &= host_screen[y * 240 + x] == swap16(UI_QR_LIGHT);
    bad += check("ABOUT QR keeps its complete white quiet zone above the fixed footer", quiet);
    memcpy(header, host_screen, sizeof header);
    memcpy(footer, host_screen + 221 * 240, sizeof footer);
    turn(EN_PRESET, 3);
    ui_draw();
    bad += check("PRESETS scrolls ABOUT with its header and controls fixed",
                 ui.menu == 2 && ui.menu_scroll > 0 && !memcmp(header, host_screen, sizeof header) &&
                 !memcmp(footer, host_screen + 221 * 240, sizeof footer));
    turn(EN_PRESET, 12);
    ui_draw();
    bad += check("PRESETS continues from ABOUT into CREDITS without changing pages",
                 ui.menu == 2 && ui.menu_scroll > 216 && !memcmp(header, host_screen, sizeof header) &&
                 !memcmp(footer, host_screen + 221 * 240, sizeof footer));
    uint16_t position = ui.menu_scroll;
    press(B_OCTUP);
    bad += check("OCT+ does not switch pages or alter the octave in the document",
                 ui.menu == 2 && ui.menu_scroll == position && song.octave == 0);
    turn(EN_PRESET, 10000);
    turn(EN_PRESET, 10000);
    ui_draw();
    bad += check("the continuous document stops at its final credit",
                 ui.menu == 2 && ui.menu_scroll == menu_scroll_max());
    turn(EN_PRESET, -10000);
    bad += check("scrolling back through CREDITS returns to ABOUT's top", ui.menu == 2 && !ui.menu_scroll);
    press(B_OCTDN);
    bad += check("OCT- returns to the menu without changing sounds or patterns",
                 ui.menu == 1 && !memcmp(sounds, trk, sizeof sounds));
    press(B_OCTUP);
    bad += check("reopening ABOUT starts the continuous document at the top", ui.menu == 2 && !ui.menu_scroll);
    menu_close();
    ui.msg_t = 0;
    usb.config = 1; usb.suspended = 0;
    song.batt_raw = 530;
    ui.force = 1; draw_head(); ui.force = 0;
    memcpy(battery, host_screen, sizeof battery);
    uint32_t signature = ui.head_sig;
    for (x = 0; x < 4; x++) { fm1_ms += 600; draw_head(); }
    bad += check("USB power shows a steady bolt inside the battery without animation",
                 batt_shown() == 4 && ui.head_sig == signature && !memcmp(battery, host_screen, sizeof battery) &&
                 host_screen[6 * 240 + 217] != 0 && host_screen[9 * 240 + 225] != 0);
    usb.suspended = 1;
    draw_head();
    bad += check("USB suspension restores the battery outline and measured level",
                 batt_shown() == 0 && ui.head_sig != signature && host_screen[6 * 240 + 217] != 0);
    usb.config = 0; usb.suspended = 0;
    song.batt_raw = 531; int levels = batt_shown() == 1;
    song.batt_raw = 561; levels &= batt_shown() == 2;
    song.batt_raw = 591; levels &= batt_shown() == 3;
    bad += check("battery operation retains all three ADC level thresholds", levels);
    return bad;
}

/* GREY is grayscale on every screen: every page of every engine, HOME, the menu, ABOUT, the dialogs (RGB565 gray:
 * R = B, G = 2 R). MONO (black and white, blended per channel) is neutral on every screen: R = B, G within 2 of 2 R
 * (the 5-6-5 rounding of its edges) */
static int screen_gray(void)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        if ((c >> 11) != (c & 31u) || ((c >> 5) & 63u) != (c >> 11) * 2u)
            return 0;
    }
    return 1;
}
static int screen_neutral(void)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        int32_t d = (int32_t)((c >> 5) & 63u) - (int32_t)(c >> 11) * 2;
        if ((c >> 11) != (c & 31u) || d < -2 || d > 2)
            return 0;
    }
    return 1;
}
static int mono_screens(uint32_t pal, int (*chk)(void), uint32_t *n)
{
    int ok = 1;
    uint32_t e, i;
    for (e = 0; e < NENGINES; e++) {
        if (!eng_ok(e))
            continue;
        for (i = 0; i < NPAGES; i++) {
            ui_power_on(); set_engine_of(TSEL, e); song.playing = 1;
            ui.home = 0; ui.page = (uint8_t)i; page_entered();
            if (!page_visible(i)) continue;
            settings.palette = pal; palette_set(pal);
            memset(host_screen, 0, sizeof host_screen);
            ui.force = 1; ui_draw();
            ok &= chk();
            (*n)++;
        }
        ui_power_on(); set_engine_of(TSEL, e); go_home();
        settings.palette = pal; palette_set(pal);
        ui.force = 1; ui_draw(); ok &= chk();
    }
    for (i = 0; i < 4u; i++) {
        ui_power_on();
        settings.palette = pal; palette_set(pal);
        if (i < 2u) { ui.menu = (uint8_t)(i + 1u); ui.menu_scroll = 300; }
        else if (i == 2u) ui.confirm = CF_OVR_PROJ;
        else ui.uboot = 2;
        ui.force = 1; ui_draw(); ok &= chk();
    }
    return ok;
}
static int test_mono_screens(void)
{
    int bad = 0, ok;
    uint32_t e, n = 0, nb = 0;
    ok = mono_screens(UI_GREY_INDEX, screen_gray, &n);
    bad += check("GREY: every pixel of every page, HOME, menu, ABOUT, dialog and countdown is gray", ok && n >= NENGINES * 20u);
    ok = mono_screens(UI_BW_INDEX, screen_neutral, &nb);
    bad += check("MONO (black and white): every pixel of every page, HOME, menu, ABOUT, dialog and countdown is neutral",
                 ok && nb == n);
    ui_power_on();
    for (e = 0; e < NPALETTES; e++) {               /* the other palettes are not gray: the check sees colour */
        ui_power_on(); palette_set(e); ui.force = 1; ui_draw();
        ok = screen_gray();
        if (e == UI_GREY_INDEX ? !ok : ok) bad += check("palette colour shows on HOME", 0);
    }
    palette_set(UI_GREY_INDEX);
    return bad;
}

/* ROLL: the rolling digits of the four cards and the header BPM (ui_draw.c roll_*). The old GLOBAL page's four (1.2: a
 * test page over HOME > CLOCK, whose KNOB 4 is empty since TUNE went to the MENU): BPM (also in the header), SWG, CLK (a
 * name), TUNE (signed). A roll frame differs from the static render only inside the
 * value strip, its last frame is the static render; unchanged digits stay; the direction follows the sign; a
 * change mid-roll retargets; a fast turn, another shape, a page / track / palette change and ui.force snap. */
static uint16_t roll_shot[240 * 240];
static const page_t ROLL_PG = {"CLOCK", FAM_TRK, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_TUNE}};
static void roll_settle(void)                     /* CLOCK + TUNE, nothing rolling, no knob hot */
{
    uint32_t k;
    go_title("CLOCK");
    page_over = &ROLL_PG;                         /* (cur_page(): its knobs, its cards; a layer's way, ui_layer.c) */
    ui.hot_t = 0; ui.bpm_t = 0;
    for (k = 0; k < 12u; k++) frame();
}
/* host_screen against roll_shot: the pixels that differ inside the box (in = 1) or outside it */
static uint32_t roll_diff(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int in)
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = 0; y < 240; y++)
        for (x = 0; x < 240; x++) {
            int inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (inside == in && host_screen[y * 240 + x] != roll_shot[y * 240 + x]) n++;
        }
    return n;
}
/* the mean row (x16) of the ink (not the card surface) in columns x0..x1-1, rows y0..y1-1 of a screen */
static int32_t roll_ink_row(const uint16_t *s, int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
    int32_t x, y, n = 0, sum = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            if (s[y * 240 + x] != swap16(T_SURF)) { n++; sum += y * 16; }
    return n ? sum / n : -1;
}
static int roll_static_now(void)                  /* the screen is the static render (a forced redraw changes nothing) */
{
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1;
    ui_draw();
    return roll_diff(0, 0, 0, 0, 0) == 0;
}
#define STRIP(c) CARD_X(c), Y_LABEL + ROLL_Y, CARD_X(c) + COL_W, Y_LABEL + ROLL_Y + ROLL_H
static int test_roll(void)
{
    int bad = 0, ok;
    uint32_t k, n, e, id;
    int32_t up, down, st;
    static const struct { const char *a, *b; int d; } DIRS[] = {
        {"120", "121", 1}, {"121", "120", -1}, {"129", "130", 1}, {"-12", "-13", -1}, {"-13", "-12", 1}, {"+5", "+6", 1},
        {"1.25", "1.30", 1}, {"0.9", "0.8", -1}, {"9", "10", 0}, {"0", "-1", 0}, {"+5", "-5", 0}, {"C3", "C4", 0},
        {"1/4", "1/8", 0}, {"OFF", "ON", 0}, {"2X", "4X", 0}, {"120", "120", 0}, {"123456", "123457", 1},
        {"1234567", "1234568", 0}, {"12%", "13%", 0}};

    ok = 1;
    for (k = 0; k < sizeof DIRS / sizeof DIRS[0]; k++)
        if (roll_dir(DIRS[k].a, DIRS[k].b) != DIRS[k].d) { printf("  roll_dir %s -> %s\n", DIRS[k].a, DIRS[k].b); ok = 0; }
    bad += check("roll: numbers of one shape roll by the sign of the change; names, length, sign changes snap", ok);
    ok = 1;                                         /* every page of every engine: names never roll, numbers do */
    n = 0;
    for (e = 0; e < NENGINES; e++)
        for (id = 0; id < NPAGES && eng_ok(e); id++) {
            uint32_t c;
            ui_power_on(); set_engine_of(TSEL, e);
            ui.home = 0; ui.page = (uint8_t)id; page_entered();
            if (!page_visible(id) || PAGES[id].graph == GR_MOD) continue;
            ui.force = 1; ui_draw();
            for (c = 0; c < 4u; c++) {
                int16_t *vp;
                const param_desc_t *d = PAGES[id].scope == SC_GLOBAL || PAGES[id].scope == SC_TRACK ||
                                        PAGES[id].scope == SC_ENGINE ? page_desc(cur_page(), c, &vp) : 0;
                int32_t v, v0;
                if (!d || !d->label || d->label[0] == '-' || ((act_cols() >> c) & 1u)) continue;
                v0 = *vp;
                for (v = d->min; v < d->max && v - d->min < 40; v++) {
                    *vp = (int16_t)v; ui.frame += ROLL_SNAP; draw_columns();
                    *vp = (int16_t)(v + 1); ui.frame += ROLL_SNAP; draw_columns();
                    if (ui.roll[c].from[0] && (d->fmt == F_ENUM || d->fmt == F_NOTE || d->fmt == F_ONOFF)) {
                        printf("  %s/%s %s: %s rolls\n", ENGINES[e]->name, PAGES[id].title, d->label, ui.roll[c].from);
                        ok = 0;
                    }
                    n += ui.roll[c].from[0] != 0;
                }
                *vp = (int16_t)v0;
            }
        }
    ok &= n > 500u;
    bad += check("roll: every page and engine: a name (enum, even ALG 1..8, note, ON/OFF) snaps, numbers roll", ok);

    /* TUNE 0 -> 1 (card 4): a roll of ROLL_FRAMES frames, the last one the static render */
    ui_power_on(); roll_settle();
    turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 1 && str_eq(ui.roll[3].from, "0") && ui.roll[3].dir == 1;
    for (n = 1; ui.roll[3].from[0] && n < 20u; n++) frame();
    ok &= n == ROLL_FRAMES;
    bad += check("roll: a card's value rolls for 9 frames (TUNE 0 -> 1), the value itself at once", ok);
    bad += check("roll: the roll's last frame is pixel-identical to the static render", roll_static_now());

    /* mid-roll: only the value strip differs from the static render */
    ui_power_on(); roll_settle();
    turn(EN_K4, 1); frame(); frame(); frame();
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1; ui_draw();                       /* (snaps: the static render) */
    ok = roll_diff(STRIP(3), 0) == 0 && roll_diff(STRIP(3), 1) > 20u && !ui.roll[3].from[0];
    bad += check("roll: a mid-roll frame differs from the static render inside the value strip only", ok);

    /* BPM 120 -> 121 (SELECT): the header rolls, the digits 1 and 2 stay; the BPM card too (no unit:
     * the label says BPM: set in M, it rolls like any number) */
    ui_power_on(); roll_settle();
    turn(EN_SELECT, 1);
    ok = song.g[G_BPM] == 121 && str_eq(ui.roll[ROLL_BPM].from, "120") && ui.roll[ROLL_BPM].dir == 1 &&
         str_eq(ui.roll[0].from, "120") && ui.roll[0].dir == 1;
    frame(); frame();
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1; ui_draw();
    {   /* the header's strip: from the metronome's ink to the lock's place (ui_draw.c draw_head) */
        head_geo_t g;
        int32_t x0, x1;
        head_geo(&g, "121");
        x0 = g.sx0; x1 = g.sx1;
        n = roll_diff(x0, 0, x1, H_HEAD, 1);
        ok &= n > 20u && roll_diff(x0, 0, x1, H_HEAD, 0) == roll_diff(STRIP(0), 1);   /* (+ the card's strip) */
        ok &= roll_diff(g.bx, 0, g.bx + text_w(&AF_M, "12"), H_HEAD, 1) == 0;
        ok &= x0 < g.bx && x1 <= 140 && x1 - x0 <= 40;
    }
    ok &= roll_diff(CARD_X(0), Y_LABEL + ROLL_Y, CARD_X(0) + 5 + text_w(&AF_M, "12"), Y_LABEL + ROLL_Y + ROLL_H, 1) == 0;
    /* TUNE 10 -> 11 on a card: the 1 stays */
    ui_power_on(); song.g[G_TUNE] = 10; roll_settle();
    turn(EN_K4, 1); frame(); frame();
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1; ui_draw();
    ok &= song.g[G_TUNE] == 11 && roll_diff(STRIP(3), 1) > 20u && roll_diff(STRIP(3), 0) == 0;
    ok &= roll_diff(CARD_X(3), Y_LABEL + ROLL_Y, CARD_X(3) + 5 + text_w(&AF_M, "1"), Y_LABEL + ROLL_Y + ROLL_H, 1) == 0;
    bad += check("roll: header BPM 120 -> 121 and a card 10 -> 11: only the strip changes, unchanged digits stay put", ok);

    /* the direction: 5 -> 6 and -5 -> -6 at frame 4: the new digit comes from below (up) / above (down) */
    ui_power_on(); song.g[G_TUNE] = 5; roll_settle();
    st = roll_ink_row(host_screen, CARD_X(3) + 5, CARD_X(3) + 16, Y_LABEL + ROLL_Y, Y_LABEL + ROLL_Y + ROLL_H);
    turn(EN_K4, 1); frame(); frame(); frame();
    up = roll_ink_row(host_screen, CARD_X(3) + 5, CARD_X(3) + 16, Y_LABEL + ROLL_Y, Y_LABEL + ROLL_Y + ROLL_H);
    ok = ui.roll[3].dir == 1;
    ui_power_on(); song.g[G_TUNE] = 7; roll_settle();
    turn(EN_K4, -1); frame(); frame(); frame();
    down = roll_ink_row(host_screen, CARD_X(3) + 5, CARD_X(3) + 16, Y_LABEL + ROLL_Y, Y_LABEL + ROLL_Y + ROLL_H);
    ok &= ui.roll[3].dir == -1 && st > 0 && up > st + 16 && down < st - 16;
    ui_power_on(); song.g[G_TUNE] = -5; roll_settle();
    turn(EN_K4, -1);
    ok &= song.g[G_TUNE] == -6 && str_eq(ui.roll[3].from, "-5") && ui.roll[3].dir == -1;
    bad += check("roll: an increase rolls up, a decrease down (-5 -> -6 is down)", ok);

    /* retarget: a change mid-roll restarts from the value it was going to; a fast turn snaps */
    ui_power_on(); song.g[G_TUNE] = 1; roll_settle();
    turn(EN_K4, 1); frame(); frame(); frame();
    turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 3 && str_eq(ui.roll[3].from, "2") && (uint8_t)(ui.frame - ui.roll[3].t0) == 0;
    for (n = 1; ui.roll[3].from[0] && n < 20u; n++) frame();
    ok &= n == ROLL_FRAMES && roll_static_now();
    bad += check("roll: a change mid-roll retargets (from the last value, restarted) and ends static", ok);
    ui_power_on(); song.g[G_TUNE] = 1; roll_settle();
    turn(EN_K4, 1); turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 3 && !ui.roll[3].from[0] && roll_static_now();
    bad += check("roll: a change within 40 ms of the last one snaps (a fast turn)", ok);

    /* snaps: another shape, a name, the page, the track, the palette, ui.force */
    ui_power_on(); roll_settle();
    turn(EN_K4, -1);
    ok = song.g[G_TUNE] == -1 && !ui.roll[3].from[0] && roll_static_now();
    for (k = 0; k < 5u; k++) frame();
    turn(EN_K3, 1);
    ok &= song.g[G_CLOCK] != 0 && !ui.roll[2].from[0] && roll_static_now();
    bad += check("roll: a sign / length change (0 -> -1) and a name (CLK) snap", ok);
    ok = 1;
    for (k = 0; k < 5u; k++) {
        ui_power_on(); roll_settle();
        turn(EN_K4, 1);
        ok &= ui.roll[3].from[0] != 0;
        if (k == 0) { page_over = 0; go_title("ENV"); }
        else if (k == 1) turn(EN_ALGO, 1);
        else if (k == 2) { settings.palette = 1; palette_set(1); }          /* (no ui.force) */
        else if (k == 3) set_engine_of(TSEL, eng_step(TSEL->eng_req, 1));   /* (no ui.force) */
        else ui.force = 1;
        frame();
        ok &= !ui.roll[3].from[0] && !ui.roll[ROLL_BPM].from[0];
    }
    ui_power_on(); roll_settle();
    turn(EN_SELECT, 1);
    ok &= ui.roll[ROLL_BPM].from[0] != 0;
    ui.force = 1; frame();
    ok &= !ui.roll[ROLL_BPM].from[0];
    bad += check("roll: a page, track, palette or engine change and ui.force snap", ok);
    palette_set(UI_GREY_INDEX);

    /* MONO: every roll frame is gray */
    ui_power_on(); roll_settle();
    ok = 1;
    turn(EN_SELECT, 9);
    turn(EN_K4, -1);
    for (k = 0; k < ROLL_FRAMES; k++) { ok &= screen_gray(); frame(); }
    bad += check("roll: MONO roll frames are gray", ok);
    page_over = 0;
    ui_power_on();
    return bad;
}

/* #63 ("parameters moving on their own, fixed by a page change"): a soak of the cards and the rolling digits.
 * ~40 min of UI frames: random turns of KNOB 1..4 and SELECT (bursts back and forth), page / HOME / track
 * changes, the transport and REC, LFO rates, uneven frame times, ui.frame
 * and fm1_ms wrapping. After every frame no roll is older than ROLL_FRAMES; at random quiet moments (no
 * roll, nothing hot) the cards are pixel-identical to a forced redraw (what a page change would show). */
static int test_soak_cards(void)
{
    uint32_t seed = 63u, f, quiet = 0, old = 0, stale = 0, cmp = 0, c;
    static uint16_t shot[240 * CARD_H];
#define SOAK_RND(n) ((seed = seed * 1103515245u + 12345u) >> 8) % (n)
    ui_power_on();
    ui.frame = 0xFFFFFF00u;
    fm1_ms = 0xFFFFFFFFu - 60000u;
    for (f = 0; f < 150000u; f++) {
        uint32_t r = SOAK_RND(1000u);
        if (r < 25u) {                                  /* a knob, 1..3 detents either way */
            uint32_t role = SOAK_RND(5u) ? EN_K1 + SOAK_RND(4u) : EN_SELECT;
            int32_t s = (int32_t)SOAK_RND(3u) + 1;
            host_enc[panel.enc[role]] += (SOAK_RND(2u) ? s : -s) * panel.dir[role];
            quiet = 0;
        } else if (r < 29u) {                           /* a page */
            uint32_t i = SOAK_RND(NPAGES);
            if (page_visible(i)) { ui.home = 0; ui.page = (uint8_t)i; page_entered(); }
            quiet = 0;
        } else if (r < 30u) { ui.home = 1; page_entered(); quiet = 0; }
        else if (r < 31u) transport_req = song.playing ? 2u : 1u;
        else if (r == 32u) { host_enc[panel.enc[EN_ALGO]] += SOAK_RND(2u) ? 1 : -1; quiet = 0; }
        else if (r == 33u) trk[SOAK_RND(NTRK)].p[P_LRATE] = (int16_t)SOAK_RND(128u);
        fm1_ms += SOAK_RND(50u) ? 16u : 1u + SOAK_RND(200u);
        host_ticks += 16000u + SOAK_RND(3u) * 4000u;
        ui_input();
        if (song.playing || transport_req) events_block(FS * 16u / 1000u);
        ui_draw();
        quiet++;
        if (ui.menu || ui.layer || ui.confirm || name_on() || ui.uboot)
            continue;                                   /* (the cards are not drawn) */
        for (c = 0; c < 5u; c++)
            old += ui.roll[c].from[0] && (uint8_t)(ui.frame - ui.roll[c].t0) >= ROLL_FRAMES;
        if (quiet > 45u && !ui.hot_t && !ui.bpm_t && !ui.msg_t && SOAK_RND(8u) == 0u) {
            for (c = 0; c < 5u && !ui.roll[c].from[0]; c++) {}
            if (c < 5u)
                continue;
            memcpy(shot, &host_screen[Y_LABEL * 240], sizeof shot);
            ui.force = 1;
            ui_draw();
            cmp++;
            stale += memcmp(shot, &host_screen[Y_LABEL * 240], sizeof shot) != 0;
        }
    }
#undef SOAK_RND
    if (old || stale || cmp < 500u) printf("  soak: %u old rolls, %u stale cards in %u checks\n", old, stale, cmp);
    stop_transport();
    ui_power_on();
    return check("#63 soak: 150000 random frames, wrapping clocks: no roll lasts, cards = a forced redraw",
                 !old && !stale && cmp >= 500u);
}

static void screen_save(const char *dir, const char *name)
{
    char path[512];
    uint32_t i;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    if (!f) return;
    ui.msg_t = 0; ui.hot_t = 0; ui.force = 1;
    ui_draw();
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);           /* canvas stores panel-order RGB565 */
        uint8_t b[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u), (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}


/* Product controls are exercised through the actual button/encoder handlers
 * and canvas, including all pages, both fonts and every shipped palette. */
/* Discussions #92 / #94: the PRESETS knob on every page. USER, PROJECT: the selection, as KNOB 1; TOOLS: nothing; HOME,
 * PRESETS and every other page (SEQ's placeholder too): the next sound, SAVE held undoes it */
static int test_presets_knob(void)
{
    int bad = 0, sel_ok = 1, snd_ok = 1, undo_ok = 1, tools_ok = 1;
    uint32_t i, n_snd = 0;
    for (i = 0; i < NPAGES; i++) {
        track_t before;
        uint32_t g = PAGES[i].graph, uslot;
        int16_t slot;
        ui_power_on();
        song.sel = 0;
        ui.home = 0; ui.page = (uint8_t)i; page_entered();
        if (!page_visible(i))
            continue;
        frame();
        before = *TSEL;
        uslot = ui.uslot; slot = song.g[G_SLOT];
        turn(EN_PRESET, 1);
        {
            int same_snd = TSEL->preset == before.preset && TSEL->eng_req == before.eng_req &&
                           !memcmp(TSEL->p, before.p, sizeof before.p);
            if (g == GR_USER) {
                sel_ok &= ui.uslot == uslot + 1u && same_snd;
            } else if (g == GR_SLOTS) {
                sel_ok &= song.g[G_SLOT] == slot && same_snd;   /* (one song: no slot to pick) */
            } else if (g == GR_TOOLS) {
                tools_ok &= same_snd && !ui.confirm;
            } else {
                snd_ok &= !same_snd && ui.page == i && !ui.home;
                hold(B_SAVE);
                undo_ok &= TSEL->preset == before.preset && TSEL->eng_req == before.eng_req &&
                           !memcmp(TSEL->p, before.p, sizeof before.p);
                n_snd++;
            }
        }
    }
    bad += check("#94 PRESETS on USER, PROJECT: the selection; nothing loaded", sel_ok);
    bad += check("#94 PRESETS on TOOLS does nothing", tools_ok);
    bad += check("#94 PRESETS elsewhere (EDIT, ENV, LFO, FX, SCL, ARP, MIXER, SEQ, ...): the next sound",
                 snd_ok && n_snd >= 15u);
    bad += check("#94   SAVE held undoes each of those loads", undo_ok);
    ui_power_on();
    hold(B_HOME);
    i = ui.menu_sel;
    turn(EN_PRESET, 1);
    bad += check("#94 the menu: PRESETS still moves the selection", ui.menu == 1 && ui.menu_sel == i + 1u);
    return bad;
}

/* 1.2 (Discussion #153): the pages by button. HOME: HOME > MIXER > CLOCK; SEQ and GLO: the sequencer's placeholder
 * (TODO); SAVE: USER -> PROJECT -> TOOLS -> PRESETS; a remembered page that left its family */
static int test_page_cycles(void)
{
    static const char *const SAVE_CYC[5] = {"USER", "PROJECT", "TOOLS", "PRESETS", "USER"};
    int bad = 0, ok = 1;
    uint32_t i;
    /* the cycles: each button's taps, in order */
    ui_power_on();
    for (i = 0; i < 5u; i++) {
        press(B_SAVE); frames(320);
        ok &= !ui.home && cur_page()->fam == FAM_SAVE && str_eq(cur_page()->title, SAVE_CYC[i]);
    }
    bad += check("1.5: SAVE cycles USER > PROJECT > TOOLS > PRESETS > USER", ok);
    ok = 1;
    for (i = 0; i < NPAGES; i++) {                       /* every page in a family its button reaches, and its LED */
        uint32_t f = PAGES[i].fam;
        ok &= f != FAM_SEQ || PAGES[i].graph == GR_TODO;   /* (the sequencer's placeholder) */
        ok &= f != FAM_GLO;                                /* (GLO opens SEQ's page) */
        ok &= f != FAM_TRK || PAGES[i].graph == GR_TRK || str_eq(PAGES[i].title, "CLOCK");   /* HOME's: MIXER CLOCK */
        ok &= PAGES[i].graph != GR_TRK || f == FAM_TRK;
        ui_power_on(); ui.home = 0; ui.page = (uint8_t)i; page_entered();
        ok &= FAM_BTN[cur_fam()] == (f == FAM_TRK ? B_HOME : f == FAM_GLO ? B_GLO : f == FAM_SEQ ? B_SEQ :
                                    f == FAM_SAVE ? B_SAVE : FAM_BTN[f]);
    }
    for (i = 0; i < NPAGES; i++)
        ok &= !str_eq(PAGES[i].title, "STEP") && !str_eq(PAGES[i].title, "AUTOMATION") && !str_eq(PAGES[i].title, "PHRASES");
    bad += check("SEQ has its placeholder alone (TODO); no STEP, AUTOMATION, PHRASES; MIXER and CLOCK HOME's (their LED "
                 "HOME)", ok);
    {   /* 1.2: GLOBAL and SYSTEM are gone; their knobs: BPM SWG CLK on HOME > CLOCK, TUNE and ROUT on no page (the MENU's),
         * MIDI SYNC CPU on none (INFO shows the USB state and the CPU) */
        uint32_t k, clock = 0;
        ok = 1;
        for (i = 0; i < NPAGES; i++) {
            ok &= !str_eq(PAGES[i].title, "GLOBAL") && !str_eq(PAGES[i].title, "SYSTEM");
            for (k = 0; k < 4u && PAGES[i].scope == SC_GLOBAL; k++)
                ok &= PAGES[i].id[k] != G_TUNE && PAGES[i].id[k] != G_ROUTE &&
                      (str_eq(PAGES[i].title, "TECHNO") || (PAGES[i].id[k] != G_GPRE && PAGES[i].id[k] != G_GSCL &&
                                                         PAGES[i].id[k] != G_GROOT));   /* (2026-10: GEN's ids) */
            if (str_eq(PAGES[i].title, "CLOCK"))
                clock = i + 1u;
        }
        ok &= clock && PAGES[clock - 1u].fam == FAM_TRK && PAGES[clock - 1u].scope == SC_GLOBAL &&
              PAGES[clock - 1u].id[0] == G_BPM && PAGES[clock - 1u].id[1] == G_SWING && PAGES[clock - 1u].id[2] == G_CLOCK &&
              PAGES[clock - 1u].id[3] == 0xFFu;
        bad += check("1.2: no GLOBAL, no SYSTEM page; HOME > CLOCK has BPM SWG CLK (three knobs), TUNE and ROUT on no page", ok);
    }
    /* remembered pages: one that left its family, or out of range, gives the family's first (1.2: GLO's whatever it was,
     * GLOBAL and SYSTEM too, now SEQ's; HOME's never remembered: from HOME the MIXER) */
    ui_power_on();
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_TRK) ui.fam_last[FAM_GLO] = (uint8_t)i;
    ui.fam_last[FAM_SAVE] = 200u;
    ui.fam_last[FAM_ENV] = 250u;
    open_family(FAM_GLO); ok = cur_page()->graph == GR_TODO && cur_page()->fam == FAM_SEQ;
    open_family(FAM_SEQ); ok &= cur_page()->graph == GR_TODO;   /* (its only page) */
    go_home();
    open_family(FAM_SEQ); ok &= cur_page()->graph == GR_TODO;
    open_family(FAM_ENV); ok &= str_eq(cur_page()->title, "ENV");
    open_family(FAM_SAVE); ok &= str_eq(cur_page()->title, "USER");
    open_family(FAM_GLO); open_family(FAM_GLO);          /* TODO again (its only page) */
    ok &= cur_page()->graph == GR_TODO;
    go_home(); open_family(FAM_GLO); ok &= cur_page()->graph == GR_TODO;
    for (i = 0; i < 256u; i++) {                         /* any remembered index (the old GLOBAL 25, SYSTEM 27, ..) */
        go_home(); ui.fam_last[FAM_GLO] = (uint8_t)i; open_family(FAM_GLO);
        ok &= cur_page()->graph == GR_TODO && ui.fam_last[FAM_GLO] == ui.page;
        go_home(); ui.fam_last[FAM_TRK] = (uint8_t)i; press(B_HOME);
        ok &= cur_page()->graph == GR_TRK && !ui.home;
    }
    bad += check("1.2: a remembered page no longer in its family (or none) lands on the family's first; GLO always SEQ's, "
                 "HOME always the MIXER first", ok);

    return bad;
}

static int led_of(const uint8_t *l, uint32_t id);
static int test_product_ux(void)
{
    int bad = 0, ok = 1;
    uint32_t i, p, b;
    for (i = 0; i < NPAGES; i++) {
        ui_power_on(); set_engine_of(TSEL, ENGI_WAVE);
        ui.home = 0; ui.page = (uint8_t)i; page_entered();
        if (!page_visible(i))
            continue;
        press(B_REC);
        ok &= !transport_req && ui.page == i && !ui.home;
    }
    bad += check("REC does nothing on every page (no recording yet; PLAY does not start)", ok);
    ui_power_on(); hold(B_HOME); press(B_REC);
    bad += check("REC in the MENU does nothing (the menu stays, no transport start)", ui.menu == 1 && !transport_req);
    ui.menu_sel = MI_ABOUT; press(B_OCTUP); press(B_REC);
    bad += check("REC in ABOUT does nothing (the document and its scroll stay)",
                 ui.menu == 2 && !ui.menu_scroll && !transport_req);
    ui_power_on(); press(B_HOME);
    bad += check("1.2: HOME tapped on HOME opens the four-channel MIXER", cur_page()->graph == GR_TRK && !ui.home &&
                 cur_fam() == FAM_TRK && FAM_BTN[cur_fam()] == B_HOME);
    int16_t len = TSEL->p[P_SLEN], send = TSEL->p[P_REV];
    turn(EN_K3, 1);
    bad += check("MIXER K3 edits reverb, never sequence length", TSEL->p[P_SLEN] == len && TSEL->p[P_REV] == send + 1);
    press(B_HOME);
    ok = !ui.home && !ui.menu && str_eq(cur_page()->title, "CLOCK") && cur_fam() == FAM_TRK && FAM_BTN[cur_fam()] == B_HOME;
    {   /* (CLOCK: HOME's LED lit, as on MIXER; GLO's not) */
        uint8_t pos0[sizeof led_pos];
        ui_leds();                                      /* (its positions made once, then ours) */
        memcpy(pos0, led_pos, sizeof pos0);
        memset(led_pos, 0xFF, sizeof led_pos);
        led_pos[panel.btn[B_HOME]] = 0u << 3 | 1u;
        led_pos[panel.btn[B_GLO]] = 0u << 3 | 2u;
        frame(); ui_leds();
        ok &= led_of(fm1_led, panel.btn[B_HOME]) && !led_of(fm1_led, panel.btn[B_GLO]);
        memcpy(led_pos, pos0, sizeof pos0);
    }
    {   /* CLOCK's knobs: BPM SWG CLK as GLO > GLOBAL's were; KNOB 4 nothing */
        int16_t bpm = song.g[G_BPM], swg = song.g[G_SWING], clk = song.g[G_CLOCK], tune = song.g[G_TUNE];
        turn(EN_K1, 1); turn(EN_K2, 1); turn(EN_K3, 1); turn(EN_K4, 5);
        ok &= song.g[G_BPM] == bpm + 1 && song.g[G_SWING] == swg + 1 && song.g[G_CLOCK] != clk && song.g[G_TUNE] == tune;
        song.g[G_CLOCK] = clk;
    }
    press(B_HOME);
    ok &= ui.home && !ui.menu;
    press(B_HOME);
    ok &= cur_page()->graph == GR_TRK;
    go_title("ENV"); press(B_HOME);
    ok &= ui.home;
    go_title("CLOCK"); press(B_HOME);
    ok &= ui.home;
    bad += check("1.2: HOME cycles HOME > MIXER > CLOCK > HOME (CLOCK: BPM SWG CLK, HOME's LED); from any other page HOME", ok);
    hold(B_HOME);
    bad += check("1.2: HOME held still opens the menu", ui.menu == 1);
    ui_power_on();
    frames(320);                                       /* (taps further apart than a double tap: #83) */
    press(B_GLO); frames(320); ok = cur_page()->graph == GR_TODO && str_eq(cur_page()->title, "SEQ");
    press(B_GLO); frames(320); ok &= cur_page()->graph == GR_TODO && !ui.home;
    go_title("CLOCK"); press(B_GLO); frames(320); ok &= cur_page()->graph == GR_TODO;
    bad += check("GLO opens SEQ's placeholder directly, again the same (no GLOBAL, no SYSTEM, no MIXER)", ok);
    go_home(); press(B_SEQ); frames(320);
    ok = cur_page()->graph == GR_TODO && !ui.home && !ui.layer;
    go_home(); btn_down(B_SEQ); frames(500);
    ok &= !ui.layer;
    btn_up(B_SEQ); frame();
    bad += check("SEQ opens its placeholder (TODO); held: no layer, its page on release", ok && cur_page()->graph == GR_TODO);
    ui_power_on(); open_family(FAM_EDIT);
    ok = 1;
    for (i = 0; i < 8u; i++) { open_family(FAM_EDIT); ok &= page_visible(ui.page); }
    for (i = 0; i < NPAGES; i++)
        for (p = 0; p < 4u; p++)
            ok &= PAGES[i].id[p] == 0xFF || PAGES[i].scope == SC_GLOBAL || PAGES[i].id[p] < P_FM1_ATK ||
                  PAGES[i].id[p] > P_FM4_LEVEL;
    bad += check("no page holds DIGITAL's operator values (ids 61..80, removed in 1.5)", ok);
    ui_power_on(); seq_start(); go_home();
    {
        int16_t was = TSEL->p[ENGINES[TSEL->eng_req]->knob[0]];
        turn(EN_K1, 1);
        ok = TSEL->p[ENGINES[TSEL->eng_req]->knob[0]] != was;
        press(B_PLAY); events_block(32);
        ok &= TSEL->p[ENGINES[TSEL->eng_req]->knob[0]] != was;
    }
    bad += check("1.5: playing, a sound knob: the edit stays (no automation)", ok);
    ui_power_on(); ui_leds();
    memset(led_pos, 0xFF, sizeof led_pos);
    led_pos[panel.btn[B_PLAY]] = 1; led_pos[panel.btn[B_REC]] = 2;
    song.playing = 1; ok = 1;
    for (i = 0; i < 120u; i++) { fm1_ms += 17; ui_leds(); ok &= (fm1_led[0] & 6u) == 0u && (fm1_led[8] & 2u); }
    bad += check("PLAY's green LED stays lit across time and audio block phase; REC dark (no recording)", ok);
    {   /* the power-on sweep (hal/fm1_input.h fm1_led_anim_on) has the LEDs: the UI writes none of them while it runs,
         * all of its own picture on the next frame after */
        uint8_t l0[FM1_NCOL], d0[FM1_NCOL];
        memset(fm1_led, 0x5A, sizeof fm1_led);
        memset(fm1_led_dim, 0xA5, sizeof fm1_led_dim);
        memcpy(l0, fm1_led, sizeof l0);
        memcpy(d0, fm1_led_dim, sizeof d0);
        host_anim = 1;
        ui_leds();
        ok = !memcmp(l0, fm1_led, sizeof l0) && !memcmp(d0, fm1_led_dim, sizeof d0);
        host_anim = 0;
        ui_leds();
        ok &= (fm1_led[0] & 6u) == 0u && (fm1_led[8] & 2u) && fm1_led[1] != 0x5A;
        bad += check("the power-on LED sweep: the UI leaves the LEDs alone while it runs, takes them after", ok);
    }
    led_pos_init();
    ok = 1;
    for (p = 0; p < NPALETTES; p++) for (b = 0; b < 1u; b++) {
        uint16_t graph[240 * H_GRAPH], columns[240 * (Y_SEP_END - Y_LABEL)];
        ui_power_on(); palette_set(p); settings.zoom = 1;
        open_family(FAM_ENV); frame();
        memcpy(graph, host_screen + Y_GRAPH * 240, sizeof graph);
        memcpy(columns, host_screen + Y_LABEL * 240, sizeof columns);
        ui.hot_col = 1; ui.hot_t = 40; ui.force = 1; ui_draw();
        ok &= !memcmp(graph, host_screen + Y_GRAPH * 240, sizeof graph);
        int changed = 0;
        for (uint32_t y = 0; y < Y_SEP_END - Y_LABEL; y++) for (uint32_t x = 0; x < 240; x++) {
            uint16_t old = columns[y * 240 + x], now = host_screen[(Y_LABEL + y) * 240 + x];
            if (x >= 64 && x < 119) changed |= old != now;
            else ok &= old == now;
        }
        ok &= changed;
    }
    bad += check("all palettes: active column is subtle, stable-size, no zoom over graph", ok);

    ui_power_on(); return bad;
}

static int test_panel(void)
{
    int bad = 0;
    panel = PANEL_DEFAULT;
    panel.btn[B_PLAY] = panel.btn[B_REC];
    panel_init();
    bad += check("duplicate button mappings recover to the default panel", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.enc[EN_K1] = panel.enc[EN_K2];
    panel_init();
    bad += check("duplicate knob mappings recover to the default panel", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.btn[B_PLAY] = 255;
    panel_init();
    bad += check("out-of-range panel data recovers without an invalid shift", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.dir[EN_K1] = -1;
    panel_init();
    bad += check("a valid reversed knob mapping is retained", panel.dir[EN_K1] == -1);
    return bad;
}

/* ------------------------------------------------- the FX hold layer --- */
static void key_down(uint32_t k) { fm1_in.notes |= 1u << k; host_notes |= 1u << k; keyboard_block(); }
static void key_up(uint32_t k) { fm1_in.notes &= ~(1u << k); keyboard_block(); }
static uint32_t gates(void)                        /* voices of every track with their key down */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NTRK; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].gate != 0;
    return n;
}
static uint32_t white(uint32_t w) { return key_at(0, w); }

/* a layer's key LEDs as the old 250 ms blink read them: lit, and the breathing ones (#119) "on" at even quarters,
 * so (a ^ b) over ms 0 and 250 = the ones that can be pressed, (a & b) = lit */
static uint32_t leds_at(uint32_t ms)
{
    uint32_t br, m;
    fm1_ms = ms;
    m = layer_leds(&br);
    return m | (((ms / 250u) & 1u) ? 0u : br);
}

static int test_layer(void)
{
    int bad = 0, ok, flash;
    uint32_t i, mo, k;
    /* tap: on release, the page; held briefly: no map ever */
    ui_power_on();
    btn_down(B_FX); frame();
    ok = ui.home;
    frames(64); btn_up(B_FX); frame();
    bad += check("FX tap: nothing on press, the FX page when let go", ok && !ui.home && cur_page()->fam == FAM_FX);
    frames(320);                                       /* (not a double tap: #83) */
    press(B_FX);
    bad += check("  a second tap: the next page of the family (REVERB, then DELAY)", str_eq(cur_page()->title, "DELAY"));
    ui_power_on();
    btn_down(B_FX);
    for (flash = 0, i = 0; i < 22u; i++) { frame(); flash |= ui.layer; }      /* 0.35 s */
    btn_up(B_FX); frame();
    bad += check("  held 0.35 s (HOLD 0.4): still a tap, the map never shown", !flash && !ui.home);
    /* peek: past HOLD alone, the map; letting go does nothing */
    ui_power_on();
    btn_down(B_FX); frames(368);
    ok = !ui.layer;
    frames(64);
    ok &= ui.layer == LAYER_FX;
    btn_up(B_FX); frame();
    bad += check("FX held past 0.4 s: the map (not before); let go: no page, the map gone",
                 ok && ui.home && !ui.layer);
    /* the HOLD setting: 0.6 s */
    ui_power_on();
    hold(B_HOME); ui.menu_sel = MI_HOLD; turn(EN_K1, 1); turn(EN_K1, 1); ok = settings_hold == 3u; hold(B_HOME);
    btn_down(B_FX); frames(512); flash = ui.layer; btn_up(B_FX); frame();
    bad += check("menu HOLD 0.6 s (KNOB 1): FX held 0.5 s is a tap, no map", ok && !flash && !ui.home && !ui.menu);
    {
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        settings_export(&p); settings_hold = HOLD_DEF;
        bad += check("  HOLD is saved with the settings and read back", settings_import(&p, sizeof p) && settings_hold == 3u);
    }
    /* the LEDS setting: KNOB 1 / OCT+ up to INV, left / OCT- down to OFF; saved in the retired zoom field */
    ui_power_on();
    hold(B_HOME); ui.menu_sel = MI_LEDS;
    ok = settings_leds == LEDS_DIM;
    turn(EN_K1, 1); ok &= settings_leds == LEDS_INV;
    turn(EN_K1, 1); ok &= settings_leds == LEDS_INV;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_DIM;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_DIM_LO;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_OFF;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_OFF;
    press(B_OCTUP); ok &= settings_leds == LEDS_DIM_LO && ui.menu == 1u;
    press(B_OCTUP); ok &= settings_leds == LEDS_DIM;
    press(B_OCTUP); ok &= settings_leds == LEDS_INV;
    press(B_OCTUP); ok &= settings_leds == LEDS_INV && ui.menu == 1u;   /* (stops at the end) */
    for (k = 0; k < 4u; k++) press(B_OCTDN);
    ok &= settings_leds == LEDS_OFF && ui.menu == 1u;
    ok &= !strcmp(LEDS_NAME[LEDS_OFF], "OFF") && !strcmp(LEDS_NAME[LEDS_DIM_LO], "DIM LO") &&
          !strcmp(LEDS_NAME[LEDS_DIM], "DIM HI") && !strcmp(LEDS_NAME[LEDS_INV], "INV");
    {   /* a setting changed from elsewhere (the editor's MENU_SET: menu_put) while the MENU is shown: redrawn */
        static uint16_t before[240 * 240];
        frame();
        memcpy(before, host_screen, sizeof before);
        menu_put(MI_LEDS, 3);                          /* (OFF -> INV, the row shown) */
        frame();
        ok &= settings_leds == LEDS_INV && memcmp(before, host_screen, sizeof before) && menu_get(MI_LEDS) == 3u;
        menu_put(MI_LEDS, 0);
        frame();
        ok &= !memcmp(before, host_screen, sizeof before);
    }
    hold(B_HOME);
    bad += check("menu LEDS: DIM HI by default; KNOB 1 OFF < DIM LO < DIM HI < INV (stops at the ends), OCT+ / OCT- the same; "
                 "a change from the editor redraws the shown MENU", ok && !ui.menu);
    {
        persist_t p = {0};
        uint32_t m;
        p.magic = PERSIST_MAGIC; p.panel = panel;
        ok = 1;
        settings.zoom = 0;                              /* (DIM HI keeps an older record's 0 / 1 as saved) */
        for (m = 0; m < LEDS_COUNT; m++) {
            settings_leds = (uint8_t)m;
            settings_export(&p); settings_leds = LEDS_DIM;
            ok &= settings_import(&p, sizeof p) && settings_leds == m && leds_stored_ok(p.zoom) &&
                  p.zoom == (m == LEDS_DIM ? 0u : LEDS_TAG | m);
            settings_export(&p);                    /* (idempotent: saved again, the same field) */
            ok &= p.zoom == (m == LEDS_DIM ? 0u : LEDS_TAG | m);
        }
        settings_leds = LEDS_DIM;
        settings_export(&p);
        bad += check("  LEDS is saved with the settings and read back (all four); DIM HI saves the field as before (0)",
                     ok && p.zoom == 0u && settings_import(&p, sizeof p) && settings_leds == LEDS_DIM);
    }
    /* combo: a key with FX: at once, silent, no MIDI */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    go_page(GR_TODO); song.playing = 1;
    {
        btn_down(B_FX); key_down(white(2)); frame();
        ok = ui.layer == LAYER_FX && !gates() && mo_w == mo && (kb_layer >> white(2)) & 1u &&
             (perf_held & PF_BIT(PF_FRZ));                    /* white key 3 (A3): FREEZE */
        for (i = 0; i < 40u; i++) events_block(CTL);
        bad += check("FX + a key: the map at once; no voice, no MIDI", ok);
        key_up(white(2)); frame();
        bad += check("  the key let go: its effect off, no note-off sent", !perf_held && !kb_layer && mo_w == mo);
        btn_up(B_FX); frame();
        bad += check("  FX let go after a combo: no tap, the map gone", !ui.layer && cur_page()->graph == GR_TODO);
    }
    song.playing = 0;
    /* FX let go first: the effect stays with its key, the map too; a key pressed now is a note */
    ui_power_on();
    btn_down(B_FX); key_down(white(0)); frame();          /* white key 1 (F3): LPF */
    btn_up(B_FX); frame();
    ok = (perf_held & PF_BIT(PF_LPF)) && ui.layer == LAYER_FX;
    key_down(white(6)); frame();
    ok &= gates() > 0 && !((kb_layer >> white(6)) & 1u);
    key_up(white(6)); key_up(white(0)); frame();
    bad += check("FX let go first: the key holds its effect and the map; a new key is a note", ok && !ui.layer && !perf_held);
    /* OCT UP (B3) and OCT DN (C4), the 4th and 5th white keys: held effects, silent; KNOB 4 the shimmer while
     * held, back to 0 with FX; D4 on does nothing */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    btn_down(B_FX); key_down(white(3)); frame();
    ok = ui.layer == LAYER_FX && !gates() && mo_w == mo && perf_held == PF_BIT(PF_OUP);
    perf_begin(CTL);                                    /* (an audio block: OCT UP runs, KNOB 4 is SHIMR) */
    turn(EN_K4, 40);
    ok &= perf_k[3] == 40;
    key_up(white(3)); key_down(white(4)); frame();
    ok &= perf_held == PF_BIT(PF_ODN) && !gates() && mo_w == mo;
    key_up(white(4)); key_down(white(5)); frame();
    ok &= !perf_held && (kb_layer >> white(5)) & 1u && !gates();     /* (D4: nothing) */
    key_up(white(5)); key_down(white(12)); frame();
    ok &= !perf_held && (kb_layer >> white(12)) & 1u && !gates();
    key_up(white(12)); btn_up(B_FX); frame();
    bad += check("FX + B3 / C4: OCT UP / OCT DN held, silent, no MIDI; KNOB 4 turns; D4, D5 nothing; FX let go: K4 0",
                 ok && !perf_held && !ui.layer && !perf_k[3] && mo_w == mo);
    /* REVERB > TYPE (FX family, global; 2026-10-18): DRY / SPRING / GATED / PLATE on KNOB 1 (PLATE at power-on), SIZE
     * and MIX beside it, kept by a project */
    ui_power_on();
    go_title("REVERB");
    ok = cur_page()->fam == FAM_FX && cur_page()->id[0] == G_RTYPE && cur_page()->id[1] == G_RSIZE &&
         cur_page()->id[2] == G_RMIX && song.g[G_RTYPE] == 3 && str_eq(GP[G_RTYPE].names[3], "PLATE");
    turn(EN_K1, -3);
    ok &= song.g[G_RTYPE] == 0 && str_eq(GP[G_RTYPE].names[song.g[G_RTYPE]], "DRY");
    turn(EN_K1, 1);
    ok &= song.g[G_RTYPE] == 1 && str_eq(GP[G_RTYPE].names[song.g[G_RTYPE]], "SPRING");
    turn(EN_K1, 5);
    ok &= song.g[G_RTYPE] == GP[G_RTYPE].max;
    turn(EN_K1, -(int32_t)GP[G_RTYPE].max + 1);
    ok &= song.g[G_RTYPE] == 1;
    song.playing = 0;
    project_save(2);
    turn(EN_K1, -1);
    ok &= song.g[G_RTYPE] == 0;
    project_load(2);
    bad += check("REVERB page: TYPE PLATE (power-on) / DRY / SPRING on KNOB 1 (SIZE, MIX beside it); a project keeps SPRING",
                 ok && song.g[G_RTYPE] == 1);
    /* DLY 2 > TYPE (Blue Hare): CLEAN (power-on) / TAPE / WORN on KNOB 1, kept by a project */
    ui_power_on();
    go_title("VOICE 3");                                /* (Blue Hare) OCT beside SPREAD */
    bad += check("  VOICE 3 page: SPRD, OCT (-4 .. 4, the sound's octave)", cur_page()->id[0] == P_SPRD && cur_page()->id[1] == P_OCT &&
                 TP[P_OCT].min == -4 && TP[P_OCT].max == 4);
    go_title("DELAY");                                  /* (2026-10-18) the patch's delay: MIX FEED TIME, kept by a project */
    ok = cur_page()->fam == FAM_FX && cur_page()->scope == SC_TRACK && cur_page()->id[0] == P_DLY &&
         cur_page()->id[1] == P_DFDBK && cur_page()->id[2] == P_DTIME && TSEL->p[P_DLY] == 0;
    turn(EN_K1, 40);
    turn(EN_K3, 1);                                     /* (TIME steps the divisions by length) */
    {
        int16_t mixv = TSEL->p[P_DLY], tv = TSEL->p[P_DTIME];
        ok &= mixv > 0 && tv != 1;
        song.playing = 0;
        project_save(2);
        TSEL->p[P_DLY] = 0;
        TSEL->p[P_DTIME] = 1;
        project_load(2);
        bad += check("  DELAY page: MIX FEED TIME, the patch's, out at power-on; a project keeps them",
                     ok && TSEL->p[P_DLY] == mixv && TSEL->p[P_DTIME] == tv);
    }
    go_title("CHORUS");
    bad += check("  CHORUS page: CRT CDP", cur_page()->fam == FAM_FX && cur_page()->id[0] == G_CRATE &&
                 cur_page()->id[1] == G_CDEPTH && cur_page()->id[2] == 0xFFu);
    /* chording: a key held before FX stays a note, its note-off arrives */
    ui_power_on();
    mo = mo_w;
    key_down(white(4)); frame(); btn_down(B_FX); frame(); key_up(white(4)); frame(); btn_up(B_FX); frame();
    for (i = 0; i < 4u; i++) events_block(CTL);
    bad += check("a key held before FX stays a note: note-on, note-off, nothing left held", mo_w == mo + 2u && !gates() &&
                 !kb_layer);
    /* knob macros: not the page's, not recorded, back to 0 when FX is let go */
    ui_power_on();
    go_title("ENV"); song.playing = 1;
    {
        int16_t atk = TSEL->p[P_ATK];
        btn_down(B_FX); frame();
        turn(EN_K1, -12); turn(EN_K2, 30);
        ok = perf_k[0] == -12 && perf_k[1] == 30 && TSEL->p[P_ATK] == atk && ui.layer == LAYER_FX;
        turn(EN_PRESET, 3);
        ok &= user_of(TSEL) == UP_SLOTS && TSEL->preset == trk[0].preset;
        btn_up(B_FX); frame();
        bad += check("FX + KNOB 1 / 2: the macros, not ATK, not recorded; FX let go: back to 0, no tap",
                     ok && !perf_k[0] && !perf_k[1] && str_eq(cur_page()->title, "ENV"));
    }
    song.playing = 0;
    /* buttons in the layer: PLAY works, SAVE, HOME, a page button are swallowed */
    ui_power_on();
    btn_down(B_FX); frame();
    press(B_PLAY);
    ok = transport_req == 1u && ui.layer == LAYER_FX;
    transport_req = 0;
    press(B_SAVE); ok &= ui.home;
    hold(B_SAVE); ok &= !msg_is("UNDO/REDO T1") && ui.home;
    press(B_ENV); ok &= ui.home;
    hold(B_HOME); ok &= !ui.menu && ui.home;
    btn_up(B_FX); frame();
    bad += check("in the layer: PLAY plays; SAVE (tap and hold), ENV, HOME (hold) do nothing", ok && ui.home && !ui.menu);
    btn_down(B_FX); frame(); btn_down(B_ENV); frame(); btn_up(B_FX); frame(); btn_up(B_ENV); frame();
    bad += check("  a page button pressed with FX, let go after it: still nothing", ui.home);
    /* release-act: every page button acts when let go */
    {
        static const uint8_t PB[6] = {B_ENV, B_LFO, B_SCL, B_ARP, B_GLO, B_EDIT};
        static const uint8_t PF[6] = {FAM_ENV, FAM_LFO, FAM_SCL, FAM_ARP, FAM_SEQ, FAM_EDIT};   /* (GLO: SEQ's placeholder) */
        ok = 1;
        for (i = 0; i < 6u; i++) {
            ui_power_on();
            btn_down(PB[i]); frame();
            ok &= ui.home;
            btn_up(PB[i]); frame();
            ok &= !ui.home && cur_page()->fam == PF[i];
            if (PB[i] == B_SCL || PB[i] == B_GLO || PB[i] == B_EDIT)
                continue;                             /* (a layer of their own: held long is a peek) */
            ui_power_on();
            hold(PB[i]);                              /* (no layer of their own: a long press is a tap too) */
            ok &= !ui.home && cur_page()->fam == PF[i];
        }
        bad += check("ENV LFO SCL ARP GLO EDIT act when let go, not on press (ENV LFO ARP held long: the same)", ok);
    }
    ui_power_on(); hold(B_HOME);
    ok = ui.menu == 1; hold(B_HOME); ok &= !ui.menu;
    go_home(); hold(B_SEQ); ok &= !ui.home && !ui.layer && cur_page()->graph == GR_TODO;
    bad += check("HOME (menu) hold of 0.7 s unchanged; SEQ held: no layer, its page (a long press a tap)", ok);
    /* no layer in the menu or a dialog: FX + a key is a note */
    ui_power_on(); hold(B_HOME);
    btn_down(B_FX); frame(); key_down(white(3)); frame();
    ok = !ui.layer && !perf_mask && gates() > 0 && !kb_layer;
    key_up(white(3)); btn_up(B_FX); frame();
    bad += check("in the menu: no layer, FX + a key plays the key; FX let go: no page", ok && ui.menu == 1);
    ui_power_on(); ui.confirm = CF_INIT_SOUND;
    btn_down(B_FX); frames(512);
    ok = !ui.layer;
    btn_up(B_FX); frame();
    bad += check("in a dialog: FX held shows no map and taps nothing", ok && ui.confirm == CF_INIT_SOUND);
    /* UBOOT: the countdown closes the map and stops every effect; OCT- + OCT+ are still seen */
    ui_power_on();
    btn_down(B_FX); key_down(white(5)); frame();
    btn_down(B_OCTDN); btn_down(B_OCTUP); frame();
    ok = ui.layer == LAYER_FX && (fm1_in.buttons & 3u << panel.btn[B_OCTDN]) != 0u;
    ui.uboot = 3; frame();                          /* (main.c: OCT- + OCT+ held 2 s) */
    ok &= !ui.layer && perf_kill && !perf_mask && perf_begin(CTL) >= 0;
    ok &= (fm1_in.buttons & ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]))) ==
          ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]));
    ui.uboot = 0;
    btn_up(B_OCTDN); btn_up(B_OCTUP); key_up(white(5)); btn_up(B_FX); frame();
    bad += check("UBOOT countdown over the layer: the map closes, effects off; OCT- + OCT+ untouched", ok && !perf_kill && !kb_layer);
    /* the LEDs of the map */
    ui_power_on();
    btn_down(B_FX); key_down(white(1)); frame();
    {
        uint32_t a, b2;
        song.g[G_BPM] = 120;
        a = leds_at(0); b2 = leds_at(250);
        k = white(1);
        ok = ((a >> k) & 1u) && ((b2 >> k) & 1u);                    /* held: lit */
        for (i = 0; i < 5u; i++)                                      /* the other effects (F3 .. C4): breathe */
            ok &= i == 1u || ((a ^ b2) >> white(i)) & 1u;
        for (i = 5; i < 16u; i++)                                     /* D4 .. G5: no effect, dark */
            ok &= !((a | b2) >> white(i) & 1u);
        ok &= ((a ^ b2) >> key_at(1, 0)) & 1u && !((a | b2) >> key_at(1, NTRK) & 1u);   /* a mute breathes, a spare black dark */
    }
    key_up(white(1)); btn_up(B_FX); frame();
    bad += check("map LEDs: held lit, the 5 effects breathe, D4 .. G5 dark", ok);
    usb.config = 0;
    return bad;
}

/* ------------------------------------------------------------ NAME --- */
static void nm_tap(uint32_t k)                    /* a key tapped in NAME: down a frame, up a frame (32 ms) */
{
    fm1_in.notes |= 1u << k; host_notes |= 1u << k; frame();
    fm1_in.notes &= ~(1u << k); frame();
}
static uint32_t nm_black_key(uint32_t f, uint32_t octave)   /* the black key of function f (NB_*: F# G# A# C# D#), octave 0 / 1 */
{
    return key_at(1, f + 5u * octave);
}
static int test_name(void)
{
    int bad = 0, ok;
    uint32_t i, mo;
    char b[16];
    ui_power_on();
    go_page(GR_USER);
    ui.uslot = 2;
    press(B_OCTUP);                               /* (SAVE picked on entry) */
    up_auto_name(b, TSEL->eng_req, 2);
    bad += check("NAME: USER SAVE into an empty slot opens NAME, prefilled with the automatic name",
                 name_on() && nm.kind == NK_USER_SAVE && nm.slot == 2u && str_eq(nm.s, b) && nm.cur == nm.len);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    bad += check("  DELETE removes the character before the cursor, down to empty", nm.len == 0u && nm.cur == 0u && !nm.s[0]);
    /* multi-tap: the same key cycles, another key commits, 0.8 s commits */
    nm_tap(white(0));
    ok = nm.len == 1u && nm.s[0] == 'A' && nm.key == 1u && nm.cur == 0u;
    nm_tap(white(0));
    ok &= nm.s[0] == 'B' && nm.len == 1u;
    nm_tap(white(0));
    bad += check("  a white key types its first letter; tapped again within 0.8 s: the next one, cycling (A B A)",
                 ok && nm.s[0] == 'A' && nm.len == 1u && nm.cur == 0u);
    nm_tap(white(1));
    bad += check("  another key keeps the letter and types its own (A, C)", nm.len == 2u && str_eq(nm.s, "AC") && nm.cur == 1u &&
                 nm.key == 2u);
    frames(800);
    bad += check("  0.8 s without a tap keeps it: the cursor moves on", nm.key == 0u && nm.cur == 2u && str_eq(nm.s, "AC"));
    nm_tap(white(1));
    frames(400);
    nm_tap(white(1));
    bad += check("  a second tap within 0.8 s (0.43 s later) still cycles (C -> D)", str_eq(nm.s, "ACD") && nm.key == 2u);
    frames(800);
    nm_tap(white(4)); nm_tap(white(4)); nm_tap(white(4));   /* IJK: K */
    nm_tap(nm_black_key(NB_SPACE, 1));                       /* SPACE (the upper octave's G#) keeps K first */
    bad += check("  a three-letter key (IJK) cycles to K; SPACE keeps it and adds a space", str_eq(nm.s, "ACDK ") && nm.cur == 5u &&
                 !nm.key);
    nm_tap(nm_black_key(NB_DEL, 1));
    nm_tap(nm_black_key(NB_LEFT, 0));
    nm_tap(nm_black_key(NB_LEFT, 0));
    nm_tap(white(2));                             /* E inserted before D */
    frames(800);
    bad += check("  DELETE, cursor left twice, a letter inserted at the cursor", str_eq(nm.s, "ACEDK") && nm.cur == 3u);
    nm_tap(nm_black_key(NB_RIGHT, 1));
    nm_tap(nm_black_key(NB_RIGHT, 0));
    nm_tap(nm_black_key(NB_RIGHT, 0));
    bad += check("  cursor right stops at the end", nm.cur == 5u);
    /* 123 */
    nm_tap(nm_black_key(NB_MODE, 0));
    nm_tap(white(1)); nm_tap(white(9)); nm_tap(white(15));
    bad += check("  D# switches to 123: one tap, one character (2, 0, +), no cycling", nm.num && str_eq(nm.s, "ACEDK20+") &&
                 nm.cur == 8u && !nm.key);
    nm_tap(nm_black_key(NB_MODE, 1));
    nm_tap(white(15)); nm_tap(white(15)); nm_tap(white(15));
    frames(800);
    bad += check("  back to ABC: the last key (0-.) gives 0, then -, then .", !nm.num && str_eq(nm.s, "ACEDK20+.") && nm.cur == 9u);
    /* the knobs */
    turn(EN_K1, -2);
    turn(EN_K2, 1);
    bad += check("  KNOB 1 moves the cursor, KNOB 2 changes the character there (+ -> SPACE, wrapping)",
                 nm.cur == 7u && str_eq(nm.s, "ACEDK20 ."));
    turn(EN_K2, 5);
    bad += check("  KNOB 2 on: SPACE + 5 = E", nm.s[7] == 'E');
    turn(EN_K1, 9);
    turn(EN_K2, 1);
    bad += check("  KNOB 1 to the end, KNOB 2 adds a character there (A)", str_eq(nm.s, "ACEDK20E.A") && nm.cur == 9u && nm.len == 10u);
    turn(EN_K2, -2);
    bad += check("  .. and turns it back past SPACE to the last symbol (+)", nm.s[9] == '+');
    /* full */
    nm_tap(nm_black_key(NB_RIGHT, 0));
    nm_tap(white(7)); frames(800); nm_tap(white(7)); frames(800);
    ui.msg_t = 0;
    nm_tap(white(7));
    bad += check("  12 characters at most: NAME FULL, nothing typed", nm.len == 12u && msg_is("NAME FULL") && str_eq(nm.s, "ACEDK20E.+PP"));
    /* held DELETE repeats */
    fm1_in.notes |= 1u << nm_black_key(NB_DEL, 0); host_notes |= 1u << nm_black_key(NB_DEL, 0);
    frames(16 * 40);
    fm1_in.notes &= ~(1u << nm_black_key(NB_DEL, 0)); frame();
    bad += check("  DELETE held 0.64 s: one, then it repeats after 0.45 s, every 0.09 s (3 in all)", nm.len == 9u);
    /* the keys never sound or send MIDI */
    usb.config = 1; mo = mo_w;
    {
        song.playing = 1;
        key_down(white(3)); key_down(nm_black_key(NB_SPACE, 0)); frame();
        for (i = 0; i < 8u; i++) events_block(CTL);
        ok = !gates() && mo_w == mo && song.keys_mute;
        key_up(white(3)); key_up(nm_black_key(NB_SPACE, 0)); frame();
        stop_transport();
        bad += check("  keys in NAME: no voice, no MIDI (seq.c: song.keys_mute)", ok && mo_w == mo);
    }
    usb.config = 0;
    {   /* LEDs: all 27 keys do something; the cycling key blinks */
        uint32_t a, c, t0;
        nm_tap(nm_black_key(NB_DEL, 0)); nm_tap(nm_black_key(NB_DEL, 0));
        nm_tap(white(5));
        t0 = (fm1_ms / 500u + 1u) * 500u;
        fm1_ms = t0; a = name_leds(); fm1_ms = t0 + 250u; c = name_leds();
        nm.t = fm1_ms;                            /* (the letter still cycling) */
        bad += check("  LEDs: every key lit, the cycling key (LM) blinks", ((a | c) == 0x7FFFFFFu) && ((a ^ c) == (1u << white(5))));
    }
    /* cancel */
    {
        uint32_t page = ui.page;
        press(B_OCTDN);
        bad += check("  OCT- cancels: nothing written, back on the USER page", !name_on() && !up_used(2) && ui.page == page && !ui.home);
    }
    /* typing a name and saving it */
    ui.act = 4;
    press(B_OCTUP);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    nm_tap(white(1)); nm_tap(white(0)); nm_tap(white(0)); nm_tap(nm_black_key(NB_RIGHT, 0));    /* C B */
    nm_tap(nm_black_key(NB_SPACE, 0)); nm_tap(nm_black_key(NB_MODE, 0)); nm_tap(white(1));        /* " 2" */
    nm_tap(nm_black_key(NB_SPACE, 0));                                                           /* a trailing space */
    song.playing = 1;
    press(B_OCTUP);
    bad += check("  OCT+ while playing: STOP TO SAVE, NAME stays", name_on() && msg_is("STOP TO SAVE") && !up_used(2));
    stop_transport();
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  OCT+ stopped: saved as typed, the trailing space dropped (CB 2)", !name_on() && up_used(2) && str_eq(b, "CB 2") &&
                 msg_is("SAVED (RAM)"));
    /* SAVE held does nothing in NAME; the FX layer neither */
    ui.act = 4; press(B_OCTUP); press(B_OCTUP);   /* (U03 used: OVERWRITE?, YES) */
    bad += check("  overwrite: the dialog, then NAME", name_on() && nm.kind == NK_USER_SAVE);
    ui.msg_t = 0;
    hold(B_SAVE);
    btn_down(B_FX); frames(600); ok = !ui.layer; btn_up(B_FX); frame();
    bad += check("  SAVE held, FX held: nothing in NAME (no undo, no map, no page)", name_on() && ok && cur_page()->graph == GR_USER &&
                 !ui.msg_t);
    press(B_OCTDN);
    /* rename: EDIT on USER */
    ui.uslot = 5;
    press(B_EDIT);
    bad += check("  EDIT on an empty slot: EMPTY SLOT", !name_on() && msg_is("EMPTY SLOT") && cur_page()->graph == GR_USER);
    ui.uslot = 2;
    up_load(2);                                   /* (the sound now comes from U03) */
    trk[0].p[P_E0] = 33;                          /* an edit: a rename must not store it */
    press(B_EDIT);
    bad += check("  EDIT on a used slot: NAME (rename) with its name", name_on() && nm.kind == NK_USER_RENAME && str_eq(nm.s, "CB 2"));
    nm_tap(nm_black_key(NB_DEL, 0));
    nm_tap(white(10)); nm_tap(white(10));          /* VW: W */
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  OCT+: renamed (CB W), the sound stored there unchanged", !name_on() && str_eq(b, "CB W") &&
                 up_value(up_rec(2), P_E0) != 33 && msg_is("RENAMED (RAM)"));
    press(B_EDIT);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    press(B_OCTUP);
    {
        char au[16];
        up_name(2, b);
        up_auto_name(au, eng_of_id(up_rec(2)->engine), 2);   /* (an index: the record names its engine by id) */
        bad += check("  a name deleted to nothing: the automatic one", str_eq(b, au));
    }
    /* the sound's own name is the prefill */
    trk[0].user = 3;
    ui.uslot = 7; ui.act = 4;
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  SAVE of a sound that came from U03: prefilled with U03's name", name_on() && str_eq(nm.s, b));
    press(B_OCTDN);
    /* projects */
    go_page(GR_SLOTS);
    song.g[G_SLOT] = 2;
    turn(EN_K4, 1);
    press(B_OCTUP);
    for (i = 0; i < 4u; i++) { nm_tap(white(7)); nm_tap(white(6)); frames(800); }   /* P N P N .. */
    press(B_OCTUP);
    {
        char pn[16];
        int named = project_name(1, pn);
        bad += check("PROJECT SAVE: NAME, typed, saved: the slot has the name, the project too", named && str_eq(pn, "PNPNPNPN") &&
                     str_eq(proj_name, "PNPNPNPN"));
    }
    proj_name[0] = 0;
    project_load(1);
    bad += check("  loading it brings its name back (the next save's prefill)", str_eq(proj_name, "PNPNPNPN"));
    trk[0].p[P_LEVEL] = 7;
    press(B_EDIT);
    for (i = 0; i < 4u; i++) nm_tap(nm_black_key(NB_DEL, 1));
    press(B_OCTUP);
    {
        char pn[16];
        project_name(1, pn);
        bad += check("  EDIT renames a project (PNPN); its music unchanged", str_eq(pn, "PNPN") && stored_param(1, 0, P_LEVEL) != 7 &&
                     msg_is("RENAMED (RAM)"));
    }
    memset(proj_ram_len, 0, sizeof proj_ram_len);       /* (no song stored) */
    proj_find();
    press(B_EDIT);
    bad += check("  EDIT with no song stored: EMPTY SLOT", !name_on() && msg_is("EMPTY SLOT"));
    project_save(0);
    press(B_EDIT);
    hold(B_HOME);
    bad += check("  HOME held in NAME: the menu, NAME closed", ui.menu && !name_on());
    hold(B_HOME);
    return bad;
}

/* the EDIT cycle (no ENGINE page: engines are the EDIT layer's) and its memory */
static int engine_cycle(const char *const *want, uint32_t n)   /* EDIT tapped from HOME, then n - 1 times more */
{
    uint32_t i;
    int ok = 1;
    frames(320);
    go_home(); frame();
    for (i = 0; i < n; i++) {
        press(B_EDIT);
        frames(320);                                  /* (taps further apart than a double tap: #83 locks) */
        ok &= str_eq(cur_page()->title, want[i]);
    }
    return ok;
}
static int test_edit_cycle(void)
{
    static const char *const CYC_P[] = {"SOUND", "VOICE", "VOICE 2", "VOICE 3", "SOUND"};
    static const char *const CYC_A[] = {"EDIT 1", "EDIT 2", "VOICE", "VOICE 2", "VOICE 3", "EDIT 1"};
    int bad = 0, ok;
    uint32_t i;
    char ti[24];
    ui_power_on();
    ui_prefs2 &= (uint8_t)~(PREF_EXPERT >> 16);
    btn_down(B_EDIT); frame();
    ok = ui.home;
    btn_up(B_EDIT); frame();
    bad += check("EDIT opens SOUND (EXPERT OFF, the default), when let go (not on press)", ok && str_eq(cur_page()->title, "SOUND"));
    for (i = 0; i < NPAGES; i++)
        ok &= !str_eq(PAGES[i].title, "ENGINE");
    bad += check("no ENGINE page (engines are the EDIT layer's)", ok);
    set_engine_of(TSEL, 0);
    bad += check("EDIT cycle (SID, EXPERT OFF): SOUND VOICE VOICE 2 VOICE 3 SOUND", engine_cycle(CYC_P, NELEM(CYC_P)));
    menu_put(MI_EXPERT, 1);
    bad += check("EDIT cycle (SID, EXPERT ON): EDIT 1 EDIT 2 VOICE VOICE 2 VOICE 3 EDIT 1", engine_cycle(CYC_A, NELEM(CYC_A)));
    go_title("EDIT 2"); ui.fam_last[FAM_EDIT] = ui.page;
    go_home(); press(B_EDIT);
    bad += check("EDIT remembers its last page (as every family)", str_eq(cur_page()->title, "EDIT 2"));
    menu_put(MI_EXPERT, 0);
    bad += check("  EXPERT OFF on EDIT 2: SOUND shown", str_eq(cur_page()->title, "SOUND"));
    {   /* (2026-10-17) the plain pages: SID's quick knobs; VOSIM's VOWEL SIZE SWEEP BRTH, MODAL's MATRL EXCIT DECAY
         * BRIGHT. VOWEL and SIZE write F1 F2 and read back from them; not momentary */
        const param_desc_t *d;
        int16_t *vp;
        int32_t f1, f2;
        go_title("SOUND");
        d = page_desc(cur_page(), 0, &vp);
        ok = d == track_desc(TSEL, ENGINES[0]->knob[0]) && vp == &TSEL->p[ENGINES[0]->knob[0]];
        set_engine_of(TSEL, ENGI_VOSIM);
        go_title("SOUND");
        page_title(ti);
        d = page_desc(cur_page(), 0, &vp);
        ok &= !strncmp(ti, "VOCAL", 5) && str_eq(d->label, "VOWEL") && vp == &plain_mval &&
              str_eq(page_desc(cur_page(), 1, &vp)->label, "SIZE") && page_desc(cur_page(), 2, &vp) == track_desc(TSEL, P_E6);
        TSEL->p[P_E0] = 61; TSEL->p[P_E1] = 81;         /* (A, as VOWEL AH sets it) */
        ok &= vosim_macro_get(TSEL, 0) * 5 / 128 == 2 && vosim_macro_get(TSEL, 1) == 0;   /* (A's fifth, SIZE 0) */
        vosim_macro_set(TSEL, 0, 115);                  /* I */
        vs_vowel_at(115, &f1, &f2);
        ok &= TSEL->p[P_E0] == f1 && TSEL->p[P_E1] == f2 && vosim_macro_get(TSEL, 0) == 115;
        vosim_macro_set(TSEL, 1, 40);                   /* a bigger body: both down by 10 */
        ok &= TSEL->p[P_E0] == f1 - 10 && TSEL->p[P_E1] == f2 - 10 && vosim_macro_get(TSEL, 1) == 40 &&
              vosim_macro_get(TSEL, 0) == 115;
        go_title("SOUND");
        i = (uint32_t)TSEL->p[P_E1];
        turn(EN_K1, -40);                                /* KNOB 1: VOWEL down to U */
        ok &= TSEL->p[P_E1] != (int16_t)i && vosim_macro_get(TSEL, 0) < 115;
        set_engine_of(TSEL, ENGI_MODAL);
        go_title("SOUND");
        ok &= page_desc(cur_page(), 0, &vp) == track_desc(TSEL, P_E0) && page_desc(cur_page(), 1, &vp) == track_desc(TSEL, P_E1) &&
              page_desc(cur_page(), 2, &vp) == track_desc(TSEL, P_E4) && page_desc(cur_page(), 3, &vp) == track_desc(TSEL, P_E6);
        bad += check("  the plain pages: SID's quick knobs; VOSIM VOCAL: VOWEL (writes F1 F2, reads them back) SIZE SWEEP BRTH; MODAL", ok);
    }
    {   /* (2026-10-18) FX with EXPERT OFF: REVERB (TYPE SIZE MIX, the global reverb) and DELAY (MIX FEED TIME, the
         * patch's), a second tap the other; a preset load leaves the patch's delay as it is */
        int16_t *vp;
        ui_power_on();
        menu_put(MI_EXPERT, 0);
        go_home(); frame();
        press(B_FX); frames(320);
        ok = str_eq(cur_page()->title, "REVERB") && page_desc(cur_page(), 0, &vp) == &GP[G_RTYPE] &&
             page_desc(cur_page(), 2, &vp) == &GP[G_RMIX] && str_eq(N_RTYPE[0], "DRY") && str_eq(N_RTYPE[3], "PLATE");
        press(B_FX); frames(320);
        ok &= str_eq(cur_page()->title, "DELAY") && page_desc(cur_page(), 0, &vp) && vp == &TSEL->p[P_DLY] &&
              page_desc(cur_page(), 1, &vp) && vp == &TSEL->p[P_DFDBK] && page_desc(cur_page(), 2, &vp) &&
              vp == &TSEL->p[P_DTIME] && TSEL->p[P_DLY] == 0;
        press(B_FX); frames(320);
        ok &= str_eq(cur_page()->title, "REVERB");
        TSEL->p[P_DLY] = 77;
        apply_preset_to(TSEL, 1);
        ok &= TSEL->p[P_DLY] == 77;
        bad += check("FX (EXPERT OFF): REVERB and DELAY only; the delay out at power-on, the patch's (a preset keeps it)", ok);
    }
    return bad;
}



/* ------------------------------------------------ the GLO SCL EDIT layers --- */
static uint32_t black(uint32_t b) { return key_at(1, b); }
/* the EDIT layer's INIT key: the white key after the engines, or with 16 engines the last black key (ui_layer.c) */
static uint32_t init_key(void) { return LY_INIT_BK ? black(LY_INIT_P) : white(LY_INIT); }
static void lay_combo(uint32_t btn, uint32_t k) { btn_down(btn); key_down(k); frame(); }
static void oct_back(void) { btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame(); }

static int test_quick_layers(void)
{
    static const uint8_t LB[3] = {B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[3] = {LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    static const char *const HINT[3] = {"HOLD [GLO] QUICK", "HOLD [SCL] QUICK", "HOLD [EDIT] QUICK"};
    int bad = 0, ok, flash, hint;
    uint32_t i, k, a, b2, mo;
    track_t before;
    /* tap / peek / combo, the hint once */
    ok = 1; hint = 1;
    for (i = 0; i < 3u; i++) {
        ui_power_on();
        btn_down(LB[i]);
        for (flash = 0, k = 0; k < 20u; k++) { frame(); flash |= ui.layer; }      /* 0.32 s */
        btn_up(LB[i]); frame();
        ok &= !flash && !ui.home && (LB[i] == B_GLO ? cur_page()->graph == GR_TODO :
                                     LB[i] == B_SCL ? cur_page()->fam == FAM_SCL : str_eq(cur_page()->title, "EDIT 1"));
        hint &= msg_is(HINT[i]);
        go_home(); frame();
        btn_down(LB[i]); frames(368);
        ok &= !ui.layer;
        frames(64);
        ok &= ui.layer == LL[i] && ((layer_seen >> LL[i]) & 1u);
        btn_up(LB[i]); frame();
        ok &= ui.home && !ui.layer;
        ui.msg_t = 0;
        press(LB[i]);
        hint &= !msg_is(HINT[i]);
        go_home(); frame();
        btn_down(LB[i]); key_down(white(9)); frame();
        ok &= ui.layer == LL[i] && !gates();
        key_up(white(9)); btn_up(LB[i]); frame();
        ok &= ui.home && !ui.layer;
    }
    bad += check("GLO SCL EDIT: a tap opens the page, held past HOLD the map (no page), a key: the map at once", ok);
    bad += check("  after a tap \"HOLD [BTN] QUICK\" until the layer has been opened once", hint);
    {
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        layer_seen = 0x1Cu;
        settings_export(&p); layer_seen = 0;
        bad += check("  the seen bits are kept with the settings (a spare favorites byte)",
                     settings_import(&p, sizeof p) && layer_seen == 0x1Cu);
    }

    /* GLO: mutes latch (lit = sounding), F3 .. B3 the layer to edit (2026-10-16; was SOLO), UNMUTE ALL, TAP, levels,
     * OCT- */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    lay_combo(B_GLO, black(1));
    ok = trk[1].p[P_MUTE] == 1 && ui.layer == LAYER_GLO && !gates() && mo_w == mo;
    a = leds_at(0); b2 = leds_at(250);
    ok &= ((a & b2) >> black(0)) & 1u && !(((a | b2) >> black(1)) & 1u);           /* T1 sounding lit, T2 muted dark */
    ok &= ((a & b2) >> white(0)) & 1u && ((a ^ b2) >> white(1)) & 1u && ((a ^ b2) >> white(7)) & 1u &&
          !(((a | b2) >> white(5)) & 1u);                                     /* F3: the layer edited, lit */
    key_up(black(1)); btn_up(B_GLO); frame();
    bad += check("GLO + black key 2: T2 MUTE latched (SET), silent, no MIDI; LEDs: sounding lit, muted dark, the edited layer lit", ok &&
                 trk[1].p[P_MUTE] == 1 && !ui.layer);
    lay_combo(B_GLO, black(1)); key_up(black(1)); frame();
    key_down(white(4)); key_up(white(4)); frame();
    lay_combo(B_GLO, black(2)); key_up(black(2)); frame();
    ok = trk[1].p[P_MUTE] == 0 && trk[2].p[P_MUTE] == 1;
    key_down(white(4)); frame(); key_up(white(4)); frame();
    btn_up(B_GLO); frame();
    bad += check("  again: unmuted; C4: UNMUTE ALL", ok && !trk[2].p[P_MUTE] && !trk[1].p[P_MUTE]);
    lay_combo(B_GLO, white(2));
    perf_begin(CTL);
    ok = song.sel == 2u && !perf_solo && !((perf_act >> PF_M1) & 15u) && !gates();   /* layer 3 edited, nothing muted */
    key_up(white(2)); btn_up(B_GLO); frame();
    bad += check("GLO + A3: layer 3 is the one the knobs edit (as ALGORITHM picks it); no note, no mute",
                 ok && song.sel == 2u && !trk[0].p[P_MUTE] && !ui.layer);
    track_select(0);
    song.g[G_BPM] = 100;
    btn_down(B_GLO); frame();
    for (i = 0; i < 3u; i++) { key_down(white(7)); frame(); key_up(white(7)); frames(480); }
    ok = song.g[G_BPM] == 120;
    btn_up(B_GLO); frame();
    song.g[G_CLOCK] = 1;
    lay_combo(B_GLO, white(7)); key_up(white(7)); frame();
    ok &= msg_is("TAP: CLK IS EXT") && song.g[G_BPM] == 120;
    btn_up(B_GLO); frame(); song.g[G_CLOCK] = 0;
    bad += check("GLO + F4 x3 at 0.5 s: TAP 120 BPM; with CLK EXT: dimmed, it says why", ok);
    {
        int16_t l2 = trk[2].p[P_LEVEL], atk = TSEL->p[P_ATK];
        go_title("ENV");
        btn_down(B_GLO); frame();
        turn(EN_K3, -10);
        ok = trk[2].p[P_LEVEL] == l2 - 10 && TSEL->p[P_ATK] == atk && ui.layer == LAYER_GLO;
        key_down(black(0)); frame(); key_up(black(0)); frame();
        ok &= trk[0].p[P_MUTE] == 1 && song.octave == 0;
        oct_back();
        ok &= trk[2].p[P_LEVEL] == l2 && !trk[0].p[P_MUTE] && song.octave == 0 && ui.layer == LAYER_GLO;
        btn_up(B_GLO); frame();
        bad += check("GLO KNOB 3: T3 LEVEL from any page; OCT-: mutes and levels as it opened, no octave", ok &&
                     str_eq(cur_page()->title, "ENV"));
    }
    song.playing = 1; transport_req = 0;
    btn_down(B_GLO); frame(); press(B_PLAY);
    ok = transport_req == 3u;
    events_block(CTL);
    ok &= song.playing && transport_req == 0u;
    btn_up(B_GLO); frame();
    stop_transport();
    btn_down(B_GLO); frame(); press(B_PLAY);
    ok &= transport_req == 1u;
    btn_up(B_GLO); frame(); transport_req = 0;
    bad += check("GLO + PLAY: RESTART playing (from the top, not stopped); stopped: PLAY", ok);
    usb.config = 0;

    /* SCL: a key is ROOT, KNOB 2 the scale, the LEDs, OCT- */
    ui_power_on();
    TSEL->p[P_ROOT] = 0; TSEL->p[P_SCALE] = 1;                     /* C MAJ */
    lay_combo(B_SCL, black(4));                                    /* D#4 */
    ok = TSEL->p[P_ROOT] == 3 && ui.layer == LAYER_SCL && !gates();
    key_up(black(4)); frame();
    key_down(white(1)); key_up(white(1)); frame();                  /* G3 */
    ok &= TSEL->p[P_ROOT] == 7;
    turn(EN_K2, 1);
    ok &= TSEL->p[P_SCALE] == 2;
    a = leds_at(0); b2 = leds_at(250);
    ok &= ((a & b2) >> white(1)) & 1u && ((a & b2) >> white(8)) & 1u;   /* G3 G4: the root, lit */
    ok &= ((a ^ b2) >> white(2)) & 1u && ((a ^ b2) >> black(4)) & 1u;   /* A, A# (G minor): blink */
    ok &= !(((a | b2) >> black(0)) & 1u);                               /* F#: not in G minor, dark */
    btn_up(B_SCL); frame();
    bad += check("SCL + D#4, then G3: ROOT D#, G (latched); KNOB 2: SCL; LEDs: root lit, the scale breathes", ok &&
                 TSEL->p[P_ROOT] == 7 && TSEL->p[P_SCALE] == 2 && ui.home);
    btn_down(B_SCL); frame(); key_down(white(4)); key_up(white(4)); frame(); turn(EN_K2, 3);
    ok = TSEL->p[P_ROOT] == 0 && TSEL->p[P_SCALE] == 5;
    oct_back();
    btn_up(B_SCL); frame();
    bad += check("  OCT- in SCL: ROOT and SCL as the layer opened", ok && TSEL->p[P_ROOT] == 7 && TSEL->p[P_SCALE] == 2);

    /* EDIT: the engines from F3 (NENGINES of them), sound loads with UNDO, INIT with the dialog */
    ui_power_on();
    set_engine_of(TSEL, 1); go_home(); frame();          /* (not the first engine: its key is a change) */
    TSEL->p[P_ROOT] = 5; song.playing = 1;
    before = *TSEL;
    sync_reload = 0;
    lay_combo(B_EDIT, white(eng_rank(ENGI_SID)));             /* (the keys: the engines one can pick, engines.c eng_vis) */
    ok = TSEL->eng_req == ENGI_SID && TSEL->preset == 0u && ui.layer == LAYER_EDIT && sync_reload && !gates();
    key_up(white(eng_rank(ENGI_SID))); frame();
    key_down(white(1)); key_up(white(1)); frame();
    ok &= TSEL->eng_req == eng_vis(1);                    /* (G3: the second in display order) */
    key_down(white(LY_INIT - 1u)); key_up(white(LY_INIT - 1u)); frame();
    ok &= TSEL->eng_req == eng_vis(LY_INIT - 1u);         /* (the last engine key) */
    ok &= TSEL->p[P_SLEN] == before.p[P_SLEN] &&
          TSEL->p[P_ROOT] == 5;
    btn_up(B_EDIT); frame();
    bad += check("EDIT + white key n: the n-th engine shown, while playing; LEN, ROOT stay", ok);
    hold(B_SAVE);
    bad += check("  SAVE held: UNDO back to before the layer's loads",
                 TSEL->eng_req == 1u && TSEL->preset == before.preset);
    a = 0;
    ok = 1;
    btn_down(B_EDIT); frames(480);
    for (i = 0; i < LY_INIT; i++) {
        key_down(white(i)); key_up(white(i)); frame();
        a += i < NENG_SHOWN ? TSEL->eng_req == eng_for_track(TSEL, eng_vis(i)) : 0u;   /* (DUBSIREN: the last patch's
                                                                                * only, its substitute elsewhere) */
        ok &= eng_ok(TSEL->eng_req);
    }
    btn_up(B_EDIT); frame();
    bad += check("  every engine one can pick has its white key from F3 (NENG_SHOWN, 16 at most; never a retired one)",
                 ok && a == LY_INIT && LY_INIT == (NENG_SHOWN < 16u ? NENG_SHOWN : 16u));
    set_engine_of(TSEL, 0);
    lay_combo(B_EDIT, white(eng_rank(ENGI_WAVE))); key_up(white(eng_rank(ENGI_WAVE))); frame();
    turn(EN_K2, 1);
    ok = TSEL->eng_req == ENGI_WAVE && TSEL->preset == 1u;
    turn(EN_K3, 1);
    ok &= preset_favorite();
    oct_back();
    ok &= TSEL->eng_req == 0u;
    btn_up(B_EDIT); frame();
    bad += check("  KNOB 2: the engine's next sound, KNOB 3 FAV; OCT-: the sound as the layer opened", ok && ui.home);
    btn_down(B_EDIT); frame(); turn(EN_K1, 1);
    ok = TSEL->eng_req == eng_step(0, 1);
    btn_up(B_EDIT); frame();
    bad += check("  KNOB 1: the next engine", ok);

    song.playing = 0;
    lay_combo(B_EDIT, init_key());
    ok = ui.confirm == CF_INIT_SOUND;
    frame();
    ok &= !ui.layer;
    key_up(init_key()); btn_up(B_EDIT); frame();
    ok &= ui.home && ui.confirm == CF_INIT_SOUND;
    TSEL->p[P_E0] = (int16_t)(TSEL->p[P_E0] + 5);
    press(B_OCTUP);
    bad += check("EDIT + the key after the engines: INITIALIZE SOUND? dialog (closes the layer, no tap); OCT+ inits", ok && !ui.confirm &&
                 msg_is("SOUND INIT") && TSEL->p[P_E0] == ENGINES[eng_step(0, 1)]->presets[0].e[0]);

    /* no layer in the menu or a dialog: GLO + a key plays */
    ui_power_on(); hold(B_HOME);
    btn_down(B_GLO); frame(); key_down(white(3)); frame();
    ok = !ui.layer && gates() > 0 && !kb_layer;
    key_up(white(3)); btn_up(B_GLO); frame();
    bad += check("in the menu: no GLO layer, GLO + a key plays it; GLO let go: no page", ok && ui.menu == 1);
    ui_power_on(); ui.confirm = CF_INIT_SOUND;
    btn_down(B_SCL); frames(512);
    ok = !ui.layer;
    btn_up(B_SCL); frame();
    bad += check("in a dialog: SCL held shows no map and taps nothing", ok && ui.confirm == CF_INIT_SOUND);
    /* one layer at a time: a second layer button is ignored, its keys stay notes after */
    ui_power_on();
    btn_down(B_GLO); frame(); btn_down(B_EDIT); frame(); key_down(white(1)); frame();
    ok = ui.layer == LAYER_GLO && TSEL->eng_req == trk[1].eng_req && song.sel == 1u;   /* (G3: layer 2 edited) */
    key_up(white(1)); btn_up(B_GLO); frame();
    key_down(white(1)); frame();
    ok &= gates() > 0 && !ui.layer;
    key_up(white(1)); btn_up(B_EDIT); frame();
    bad += check("GLO then EDIT: GLO's layer; EDIT ignored (no layer, no page, its keys notes)", ok && ui.home);
    /* (2026-10-17) ARP: GEN starts, GEN MIX shown (the roles' densities), STROBE on it (2026-10-18, the ocean's place):
     * a bass note raises its bars, another role tears it; ARP again: GEN stops, GEN's page */
    ui_power_on();
    btn_down(B_ARP); frame(); btn_up(B_ARP); frame();
    {
        static int32_t ab[2 * CTL];
        uint32_t i, before = 0, after = 0, lit = 0;    /* (lit: the floor's pixels off the black) */
        for (i = 0; i < 64u; i++)                       /* (the audio side: GEN takes the start, plays its first steps) */
            mix_block(ab, CTL);
        frames(40);
        const page_t *pg = cur_page();
        ok = tc.on && pg->fam == FAM_ARP && pg->id[0] == G_GDPAD && pg->id[3] == G_GDTEX && st.ready;
        for (i = 0; i < ST_NBAR; i++)
            before += (uint32_t)st.bar[i];
        tc.s.played[TR_BASS]++;
        tc.s.played[TR_STAB]++;
        cv_begin(240, 122, 0);                          /* (a frame of it alone: its bars up, the tear on) */
        st_frame();
        for (i = 0; i < ST_NBAR; i++)
            after += (uint32_t)st.bar[i] + (uint32_t)st.btgt[i];
        for (i = 0; i < cv_w * cv_h; i++)
            lit += cv_px[i] != cv_px[0];
        ok &= after > before && st.glitch > 0 && lit > 500u;
        btn_down(B_ARP); frame(); btn_up(B_ARP); frame();
        mix_block(ab, CTL);
        ok &= !tc.on && cur_page()->id[0] == G_GPRE;
        bad += check("ARP: TECHNO plays, TECHNO MIX shown with STROBE (a bass note raises its bars, a stab tears it); ARP again: stopped", ok);
        {   /* (2026-10-18) safe to watch: drum hits every few frames for 2 s, every scene and scheme: the strip's mean
             * brightness never jumps more than 6 % of white between two frames (no flash); every scene draws */
            uint32_t sc, sch, f, k2, drawn = 0, worst = 0;
            char dbg[200];
            for (sc = 0; sc < ST_NSCENE; sc++)
                for (sch = 0; sch < 6u; sch++) {
                    uint32_t prev = 0, lit2 = 0;
                    tc.on = 1;
                    st.scene = (uint8_t)sc;
                    st.scheme = (uint8_t)sch;
                    for (f = 0; f < 66u; f++) {
                        uint32_t sum = 0;
                        if (f % 7u == 0u)
                            tc.s.played[TR_DRUMS]++;
                        if (f % 11u == 0u)
                            tc.s.played[TR_BASS]++;
                        cv_begin(240, 122, 0);
                        st.part = 0;                    /* (the scene held: no part change mid-test) */
                        st.scene = (uint8_t)sc;
                        st.scheme = (uint8_t)sch;
                        st_frame();
                        for (k2 = 0; k2 < cv_w * cv_h; k2++) {
                            uint16_t px = swap16(cv_px[k2]);
                            sum += ux_luma(px);
                            lit2 += f == 65u && cv_px[k2] != cv_px[0];
                        }
                        sum /= cv_w * cv_h;
                        if (f && (sum > prev ? sum - prev : prev - sum) > worst)
                            worst = sum > prev ? sum - prev : prev - sum;
                        prev = sum;
                    }
                    drawn += lit2 > 200u;
                }
            tc.on = 0;
            snprintf(dbg, sizeof dbg, "  safe to watch: every scene x scheme drawn (%u of %u), the brightness's largest step "
                     "between frames %u of 255 (no flash)", drawn, ST_NSCENE * 6u, worst);
            bad += check(dbg, drawn == ST_NSCENE * 6u && worst <= 15u);
        }
    }
    return bad;
}

/* bug fixes (UI): one regression each */
static int test_bughunt_ui(void)
{
    int bad = 0, ok;
    /* 1: REC in NAME, the menu or a dialog: nothing (PLAY cannot stop the transport there) */
    ui_power_on();
    go_page(GR_USER); ui.uslot = 3;
    press(B_OCTUP);                                     /* NAME opens */
    press(B_REC);
    bad += check("REC in NAME does nothing (no transport start); NAME stays", name_on() && !transport_req && !song.playing);
    ui_power_on(); hold(B_HOME); press(B_REC); press(B_PLAY);
    bad += check("REC in the menu does nothing (then PLAY: nothing either)", ui.menu && !transport_req);
    ui_power_on();
    up_ui(2, 5);
    go_page(GR_USER); ui.uslot = 5; press(B_OCTUP);
    press(B_REC);
    ok = ui.confirm == CF_OVR_USER && !transport_req;
    press(B_OCTUP);
    bad += check("REC in the OVERWRITE? dialog does nothing; OCT+ then opens NAME", ok && name_on());
    /* 3: USER ERASE asks first (ERASE U02?): OCT- keeps the preset, OCT+ erases it */
    ui_power_on();
    go_page(GR_USER); ui.uslot = 1;
    up_ui(2, 1);
    turn(EN_K1 + 2, 1);                                 /* KNOB 3: ERASE picked */
    press(B_OCTUP);
    ok = ui.confirm == CF_ERASE_USER && ui.confirm_trk == 1u && up_used(1);
    {
        char a[24], b[24];
        confirm_text(a, b);
        ok &= str_eq(a, "ERASE U02?") && b[0];
    }
    press(B_OCTDN);
    ok &= !ui.confirm && up_used(1);
    turn(EN_K1 + 2, 1); press(B_OCTUP); press(B_OCTUP);
    bad += check("USER ERASE: the ERASE U02? dialog; OCT- keeps the preset, OCT+ erases it",
                 ok && !ui.confirm && !up_used(1));
    turn(EN_K1 + 2, 1); press(B_OCTUP);
    bad += check("  an empty slot: no dialog, EMPTY SLOT", !ui.confirm && msg_is("EMPTY SLOT"));
    {   /* 5: ALGORITHM does nothing while a layer's button is held (as PRESETS); after it, the track again */
        int16_t r0;
        ui_power_on();
        r0 = trk[0].p[P_ROOT];
        btn_down(B_SCL); frames(500);
        turn(EN_ALGO, 1);
        ok = ui.layer == LAYER_SCL && song.sel == 0u;
        key_down(white(1)); frame(); key_up(white(1)); frame();   /* ROOT on T1 */
        ok &= trk[0].p[P_ROOT] != r0;
        oct_back();
        ok &= trk[0].p[P_ROOT] == r0;
        btn_up(B_SCL); frame();
        btn_down(B_EDIT); turn(EN_ALGO, 1);             /* armed, not open yet: ignored too */
        ok &= song.sel == 0u;
        btn_up(B_EDIT); frame(); go_home(); frame();
        turn(EN_ALGO, 1);
        bad += check("ALGORITHM ignored with a layer's button held (OCT- puts back all); then T2",
                     ok && song.sel == 1u);
    }
    /* 7: REC in the layers: nothing (PLAY does not start); the layer stays, no page */
    ok = 1;
    {
        static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
        uint32_t i;
        for (i = 0; i < 4u; i++) {
            ui_power_on();
            btn_down(LB[i]); frame();
            press(B_REC);
            ok &= transport_req == 0u && ui.layer != 0u;
            btn_up(LB[i]); frame();
            ok &= ui.home;
        }
    }
    bad += check("REC in every layer: nothing (PLAY does not start); the layer stays, no page", ok);
    /* 8: the OCT- LED lit in the SET layers (OCT- = UNDO), OCT+ dark; FX (HOLD): the octave as usual */
    ok = 1;
    {
        static const uint8_t LB[3] = {B_GLO, B_SCL, B_EDIT};
        uint32_t i;
        for (i = 0; i < 3u; i++) {
            ui_power_on();
            song.octave = 1;                            /* (outside: OCT+ lit) */
            btn_down(LB[i]); frames(500);
            ok &= layer_set_open() && oct_leds_seen(1) == 1u && oct_leds_seen(0) == 1u;
            btn_up(LB[i]); frame();
            ok &= oct_leds() == 2u;
        }
        ui_power_on();
        btn_down(B_FX); frames(500);
        ok &= ui.layer == LAYER_FX && oct_leds() == 0u;
        btn_up(B_FX); frame();
    }
    bad += check("SET layers: the OCT- LED lit (UNDO), OCT+ dark; after: the octave; FX: the octave", ok);
    /* 9: no "HOLD [EDIT] QUICK" over NAME (EDIT tapped on USER / PROJECT renames) */
    ui_power_on();
    layer_seen = 0;
    up_ui(2, 0);
    go_page(GR_USER); ui.uslot = 0; frame();
    ui.msg_t = 0;
    press(B_EDIT);
    ok = name_on() && !msg_is("HOLD [EDIT] QUICK");
    ui_power_on();
    layer_seen = 0;
    press(B_EDIT);
    bad += check("EDIT tap = RENAME on USER: no layer hint over NAME (elsewhere the hint as before)",
                 ok && msg_is("HOLD [EDIT] QUICK"));
    return bad;
}

/* the chord keys on the device (chord.c): SCL tapped again is the CHORD page, KNOB 1 CHRD, KNOB 2 VOIC, the graph
 * names the last chord (MONO gray); a sound load keeps them; the SCL layer's KNOB 3 / 4 are CHRD / VOIC, OCT-
 * puts them back */
static int test_chord_page(void)
{
    int bad = 0, ok;
    track_t *t;
    ui_power_on();
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    go_home(); frame();
    press(B_SCL); frames(400);
    ok = str_eq(cur_page()->title, "SCL");
    press(B_SCL); frames(400);
    ok &= str_eq(cur_page()->title, "CHORD") && cur_page()->graph == GR_CHORD && cur_page()->fam == FAM_SCL;
    bad += check("SCL tapped again: the CHORD page (CHRD VOIC, the chord graph)", ok && cur_page()->id[0] == P_CHRD &&
                 cur_page()->id[1] == P_VOIC);
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  CHRD OFF: the page says what to do, MONO gray", screen_gray());
    turn(EN_K1, 1); frame();
    turn(EN_K2, 1); frame();
    bad += check("  KNOB 1: CHRD DIA3, KNOB 2: VOIC OPEN", t->p[P_CHRD] == CH_DIA3 && t->p[P_VOIC] == VC_OPEN);
    key_down(7); frame();                                          /* C4 in C (SCALE CHR: the major of C) */
    ok = gates() == 3u && chord_last[song.sel].root == 60;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    ok &= screen_gray();
    key_up(7); frame();
    bad += check("  a key plays the chord, the graph shows it (C), MONO gray", ok && !gates());
    t->p[P_CHRD] = CH_MIN7; t->p[P_VOIC] = VC_BASS;
    apply_preset_to(t, 1);
    bad += check("  a sound load keeps CHRD and VOIC (SCL settings)", t->p[P_CHRD] == CH_MIN7 && t->p[P_VOIC] == VC_BASS);
    ui_power_on();
    t = TSEL;
    t->p[P_CHRD] = CH_OFF; t->p[P_VOIC] = VC_CLOSE;
    go_home(); frame();
    btn_down(B_SCL); frames(800);
    ok = ui.layer == LAYER_SCL;
    turn(EN_K3, 2); frame();
    turn(EN_K4, 1); frame();
    ok &= t->p[P_CHRD] == CH_DIA7 && t->p[P_VOIC] == VC_OPEN && t->p[P_QUANT] == 0 && t->p[P_TRANS] == 0;
    turn(EN_K1, 2); frame();
    ok &= t->p[P_ROOT] == 2;
    oct_back();
    btn_up(B_SCL); frame();
    bad += check("SCL layer: KNOB 1 ROOT, 3 CHRD, 4 VOIC (QNT TRN untouched); OCT- puts them back", ok &&
                 t->p[P_CHRD] == CH_OFF && t->p[P_VOIC] == VC_CLOSE && t->p[P_ROOT] == 0 && ui.home);
    btn_down(B_SCL); frames(800);
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    ok = screen_gray();
    btn_up(B_SCL); frame();
    bad += check("  its map and cards MONO gray", ok);
    return bad;
}

/* 1.2: MENU > CONTROL > STEP PREVIEW (Discussion #169) and CHORD ENTRY (#155), MENU > DISPLAY > SCOPE (#165; its sound
 * path: click_test.c). Every new setting 0 by default (today's behaviour), a bit of its own byte */
static uint32_t gated(const track_t *t, uint32_t note)   /* voices of t sounding note with their gate on */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate && (note > 127u || t->v[i].note == note);
    return n;
}
static void blocks(uint32_t n) { while (n--) events_block(CTL); }
static void tap_k(uint32_t k) { key_down(k); frame(); key_up(k); frame(); }


/* bug fixes, second round (UI) */
static uint32_t accent_in(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t n = 0;
    int32_t x, y;
    uint16_t a = (uint16_t)((T_ACCENT >> 8) | (T_ACCENT << 8));
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += host_screen[y * 240 + x] == a;
    return n;
}
static int test_bughunt_ui2(void)
{
    int bad = 0;
    uint32_t i;
    {   /* 1. a layer button pressed under a dialog and still held after it closes: dead, its keys stay notes */
        static const uint32_t BTN[2] = {B_FX, B_GLO};
        for (i = 0; i < 2u; i++) {
            uint32_t k = white(0), ok;
            ui_power_on(); TSEL->p[P_VOICE] = V_POLY;
            go_home(); frame();
            confirm_open(CF_INIT_SOUND, 0); frame();
            btn_down(BTN[i]); frame(); frames(100);
            btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();   /* the dialog closes, the button still held */
            frames(800);
            ok = !ui.confirm && (ui.ly_t0 & LY_DEAD) && !ui.layer && !kb_mask && !perf_mask;
            key_down(k); frame();
            ok = ok && kb_note[k] != KB_SILENT && gates() == 1u && !((kb_layer >> k) & 1u) && !perf_held && !ui.layer;
            key_up(k); frame();
            btn_up(BTN[i]); frame();
            bad += check(i ? "dead GLO held from a dialog: no map, the key plays a note"
                           : "dead FX held from a dialog: no map, the key plays a note, no effect", ok);
        }
    }
    {   /* 3. MIXER: the knob just turned is the one drawn in the accent (K1 LEVEL, K2 PAN, K3 REV, K4 the MUTE badge) */
        uint32_t k, a[4][4], ok = 1;
        for (k = 0; k < 4u; k++) {
            int32_t X = CARD_X(0), Y = Y_GRAPH;
            ui_power_on();
            settings.palette = 1; palette_set(1);
            go_page(GR_TRK); frame(); frame();
            turn(EN_K1 + k, 1);                                       /* (K4 right: MUTE on, its badge shown) */
            ui.force = 1; frame();
            a[k][0] = accent_in(X + 4, Y + 37, X + 38, Y + 71);       /* LEVEL */
            a[k][1] = accent_in(X + 3, Y + 84, X + 28, Y + 115);      /* PAN */
            a[k][2] = accent_in(X + 30, Y + 84, X + 55, Y + 115);     /* REV */
            a[k][3] = accent_in(X + 22, Y + 4, X + 56, Y + 20);       /* the MUTE badge */
        }
        for (k = 0; k < 4u; k++) {
            uint32_t j;
            for (j = 0; j < 4u; j++)
                ok &= j == k ? a[k][j] > 0u : a[k][j] == 0u;
        }
        bad += check("MIXER: K1..K4 light LEVEL / PAN / REV / the MUTE badge, nothing else", ok);
    }
    {   /* 4. renaming the slot the music came from renames the music: the next SAVE prefills the new name */
        char pn[16];
        uint32_t ok;
        ui_power_on();
        go_page(GR_SLOTS);
        project_save_as(0, "OLD");
        project_load(0); frame();
        ok = str_eq(proj_name, "OLD");
        proj_name[0] = 0;                                             /* (the music no longer the stored one's) */
        proj_cur = PROJ_NO_SLOT;
        project_rename(0, "ELSE");
        ok &= str_eq(proj_name, "");                                  /* the stored one's name not the music's */
        project_load(0); frame();
        press(B_EDIT);                                                /* rename the song on the PROJECT page */
        while (nm.len) { nm_do(NB_DEL); }
        nm_white(13); frames(900);
        str_cpy(nm.s, "NEW", sizeof nm.s); nm.len = nm.cur = 3; nm.key = 0;
        press(B_OCTUP);
        project_name(1, pn);
        ok &= str_eq(pn, "NEW") && str_eq(proj_name, "NEW");
        turn(EN_K4, 1); press(B_OCTUP);                               /* SAVE: OVERWRITE? */
        press(B_OCTUP);                                               /* YES: NAME */
        ok &= name_on() && str_eq(nm.s, "NEW");
        press(B_OCTDN);
        bad += check("rename of the loaded slot: the music's name follows, SAVE prefills it", ok);
    }
    {   /* 5. NAME: PLAY stops a transport started meanwhile (so the name can be saved), never starts it; REC ignored */
        uint32_t ok;
        ui_power_on(); go_page(GR_USER); ui.uslot = 3; press(B_OCTUP);
        ok = name_on();
        press(B_PLAY); frame();
        ok &= !transport_req && !song.playing && name_on();          /* stopped: PLAY starts nothing */
        press(B_REC); frame();
        ok &= !transport_req && name_on();
        song.playing = 1;                                             /* an external MIDI Start / the editor */
        press(B_OCTUP);
        ok &= name_on() && msg_is("STOP TO SAVE");
        press(B_REC); frame();
        ok &= !transport_req;
        press(B_PLAY); frame();
        ok &= transport_req == 2u && name_on();                       /* PLAY stops it, NAME stays */
        stop_transport();
        press(B_OCTUP);
        ok &= !name_on() && up_used(3);
        bad += check("NAME: PLAY stops a running transport (never starts), REC ignored, then OCT+ saves", ok);
    }
    {   /* 7. EDIT: INIT is drawn in the cell after the engines, and that cell's white key is the one that inits */
        uint32_t p, ok = LY_INIT < 16u || LY_INIT_BK;   /* (layer_edit: INIT in the cell after the engines'; 16 engines:
                                                         * on the last black key) */
        if (LY_INIT_BK) {
            ui_power_on(); go_home(); frame();
            btn_down(B_EDIT); key_down(black(LY_INIT_P)); frame(); frames(50);
            ok &= ui.confirm == CF_INIT_SOUND;
            key_up(black(LY_INIT_P)); btn_up(B_EDIT); frame();
        }
        for (p = LY_INIT; p < 16u; p++) {
            ui_power_on(); go_home(); frame();
            btn_down(B_EDIT); key_down(white(p)); frame(); frames(50);
            ok &= (ui.confirm == CF_INIT_SOUND) == (p == LY_INIT);
            key_up(white(p)); btn_up(B_EDIT); frame();
        }
        bad += check("EDIT: INIT's map cell (after the engines) and its key agree", ok);
    }
    return bad;
}

#ifndef UI_TEST_NO_MAIN
/* L is a sparse face (tools/gen_aa_font.py L_CHARS): every string drawn in it has all its glyphs */
static int test_large_face(void)
{
    static const char *const FIXED[] = {"BLUE HARE", "0123456789"};   /* main.c, ui_menu.c; ui_draw.c draw_uboot */
    uint32_t i, missing = 0;
    const char *s;
    for (i = 0; i < NB + NE + 2u; i++)                              /* ui_input.c setup_show: the control names */
        for (s = i < NB ? B_NAME[i] : i < NB + NE ? E_NAME[i - NB] : FIXED[i - NB - NE]; *s; s++)
            missing += glyph_at(&AF_L, (uint8_t)*s) < 0;
    return check("the L face holds every glyph of the strings drawn in it", !missing);
}



/* 1.5: engine ids and this build's indices (core.h): every engine's id, both ways; the retired ids (1 DIGITAL, 4 SAMPLE,
 * 8 GRAIN, 13 SLICE) map to none, are "-" with no presets, a clean break: a project naming one is refused, a user
 * preset of one is not valid, a song's instrument of one is dropped; the engines one can pick in their display order;
 * the power-on parts (the substitutes of a build without some engines: tests/os/engines_test.c) */
static int test_retired_engines(void)
{
    static const uint8_t GONE[4] = {1, 4, 8, 13};
    static const struct { uint32_t idx, id; const char *name; } MAP[] = {   /* (HARE FM1, the host tests' build) */
        {ENGI_SID, EID_SID, "SID"}, {ENGI_WAVE, EID_WAVE, "WAVE"}, {ENGI_VOSIM, EID_VOSIM, "VOSIM"},
        {ENGI_OBIE, EID_OBIE, "OBIE"}, {ENGI_HOOVER, EID_HOOVER, "HOOVER"}, {ENGI_DUBSIREN, EID_DUBSIREN, "DUBSIREN"},
        {ENGI_MODAL, EID_MODAL, "MODAL"}, {ENGI_KIT, EID_KIT, "KIT"}, {ENGI_BREAKS, EID_BREAKS, "BREAKS"}};
    static const uint8_t ARCH[10] = {EID_SPEECH, EID_FMPERC, EID_WASP, EID_SANDSTORM, EID_ECHOES, EID_CANYON, EID_WEST,
                                     EID_SH101, EID_BLISS, EID_STRINGS};   /* (archived, 2026-10-16 .. 17) */
    static const char *const ORDER[] = {"SID", "WAVE", "VOSIM", "OBIE", "HOOVER", "DUBSIREN", "MODAL", "KIT", "BREAKS"};
    int bad = 0, ok = 1, sub;
    uint32_t i, k, e, total, seen = 0, all = 0, last = 0xFFu, r = 0, n = 0;
    ui_power_on();
    ok = NENGINES == NELEM(MAP);
    for (i = 0; i < NELEM(MAP); i++)
        ok &= MAP[i].idx == i && ENGINES[i]->id == MAP[i].id && eng_id(i) == MAP[i].id && eng_of_id(MAP[i].id) == i &&
              eng_load_id(MAP[i].id, &sub) == i && !sub && str_eq(ENGINES[i]->name, MAP[i].name) &&
              str_eq(N_ENGNAME[MAP[i].id], MAP[i].name) && eng_by_id(MAP[i].id) == ENGINES[i];
    bad += check("engine ids: every engine's id and index both ways (ENGINES[] in the ENGI_ order), its name by id", ok);
    for (i = 0, ok = 1; i < 4u; i++)
        ok &= EID_RETIRED(GONE[i]) && eng_of_id(GONE[i]) == ENG_NO && eng_load_id(GONE[i], &sub) == ENG_NO &&
              eng_by_id(GONE[i]) == &ENG_NONE && !eng_by_id(GONE[i])->npresets && str_eq(N_ENGNAME[GONE[i]], "-");
    ok &= eng_of_id(EID_COUNT) == ENG_NO && eng_load_id(EID_COUNT, &sub) == ENG_NO && eng_by_id(200) == &ENG_NONE;
    bad += check("retired ids 1 4 8 13 (DIGITAL SAMPLE GRAIN SLICE) and ids past the last: no engine, \"-\", refused", ok);
    for (i = 0, ok = 1; i < NELEM(ARCH); i++)
        ok &= EID_RETIRED(ARCH[i]) && eng_of_id(ARCH[i]) == ENG_NO && eng_load_id(ARCH[i], &sub) == ENG_NO &&
              eng_by_id(ARCH[i]) == &ENG_NONE && str_eq(N_ENGNAME[ARCH[i]], "-");
    ok &= !EID_RETIRED(EID_SID) && !EID_RETIRED(EID_MODAL) && !EID_RETIRED(EID_VOSIM);
    bad += check("archived engines (SPEECH FMPERC WASP SANDSTORM ECHOES CANYON WEST SH101 BLISS STRINGS ..): retired as those, their ids kept; the kept ones not", ok);
    for (i = 0; i < NENGINES; i++)
        all |= (uint32_t)eng_ok(i) << i;
    /* (a project naming one is refused: tests/os/robust_test.c, which crafts the stored stream) */
    {   /* a user preset stored with one (an older firmware's bank): not valid, the slot empty */
        up_rec_t rec;
        memset(&rec, 0, sizeof rec);
        rec.used = UP_USED; rec.ver = UP_VER; rec.engine = 8; rec.np = P_COUNT;
        memcpy(rec.name, "OLD GRAIN", 9);
        for (k = 0; k < P_COUNT; k++)
            up_set_value(&rec, k, TSEL->p[k]);
        *up_rec(9) = rec;
        set_engine_of(TSEL, ENGI_MODAL);
        bad += check("  a user preset of one: the slot shows empty, a load does nothing", !up_used(9) && up_load(9) == 1 &&
                     TSEL->eng_req == ENGI_MODAL);
        memset(up_rec(9), 0, sizeof rec);
    }
    ui_power_on();
    bad += check("power-on: track 2 is WAVE WARM PAD (these tests' UT_DEF)", trk[1].eng_req == ENGI_WAVE &&
                 str_eq(ENGINES[ENGI_WAVE]->presets[trk[1].preset].name, "WARM PAD"));
    preset_all_pos(&total);
    for (i = 0, seen = 0; i < total; i++) {
        e = preset_all_at(i, &k);
        if (e < NENGINES)
            seen |= 1u << e;
    }
    bad += check("PRESETS: the list holds every engine's presets, none of a retired one", seen == all);
    go_page(GR_BROWSE);
    set_engine_of(TSEL, 0);
    song.sel = NTRK - 1u;                               /* (the last patch: every engine, DUBSIREN too) */
    set_engine_of(TSEL, 0);
    for (i = 0, seen = 0; i < NENG_SHOWN; i++) {
        turn(EN_K2, 1);
        seen |= 1u << TSEL->eng_req;
    }
    bad += check("PRESETS KNOB 2: the engines in order, back to the first",
                 seen == all && TSEL->eng_req == 0u && eng_step(0, 1) == ENGI_WAVE && eng_step(ENGI_WAVE, 1) == ENGI_VOSIM &&
                 eng_step(ENGI_WAVE, -1) == 0u && eng_step(0, -1) == ENGI_BREAKS);
    /* the display order (engines.c ENGINES[]): every engine one can pick once; the PRESETS list follows it */
    ok = NENG_SHOWN == NELEM(ORDER);
    for (i = 0; ok && i < NENG_SHOWN; i++)
        ok &= str_eq(ENGINES[eng_vis(i)]->name, ORDER[i]) && eng_rank(eng_vis(i)) == i && eng_ok(eng_vis(i));
    for (i = 0; i < total; i++) {                      /* the list's engines, each once, in that order */
        e = preset_all_at(i, &k);
        if (e >= NENGINES || e == last)
            continue;
        ok &= n < NENG_SHOWN && e == eng_vis(n);
        n++;
        last = e;
        r++;
    }
    bad += check("engines shown SID WAVE VOSIM OBIE HOOVER DUBSIREN MODAL KIT BREAKS (ENGINES[]); PRESETS lists them so",
                 ok && r == NENG_SHOWN);
    ui_power_on();
    return bad;
}

/* Discussion #81: the notes MIDI IN holds on the selected track light their keys as the sequencer's do: USB and TRS,
 * routed by ROUT; at the octave now, the lowest key; off the keyboard nothing; the pedal holds them; the layer's map
 * and the DRUM grid keep their keys */
static void usb_note(uint32_t ch, uint32_t note, uint32_t vel)   /* a USB-MIDI packet, as usb.c queues it */
{
    uint32_t st = (vel ? 0x90u : 0x80u) | ch;
    midi_in_event((st >> 4) | st << 8 | note << 16 | vel << 24);
    events_block(CTL);
}
static void trs_note(uint32_t a, uint32_t b, uint32_t c)        /* TRS MIDI IN: midi_uart.c's parser */
{
    um_byte(a); um_byte(b); um_byte(c);
    events_block(CTL);
}
static int test_midi_leds(void)
{
    int bad = 0, ok;
    ui_power_on();
    memset(&um, 0, sizeof um);
    song.sel = 0;
    usb_note(0, 60, 100);                              /* channel 1 -> T1 (ROUT CH1-4) */
    bad += check("#81 USB MIDI C4 into the selected track lights key 8 (C4)", midi_sel_on[0][60] == 1u &&
                 key_leds(0) == 1u << 7 && midi_leds(TSEL) == 1u << 7);
    usb_note(1, 67, 100);                              /* channel 2 -> T2: not the selected track */
    ok = key_leds(0) == 1u << 7;
    song.sel = 1;
    ok &= key_leds(0) == 1u << 14;
    song.sel = 0;
    bad += check("  another track's MIDI notes light nothing; selected, its G4 lights key 15", ok);
    usb_note(1, 67, 0);
    usb_note(0, 60, 0);
    bad += check("  note-off: dark", key_leds(0) == 0u && !midi_owners[0]);
    trs_note(0x90, 64, 90);                           /* TRS: E4 on channel 1 */
    ok = key_leds(0) == 1u << 11;
    trs_note(0x80, 64, 0);
    bad += check("  TRS MIDI IN: E4 lights key 12 while held", ok && key_leds(0) == 0u);
    song.g[G_ROUTE] = 1;                               /* ROUT SEL: every channel to the selected track */
    song.sel = 2;
    usb_note(9, 62, 100);
    ok = midi_sel_on[9][62] == 3u && key_leds(0) == 1u << 9;
    usb_note(9, 62, 0);
    song.g[G_ROUTE] = 0;
    song.sel = 0;
    bad += check("  ROUT SEL: channel 10 into the selected track lights its key", ok && key_leds(0) == 0u);
    usb_note(0, 60, 100);
    song.octave = -1;
    ok = key_leds(0) == 1u << 19;
    song.octave = 1;
    ok &= key_leds(0) == 0u;                            /* (C4 below the keys an octave up) */
    song.octave = 0;
    usb_note(0, 60, 0);
    usb_note(0, 100, 100);                             /* G#7: above the keyboard */
    usb_note(0, 40, 100);                              /* E2: below it */
    ok &= key_leds(0) == 0u && midi_owners[0] == 2u;
    usb_note(0, 100, 0);
    usb_note(0, 40, 0);
    bad += check("  the keys follow the octave; notes off the keyboard are not shown", ok);
    trk[0].p[P_QUANT] = 1;                             /* SNAP in C major: C#4 rounds down to C4 too */
    trk[0].p[P_SCALE] = 1;
    trk[0].p[P_ROOT] = 0;
    usb_note(0, 60, 100);
    ok = kb_map(&trk[0], 8) == 60u && key_leds(0) == 1u << 7;
    usb_note(0, 60, 0);
    trk[0].p[P_QUANT] = trk[0].p[P_SCALE] = 0;
    bad += check("  QNT SNAP: only the lowest key giving the note lights", ok && key_leds(0) == 0u);
    midi_event(0xB0, 0, 64, 127);                      /* the pedal down: a released note still sounds */
    usb_note(0, 65, 100);
    usb_note(0, 65, 0);
    ok = key_leds(0) == 1u << 12;
    midi_event(0xB0, 0, 64, 0);
    bad += check("  a note the pedal holds stays lit until the pedal is up", ok && key_leds(0) == 0u);
    fm1_in.notes = 1u << 0;                            /* with a key held */
    usb_note(0, 60, 100);
    ok = key_leds(0) == (1u << 0 | 1u << 7);
    fm1_in.notes = 0;
    bad += check("  with a key held: both lit", ok);
    ui.layer = LAYER_SCL;
    {
        uint32_t b1, b2;
        ok = key_leds(&b1) == layer_leds(&b2) && b1 == b2 && b1;
    }
    ui.layer = 0;
    open_family(FAM_SEQ);                              /* SEQ: the keys as elsewhere */
    frame();
    ok &= (key_leds(0) & (1u << 7)) != 0u;
    bad += check("  the layer's map keeps its keys over the MIDI notes; SEQ shows them", ok);
    usb_note(0, 60, 0);
    return bad;
}

/* #119: what can be pressed breathes (hal/fm1_input.h fm1_led_breath: dark .. ~60 % of lit), never a hard blink: every
 * layer, held and locked (double tap), in every MENU > LEDS mode: its keys that can be pressed and its button in the
 * breath set only (neither lit nor the glow), the ones in effect lit, the picture the same at any time (no 250 ms
 * toggle left); OCT+ in a dialog likewise; closing the layer leaves nothing breathing; the glow's level and
 * the breath's peak follow LEDS (fm1_led_dim_level: DIM LO the darker glow and ~30 %, else DIM HI's and ~60 %) */
static uint32_t breath_keys(void)                     /* the breathing keys, bit k = key k (the real positions) */
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++) {
        uint8_t q = led_pos[14u + k];
        m |= (uint32_t)(q != 0xFF && ((fm1_led_breath[q >> 3] >> (q & 7u)) & 1u)) << k;
    }
    return m;
}
static uint32_t lit_keys(const uint8_t *l)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++) {
        uint8_t q = led_pos[14u + k];
        m |= (uint32_t)(q != 0xFF && ((l[q >> 3] >> (q & 7u)) & 1u)) << k;
    }
    return m;
}
static int led_of(const uint8_t *l, uint32_t id)
{
    uint8_t q = led_pos[id];
    return q != 0xFF && ((l[q >> 3] >> (q & 7u)) & 1u);
}
static int test_breath(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    static const uint8_t MODES[4] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
    int bad = 0, ok = 1, okl = 1, okm = 1, oko = 1;
    uint32_t i, m, lk, t, held;
    ui_power_on();
    ui_leds();
    for (i = 0; i < 41u; i++)                           /* (the stub key map has none: 4 LEDs a column) */
        led_pos[i] = (uint8_t)((i / 4u) << 3 | (1u + i % 4u));
    for (i = 0; i < 4u; i++)
        for (lk = 0; lk < 2u; lk++)
            for (m = 0; m < 4u; m++) {
                uint32_t br0 = 0, lit0 = 0, want_br, want_lit, hk = LL[i] == LAYER_GLO ? white(1) : white(2);
                ui_power_on();
                settings_leds = MODES[m];
                go_title("ENV"); frame();
                if (lk) {                                   /* locked (#83), then a key held */
                    press(LB[i]); frames(64); press(LB[i]); frames(100);
                    okl &= ui.lock == LL[i];
                } else {
                    btn_down(LB[i]);
                }
                key_down(hk); frame();
                okl &= ui.layer == LL[i];
                want_lit = layer_leds(&want_br);
                held = LL[i] == LAYER_FX || LL[i] == LAYER_GLO;  /* (HOLD keys: lit while held) */
                for (t = 0; t < 8u; t++) {                  /* 0 .. 1.75 s: the same picture */
                    fm1_ms += 250u;
                    ui_leds();
                    if (!t) {
                        br0 = breath_keys();
                        lit0 = lit_keys(fm1_led);
                    }
                    ok &= breath_keys() == br0 && lit_keys(fm1_led) == lit0 && !lit_keys(fm1_led_dim) &&
                          led_of(fm1_led_breath, panel.btn[LB[i]]) && !led_of(fm1_led, panel.btn[LB[i]]) &&
                          !led_of(fm1_led_dim, panel.btn[LB[i]]);
                }
                ok &= br0 == want_br && lit0 == want_lit && br0 && !(br0 & lit0);
                ok &= !held || (((lit0 >> hk) & 1u) && !((br0 >> hk) & 1u));   /* held: lit, not breathing */
                okm &= host_dim_lo == (MODES[m] == LEDS_DIM_LO);
                key_up(hk); frame();
                if (lk)
                    press(LB[i]);
                else
                    btn_up(LB[i]);
                frames(64);
                ui_leds();
                for (t = 0; t < FM1_NCOL; t++)
                    okl &= !ui.layer && !fm1_led_breath[t];
            }
    settings_leds = LEDS_DIM;
    bad += check("#119 FX GLO SCL EDIT, held and locked, every LEDS mode: the keys that can be pressed breathe", ok);
    bad += check("  ..held / in effect lit, never the glow, the layer's button breathes, steady over time", ok);
    bad += check("  ..closed: nothing breathes", okl);
    bad += check("  ..the breath's peak follows LEDS (DIM LO ~30 % of lit, else ~60 %)", okm);
    /* OCT+ in a dialog: breathing, OCT- lit, in every mode */
    for (m = 0; m < 4u; m++) {
        ui_power_on();
        settings_leds = MODES[m];
        go_title("ENV"); frame();
        ui.confirm = CF_INIT_SOUND;
        for (t = 0; t < 4u; t++) {
            fm1_ms += 250u;
            ui_leds();
            oko &= led_of(fm1_led_breath, panel.btn[B_OCTUP]) && !led_of(fm1_led, panel.btn[B_OCTUP]) &&
                   !led_of(fm1_led_dim, panel.btn[B_OCTUP]) && led_of(fm1_led, panel.btn[B_OCTDN]) ==
                   (MODES[m] != LEDS_INV) && !led_of(fm1_led_breath, panel.btn[B_OCTDN]);
        }
        ui.confirm = 0;
        ui_leds();
        oko &= !led_of(fm1_led_breath, panel.btn[B_OCTUP]);
    }
    settings_leds = LEDS_DIM;
    led_pos_init();
    bad += check("#119 a dialog: OCT+ breathes (not lit, not the glow), OCT- lit (INV: dark), every LEDS mode", oko);
    ui_power_on();
    return bad;
}

/* #35: every idle button and key glows dim, the active ones are lit; a map of the keys' own stays lit / dark */
static int test_idle_glow(void)
{
    int bad = 0, ok;
    ui_power_on();
    ui_leds();
    memset(led_pos, 0xFF, sizeof led_pos);
    led_pos[panel.btn[B_HOME]] = 0u << 3 | 1u;
    led_pos[panel.btn[B_ENV]] = 0u << 3 | 2u;
    led_pos[panel.btn[B_PLAY]] = 0u << 3 | 3u;
    led_pos[14u + 0u] = 1u << 3 | 1u;              /* keys 1 and 2 */
    led_pos[14u + 1u] = 1u << 3 | 2u;
    go_home();
    ui_leds();
    bad += check("HOME: its button lit, ENV and PLAY (stopped) glow, the keys glow, none lit",
                 fm1_led[0] == 2u && fm1_led_dim[0] == 14u && fm1_led[1] == 0u && fm1_led_dim[1] == 6u);
    fm1_in.notes = 1u;
    song.playing = 1;
    ui_leds();
    ok = fm1_led[0] == 2u && (fm1_led_dim[0] & 8u) == 0u && fm1_led[8] == 2u && fm1_led[1] == 2u && fm1_led_dim[1] == 6u;
    fm1_in.notes = 0;
    song.playing = 0;
    bad += check("a key held lit over the glow; PLAY running: its green, its own LED dark", ok);
    ui.layer = 1;
    ui_leds();
    ok = fm1_led_dim[1] == 0u && fm1_led_dim[0] == 14u;
    ui.layer = 0;
    ui_leds();
    ok &= fm1_led_dim[1] == 6u;
    bad += check("a layer's map: the keys lit or dark (no glow), the buttons still glow", ok);
    /* MENU > LEDS INV: the active ones dark, the idle ones lit (stock), no glow; a breath as in DIM (#119); a map
     * of the keys' own as in DIM */
    settings_leds = LEDS_INV;
    ui_leds();
    ok = fm1_led[0] == 12u && fm1_led_dim[0] == 0u && fm1_led[1] == 6u && fm1_led_dim[1] == 0u;   /* HOME dark */
    fm1_in.notes = 1u;
    song.playing = 1;
    ui_leds();
    ok &= fm1_led[0] == 4u && fm1_led_dim[0] == 0u && fm1_led[1] == 4u && fm1_led_dim[1] == 0u && fm1_led[8] == 2u;   /* PLAY, key 1 dark; green */
    fm1_in.notes = 0;
    song.playing = 0;
    bad += check("LEDS INV: the page's button, PLAY running and a key held go dark, the rest lit, no glow", ok);
    ui.layer = 1;
    led_pos[panel.btn[layer_btn()]] = 0u << 3 | 4u;   /* the layer's button */
    {
        uint32_t seen = 0, t;
        for (t = 0; t < 4u; t++) {                  /* the layer's button breathes: never lit (inverted), never the glow */
            fm1_ms = t * 250u;
            ui_leds();
            seen |= ((fm1_led[0] >> 4) & 1u ? 1u : 2u) | ((fm1_led_dim[0] >> 4) & 1u ? 4u : 0u) |
                    ((fm1_led_breath[0] >> 4) & 1u ? 8u : 0u);
        }
        ok = seen == 10u && fm1_led_dim[1] == 0u && fm1_led[1] == (uint8_t)((layer_leds(0) & 3u) << 1);
    }
    ui.layer = 0;
    fm1_ms = 0;
    bad += check("LEDS INV: the layer's button breathes (not inverted); a layer's map stays lit / dark as in DIM", ok);
    settings_leds = LEDS_DIM;
    led_pos_init();
    return bad;
}

/* "On the step sequencer all the LEDs go out": SEQ > STEP, the piano roll and the DRUM grid, stopped and playing,
 * in every LEDS mode, with the LEDs where the hardware has them (hal/fm1_input.h FM1_KEYMAP). The buttons keep
 * their glow and their active states; on the piano roll the keys glow under the held and playing ones; on the
 * grid its map is lit and its keys that do something glow (DIM LO / HI). Before: keys_own() took the grid's keys
 * out of the glow, so in DIM 26 of 27 keys of an empty 16-step grid went fully dark */
/* the white keys of the grid's steps (bit p = step p of the page shown), bit k = key k */
static uint32_t step_keys(uint32_t steps)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        if (!key_black(k) && key_place(k) < 16u && ((steps >> key_place(k)) & 1u))
            m |= 1u << k;
    return m;
}

/* Discussion #127 (1.2): MENU > SCALE LEDS ON, outside the keys' own maps the keys of the selected track's scale glow
 * (the others dark), the root's at the mid level; the keys held / playing lit over it; LEDS OFF / DIM LO / DIM HI
 * alike, INV turned round (root lit, scale mid, the others glowing, the active dark); TRN; QNT WHITE: the white
 * keys; a DRUM track, a layer, the default OFF: as before */
/* MENU > DISPLAY > SCREEN OFF (1.1.5, ui.c scr_*): the screen and its backlight off after NEVER / 5 / 15 / 30 (default)
 * / 60 minutes without panel input; the press, key or turn that wakes it does nothing else; the dialogs, the count-in
 * and the UPDATE MODE countdown keep it on and wake it; the sound goes on as if it were on (forked sessions, bit for
 * bit); the setting saved (a spare favorites byte, 0 = NEVER since 1.1.5.1) and offered in the MENU */
static void scr_idle(uint32_t ms)                  /* nobody touches it for ms (a frame a minute, then frames) */
{
    while (ms > 60000u) { fm1_ms += 60000u - 16u; host_ticks += 60000u * 1000u; frame(); ms -= 60000u; }
    frames(ms);
}
static int scr_woke(uint32_t from)                 /* since host_pw_n was `from`: SLPOUT >= 120 ms after the SLPIN,
                                                    * DISPON + backlight >= 120 ms after the SLPOUT, nothing else */
{
    return host_pw_n == from + 2u && host_pw[from] == 1u && host_pw[from + 1u] == 2u && host_bl &&
           host_pw_ms[from] - host_pw_ms[from - 1u] >= 120u && host_pw_ms[from + 1u] - host_pw_ms[from] >= 120u &&
           scrn.st == SCR_ON;
}
static uint64_t scr_session(uint32_t setting, uint32_t *flags)
{
    int32_t o[2u * CTL];
    uint64_t h = 1469598103934665603ull, e = 0;
    uint32_t f, b, i, off = 0;
    ui_power_on();
    scr_put(setting);
    song.master_q12 = 4096;
    seq_hook = host_seq;                            /* track 1 plays a line (the tests' song player: hostsim.c) */
    memset(host_song, 0, sizeof host_song);
    memset(&hs, 0, sizeof hs);
    host_rows = 0;
    for (i = 0; i < 32u; i += 3u)
        put_row(0, i, 40u + i);
    host_rows = 32;
    transport_req = 1;
    for (f = 0; f < 400u; f++) {
        if (f == 50u)
            fm1_ms += 6u * 60000u;                 /* six minutes untouched: 5 MIN goes dark */
        frame();
        off |= !host_bl;
        for (b = 0; b < 4u; b++) {
            mix_block(o, CTL);
            for (i = 0; i < 2u * CTL; i++) {
                h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
                e += (uint64_t)(o[i] < 0 ? -o[i] : o[i]);
            }
        }
    }
    *flags = off | (song.playing ? 2u : 0u) | (e ? 4u : 0u);
    stop_transport();
    return h;
}
static uint64_t scr_session_fork(uint32_t setting, uint32_t *flags)   /* from the same state each time */
{
    int fd[2];
    uint64_t r[2] = {0, 0};
    pid_t p;
    if (pipe(fd))
        return 0;
    p = fork();
    if (!p) {
        uint32_t fl = 0;
        r[0] = scr_session(setting, &fl);
        r[1] = fl;
        if (write(fd[1], r, sizeof r) != sizeof r) _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], r, sizeof r) != sizeof r) r[0] = 0;
    close(fd[0]);
    waitpid(p, 0, 0);
    *flags = (uint32_t)r[1];
    return r[0];
}
/* 1.2 (Discussions #72 / #73): MOMENTARY, LFO held. KNOB 1..4 on HOME and the sound / FX pages change their values as
 * ever and LFO let go puts each back (no LFO page then; LFO alone still opens it); OCT+ with LFO held keeps them (no
 * octave); not recorded as automation; a sound load meanwhile puts the values back first (the undo copy is the sound as
 * it was); the knobs on STEP / action pages / lists act as ever; a layer's knobs are the layer's; up to 8 values */
static int test_momentary(void)
{
    int bad = 0, ok;
    uint32_t id0, id1, k;
    int16_t v0, v1, g0;
    ui_power_on();
    ui.home = 1;
    id0 = ENGINES[TSEL->eng_req]->knob[0];
    id1 = ENGINES[TSEL->eng_req]->knob[1];
    v0 = TSEL->p[id0];
    v1 = TSEL->p[id1];
    btn_down(B_LFO); frame();
    turn(EN_K1, 6);
    turn(EN_K2, v1 > TP_MIN_OF(id1) ? -4 : 4);
    ok = TSEL->p[id0] != v0 && TSEL->p[id1] != v1 && mom.n == 2u && msg_is("MOMENTARY  [OCT+] KEEP");
 ui_leds();
    btn_up(B_LFO); frame();
    bad += check("MOMENTARY: LFO held, KNOB 1 / 2 on HOME change their values; LFO let go puts them back (no LFO page)",
                 ok && TSEL->p[id0] == v0 && TSEL->p[id1] == v1 && !mom.n && ui.home && msg_is("BACK"));
    press(B_LFO);
    bad += check("  LFO alone still opens its page", !ui.home && cur_page()->fam == FAM_LFO);
    go_home();
    turn(EN_K1, 2);
    bad += check("  without LFO a turn stays", TSEL->p[id0] != v0 && !mom.n);
    v0 = TSEL->p[id0];
    btn_down(B_LFO); frame();
    turn(EN_K1, 5);
    k = (uint32_t)song.octave;
    press(B_OCTUP);
    ok = !mom.n && msg_is("KEPT") && song.octave == (int8_t)k;
    btn_up(B_LFO); frame();
    bad += check("  OCT+ with LFO held keeps them (no octave shift), LFO let go then changes nothing",
                 ok && TSEL->p[id0] != v0 && ui.home);
    /* an FX page (a global) and an engine page */
    open_family(FAM_FX); frame();
    for (k = 0; k < NPAGES && cur_page()->id[0] != G_RSIZE && cur_page()->id[1] != G_RSIZE; k++) { open_family(FAM_FX); frame(); }
    g0 = song.g[G_RSIZE];
    btn_down(B_LFO); frame();
    turn(cur_page()->id[0] == G_RSIZE ? EN_K1 : EN_K2, 9);
    ok = song.g[G_RSIZE] != g0;
    btn_up(B_LFO); frame();
    bad += check("  a global FX value (REVERB SIZE) too: back on release", ok && song.g[G_RSIZE] == g0 && cur_page()->scope == SC_GLOBAL);
    /* a sound load meanwhile: the values back first, then the load (its sound stays) */
    v0 = TSEL->p[id0];
    btn_down(B_LFO); frame();
    turn(EN_K1, 8);
    turn(EN_PRESET, 1);
    ok = !mom.n;
    btn_up(B_LFO); frame();
    {
        int16_t after = TSEL->p[id0];
        undo_swap();
        ok &= TSEL->p[id0] == v0;
        undo_swap();
        ok &= TSEL->p[id0] == after;
    }
    bad += check("  a sound load with LFO held: the values back first (the undo copy is the sound as it was), the load stays", ok);
    /* a layer: its knobs */
    go_home();
    v0 = TSEL->p[id0];
    btn_down(B_LFO); frame();
    btn_down(B_GLO); frame();
    turn(EN_K1, 3);
    ok = !mom.n && TSEL->p[id0] == v0;
    btn_up(B_GLO); frame();
    btn_up(B_LFO); frame();
    bad += check("  with a layer's button held the knobs are the layer's", ok);
    /* at most MOM_N values */
    go_home(); frames(400);                        /* (#39: the layer let go, the knobs quiet a moment) */
    btn_down(B_LFO); frame();
    for (k = 0; k < MOM_N; k++) {
        mom.vp[k] = &trk[k % NTRK].p[P_LEVEL + k / NTRK]; mom.v[k] = *mom.vp[k];
    }
    mom.n = MOM_N;
    v0 = TSEL->p[id0];
    ui.msg_t = 0;
    turn(EN_K1, -3);
    ok = TSEL->p[id0] == v0 && msg_is("MOMENTARY FULL");
    btn_up(B_LFO); frame();
    bad += check("  at most 8 values: MOMENTARY FULL, the turn does nothing", ok && !mom.n);
    return bad;
}
/* 1.2 (Discussion #135): MENU > ANIM IDLE, the LEDs' idle animation (ui_input.c idle_leds; the picture and the scan:
 * tests/os/input_test.c). After a minute without panel input it starts (ui_leds then writes none), with the glow as LEDS
 * says; a key or a button held, a knob turned end it and act as ever (the turn changes its value, the key plays);
 * never while playing, armed, in a layer, the menu, a dialog or NAME, with a MIDI note lit, with ANIM ON or OFF; kept
 * with the settings ([15][10] bit 0) */
static int test_idle_leds(void)
{
    int bad = 0, ok;
    int16_t v;
    persist_t p;
    ui_power_on();
    host_idle = host_anim = 0;
    menu_put(MI_ANIM, 2);
    settings_leds = LEDS_DIM;
    frames(160);
    fm1_ms += IDLE_LEDS_MS - 1000u; frame(); ui_leds();
    ok = !host_idle;
    fm1_ms += 1000u; frame(); ui_leds();
    bad += check("ANIM IDLE: the idle animation after a minute without input (not before), with the glow (LEDS DIM)",
                 ok && host_idle && host_idle_glow);
    memset(fm1_led, 0x5A, sizeof fm1_led);
    ui_leds();
    bad += check("  it has the LEDs: ui_leds writes none", fm1_led[0] == 0x5A && fm1_led[3] == 0x5A);
    v = TSEL->p[ENGINES[TSEL->eng_req]->knob[0]];
    ui.home = 1;
    turn(EN_K1, 3); ui_leds();
    bad += check("  a knob turned ends it at once and is not swallowed (the value turns)", !host_idle &&
                 TSEL->p[ENGINES[TSEL->eng_req]->knob[0]] != v && fm1_led[0] != 0x5A);
    frame(); fm1_ms += IDLE_LEDS_MS; frame(); ui_leds();   /* (the turn counted as input: a minute from it) */
    ok = host_idle;
    fm1_in.notes = 1u << 4; host_notes |= 1u << 4; frame(); ui_leds();
    ok &= !host_idle;
    fm1_in.notes = 0; frame(); ui_leds();
    ok &= !host_idle;
    fm1_ms += IDLE_LEDS_MS - 100u; frame(); ui_leds();
    bad += check("  a key held ends it (the scan does at once) and counts as input: a minute again", ok && !host_idle);
    fm1_ms += 200u; frame(); ui_leds();
    ok = host_idle;
    song.playing = 1; frame(); ui_leds();
    ok &= !host_idle;
    song.playing = 0; ui.menu = 1; frame(); ui_leds();
    ok &= !host_idle;
    ui.menu = 0; ui.confirm = CF_INIT_SOUND; frame(); ui_leds();
    ok &= !host_idle;
    ui.confirm = 0; ui.lock = LAYER_GLO; ui.layer = LAYER_GLO; ui_leds();
    ok &= !host_idle;
    ui.lock = 0; ui.layer = 0; fm1_ms += IDLE_LEDS_MS; ui_leds();   /* (a dialog counts as input, as for SCREEN OFF) */
    ok &= host_idle;
    bad += check("  never while playing, in the menu, a dialog or a layer; back when they end", ok);
    menu_put(MI_ANIM, 0); ui_leds();
    ok = !host_idle;
    menu_put(MI_ANIM, 1); ui_leds();
    ok &= !host_idle;
    menu_put(MI_ANIM, 2); settings_leds = LEDS_OFF; ui_leds();
    bad += check("  ANIM ON / OFF: none; LEDS OFF: no glow", ok && host_idle && !host_idle_glow);
    settings_export(&p);
    ok = p.favorites.factory[15][10] == 1u && !(p.favorites.factory[15][30] & PREF_ANIM_OFF);
    p.favorites.factory[15][10] = 0;
    ok &= settings_import(&p, sizeof p) == 1 && menu_get(MI_ANIM) == 0u;
    bad += check("  kept with the settings ([15][10] bit 0; 0, every older setting: ANIM ON)", ok);
    host_idle = 0;
    ui_idle = 0;
    settings_leds = LEDS_DIM;
    return bad;
}
static int test_screen_off(void)
{
    int bad = 0, ok;
    uint32_t n, rows, i;
    int16_t *vp, v0;

    ui_power_on();
    ok = ui_scr == 0u && scr_get() == 0u && SCR_MIN[scr_get()] == 0u && str_eq(MI_NAME[MI_SCROFF], "SCREEN OFF") &&
         MI_TAB[MI_SCROFF] == MTAB_DISPLAY && menu_n(MI_SCROFF) == 5u && str_eq(menu_vname(MI_SCROFF, 0), "NEVER") &&
         str_eq(menu_vname(MI_SCROFF, 1), "5 MIN") && str_eq(menu_vname(MI_SCROFF, 2), "15 MIN") &&
         str_eq(menu_vname(MI_SCROFF, 3), "30 MIN") && str_eq(menu_vname(MI_SCROFF, 4), "60 MIN") &&
         menu_get(MI_SCROFF) == 0u;
    bad += check("SCREEN OFF: MENU > DISPLAY row, NEVER 5 15 30 60 MIN, NEVER by default (stored 0, 1.1.5.1)", ok);
    menu_put(MI_SCROFF, 3u);                         /* 30 MIN for what follows */

    host_pw_n = 0;
    scr_idle(30u * 60000u - 2000u);
    ok = host_pw_n == 0u && host_bl && scrn.st == SCR_ON;
    scr_idle(2000u);
    ok &= host_pw_n == 1u && host_pw[0] == 0u && !host_bl && scrn.st == SCR_OFF && kb_asleep;
    rows = host_blit_rows;
    ui_message("SAVED");
    scr_idle(5000u);
    ok &= host_blit_rows == rows && host_pw_n == 1u && !ui.msg_t;
    bad += check("SCREEN OFF: 30 MIN untouched -> DISPOFF + SLPIN once; dark: nothing drawn", ok);

    n = host_pw_n;
    ok = ui.home && !ui.menu;
    btn_down(B_ENV); frame();                         /* a page button wakes it, does not open its page */
    ok &= scrn.st == SCR_SLPOUT && host_pw_n == n + 1u && host_pw[n] == 1u;   /* (SLPIN long ago: SLPOUT at once) */
    frames(100u);
    ok &= host_pw_n == n + 1u && host_blit_rows == rows;   /* (dark until 120 ms after it) */
    rows = host_blit_rows;
    frames(200u);
    ok &= scr_woke(n) && host_blit_rows - rows >= 200u;   /* (the frame before DISPON redrew it all) */
    btn_up(B_ENV); frame(); frames(100u);
    ok &= ui.home && !ui.menu && !ui.layer && !scrn.eat && !kb_asleep;
    press(B_ENV);                                     /* awake: the button works again */
    ok &= !ui.home && cur_page()->fam == FAM_ENV;
    bad += check("SCREEN OFF: a button wakes it (SLPOUT, 120 ms, the page redrawn, DISPON), swallowed", ok);

    go_home();
    for (i = 0; i < 3u; i++) {                        /* HOME, SAVE, REC held across the wake: no menu, undo, clear */
        static const uint32_t B[3] = {B_HOME, B_SAVE, B_REC};
        scr_idle(31u * 60000u);
        n = host_pw_n;
        ok = !host_bl;
        hold(B[i]); frames(300u);
        ok &= scr_woke(n) && ui.home && !ui.menu && !ui.confirm && !song.playing && !transport_req;
        bad += check(i == 0 ? "SCREEN OFF: HOME held to wake: no menu, no HOME tap after" :
                     i == 1 ? "SCREEN OFF: SAVE held to wake: no undo, no SAVE page" :
                              "SCREEN OFF: REC held to wake: nothing armed, no clear, no transport", ok);
    }

    scr_idle(31u * 60000u);
    n = host_pw_n;
    ok = !host_bl && !gates();
    key_down(white(0)); frame();                      /* a key: wakes it, no note (seq.c kb_asleep) */
    ok &= !gates();
    frames(300u);
    ok &= scr_woke(n) && !gates();
    key_up(white(0)); frame(); frames(50u);
    ok &= !gates() && !kb_asleep;
    key_down(white(0)); frame();
    ok &= gates() == 1u;                              /* awake: the key plays */
    key_up(white(0)); frame();
    bad += check("SCREEN OFF: a key wakes it silently (no note on, none off), the next one plays", ok);

    scr_idle(31u * 60000u);
    n = host_pw_n;
    home_param(0, &vp);
    v0 = *vp;
    for (i = 0; i < 20u; i++) turn(EN_K1, 1);        /* a knob turned to wake it, still turning (on past the wake) */
    ok = scr_woke(n) && *vp == v0;
    turn(EN_PRESET, 1); turn(EN_ALGO, 1); turn(EN_SELECT, 1); turn(EN_K4, -1);
    ok &= *vp == v0 && song.sel == 0u && song.g[G_BPM] == GP[G_BPM].def && scrn.eat;
    frames(400u);                                     /* at rest SCR_EAT_MS: the knobs are the page's again */
    turn(EN_K1, 1);
    ok &= *vp != v0;
    bad += check("SCREEN OFF: a knob wakes it; turns until it rests 0.3 s change nothing (K1..4, PRESETS, ALGO, SELECT)", ok);

    menu_put(MI_SCROFF, 0u);                         /* NEVER */
    n = host_pw_n;
    scr_idle(3u * 3600000u);
    ok = host_pw_n == n && host_bl && ui_scr == 0u;
    menu_put(MI_SCROFF, 1u);                         /* 5 MIN */
    scr_idle(4u * 60000u + 50000u);
    ok &= host_pw_n == n && host_bl;
    scr_idle(15000u);
    ok &= host_pw_n == n + 1u && !host_bl;
    press(B_PLAY); frames(300u);                      /* PLAY wakes it, does not start the transport */
    ok &= scr_woke(n + 1u) && !song.playing && !transport_req;
    bad += check("SCREEN OFF: NEVER stays on for hours; 5 MIN goes dark after 5; PLAY to wake starts nothing", ok);

    n = host_pw_n;
    ui.confirm = CF_INIT_SOUND; ui.force = 1;          /* a dialog: never dark */
    scr_idle(20u * 60000u);
    ok = host_pw_n == n && host_bl;
    ui.confirm = 0; ui.force = 1;
    scr_idle(6u * 60000u);
    ok &= host_pw_n == n + 1u && !host_bl;
    ui.confirm = CF_INIT_SOUND; ui.force = 1;          /* one opening while dark (the editor, MIDI): it wakes */
    frames(300u);
    ok &= scr_woke(n + 1u) && ui.confirm == CF_INIT_SOUND;
    ui.confirm = 0; ui.force = 1;
    scr_idle(6u * 60000u);
    n = host_pw_n;
    ui.uboot = 3; ui.force = 1;                       /* the UPDATE MODE countdown */
    frames(300u);
    ok &= scr_woke(n);
    ui.uboot = 0; ui.force = 1;
    scr_idle(6u * 60000u);
    n = host_pw_n;
    scr_wake_now();                                   /* main.c: the crash screen, UPDATE, UBOOT */
    ok &= host_pw_n == n + 1u && host_pw[n] == 3u && host_bl && scrn.st == SCR_ON;
    bad += check("SCREEN OFF: a dialog, UPDATE MODE keep it on and wake it; the crash screen at once", ok);

    {   /* the setting with the settings: SCR_CODE in favorites.factory[15][27]; 1.1.5's codes: 0 (its 30 MIN, its
         * default) now NEVER, 3 (its NEVER) NEVER, 2 1 7 as they were; 30 MIN is 4 now; unknown NEVER */
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        menu_put(MI_SCROFF, 4u);
        settings_export(&p); ui_scr = 0;
        ok = p.favorites.factory[15][27] == 7u && settings_import(&p, sizeof p) && scr_get() == 4u;
        p.favorites.factory[15][27] = 0;
        ok &= settings_import(&p, sizeof p) && scr_get() == 0u;
        p.favorites.factory[15][27] = 3;              /* 1.1.5's NEVER */
        ok &= settings_import(&p, sizeof p) && scr_get() == 0u;
        p.favorites.factory[15][27] = 2;              /* 5 MIN, as on 1.1.5 */
        ok &= settings_import(&p, sizeof p) && scr_get() == 1u;
        menu_put(MI_SCROFF, 3u); ok &= ui_scr == 4u && scr_get() == 3u;
        p.favorites.factory[15][27] = 6;              /* unknown: NEVER */
        ok &= settings_import(&p, sizeof p) && scr_get() == 0u;
        ui_scr = 0x55; ok &= scr_get() == 0u;
        bad += check("SCREEN OFF: saved with the settings (0, 1.1.5's 30 MIN, and unknown = NEVER)", ok);
    }

    ui_power_on();                                    /* in the MENU: OCT+ / a knob step it */
    menu_put(MI_SCROFF, 3u);
    hold(B_HOME);
    for (i = 0; i < 5u; i++) turn(EN_PRESET, 1);
    ok = ui.menu == 1 && ui.menu_sel == MI_SCROFF;
    press(B_OCTUP); ok &= scr_get() == 4u;
    press(B_OCTUP); ok &= scr_get() == 4u;           /* (stops at the end) */
    for (i = 0; i < 4u; i++) turn(EN_K2, -1);
    ok &= scr_get() == 0u;
    press(B_OCTDN); ok &= scr_get() == 0u && ui.menu == 1;
    press(B_OCTUP); press(B_OCTUP); press(B_OCTUP); ok &= scr_get() == 3u;
    press(B_HOME); ok &= ui.menu == 0;
    bad += check("SCREEN OFF: the MENU row steps NEVER .. 60 MIN (OCT-/+, any knob), stops at the ends", ok);

    {
        uint32_t f0 = 0, f1 = 0;
        uint64_t a = scr_session_fork(0u, &f0), b = scr_session_fork(1u, &f1);
        bad += check("SCREEN OFF: the sound bit for bit as with NEVER while dark (sequencer playing)",
                     a && a == b && (f0 & 6u) == 6u && (f1 & 6u) == 6u && !(f0 & 1u) && (f1 & 1u));
    }
    ui_power_on();
    return bad;
}

static int test_scale_leds(void)
{
    static const int8_t MAP[6][FM1_NCOL] = {     /* (hal/fm1_input.h FM1_KEYMAP; the host stub is zero) */
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
        {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
        {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
        { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    };
    static const uint8_t MODES[LEDS_COUNT] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
    const uint32_t ALL = 0x7FFFFFFu, C_KEYS = 1u << 7 | 1u << 19;
    int bad = 0, ok;
    uint32_t id, p, r, k, mi, root = 0, sc, white = 0;
    track_t *t;
    ui_power_on();
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (MAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)(p << 3 | r);
    }
    for (k = 0; k < 27u; k++)
        white |= (uint32_t)!key_black(k) << k;
    song.sel = 0;
    t = TSEL;
    t->p[P_SCALE] = 1;                                  /* C major */
    t->p[P_ROOT] = 0;
    settings_leds = LEDS_DIM;
    ui_leds();
    ok = !menu_get(MI_SCLLED) && str_eq(MI_NAME[MI_SCLLED], "SCALE LEDS") && MI_TAB[MI_SCLLED] == MTAB_CONTROL &&
         !scale_leds(&root) && lit_keys(fm1_led_dim) == ALL && !lit_keys(fm1_led_mid);
    bad += check("#127 SCALE LEDS OFF (the default): every key glows as before", ok);
    menu_put(MI_SCLLED, menu_step(MI_SCLLED, 1));
    ok = menu_get(MI_SCLLED) == 1u && (ui_rec_prefs & 0x40u) && !ui_prefs && str_eq(menu_vname(MI_SCLLED, 1), "ON");
    sc = scale_leds(&root);
    bad += check("#127 MENU > CONTROL > SCALE LEDS ON (a bit of its own); C major: the white keys, the root C4 C5",
                 ok && sc == white && root == C_KEYS);
    for (mi = 0; mi < LEDS_COUNT; mi++) {
        uint32_t m = MODES[mi], held = 1u << 1 | 1u << 7;   /* F#3 (out of the scale) and C4 (the root) held */
        char what[128];
        settings_leds = (uint8_t)m;
        fm1_in.notes = 0;
        ui_leds();
        if (m == LEDS_INV)
            ok = lit_keys(fm1_led) == C_KEYS && lit_keys(fm1_led_mid) == (white & ~C_KEYS) &&
                 lit_keys(fm1_led_dim) == (ALL & ~white);
        else
            ok = !lit_keys(fm1_led) && lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == C_KEYS;
        fm1_in.notes = held;
        ui_leds();
        if (m == LEDS_INV)
            ok &= lit_keys(fm1_led) == (C_KEYS & ~held) && !(lit_keys(fm1_led_mid) & held) &&
                  !(lit_keys(fm1_led_dim) & held) && lit_keys(fm1_led_mid) == (white & ~C_KEYS);
        else
            ok &= lit_keys(fm1_led) == held && lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == C_KEYS;
        snprintf(what, sizeof what, "#127 LEDS %s: %s; the keys held %s", LEDS_NAME[m],
                 m == LEDS_INV ? "root lit, the scale mid, the others glow" : "the scale glows, the root brighter",
                 m == LEDS_INV ? "dark" : "lit over it");
        bad += check(what, ok);
    }
    fm1_in.notes = 0;
    settings_leds = LEDS_DIM;
    t->p[P_ROOT] = 2;                                   /* D major: F# and C# in, F and C out, D the root */
    sc = scale_leds(&root);
    ok = (sc >> 1 & 1u) && (sc >> 8 & 1u) && !(sc & 1u) && !(sc >> 7 & 1u) && root == (1u << 9 | 1u << 21) &&
         __builtin_popcount(sc) == 16;
    t->p[P_ROOT] = 0;
    t->p[P_TRANS] = 2;                                  /* C major, TRN +2: F3 plays G3 (in), A#3 plays C4 (the root) */
    sc = scale_leds(&root);
    ok &= (sc & 1u) && !(sc >> 1 & 1u) && root == (1u << 5 | 1u << 17);
    t->p[P_TRANS] = 0;
    bad += check("#127 the keys as they play: ROOT D, TRN +2", ok);
    t->p[P_QUANT] = QN_SNAP;                            /* SNAP: the keys' own notes (the others round down) */
    sc = scale_leds(&root);
    ok = sc == white && root == C_KEYS;
    t->p[P_QUANT] = QN_WHITE;                           /* WHITE, PEN: every white key; a root every 5 white keys */
    t->p[P_SCALE] = 5;
    sc = scale_leds(&root);
    ok &= sc == white && root == (1u << 7 | 1u << 16 | 1u << 24);
    ui_leds();
    ok &= lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == root;
    t->p[P_QUANT] = QN_OFF;
    t->p[P_SCALE] = 0;                                  /* CHR: every key, the C keys brighter */
    sc = scale_leds(&root);
    ok &= sc == ALL && root == C_KEYS;
    bad += check("#127 QNT SNAP: the keys of the scale; QNT WHITE: all the white keys, its roots; CHR: every key", ok);
    t->p[P_SCALE] = 1;
    ui.layer = LAYER_SCL;                               /* a layer: its own map, no scale glow under it */
    ui_leds();
    ok = !lit_keys(fm1_led_mid) && !lit_keys(fm1_led_dim);
    ui.layer = 0;
    bad += check("#127 a layer keeps its own LEDs", ok);
    menu_put(MI_SCLLED, 0);
    ok = !(ui_rec_prefs & 0x40u) && !scale_leds(&root);
    bad += check("#127 SCALE LEDS OFF again: the bit clears", ok);
    ui_rec_prefs = 0;
    settings_leds = LEDS_DIM;
    led_pos_init();
    ui_power_on();
    return bad;
}


/* #39: KNOB 1..4 with a layer's button held are the layer's only, also as it is let go: a knob turned in the frame
 * the button is let go, while its map still shows (an effect key held after FX), or just after: the page under the
 * layer (ENV: its ATTACK) or HOME kept its values. GLO / SCL / EDIT change only what their own knobs edit */
static int16_t lk_p[NTRK][P_COUNT], lk_g[G_COUNT];
static void lk_snap(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        memcpy(lk_p[i], trk[i].p, sizeof lk_p[i]);
    memcpy(lk_g, song.g, sizeof lk_g);
}
static int lk_same(uint32_t l)                      /* nothing changed but the layer's own (l: the layer) */
{
    uint32_t i, j;
    for (i = 0; i < NTRK; i++)
        for (j = 0; j < P_COUNT; j++) {
            int own = (l == LAYER_GLO && j == P_LEVEL) ||
                      (l == LAYER_SCL && i == 0u && (j == P_ROOT || j == P_SCALE || j == P_CHRD || j == P_VOIC)) ||
                      (l == LAYER_EDIT && i == 0u && !param_kept(j));   /* (a load: the sound, not the track's own) */
            if (lk_p[i][j] != trk[i].p[j] && !own)
                return 0;
        }
    for (j = 0; j < G_COUNT; j++)
        if (lk_g[j] != song.g[j] && l != LAYER_EDIT)
            return 0;
    return 1;
}
static void lk_turn(int32_t s)                       /* KNOB 1..4 together, no frame */
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        host_enc[panel.enc[EN_K1 + k]] += s * panel.dir[EN_K1 + k];
}
static int test_layer_knob_leak(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    uint32_t b, sc, pg, fails = 0, env_fx = 0;
    int bad = 0, ok;
    for (b = 0; b < 4u; b++)
        for (sc = 0; sc < 4u; sc++)
            for (pg = 0; pg <= NPAGES; pg++) {          /* every page, HOME last */
                ui_power_on();
                if (pg < NPAGES) { ui.home = 0; ui.page = (uint8_t)pg; page_entered(); }
                frames(100);
                lk_snap();
                btn_down(LB[b]);
                if (sc == 3u) key_down(white(1));
                frame();
                lk_turn(5); frame();
                if (sc == 0u) { lk_turn(2); btn_up(LB[b]); frame(); }          /* turning as it is let go */
                if (sc == 1u) { btn_up(LB[b]); frame(); lk_turn(2); frame(); } /* .. the next frame */
                if (sc == 2u) { btn_up(LB[b]); frames(160); lk_turn(2); frame(); }   /* .. 160 ms after */
                if (sc == 3u) { btn_up(LB[b]); frame(); lk_turn(3); frame(); key_up(white(1)); frame(); }   /* the map */
                frames(400);
                ok = lk_same(LL[b]) && !perf_k[0] && !perf_k[1] && !perf_k[2] && !perf_k[3];
                if (!ok) {
                    fails++;
                    if (fails < 6u)
                        printf("ui:   leak: %s scenario %u page %s\n", B_NAME[LB[b]], sc,
                               pg < NPAGES ? PAGES[pg].title : "HOME");
                }
                if (b == 0u && pg < NPAGES && PAGES[pg].graph == GR_ADSR && trk[0].p[P_ATK] == lk_p[0][P_ATK])
                    env_fx++;
            }
    bad += check("#39 FX / GLO / SCL / EDIT + KNOB 1..4, let go while turning: no page under the layer changes",
                 !fails && env_fx == 4u);
    /* the quiet time is short: KNOB 1 edits the page again 0.3 s after FX */
    ui_power_on();
    go_page(GR_ADSR);
    {
        int16_t m = TSEL->p[P_ATK];
        btn_down(B_FX); frame(); turn(EN_K1, 4); btn_up(B_FX); frame();
        frames(320);
        turn(EN_K1, 1);
        ok = TSEL->p[P_ATK] == m + 1 && !perf_k[0] && str_eq(cur_page()->title, "ENV");
    }
    bad += check("  0.3 s after FX is let go KNOB 1 is ENV's ATTACK again; a knob turned with FX opened no FX page", ok);
    /* a tap of FX with a knob turned in that frame: a combo, no FX page */
    ui_power_on();
    go_page(GR_ADSR);
    {
        int16_t a = TSEL->p[P_ATK];
        btn_down(B_FX); frame(); lk_turn(1); btn_up(B_FX); frame(); frames(100);
        bad += check("  FX tapped while a knob turns: no FX page, no ATTACK change", str_eq(cur_page()->title, "ENV") &&
                     TSEL->p[P_ATK] == a);
    }
    return bad;
}

/* #102: the TIMER5 scan counts the knobs' detents while ui_input runs; one that came after the layer's read of
 * KNOB 1..4 (host_enc_late) was read again by the page's: the layer's control and the page under it both moved
 * (HOME's KNOB 1 with FX's FILTER). Every layer (FX GLO SCL EDIT) x KNOB 1..4 x held past HOLD / a combo / locked
 * by a double tap, over every page and HOME, with late detents on the knob, PRESETS and ALGORITHM while it is open
 * and on the knob as it closes: the same state as with the detents read in time (the layer gets them all, one a
 * frame later), the layer's own control moved, and nothing under the layer changed (lk_same) */
typedef struct {
    int16_t p[NTRK][P_COUNT], g[G_COUNT];
    int8_t k[4];
    uint32_t sel, snd[NTRK];
    uint8_t home, page;
} rc_state_t;
static uint32_t rc_snd(const track_t *t) { return t->eng_req | (uint32_t)t->preset << 8 | (uint32_t)t->user << 16; }
static uint32_t rc_snd0;
static void rc_take(rc_state_t *st)
{
    uint32_t i;
    memset(st, 0, sizeof *st);
    for (i = 0; i < NTRK; i++) {
        memcpy(st->p[i], trk[i].p, sizeof st->p[i]);
        st->snd[i] = rc_snd(&trk[i]);
    }
    memcpy(st->g, song.g, sizeof st->g);
    memcpy(st->k, perf_k, sizeof st->k);
    st->sel = song.sel;
    st->home = ui.home;
    st->page = ui.page;
}
static void rc_enc(uint32_t role, int32_t s, int late)
{
    if (late)
        host_enc_late[panel.enc[role]] += s * panel.dir[role];
    else
        host_enc[panel.enc[role]] += s * panel.dir[role];
}
/* one run: layer button lb, mode (0 held past HOLD, 1 a combo, 2 locked), knob k, page pg (NPAGES: HOME); late:
 * the detents come after the reads. a: the state with the layer open, b: after it closed */
static void rc_run(uint32_t lb, uint32_t mode, uint32_t k, uint32_t pg, int late, rc_state_t *a, rc_state_t *b)
{
    int32_t d = (lb == B_GLO || (lb == B_FX && k == 3u)) ? -1 : 1;   /* (GLO LEVEL, FX DEPTH: room to move) */
    uint32_t i;
    ui_power_on();
    if (pg < NPAGES) { ui.home = 0; ui.page = (uint8_t)pg; page_entered(); }
    frames(64);
    lk_snap();
    rc_snd0 = rc_snd(TSEL);
    if (mode == 2u) {
        press(lb); frames(64); press(lb); frames(64);
    } else {
        btn_down(lb);
        frame();
        if (mode == 0u)
            frames(560);                                /* (past HOLD: the map, a peek) */
    }
    for (i = 0; i < 3u; i++) {
        rc_enc(EN_K1 + k, d, late && (mode != 1u || i));   /* (a combo: the first in time, it opens the map) */
        if (i == 1u) {
            rc_enc(EN_PRESET, 1, late);                 /* (in time: the layer's read drops them) */
            rc_enc(EN_ALGO, 1, late);
        }
        frame();
    }
    frame();                                            /* (the last late one) */
    rc_take(a);
    if (mode == 2u) {
        press(lb);
    } else {
        btn_up(lb);
        frame();
    }
    for (i = 0; i < 3u; i++) {                          /* turning on as it closes (#39: KNOB 1..4 quiet) */
        rc_enc(EN_K1 + k, d, late);
        frame();
    }
    frames(400);
    rc_take(b);
}
static int test_layer_knob_race(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    static rc_state_t a0, b0, a1, b1;
    uint32_t l, mode, k, pg, fails = 0, runs = 0;
    int bad = 0, ok;
    for (l = 0; l < 4u; l++)
        for (mode = 0; mode < 3u; mode++)
            for (k = 0; k < 4u; k++)
                for (pg = 0; pg <= NPAGES; pg++) {
                    if (pg < NPAGES && !page_visible(pg))
                        continue;
                    if (mode == 2u && LB[l] == B_EDIT && pg < NPAGES &&   /* (EDIT's tap acts there: no lock) */
                        (PAGES[pg].graph == GR_USER || PAGES[pg].graph == GR_SLOTS))
                        continue;
                    rc_run(LB[l], mode, k, pg, 0, &a0, &b0);
                    rc_run(LB[l], mode, k, pg, 1, &a1, &b1);
                    ok = !memcmp(&a0, &a1, sizeof a0) && !memcmp(&b0, &b1, sizeof b0) &&
                         lk_same(LL[l] == LAYER_EDIT && k == 2u ? 0u : LL[l]) && song.sel == 0u &&
                         !perf_k[0] && !perf_k[1] && !perf_k[2] && !perf_k[3];
                    if (LL[l] == LAYER_FX)                  /* the layer's own control moved */
                        ok &= a1.k[k] == 3;
                    else if (LL[l] == LAYER_GLO)
                        ok &= a1.p[k][P_LEVEL] == lk_p[k][P_LEVEL] - 3;
                    else if (LL[l] == LAYER_SCL)
                        ok &= a1.p[0][LY_SCL.id[k]] != lk_p[0][LY_SCL.id[k]];
                    else if (k < 2u)                        /* (EDIT KNOB 3 FAV: a mark, KNOB 4 nothing) */
                        ok &= a1.snd[0] != rc_snd0;
                    runs++;
                    if (!ok) {
                        fails++;
                        if (fails < 6u)
                            printf("ui:   race: %s mode %u KNOB %u page %s\n", B_NAME[LB[l]], mode, k + 1u,
                                   pg < NPAGES ? PAGES[pg].title : "HOME");
                    }
                }
    bad += check("#102 FX GLO SCL EDIT (held, combo, locked) x KNOB 1..4, detents counted mid-pass: the layer's only",
                 !fails && runs > 400u);
    /* HOME, REC armed and playing: FX's macros and GLO's T2..T4 levels record nothing into T1's automation */
    ok = 1;
    for (l = 0; l < 2u; l++)
        for (k = l; k < 4u; k++) {
            int16_t *vp, c;
            uint32_t i;
            ui_power_on();
            seq_start(); go_home(); frames(64);
            home_param(k, &vp);
            c = *vp;
            btn_down(LB[l]); frames(560);
            for (i = 0; i < 4u; i++) { rc_enc(EN_K1 + k, l ? -1 : 1, 1); frame(); }
            frame();
            btn_up(LB[l]); frames(400);
            ok &= *vp == c;
            stop_transport();

        }
    bad += check("  HOME with REC: FX's KNOB 1..4, GLO's KNOB 2..4 leave HOME's sound as it was", ok);
    /* FX LATCH: the macro stays when FX is let go, HOME's KNOB 1 never moved; FX + OCT- turns it off */
    ui_power_on();
    fx_latch = 1;
    go_home(); frames(64);
    {
        int16_t *vp, c;
        uint32_t i;
        home_param(0, &vp);
        c = *vp;
        btn_down(B_FX); frames(560);
        for (i = 0; i < 5u; i++) { rc_enc(EN_K1, 1, 1); frame(); }
        frame();
        btn_up(B_FX); frames(400);
        ok = perf_k[0] == 5 && *vp == c;
        btn_down(B_FX); frames(560); press(B_OCTDN); btn_up(B_FX); frames(400);
        ok &= !perf_k[0] && *vp == c;
    }
    fx_latch = 0;
    bad += check("  FX LATCH: FILTER stays after FX is let go, HOME's KNOB 1 never moves; FX + OCT- turns it off", ok);
    /* no layer: a late detent is HOME's KNOB 1's, once (the next pass) */
    ui_power_on();
    go_home(); frames(64);
    {
        int16_t *vp, c;
        home_param(0, &vp);
        c = *vp;
        rc_enc(EN_K1, 1, 1); frame(); frame();
        ok = *vp == c + 1;
    }
    bad += check("  no layer: a detent counted mid-pass is HOME's KNOB 1, once", ok);
    return bad;
}

/* Discussion #83: the layer lock. A double tap of FX / GLO / SCL / EDIT (within LY_DTAP_MS, both let go before HOLD)
 * opens the map with no button held, over the page the first tap left (put back); the keys and KNOB 1..4 act as held;
 * a tap closes it (no page), so does another page button (which opens its page), HOME, the menu, a dialog; OCT- puts
 * back as in the layer; a single tap, a peek, a combo and two slow taps are as before; the header shows the lock */
/* the SEQ layer's white key w tapped (SEQ TOOLS) */
static void tl_key(uint32_t w) { key_down(white(w)); frame(); key_up(white(w)); frame(); }


static int test_layer_lock(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    int bad = 0, ok, okk;
    uint32_t i, pg;
    ok = 1; okk = 1;
    for (i = 0; i < 4u; i++) {                          /* double tap on ENV: locked, ENV under it */
        ui_power_on();
        go_title("ENV"); frame();
        pg = ui.page;
        press(LB[i]);
        ok &= !ui.lock && !ui.layer;                    /* the first tap: its page at once */
        frames(64);
        press(LB[i]);
        frames(500);                                    /* (no button held, long past HOLD) */
        ok &= ui.lock == LL[i] && ui.layer == LL[i] && layer_locked() && !ui.home && ui.page == pg &&
              !fm1_in.buttons && ((layer_seen >> LL[i]) & 1u) && !msg_is("HOLD [GLO] QUICK");
        draw_head();
        okk &= layer_locked();
        press(LB[i]);                                   /* a tap: closed, no page */
        frames(16);
        okk &= !ui.lock && !ui.layer && !ui.home && ui.page == pg;
    }
    bad += check("#83 FX GLO SCL EDIT double tapped: the map stays open with no button held, over the page as it was", ok);
    bad += check("  a tap of the button closes it, no page opens", okk);
    {   /* #183: in every lock PLAY starts and stops (GLO's lock no RESTART: that needs GLO held) */
        static const uint8_t PB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
        static const uint8_t PL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
        ok = 1;
        for (i = 0; i < 4u; i++) {
            uint32_t k;
            ui_power_on();
            go_title("ENV"); frame();
            press(PB[i]); frames(64); press(PB[i]); frames(100);
            transport_req = 0;
            okk = ui.lock == PL[i];
            press(B_PLAY);                              /* stopped: PLAY */
            okk &= transport_req == 1u;
            events_block(CTL);
            okk &= song.playing != 0;
            for (k = 0; k < 2u; k++) {                  /* playing: PLAY stops, twice over */
                press(B_PLAY);
                okk &= transport_req == 2u;
                events_block(CTL);
                okk &= !song.playing && ui.lock == PL[i];
                press(B_PLAY);
                okk &= transport_req == 1u;
                events_block(CTL);
                okk &= song.playing != 0;
            }
            stop_transport();
            transport_req = 0;
            if (!okk)
                printf("    #183: %s locked, PLAY does not start / stop\n", LAYERS[PL[i]].head);
            ok &= okk;
        }
        bad += check("#183 FX GLO SCL EDIT locked: PLAY starts and stops (no RESTART without GLO held)", ok);
    }
    /* the keys and KNOB 1..4 act as held */
    ui_power_on();
    go_title("ENV"); frame();
    press(B_GLO); press(B_GLO); frames(100);
    usb.config = 1;
    {
        uint32_t mo = mo_w;
        int16_t l0 = trk[0].p[P_LEVEL];
        key_down(black(1)); frame(); key_up(black(1)); frame();
        ok = trk[1].p[P_MUTE] == 1 && !gates() && mo_w == mo;
        turn(EN_K1, 3);
        ok &= trk[0].p[P_LEVEL] == l0 + 3 && str_eq(cur_page()->title, "ENV");
        press(B_OCTDN);                                 /* OCT-: the mix put back, still locked */
        ok &= !trk[1].p[P_MUTE] && trk[0].p[P_LEVEL] == l0 && msg_is("MIX PUT BACK") && ui.lock == LAYER_GLO;
    }
    bad += check("  GLO locked: a black key mutes (silent, no MIDI), KNOB 1 T1 LEVEL, OCT- puts back, still locked", ok);
    ui_power_on();
    go_title("ENV"); frame();
    press(B_FX); press(B_FX); frames(100);
    key_down(white(0)); frame();                        /* F3: LPF while held */
    ok = (perf_held & PF_BIT(PF_LPF)) && !gates();
    key_up(white(0)); frame();
    ok &= !(perf_held & PF_BIT(PF_LPF));
    turn(EN_K2, 20);
    ok &= perf_k[1] == 20;
    press(B_FX); frames(16);
    ok &= !ui.lock && !perf_k[1];                      /* (closed: the macros snap back, as FX let go) */
    bad += check("  FX locked: a key's effect while held, KNOB 2 CRUSH; closed: the macros snap back", ok);
    ui_power_on();
    go_title("ENV"); frame();
    press(B_SCL); press(B_SCL); frames(100);
    key_down(white(1)); frame(); key_up(white(1)); frame();   /* G3: ROOT G */
    ok = TSEL->p[P_ROOT] == 7 && !gates();
    turn(EN_K2, 1);
    ok &= TSEL->p[P_SCALE] == 1 && str_eq(cur_page()->title, "ENV");
    bad += check("  SCL locked: a key sets ROOT, KNOB 2 SCL, the page stays ENV", ok);
    ui_power_on();
    go_title("ENV"); frame();
    press(B_EDIT); press(B_EDIT); frames(100);
    turn(EN_K1, 1);
    ok = TSEL->eng_req != trk[1].eng_req || TSEL->eng_req != UT_DEF[0][0];
    ok &= str_eq(cur_page()->title, "ENV") && ui.lock == LAYER_EDIT;
    key_down(init_key()); frame(); key_up(init_key()); frame();   /* INIT: the dialog closes it */
    ok &= ui.confirm == CF_INIT_SOUND && !ui.lock && !ui.layer;
    press(B_OCTDN);
    bad += check("  EDIT locked: KNOB 1 the engine; INIT's dialog closes the lock", ok);
    /* closing: another page button opens its page, HOME goes home, another layer's tap its page, the menu */
    ui_power_on();
    go_title("ENV"); frame();
    press(B_GLO); press(B_GLO); frames(100);
    press(B_LFO);
    ok = !ui.lock && !ui.layer && str_eq(cur_page()->title, "LFO");
    press(B_SCL); press(B_SCL); frames(100);
    ok &= ui.lock == LAYER_SCL && str_eq(cur_page()->title, "LFO");
    press(B_HOME); frames(16);
    ok &= !ui.lock && !ui.layer && ui.home;
    press(B_GLO); press(B_GLO); frames(100);
    press(B_SCL); frames(400);
    ok &= !ui.lock && !ui.layer && cur_page()->fam == FAM_SCL;
    press(B_FX); press(B_FX); frames(100);
    hold(B_HOME);
    ok &= ui.menu && !ui.lock && !ui.layer;
    hold(B_HOME);
    bad += check("  LFO, HOME, another layer's button, the menu close it (and act as always)", ok);
    ui_power_on();
    go_title("ENV"); frame();
    press(B_GLO); press(B_GLO); frames(100);
    btn_down(B_GLO); frames(500);                       /* its button held: the map stays (a peek), let go: closed */
    ok = !ui.lock && ui.layer == LAYER_GLO;
    btn_up(B_GLO); frame();
    ok &= !ui.layer && str_eq(cur_page()->title, "ENV");
    frames(400);
    {
        int16_t a = TSEL->p[P_ATK];
        turn(EN_K1, 1);
        ok &= TSEL->p[P_ATK] == a + 1;                  /* (the page's knobs again after the quiet time) */
    }
    bad += check("  its button held after the lock: a peek, let go closes it; KNOB 1 is ENV's again", ok);
    /* unchanged: a single tap, two slow taps, a tap and a peek, a tap and a combo, PLAY / REC in the lock */
    ui_power_on();
    go_page(GR_TRK); frame();
    press(B_SEQ); frames(400);                          /* (SEQ: its placeholder, no layer) */
    ok = cur_page()->graph == GR_TODO && !ui.lock;
    press(B_SEQ); frames(16); press(B_SEQ); frames(400);  /* (a double tap: no layer to lock) */
    ok &= cur_page()->graph == GR_TODO && !ui.lock && !ui.layer;
    press(B_GLO); frames(400);
    ok &= cur_page()->graph == GR_TODO && !ui.lock;
    press(B_GLO);
    btn_down(B_GLO); frames(500);
    ok &= ui.layer == LAYER_GLO && !ui.lock;
    btn_up(B_GLO); frame();
    ok &= !ui.layer && !ui.lock && cur_page()->graph == GR_TODO;
    press(B_SCL);
    lay_combo(B_SCL, white(2));
    key_up(white(2)); btn_up(B_SCL); frame(); frames(400);
    ok &= !ui.layer && !ui.lock && TSEL->p[P_ROOT] == 9 && cur_page()->fam == FAM_SCL;
    bad += check("  unchanged: a tap opens the page, slow taps cycle, a tap then a peek or a combo: no lock", ok);
    ui_power_on();
    go_page(GR_TRK); frame();
    press(B_GLO); frames(16);
    press(B_LFO); frames(16);
    press(B_GLO); frames(400);
    ok = !ui.lock && cur_page()->graph == GR_TODO;     /* (another button between the taps: two taps) */
    ui_power_on();
    go_title("ENV"); frame();
    press(B_FX); press(B_FX); frames(100);
    press(B_PLAY);
    ok &= song.playing || transport_req;
    ok &= ui.lock == LAYER_FX;
    stop_transport();
    bad += check("  another button between the taps: no lock; PLAY in a locked layer plays, the lock stays", ok);
    return bad;
}

/* 1.2: the FX layer's white keys are the user's: a key held in the layer + PRESETS steps its effect (NONE, the effects,
 * round; the key holds the new one at once), EDIT its default back; PRESETS loads no sound there, with no key held it
 * says how; kept with the settings when the layer closes (an older setting: the default map); the lock frees a hand */
static int test_fx_map(void)
{
    int bad = 0, ok;
    uint32_t c4 = white(4), p, snd;
    ui_power_on();
    memset(fx_keys, 0, 10);
    frame();
    for (ok = 1, p = 0; p < PF_KEYS; p++)
        ok &= perf_map[p] == PF_DEF[p];
    bad += check("FX map: settings of 0 are the default: LPF HPF FREEZE OCT UP OCT DN, D4 .. G5 none", ok);
    snd = TSEL->preset | (uint32_t)TSEL->eng_req << 8;
    btn_down(B_FX); key_down(c4); frame();
    ok = perf_held == PF_BIT(PF_ODN) && ui.layer == LAYER_FX;
    turn(EN_PRESET, 1); keyboard_block();               /* (keyboard_block: the ISR's next block) past the last: NONE */
    ok &= perf_map[4] == PF_N && !perf_held && msg_is("C4 NONE") && (kb_layer >> c4) & 1u;
    turn(EN_PRESET, 1); keyboard_block();               /* round: the first */
    ok &= perf_map[4] == PF_LPF && perf_held == PF_BIT(PF_LPF);   /* (the archived ones skipped) */
    turn(EN_PRESET, -1); turn(EN_PRESET, -1); turn(EN_PRESET, -1); keyboard_block();
    ok &= perf_map[4] == PF_OUP && perf_held == PF_BIT(PF_OUP) && lys.rp_dirty;
    ok &= (TSEL->preset | (uint32_t)TSEL->eng_req << 8) == snd && !gates();
    bad += check("  FX + C4 held + PRESETS: NONE, round to LPF and back to OCT UP, each held at once; no sound load", ok);
    btn_down(B_EDIT); frame(); btn_up(B_EDIT); frame(); keyboard_block();
    ok = perf_map[4] == PF_ODN && perf_held == PF_BIT(PF_ODN) && msg_is("C4 OCT DN (DEF)") && ui.layer == LAYER_FX;
    bad += check("  EDIT with the key held: its default (OCT DN) back, the layer open", ok);
    turn(EN_PRESET, -1); keyboard_block();              /* C4: OCT UP */
    key_up(c4); frame();
    ok = !perf_held && perf_map[4] == PF_OUP;
    turn(EN_PRESET, 1);                                 /* no key held: nothing; how */
    ok &= perf_map[4] == PF_OUP && msg_is("HOLD A KEY: ASSIGN");
    key_down(white(1)); frame();                        /* G3 */
    ok &= perf_held == PF_BIT(PF_HPF);
    key_up(white(1)); btn_up(B_FX); frame();
    ok &= !ui.layer && !lys.rp_dirty;                   /* (closed: settings_save) */
    bad += check("  the key let go: PRESETS changes nothing (HOLD A KEY: ASSIGN); FX let go: the map saved", ok);
    {   /* with the settings: exported, imported; an older record: the default */
        persist_t pp;
        settings_export(&pp);
        memset(fx_keys, 0, 10);
        frame();
        ok = perf_map[4] == PF_ODN;
        ok &= settings_import(&pp, (int)sizeof pp) == 1;
        frame();
        ok &= perf_map[4] == PF_OUP;
        for (p = 0; p < PF_KEYS; p++)
            ok &= p == 4u || perf_map[p] == PF_DEF[p];
        memset(&pp.favorites, 0, sizeof pp.favorites);
        pp.magic = 0x50455233u;                         /* PER3: no favorites */
        ok &= settings_import(&pp, (int)(sizeof pp - sizeof pp.favorites)) == 2;
        frame();
        for (p = 0; p < PF_KEYS; p++)
            ok &= perf_map[p] == PF_DEF[p];
        bad += check("  the map with the settings (exported, imported: C4 OCT UP, the rest default); older settings: default", ok);
    }
    /* the lock (a double tap of FX): one hand holds a key, the other turns PRESETS; EDIT keeps the lock */
    ui_power_on();
    memset(fx_keys, 0, 10);
    go_title("ENV"); frame();
    press(B_FX); press(B_FX); frames(100);
    ok = ui.lock == LAYER_FX;
    key_down(white(1)); frame();                        /* G3: HPF */
    turn(EN_PRESET, 1); keyboard_block();
    ok &= perf_map[1] == PF_FRZ && perf_held == PF_BIT(PF_FRZ) && msg_is("G3 FREEZE");
    btn_down(B_EDIT); frame(); btn_up(B_EDIT); frame(); keyboard_block();
    ok &= perf_map[1] == PF_HPF && perf_held == PF_BIT(PF_HPF) && ui.lock == LAYER_FX && ui.layer == LAYER_FX &&
          str_eq(cur_page()->title, "ENV");
    turn(EN_PRESET, -1); keyboard_block();
    ok &= perf_map[1] == PF_LPF;
    key_up(white(1)); frame();
    press(B_EDIT);                                      /* no key held: EDIT closes the lock as before */
    ok &= !ui.lock && !perf_held;
    bad += check("  locked (FX double tap): G3 held + PRESETS: FREEZE; EDIT: HPF, the lock stays; no key: EDIT closes it", ok);
    {   /* the cells name what the keys hold: G3 "LPF" now, C4 OCT- */
        ok = str_eq(PF_CELL[perf_map[1]], "LPF") && str_eq(PF_CELL[perf_map[4]], "OCT-") && perf_map[12] == PF_N;
        bad += check("  the map's cells: G3 LPF (remapped), C4 OCT-, D5 empty", ok);
    }
    memset(fx_keys, 0, 10);
    frame();
    return bad;
}

/* #40: KNOB 4's DEPTH rises to the right (it fell); MENU > FX LATCH ON: a key with FX turns its effect on, again off,
 * nothing held needed; the macros stay after FX; FX + OCT- all off; the menu turns them off; FX's LED shows them;
 * OFF (the default, and every older setting): as before */
static int test_fx_latch(void)
{
    int bad = 0, ok;
    uint32_t lpf = white(0), m1 = black(0);
    ui_power_on();
    btn_down(B_FX); frame();
    turn(EN_K4, 5);  ok = perf_k[3] == 0;                         /* DEPTH 100 %: right stays at 100 */
    turn(EN_K4, -30); ok &= perf_k[3] == 30;                      /* left: 70 % */
    turn(EN_K4, 10); ok &= perf_k[3] == 20;                       /* right: 80 % */
    key_down(white(3)); frame(); perf_begin(CTL);                 /* OCT UP: KNOB 4 is SHIMR, right is more */
    turn(EN_K4, 10); ok &= perf_k[3] == 30;
    key_up(white(3)); frame(); btn_up(B_FX); frame();
    bad += check("#40 FX + KNOB 4: DEPTH rises to the right, falls to the left; SHIMR (OCT UP) rises to the right",
                 ok && !perf_k[3]);
    /* OFF (the default; a byte older settings leave 0) */
    ui_power_on();
    ok = !(fx_latch & 1u);
    btn_down(B_FX); key_down(lpf); frame(); key_up(lpf); frame(); btn_up(B_FX); frame();
    ok &= !perf_latched && !perf_held;
    bad += check("  FX LATCH OFF by default: a key's effect ends when it is let go", ok);
    /* ON */
    ui_power_on();
    fx_latch = 1;
    frame();                                                      /* (ui_input hands the setting to the ISR) */
    btn_down(B_FX); key_down(lpf); frame(); key_up(lpf); frame();
    turn(EN_K2, 30);
    ok = perf_latched == PF_BIT(PF_LPF) && !perf_held && ui.layer == LAYER_FX && str_eq(layer_head(), "[FX] LATCH") &&
         (layer_leds(0) >> lpf) & 1u;
    btn_up(B_FX); frame();
    ok &= perf_latched == PF_BIT(PF_LPF) && perf_k[1] == 30 && !ui.layer;
    ok &= perf_begin(CTL) && (perf_act & PF_BIT(PF_LPF));                 /* it runs with nothing held */
    bad += check("  FX LATCH ON: FX + F3 latches LPF, CRUSH stays at 30 after FX is let go", ok);
    ui_leds();
    memset(led_pos, 0xFF, sizeof led_pos);
    led_pos[panel.btn[B_FX]] = 0u << 3 | 1u;
    go_home(); ui_leds();
    ok = (fm1_led[0] & 2u) != 0u;
    btn_down(B_FX); key_down(m1); frame(); key_up(m1); frame();   /* a mute latches too */
    ok &= perf_latched == (PF_BIT(PF_LPF) | PF_BIT(PF_M1));
    key_down(lpf); frame(); key_up(lpf); frame();                 /* F3 again: LPF off */
    ok &= perf_latched == PF_BIT(PF_M1);
    btn_up(B_FX); frame();
    bad += check("  the FX LED lit while something is on; a mute key latches; F3 again: LPF off", ok);
    btn_down(B_FX); frame(); turn(EN_K1, -40);
    ok = song.octave == 0 && oct_leds() == 1u;
    btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();
    ok &= !perf_latched && !perf_k[0] && !perf_k[1] && song.octave == 0 && msg_is("FX ALL OFF");
    btn_up(B_FX); frame();
    bad += check("  FX + OCT-: every latched effect and macro off (no octave)", ok);
    /* the quick combo: FX down and OCT- before HOLD, nothing else touched (it was nobody's: no ALL OFF, no octave) */
    btn_down(B_FX); key_down(lpf); frame(); key_up(lpf); frame(); turn(EN_K2, 20); btn_up(B_FX); frame();
    frames(400);
    ok = perf_latched == PF_BIT(PF_LPF) && perf_k[1] == 20 && !ui.layer;
    btn_down(B_FX); frame(); btn_down(B_OCTDN); frame();
    ok &= ui.layer == LAYER_FX && song.octave == 0;               /* (a combo: the map at once) */
    btn_up(B_OCTDN); frame();
    ok &= !perf_latched && !perf_k[1] && song.octave == 0 && msg_is("FX ALL OFF");
    {   uint32_t h = ui.home, pg = ui.page;
        btn_up(B_FX); frame();
        ok &= !ui.layer && ui.home == h && ui.page == pg && !ui.lock;   /* (no tap: no page) */
    }
    bad += check("  FX LATCH: FX + OCT- quick (before HOLD): all off, no octave, no page", ok);
    btn_down(B_FX); key_down(lpf); frame(); key_up(lpf); frame(); btn_up(B_FX); frame();
    frames(400);
    btn_down(B_FX); btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();   /* (both in one frame) */
    ok = !perf_latched && song.octave == 0 && msg_is("FX ALL OFF");
    btn_up(B_FX); frame();
    btn_down(B_FX); frame(); frames(800);                         /* held past HOLD, then OCT- */
    ok &= ui.layer == LAYER_FX;
    press(B_OCTDN);
    ok &= song.octave == 0;
    btn_up(B_FX); frame();
    press(B_OCTDN);                                               /* no FX: the octave as always */
    ok &= song.octave == -1 && !ui.layer;
    press(B_OCTUP);
    ok &= song.octave == 0;
    bad += check("  FX LATCH: FX + OCT- in one frame / held: no octave; OCT- alone: the octave", ok);
    fx_latch = 0;
    frame();
    btn_down(B_FX); frame(); btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();
    ok = song.octave == -1 && ui.layer == LAYER_FX;               /* LATCH OFF: FX's HOLD layer, OCT- is the octave */
    {   uint32_t h = ui.home, pg = ui.page;
        btn_up(B_FX); frame();
        ok &= !ui.layer && ui.home == h && ui.page == pg;
    }
    btn_down(B_FX); frame(); frames(800); press(B_OCTUP); btn_up(B_FX); frame();
    ok &= song.octave == 0;
    btn_down(B_FX); frame(); btn_up(B_FX); frame();               /* FX tap: its page still */
    ok &= !ui.home && cur_page()->fam == FAM_FX;
    go_home();
    frames(400);
    btn_down(B_GLO); frame(); btn_down(B_OCTDN); frame();          /* a SET layer the same: GLO + OCT- quick */
    ok &= ui.layer == LAYER_GLO;
    btn_up(B_OCTDN); frame();
    ok &= song.octave == 0 && msg_is("MIX PUT BACK");
    btn_up(B_GLO); frame();
    ok &= ui.home && !ui.layer;
    bad += check("  FX LATCH OFF: FX + OCT- quick / held shifts the octave (no page); FX tap: the FX page; "
                 "GLO + OCT- quick: put back, no octave", ok);
    fx_latch = 1;
    frame();
    btn_down(B_FX); key_down(lpf); frame(); key_up(lpf); frame(); btn_up(B_FX); frame();
    hold(B_HOME);
    ok = ui.menu && !perf_latched;
    ui.menu_sel = MI_LATCH;
    press(B_OCTDN); ok &= !(fx_latch & 1u) && ui.menu;
    press(B_OCTUP); ok &= (fx_latch & 1u) && ui.menu;
    turn(EN_K1, -1); ok &= !(fx_latch & 1u);
    turn(EN_K1, 1); ok &= (fx_latch & 1u) != 0u;
    turn(EN_K1, -1); ok &= !(fx_latch & 1u);
    press(B_HOME);
    bad += check("  the menu turns the latched effects off; MENU > FX LATCH: OCT+ / KNOB 1 right ON, OCT- / left OFF",
                 ok && !ui.menu);
    {   /* kept with the settings, in the favorites record (an older one: OFF) */
        persist_t p;
        fx_latch = 1;
        settings_export(&p);
        fx_latch = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && (fx_latch & 1u);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;                                    /* PER3: no favorites */
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !(fx_latch & 1u);
        bad += check("  FX LATCH saved with the settings; settings from before it: OFF", ok);
    }
    fx_latch = 0;
    return bad;
}

/* #48: the note divisions step by their length, longest first (triplets between their neighbours), on every list
 * that has them (SEQ DIV, the delay's TIME); the stored values and their sound stay */
static int test_div_order(void)
{
    int bad = 0, ok = 1;
    uint32_t id, c, k, found = 0;
    static const param_desc_t *const LISTS[] = {&TP[P_SDIV], &GP[G_DTIME]};
    ui_power_on();
    for (k = 0; k < NELEM(LISTS); k++) {           /* left end -> right end: each one shorter than the one before */
        const param_desc_t *d = LISTS[k];
        int32_t v = d->def, n, seen = 0;
        uint32_t prev = 0xFFFFFFFFu, len;
        for (n = 0; n < 40; n++) v = param_turn(d, v, -1);
        for (n = 0; n <= d->max - d->min; n++) {
            len = div_samples((uint32_t)v);
            if (!(len < prev && enum_rank(d, v) == d->min + n && !((seen >> v) & 1))) {
                printf("  %s: %s (value %d, place %d, length %u) after %u\n", d->label, d->names[v], (int)v,
                       (int)enum_rank(d, v), len, prev);
                ok = 0;
            }
            seen |= 1 << v;
            prev = len;
            v = param_turn(d, v, 1);
        }
        ok &= seen == (1 << (d->max - d->min + 1)) - 1;
    }
    ok &= str_eq(N_DIV[2], "1/16") && str_eq(N_DIV[4], "8T") && str_eq(N_DIV[9], "4BAR") && TP[P_SDIV].def == 2;
    ok &= param_turn(&TP[P_SDIV], 2, 1) == 5 && param_turn(&TP[P_SDIV], 2, -1) == 4 &&   /* 1/16: right 16T, left 8T */
          param_turn(&TP[P_SDIV], 3, 1) == 3 && param_turn(&TP[P_SDIV], 9, -1) == 9 &&   /* the ends hold */
          param_turn(&TP[P_SDIV], 2, -6) == 8 && param_turn(&TP[P_SDIV], 2, 40) == 3;    /* 6 left: 2BAR; far: clamped */
    bad += check("#48 DIV / RATE / TIME step longest -> shortest, every value once, the ends hold", ok);
    ok = 1;                                         /* the real knob on the pages that show them, the gauge follows */
    for (id = 0; id < NPAGES; id++)
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d;
            ui_power_on();
            ui.home = 0; ui.page = (uint8_t)id; page_entered();
            if (!page_visible(id) || !(d = page_desc(cur_page(), c, &vp)) || !vp || !enum_order(d))
                continue;
            found++;
            *vp = d->names == N_DIV ? 2 : 1;        /* 1/16 */
            turn(EN_K1 + c, 1);
            ok &= *vp == (d->names == N_DIV ? 5 : 4);  /* 16T */
            turn(EN_K1 + c, -2);
            ok &= *vp == (d->names == N_DIV ? 4 : 3);  /* 8T */
        }
    bad += check("#48 the pages' knobs step the divisions by length (DLY)", ok && found >= 1u);
    return bad;
}

/* #52 MENU > KNOB ACCEL (OFF by default, #23): ON, a sustained fast turn of a wide value goes x2..x4 (x8 over a range
 * above 64, #126); a slow turn, the start of a turn, a lone quick detent and a reversal are one step per detent; the
 * sign is always the detents' */
static uint32_t acc_ms(uint32_t role, const uint16_t *gaps, uint32_t n, int32_t dir, int32_t range, int32_t *out)
{
    uint32_t k, sum = 0;
    for (k = 0; k < n; k++) {
        fm1_ms += gaps[k];
        out[k] = accel(role, dir, range);
        sum += (uint32_t)(out[k] < 0 ? -out[k] : out[k]);
    }
    return sum;
}
static int test_knob_accel(void)
{
    int bad = 0, ok;
    uint32_t k, seed = 12345u;
    int32_t o[16];
    static const uint16_t SLOW[8] = {500, 60, 60, 45, 41, 60, 80, 60}, MID[8] = {500, 30, 30, 30, 26, 30, 30, 30},
                          F20[8] = {500, 20, 20, 20, 20, 20, 20, 20}, F10[8] = {500, 10, 10, 10, 10, 10, 10, 10},
                          BOUNCE[8] = {500, 2, 300, 1, 300, 3, 300, 2};
    ui_power_on();
    ok = acc_ms(EN_K1, F10, 8, 1, 127, o) == 8u;                  /* OFF (the default) */
    ui_prefs = PREF_ACCEL;
    memset(ui.enc_t, 0, sizeof ui.enc_t);
    ok &= acc_ms(EN_K1, SLOW, 8, 1, 127, o) == 8u && acc_ms(EN_K2, MID, 8, -1, 127, o) == 8u && o[7] == -1;
    ok &= acc_ms(EN_K3, BOUNCE, 8, 1, 127, o) == 8u;
    bad += check("#52 KNOB ACCEL: OFF by default; ON, slow (>= 26 ms a detent) and lone quick detents: 1 step each", ok);
    fm1_ms += 1000;
    acc_ms(EN_K1, F20, 8, 1, 64, o);
    ok = o[0] == 1 && o[1] == 1 && o[2] == 2 && o[7] == 2;          /* 20 ms a detent: x2 from the third */
    acc_ms(EN_K1, F10, 8, 1, 64, o);                               /* (a pause of 500 ms first: a new turn) */
    ok &= o[0] == 1 && o[1] == 1 && o[2] == 4 && o[7] == 4;         /* 10 ms: x4 (the cap of a range <= 64) */
    fm1_ms += 15; ok &= accel(EN_K1, 1, 64) == 3;                  /* 15 ms: x3 */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -1;                /* reversed: one step, the right sign */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -1;                /* (the turn starts again) */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -4;
    fm1_ms += 30; ok &= accel(EN_K1, -3, 64) == -12;               /* 3 detents in one read (10 ms each): x4 */
    bad += check("#52 KNOB ACCEL ON: 20 ms a detent x2, 15 ms x3, 10 ms x4 from a turn's third detent; reversal x1", ok);
    {   /* #126 a range above 64: 10 ms x5, 8 ms x6, 6 ms and less x8; a flick of 12 detents in ~120 ms, faster in
         * the middle, moves a -100..100 value ~50 (was 38); 12 even ones 52 */
        static const uint16_t FLICK[12] = {500, 16, 12, 10, 8, 7, 7, 8, 10, 12, 16, 20}, F6[8] = {500, 6, 6, 5, 4, 6, 6, 6};
        uint32_t f;
        fm1_ms += 1000;
        f = acc_ms(EN_K2, FLICK, 12, 1, 200, o);
        ok = f == 47u && o[0] == 1 && o[1] == 1;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F10, 8, -1, 200, o) == 2u + 6u * 5u && o[7] == -5;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 127, o) == 2u + 6u * 8u;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 64, o) == 2u + 6u * 4u;      /* (a range <= 64: x4 at most) */
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 0, o) == 8u;                  /* (a list) */
        printf("  #126 a flick of 12 detents over %u ms on -100..100: %u steps\n", 126u, (unsigned)f);
        bad += check("#126 KNOB ACCEL ON over a range above 64: 10 ms x5 .. 6 ms x8; a 12-detent flick ~120 ms: 47", ok);
    }
    fm1_ms += 1000; acc_ms(EN_K4, F10, 8, 1, 32, o);
    ok = o[7] == 1;                                                /* a narrow range (<= 32): never */
    fm1_ms += 1000; acc_ms(EN_K4, F10, 8, 1, 0, o);
    ok &= o[7] == 1;                                               /* a list of names (range 0 at the callers) */
    for (k = 0; k < 20000u && ok; k++) {                           /* random turns: the sign is the detents', at */
        int32_t s, r, gap;                                         /* most x8, a slow detent exactly one step */
        seed = seed * 1103515245u + 12345u;
        s = (int32_t)((seed >> 16) % 7u) - 3;
        gap = (int32_t)((seed >> 8) % 90u);
        fm1_ms += (uint32_t)gap;
        r = accel(EN_K1 + (seed & 3u), s, 127);
        ok &= s ? (r > 0) == (s > 0) && r * s <= 8 * s * s && r * s >= s * s : r == 0;
        if (gap >= 40 * (s < 0 ? -s : s) && r != s) ok = 0;
    }
    bad += check("#52 KNOB ACCEL: never on narrow values or lists; 20000 random reads keep the sign, x1..x8, slow = x1", ok);
    {   /* the real knob: LEVEL on MIXER, a detent a frame (16 ms): 1 + 1 + 3 x 6 = 20 for 8 detents; OFF: 8 */
        int16_t v0;
        ui_power_on(); ui_prefs = PREF_ACCEL;
        go_title("MIXER");
        TSEL->p[P_LEVEL] = 40; v0 = TSEL->p[P_LEVEL];
        fm1_ms += 1000;
        for (k = 0; k < 8u; k++) turn(EN_K1, 1);
        ok = TSEL->p[P_LEVEL] - v0 == 20;
        ui_prefs = 0; TSEL->p[P_LEVEL] = 40; fm1_ms += 1000;
        for (k = 0; k < 8u; k++) turn(EN_K1, 1);
        ok &= TSEL->p[P_LEVEL] == 48;
        bad += check("#52 KNOB ACCEL on MIXER LEVEL: a fast turn of 8 detents moves 20 (OFF: 8)", ok);
    }
    {   /* #126 the FX layer's KNOB 1 (FILTER -100..100): a flick of 12 detents 10 ms apart ~52 (was 12: the layer's
         * knobs went round accel); OFF: 12; slow (60 ms): 12; 6 right then 6 left fast: back to 0; GLO's LEVEL too */
        static const uint32_t GAP[4] = {10u, 10u, 60u, 10u};
        static const uint32_t ON[4] = {1u, 0u, 1u, 1u};
        int32_t got[4], g;
        uint32_t c;
        for (c = 0; c < 4u; c++) {
            ui_power_on();
            ui_prefs = ON[c] ? PREF_ACCEL : 0u;
            go_home(); frames(64);
            btn_down(B_FX); frames(560);
            for (k = 0; k < 12u; k++) {
                host_enc[panel.enc[EN_K1]] += (c == 3u && k >= 6u ? -1 : 1) * panel.dir[EN_K1];
                fm1_ms += GAP[c] - 16u;                         /* (frame() adds 16) */
                frame();
            }
            got[c] = perf_k[0];
            btn_up(B_FX); frames(400);
        }
        printf("  #126 FX FILTER, 12 detents: ON 10 ms %d, OFF %d, ON 60 ms %d, ON 6 right 6 left %d\n", (int)got[0],
               (int)got[1], (int)got[2], (int)got[3]);
        ok = got[0] >= 35 && got[0] <= 60 && got[1] == 12 && got[2] == 12 && got[3] == 0;
        ui_power_on(); ui_prefs = PREF_ACCEL;
        go_home(); frames(64);
        trk[1].p[P_LEVEL] = 0;
        btn_down(B_GLO); frames(560);
        for (k = 0; k < 12u; k++) {
            host_enc[panel.enc[EN_K2]] += panel.dir[EN_K2];
            fm1_ms -= 6u;
            frame();
        }
        g = trk[1].p[P_LEVEL];
        btn_up(B_GLO); frames(400);
        ok &= g > 12 + 12;
        bad += check("#126 KNOB ACCEL on the FX layer (FILTER: a 12-detent flick >= 35, OFF 12, slow 12) and GLO LEVEL", ok);
    }
    {   /* saved with the settings; settings from before it: OFF */
        persist_t p;
        ui_prefs = PREF_ACCEL;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_ACCEL;
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_prefs;
        bad += check("#52 KNOB ACCEL saved with the settings; settings from before it: OFF", ok);
    }
    ui_prefs = 0;
    return bad;
}

/* #51 ("a melody appears when I change presets"): browsing every sound in the list (every engine's presets, then user
 * presets), on HOME and the PRESETS page, stopped and playing: no note is held, the transport is not started */
static int test_browse_no_pattern(void)
{
    int bad = 0, ok = 1;
    uint32_t pass, n, total, loads = 0;
    track_t *t;
    for (pass = 0; pass < 4u; pass++) {
        ui_power_on();
        t = TSEL;
        up_store(3, "MINE");
        up_store(5, "PLAIN");
        if (pass & 1u) song.playing = 1;              /* playing: the transport runs */
        if (pass & 2u) go_page(GR_BROWSE);
        preset_pos(&total);
        for (n = 0; n < total + 2u; n++) {
            turn(EN_PRESET, 1);
            loads++;
            uint32_t v, held = 0;
            for (v = 0; v < NVOICE; v++)
                held += t->v[v].gate;
            if (held || (!(pass & 1u) && (song.playing || transport_req))) {
                printf("  pass %u, sound %u (%s %u): held %u playing %d\n", pass, n,
                       ENGINES[t->eng_req % NENGINES]->name, t->preset, held, song.playing);
                ok = 0;
                break;
            }
        }
    }
    if (!ok || loads < 200u) printf("  ok %d loads %u\n", ok, loads);
    bad += check("#51 every sound browsed (HOME, PRESETS; stopped, playing): nothing held, no start",
                 ok && loads >= 200u);
    return bad;
}

/* MENU > USB LEVEL: MASTER (the default, every older setting) / FIXED; the audio follows at once (audio_test: the levels) */
static int test_usb_level(void)
{
    int bad = 0, ok;
    ui_power_on();
    frame();
    ok = !fx_usb_fixed && str_eq(MI_NAME[MI_USB], "USB LEVEL") && str_eq(menu_flag(MI_USB)->name[0], "MASTER");
    hold(B_HOME);
    ui.menu_sel = MI_USB;
    ui.force = 1; ui_draw();
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed;      /* right: FIXED */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed;   /* left: MASTER */
    press(B_OCTUP); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed && ui.menu;
    press(B_OCTDN); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed && ui.menu;
    press(B_OCTUP);
    press(B_HOME);
    ok &= !ui.menu && fx_usb_fixed;
    {
        persist_t p;
        settings_export(&p);
        ui_prefs = 0;
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_USB_FIXED;
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_prefs;
        frame();
        ok &= !fx_usb_fixed;
    }
    bad += check("MENU > USB LEVEL: MASTER by default, KNOB 1 / OCT+ FIXED, left / OCT- MASTER, saved, the audio follows", ok);
    ui_prefs = 0;
    frame();
    return bad;
}

/* 1.1 (Discussion #131) MENU > AUDIO CLICK (OFF REC ON), CLICK LEVEL (LOW MID HIGH): OFF / MID by default and in
 * every older setting; a knob applies at once (click.c); saved and read back, a stored 3 (no value) the default. 1.5:
 * no COUNT-IN (the row, the screen and seq.c's count are gone): PLAY armed from a stop plays at once, whatever an older
 * setting's COUNT-IN bits say (kept as saved) */
static int test_click_menu(void)
{
    int bad = 0, ok;
    uint32_t i;
    ui_power_on();
    frame();
    ok = !ui_rec_prefs && click_mode == CLICK_OFF && click_lvl == 1u &&
         str_eq(MI_NAME[MI_CLICK], "CLICK") && str_eq(MI_NAME[MI_CLKLVL], "CLICK LEVEL") &&
         MI_TAB[MI_CLICK] == MTAB_AUDIO && MI_TAB[MI_CLKLVL] == MTAB_AUDIO &&
         menu_get(MI_CLICK) == 0u && menu_get(MI_CLKLVL) == 1u;
    for (i = 0; i < MI_COUNT; i++)
        ok &= !str_eq(MI_NAME[i], "COUNT-IN");
    for (i = 0; i < 3u; i++)
        ok &= (i == 2u || str_eq(menu_vname(MI_CLICK, i), (const char *[]){"OFF", "ON"}[i])) &&
              str_eq(menu_vname(MI_CLKLVL, i), (const char *[]){"LOW", "MID", "HIGH"}[i]);
    ok &= menu_n(MI_CLICK) == 2u;
    bad += check("MENU > AUDIO: CLICK OFF / ON, CLICK LEVEL LOW / MID / HIGH; OFF MID by default; no COUNT-IN", ok);
    hold(B_HOME);
    ui.menu_sel = MI_LOWCUT; ui.force = 1; ui_draw();
    turn(EN_PRESET, 1); turn(EN_PRESET, 1);
    ok = ui.menu_sel == MI_CLICK;
    turn(EN_K1, 1); ok &= click_mode == CLICK_ON;
    turn(EN_K2, 1); ok &= click_mode == CLICK_ON;      /* (stops at the end) */
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_CLKLVL;
    press(B_OCTDN); ok &= click_lvl == 0u;
    press(B_OCTUP); press(B_OCTUP); ok &= click_lvl == 2u;
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_TUNE;     /* (1.2: TUNE, AUDIO's last) */
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_LOWCUT;   /* (wraps within AUDIO) */
    press(B_HOME);
    ok &= !ui.menu;
    bad += check("MENU > AUDIO: PRESETS to CLICK, CLICK LEVEL, TUNE; knobs / OCT step them, applied at once", ok);
    {
        persist_t p;
        settings_export(&p);
        ui_rec_prefs = 0; frame();
        ok = settings_import(&p, (int)sizeof p) == 1 && (frame(), click_mode == CLICK_ON && click_lvl == 2u);
        p.favorites.factory[15][28] = 3u | 3u << 2 | 1u << 4;   /* CLICK and LEVEL no value, an old COUNT-IN 1 BAR */
        ok &= settings_import(&p, (int)sizeof p) == 1 && p.favorites.factory[15][28] == 1u << 4;
        frame();
        ok &= click_mode == CLICK_OFF && click_lvl == 1u;
        memset(&p.favorites, 0, sizeof p.favorites);   /* settings from before the favorites (PER3): the defaults */
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_rec_prefs;
        frame();
        ok &= click_mode == CLICK_OFF && click_lvl == 1u;
    }
    bad += check("CLICK / CLICK LEVEL saved and read back; a stored 3 the default; older settings OFF / MID", ok);

    /* no count-in: PLAY plays at once (an older setting's COUNT-IN 2 BARS bits too) */
    ui_power_on();
    ui_rec_prefs = 2u << 4;
    rp_apply();
    open_family(FAM_SEQ);
    frame();
    press(B_PLAY);
    ok = transport_req == 1u;
    events_block(CTL);
    frame();
    ok &= song.playing && cur_page()->graph == GR_TODO;
    bad += check("no count-in: PLAY plays at once, the page stays", ok);
    press(B_PLAY);
    events_block(CTL);
    ui_rec_prefs = 0;
    stop_transport();
    return bad;
}

/* #67 MENU > USB SERIAL: ON by default (and in every older setting): the console presented, the descriptors as
 * before. OFF: audio + MIDI only, device class 0, bcdUSB 1.10 (CFG_DESC_PLAIN: a BLUEHARE_CDC=0 build's bytes), applied
 * when the menu closes (one re-enumeration); saved; at boot the saved choice before USB starts (never on then off) */
static int dev_class(void)                              /* the device class GET_DESCRIPTOR sends now (-1: none) */
{
    const uint8_t *d;
    uint16_t n;
    return get_desc(0x0100, &d, &n) && n == 18u ? d[4] : -1;
}
static int dev_is(const uint8_t *want)                  /* the device descriptor sent now = want */
{
    const uint8_t *d;
    uint16_t n;
    return get_desc(0x0100, &d, &n) && n == 18u && !memcmp(d, want, 18);
}
static int plain_desc(void)                             /* device + configuration = the console-less bytes */
{
    const uint8_t *d, *c;
    uint16_t n, m;
    return get_desc(0x0100, &d, &n) && get_desc(0x0200, &c, &m) && n == 18u && !memcmp(d, DEV_DESC_PLAIN, 18) &&
           m == sizeof CFG_DESC_PLAIN && !memcmp(c, CFG_DESC_PLAIN, m);
}
static int test_usb_serial(void)
{
    int bad = 0, ok;
    ui_power_on();
    usb.up = 0;                                          /* (no SIE here: usb_cdc_switch only flips the descriptors) */
    frame();
    ok = usb_cdc_on && dev_class() == 0xEF && !(ui_prefs & PREF_SERIAL_OFF) && str_eq(MI_NAME[MI_SERIAL], "USB SERIAL") &&
         str_eq(menu_flag(MI_SERIAL)->name[0], "ON") && str_eq(menu_flag(MI_SERIAL)->name[1], "OFF");
    ok &= dev_is(DEV_DESC);
    bad += check("#67 MENU > USB SERIAL: ON by default, the console presented (misc / IAD, as before)", ok);
    hold(B_HOME);
    ui.menu_sel = MI_SERIAL;
    ui.force = 1; ui_draw();
    turn(EN_K1, -1);
    ok = (ui_prefs & PREF_SERIAL_OFF) && usb_cdc_on;    /* left: OFF; applied when the menu closes */
    turn(EN_K1, 1); ok &= !(ui_prefs & PREF_SERIAL_OFF);   /* right: ON (the switch's knob right) */
    press(B_OCTDN); ok &= (ui_prefs & PREF_SERIAL_OFF) && ui.menu && usb_cdc_on;   /* OCT-: OFF */
    press(B_HOME);
    ok &= ui.menu == 0 && usb_cdc_on;                 /* (HOME acts as it is let go: closed this frame) */
    frame();
    ok &= !ui.menu && !usb_cdc_on && !usb.config && plain_desc() && dev_class() == 0;
    bad += check("  KNOB 1 left / OCT- OFF, right ON; the menu closed (HOME): audio + MIDI only, class 0", ok);
    {
        persist_t p;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_SERIAL_OFF;
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_SERIAL_OFF;     /* (again: the same) */
        /* boot (main.c): the settings, usb_serial_apply, then usb_start: the console-less descriptors from the start */
        usb_cdc_on = 1;
        usb.up = 0;
        usb_serial_apply();
        ok &= !usb_cdc_on && !usb.up && plain_desc();
        memset(&p.favorites, 0, sizeof p.favorites);   /* settings from before it (PER3): ON */
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !(ui_prefs & PREF_SERIAL_OFF);
        usb_serial_apply();
        ok &= usb_cdc_on && dev_is(DEV_DESC);
    }
    bad += check("  saved with the settings (older ones: ON); at boot the device enumerates with the saved choice", ok);
    {   /* OFF: the soft key and the update frames still come in (USB-MIDI SysEx, EP1) */
        static const uint8_t KEY[8] = {0x04, 0xF0, 0x22, 0x24, 0x07, 0x35, 0x7D, 0xF7};
        ui_prefs = PREF_SERIAL_OFF;
        frame();
        usb.uboot_req = 0;
        ok = !usb_cdc_on && ep1_take(KEY, sizeof KEY) && usb.uboot_req;
        usb.uboot_req = 0;
        bad += check("  OFF: the soft key (SysEx on the MIDI endpoint) still asks for UPDATE MODE", ok);
    }
    ui_prefs = 0;
    frame();
    return bad;
}

/* #58 MENU > BPM LOCK: OFF by default (and in every older setting): SELECT sets the tempo on every page. ON: SELECT
 * leaves it on the pages ("BPM LOCKED" once per turn burst); HOME > CLOCK's BPM knob, GLO + SELECT (a combo: the map
 * opens, no GLO page; OCT- puts it back) and GLO's F4 TAP still set it; the menu never takes SELECT as the tempo */
static int test_bpm_lock(void)
{
    int bad = 0, ok;
    int16_t b0;
    uint32_t i, page;
    ui_power_on();
    ui_prefs = 0;
    frame();
    b0 = song.g[G_BPM];
    turn(EN_SELECT, 2);
    ok = song.g[G_BPM] == b0 + 2 && !msg_is("BPM LOCKED") && str_eq(menu_flag(MI_BPMLOCK)->name[0], "OFF");
    bad += check("#58 BPM LOCK OFF (the default): SELECT sets the tempo as before", ok);

    hold(B_HOME);
    ui.menu_sel = MI_BPMLOCK;
    ui.force = 1; ui_draw();
    b0 = song.g[G_BPM];
    turn(EN_SELECT, 3);
    ok = str_eq(MI_NAME[MI_BPMLOCK], "BPM LOCK") && ui.menu == 1u && song.g[G_BPM] == b0;   /* (the menu: no tempo) */
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_BPM_LOCK) != 0u;          /* right: ON */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_BPM_LOCK);              /* left: OFF */
    press(B_OCTUP); ok &= (ui_prefs & PREF_BPM_LOCK) && ui.menu;      /* OCT+: ON */
    press(B_OCTDN); ok &= !(ui_prefs & PREF_BPM_LOCK) && ui.menu;     /* OCT-: OFF */
    press(B_OCTUP);
    ok &= ui_prefs == PREF_BPM_LOCK;                                  /* (no other setting moved) */
    press(B_HOME);
    bad += check("#58 MENU > BPM LOCK: KNOB 1 right / OCT+ ON, left / OCT- OFF; SELECT in the menu is no tempo", ok && !ui.menu);

    /* ON: no tempo from SELECT on HOME and the pages; "BPM LOCKED" once per burst */
    ok = 1;
    for (i = 0; i < 3u; i++) {
        if (i == 1u) go_title("ENV");
        if (i == 2u) go_title("STEP");
        frame();
        ui.msg_t = 0; fm1_ms += 1200u;
        b0 = song.g[G_BPM];
        turn(EN_SELECT, 1);
        ok &= song.g[G_BPM] == b0 && msg_is("BPM LOCKED") && !ui.bpm_t;
        ui.msg_t = 0;
        turn(EN_SELECT, -4);                                          /* (the same burst: said once) */
        ok &= song.g[G_BPM] == b0 && !ui.msg_t;
    }
    fm1_ms += 1200u;
    turn(EN_SELECT, 1);
    ok &= msg_is("BPM LOCKED");                                       /* (a new burst: said again) */
    ok &= text_w(&AF_S, "BPM LOCKED") <= 236 - 106;                   /* (fits the header's message zone) */
    bad += check("#58 BPM LOCK ON: SELECT leaves the tempo on HOME and the pages, \"BPM LOCKED\" once per turn burst", ok);

    /* ON: HOME > CLOCK's BPM knob (up to 1.1.5 GLO > GLOBAL's) */
    go_home(); press(B_HOME); press(B_HOME); frame();   /* (HOME > MIXER > CLOCK) */
    b0 = song.g[G_BPM];
    turn(EN_K1, 1);
    ok = str_eq(cur_page()->title, "CLOCK") && cur_page()->id[0] == G_BPM && song.g[G_BPM] == b0 + 1;
    bad += check("#58 BPM LOCK ON: HOME > CLOCK KNOB 1 still sets the tempo", ok);

    /* ON: GLO + SELECT (a quick turn: a combo, no GLO page), OCT- puts it back, F4 TAP */
    go_title("ENV"); frame();
    page = ui.page;
    b0 = song.g[G_BPM];
    ui.msg_t = 0;
    btn_down(B_GLO); frame();
    turn(EN_SELECT, 3);
    ok = song.g[G_BPM] == b0 + 3 && ui.bpm_t && !msg_is("BPM LOCKED") && ui.layer == LAYER_GLO;
    btn_up(B_GLO); frame();
    ok &= ui.page == page && !ui.home;                                /* (no tap: still ENV) */
    btn_down(B_GLO); frame();
    turn(EN_SELECT, -5);
    ok &= song.g[G_BPM] == b0 - 2;
    oct_back();
    ok &= song.g[G_BPM] == b0 + 3;                                    /* (OCT-: the tempo as this layer opened) */
    btn_up(B_GLO); frame();
    bad += check("#58 BPM LOCK ON: GLO + SELECT sets the tempo (a combo: the map, no GLO page; OCT- puts it back)",
                 ok && ui.page == page);
    song.g[G_BPM] = 100;
    btn_down(B_GLO); frame();
    for (i = 0; i < 3u; i++) { key_down(white(7)); frame(); key_up(white(7)); frames(480); }
    ok = song.g[G_BPM] == 120;
    btn_up(B_GLO); frame();
    bad += check("#58 BPM LOCK ON: GLO + F4 TAP still sets the tempo", ok);

    {   /* saved with the settings; settings from before it: OFF (and so again: idempotent) */
        persist_t p, q;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_BPM_LOCK;
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && ui_prefs == PREF_BPM_LOCK && !memcmp(&p, &q, sizeof p);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;                                        /* (PER3: before the favorites record) */
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_prefs;
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && !ui_prefs;
        frame();
        b0 = song.g[G_BPM];
        go_home(); frame();
        turn(EN_SELECT, 1);
        ok &= song.g[G_BPM] == b0 + 1;
        bad += check("#58 BPM LOCK saved with the settings; settings from before it: OFF (idempotent)", ok);
    }
    ui_prefs = 0;
    ui_power_on(); frame();
    return bad;
}

/* #15 / Discussion #80 MENU > LARGE: OFF by default (and in every older setting); KNOB 1 right ON / left OFF, OCT+
 * toggles, nothing else moves. ON: HOME and the value pages draw tall cards (a K1..K4 pill over each knob's column,
 * the value in L) and the strip under them; the list / graph pages and the layers keep the layout (their labels in
 * M); the footer stays; a knob turned redraws its own card only; saved with the settings, older settings OFF
 * (idempotent) */
static uint16_t lg_px(int32_t x, int32_t y) { return swap16(host_screen[y * 240 + x]); }
static int lg_value_rows(uint32_t c)                    /* rows of card c's value band holding the value's colour */
{
    int32_t x, y, n = 0;
    for (y = Y_LABEL + 40; y < Y_LABEL + 76; y++)
        for (x = CARD_X(c); x < CARD_X(c) + CARD_W; x++)
            if (lg_px(x, y) == T_THEME) { n++; break; }
    return n;
}
static int test_large(void)
{
    int bad = 0, ok;
    uint32_t c;
    static uint16_t off[240 * 240];
    ui_power_on();
    song.playing = 0;
    frame();
    ok = !(ui_prefs & PREF_LARGE) && large_kind() == LK_OFF && card_h() == CARD_H && graph_y() == Y_GRAPH;
    ok &= MI_LARGE == MI_STYLE + 1u && str_eq(MI_NAME[MI_LARGE], "LARGE") && menu_flag(MI_LARGE) &&
          str_eq(menu_flag(MI_LARGE)->name[0], "OFF") && str_eq(menu_flag(MI_LARGE)->name[1], "ON");
    ui.force = 1; frame();
    memcpy(off, host_screen, sizeof off);
    hold(B_HOME); ui.menu_sel = MI_LARGE;
    ok &= ui.menu == 1u;
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_LARGE) != 0u;          /* right: ON */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_LARGE);              /* left: OFF */
    press(B_OCTUP); ok &= (ui_prefs & PREF_LARGE) && ui.menu;     /* OCT+: ON */
    press(B_OCTDN); ok &= !(ui_prefs & PREF_LARGE) && ui.menu;    /* OCT-: OFF */
    press(B_OCTUP);
    ok &= ui_prefs == PREF_LARGE && ui_style == ST_FLAT;          /* (no other setting moved) */
    press(B_HOME);
    bad += check("#15 MENU > LARGE: OFF by default, under STYLE; KNOB 1 right / OCT+ ON, left / OCT- OFF", ok && !ui.menu);

    /* HOME: tall cards (rows 28..132) and the strip (136..198); the footer as it was */
    frame();
    ok = ui.home && large_kind() == LK_TALL && card_h() == LG_CARD_H && graph_y() == LG_Y_GRAPH && strip_kind() == SK_SCOPE;
    ok &= lg_px(30, Y_SEP_END + 1) == T_SURF && off[(Y_SEP_END + 1) * 240 + 30] == swap16(T_BG);     /* a card where the gap was */
    ok &= lg_px(30, LG_Y_GRAPH - 2) == T_BG && off[(LG_Y_GRAPH - 2) * 240 + 30] == swap16(T_SURF);   /* a gap in the panel */
    ok &= lg_px(30, LG_Y_GRAPH + 1) == T_SURF;
    for (c = 0; c < 4u; c++)                                      /* each column's K pill, over its knob */
        ok &= lg_px(CARD_X(c) + 8, Y_LABEL + LG_MY + LG_MH / 2) == T_KEY;
    ok &= !memcmp(off + Y_FOOT * 240, host_screen + Y_FOOT * 240, 240u * H_FOOT * sizeof *off);
    bad += check("#15 LARGE ON: HOME's cards tall (a K1..K4 pill over each column), the scope a strip, the footer kept", ok);

    /* the value in L: CLOCK's BPM 124 stands about twice as tall as in M */
    go_title("CLOCK"); frame();
    ok = large_kind() == LK_TALL && strip_kind() == SK_TITLE && cur_page()->id[0] == G_BPM && lg_value_rows(0) >= AF_L_CAP_H - 3;
    ui_prefs = 0; ui.force = 1; frame();
    ok &= lg_value_rows(0) <= AF_M_CAP_H + 2;
    ui_prefs = PREF_LARGE; ui.force = 1; frame();
    bad += check("#15 LARGE: a card value in L (CLOCK's BPM 120: about 21 rows of figures, M's 11)", ok);

    /* a knob turned: its own card redrawn, the other three not */
    go_title("ENV"); frame(); frames(600);
    memcpy(off, host_screen, sizeof off);
    turn(EN_K2, 3);
    {
        int32_t x, y, diff[4] = {0, 0, 0, 0};
        for (y = Y_LABEL; y < Y_LABEL + LG_CARD_H; y++)
            for (c = 0; c < 4u; c++)
                for (x = CARD_X(c); x < CARD_X(c) + CARD_W; x++)
                    diff[c] += off[y * 240 + x] != host_screen[y * 240 + x];
        ok = large_kind() == LK_TALL && strip_kind() == SK_ADSR && diff[1] && !diff[0] && !diff[2] && !diff[3];
    }
    bad += check("#15 LARGE: ENV tall with its ADSR in the strip; KNOB 2 turned redraws card 2 only", ok);

    /* per page type: lists and rolls keep the layout, the layers too; MIXER's tracks in the strip */
    go_page(GR_BROWSE); frame();
    ok = large_kind() == LK_LABEL && card_h() == CARD_H && graph_y() == Y_GRAPH && graph_h() == H_GRAPH;
    go_page(GR_TODO); frame();
    ok &= large_kind() == LK_LABEL;
    go_page(GR_TRK); frame();
    ok &= large_kind() == LK_TALL && strip_kind() == SK_TRK && lg_px(CARD_X(0) + 8, LG_Y_GRAPH + 56) == T_SURF;
    go_title("ENV"); frame();
    btn_down(B_GLO); frames(800);
    ok &= ui.layer && large_kind() == LK_LABEL && card_h() == CARD_H;
    btn_up(B_GLO); frame(); frame();
    ok &= !ui.layer && large_kind() == LK_TALL && lg_px(30, LG_Y_GRAPH - 2) == T_BG;
    bad += check("#15 LARGE: PRESETS and SEQ keep the layout (labels in M), the layers too; MIXER tall, its tracks in the strip", ok);

    {   /* saved with the settings; settings from before it: OFF (and so again: idempotent) */
        persist_t p, q;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_LARGE;
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && ui_prefs == PREF_LARGE && !memcmp(&p, &q, sizeof p);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;                                    /* (PER3: before the favorites record) */
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_prefs;
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && !ui_prefs;
        ui.force = 1; frame();
        ok &= large_kind() == LK_OFF && lg_px(30, Y_SEP_END + 1) == T_BG;
        bad += check("#15 LARGE saved with the settings; settings from before it: OFF (idempotent)", ok);
    }
    ui_prefs = 0;
    ui_power_on(); frame();
    return bad;
}

/* MENU (1.0.5: in tabs): every row reachable with ALGORITHM and PRESETS, each tab its own page; #46 MENU > ANIM: ON by
 * default (and in every older setting), OFF snaps the rolling digits, the piano roll and the menu's tabs */
static int test_menu_prefs(void)
{
    int bad = 0, ok = 1;
    uint32_t k, j, seen = 0;
    static uint16_t shot[MTAB_COUNT][240 * 240];
    ui_power_on();
    ui_prefs = 0;
    hold(B_HOME);
    ui.menu_sel = 0;
    for (k = 0; k < MTAB_COUNT; k++) {              /* ALGORITHM right through the tabs, PRESETS through each one's rows */
        ok &= menu_tab() == k && ui.menu_sel == mtab_first(k);
        for (j = 0; j < mtab_rows(k); j++) {
            seen |= 1u << ui.menu_sel;
            turn(EN_PRESET, 1);
        }
        ok &= ui.menu_sel == mtab_first(k);         /* (round the tab, back at its first row) */
        ui.force = 1; ui_draw();
        memcpy(shot[k], host_screen, sizeof shot[k]);
        for (j = 0; j < k; j++) ok &= memcmp(shot[j], shot[k], sizeof shot[k]) != 0;
        turn(EN_ALGO, 1);
    }
    ok &= seen == (1u << MI_COUNT) - 1u && ui.menu_sel == 0u;
    bad += check("MENU: ALGORITHM and PRESETS reach every row, each tab a page of its own", ok);
    /* ANIM */
    ui.menu_sel = MI_ANIM;
    ok = str_eq(MI_NAME[MI_ANIM], "ANIM") && !(ui_prefs & PREF_ANIM_OFF);
    turn(EN_K1, 1); ok &= !(ui_prefs & PREF_ANIM_OFF) && (ui_idle & 1u) && menu_get(MI_ANIM) == 2u;   /* right: IDLE (1.2) */
    turn(EN_K1, 1); ok &= (ui_idle & 1u) && str_eq(menu_vname(MI_ANIM, menu_get(MI_ANIM)), "IDLE");  /* (the end) */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_ANIM_OFF) && !ui_idle && menu_get(MI_ANIM) == 0u;      /* left: ON */
    turn(EN_K1, -1); ok &= (ui_prefs & PREF_ANIM_OFF) != 0 && !ui_idle;                             /* left: OFF */
    turn(EN_K1, -1); ok &= (ui_prefs & PREF_ANIM_OFF) != 0;                                         /* (the end) */
    turn(EN_K1, 1); ok &= !(ui_prefs & PREF_ANIM_OFF) && !ui_idle;                                  /* right: ON */
    press(B_OCTDN); ok &= (ui_prefs & PREF_ANIM_OFF) && ui.menu;  /* OCT-: OFF */
    ok &= !(ui_prefs & ~(uint32_t)PREF_ANIM_OFF);                 /* (no other setting moved) */
    press(B_HOME);
    bad += check("#46 MENU > ANIM: ON by default, KNOB 1 / OCT steps OFF ON IDLE (1.2), the values 0 ON 1 OFF 2 IDLE",
                 ok && !ui.menu && str_eq(ANIM_N[0], "ON") && str_eq(ANIM_N[1], "OFF"));
    ui_prefs = 0;
    ui_idle = 0;
    ui_power_on(); roll_settle();
    turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 1 && ui.roll[3].from[0];              /* ON: TUNE 0 -> 1 rolls */
    ui_power_on(); ui_prefs = PREF_ANIM_OFF; roll_settle();      /* (ui_power_on clears the favorites record) */
    turn(EN_K4, 1);
    ok &= song.g[G_TUNE] == 1 && !ui.roll[3].from[0] && roll_static_now();   /* OFF: the value at once, static */
    turn(EN_SELECT, 1);
    ok &= !ui.roll[ROLL_BPM].from[0];
    bad += check("#46 ANIM OFF: the card values and the header BPM snap (no rolling digits), the frame is static", ok);
    {   /* saved with the settings; settings from before it: ON */
        persist_t p;
        ui_prefs = PREF_ANIM_OFF;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && (ui_prefs & PREF_ANIM_OFF);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_prefs;
        bad += check("#46 ANIM saved with the settings; settings from before it: ON", ok);
    }
    ui_prefs = 0;
    return bad;
}

/* MENU > STYLE (ui_menu.c, gfx.c ST_*): FLAT by default; KNOB 1 right LINE, left FLAT (stops at the ends), OCT+
 * toggles; drawn so from the next frame (ux.style, the tokens: LINE has no SURF, a rule under the header); saved in a
 * spare byte of the settings and read back; settings from before it, and an unknown value, read as FLAT; 2, the
 * retired PIXEL (1.0.1), as LINE (again on a second import: idempotent) */
static int test_style(void)
{
    int bad = 0, ok;
    const uint32_t rule = 26u * 240u + 120u;           /* (the rule under the header: ui_draw.c draw_rules) */
    ui_power_on();
    frame();
    ok = ui_style == ST_FLAT && ux.style == ST_FLAT && ux.surf != ux.bg;
    ui.force = 1; frame();
    ok &= host_screen[rule] == swap16(T_BG);
    hold(B_HOME); ui.menu_sel = MI_STYLE;
    ok &= ui.menu == 1u && str_eq(MI_NAME[MI_STYLE], "STYLE");
    turn(EN_K1, 1); ok &= ui_style == ST_LINE && ux.style == ST_LINE && ux.surf == ux.bg;
    turn(EN_K1, 1); ok &= ui_style == ST_LINE;
    turn(EN_K1, -1); ok &= ui_style == ST_FLAT;
    turn(EN_K1, -1); ok &= ui_style == ST_FLAT;
    press(B_OCTUP); ok &= ui_style == ST_LINE && ui.menu == 1u;
    press(B_OCTUP); ok &= ui_style == ST_LINE;
    press(B_OCTDN); ok &= ui_style == ST_FLAT && ux.style == ST_FLAT && ux.surf != ux.bg && ui.menu == 1u;
    press(B_OCTUP); ok &= ui_style == ST_LINE;
    ok &= !(ui_prefs & 0xFFu) && settings_leds == LEDS_DIM;   /* (no other setting moved) */
    hold(B_HOME);
    ok &= !ui.menu && ux.style == ST_LINE && host_screen[rule] == swap16(T_RULE);
    bad += check("MENU > STYLE: FLAT by default, KNOB 1 / OCT-+ FLAT / LINE, LINE drawn (rules, no SURF)", ok);
    {
        persist_t p, q;
        settings_export(&p);
        ui_style = ST_FLAT; frame();
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_style == ST_LINE;
        frame();
        ok &= ux.style == ST_LINE;
        p.favorites.factory[15][29] = 7;                /* unknown: FLAT, and so again */
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_style == ST_FLAT && !p.favorites.factory[15][29];
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && ui_style == ST_FLAT && !memcmp(&p, &q, sizeof p);
        p.favorites.factory[15][29] = 2;                /* the retired PIXEL: LINE, and so again */
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_style == ST_LINE && p.favorites.factory[15][29] == ST_LINE;
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && ui_style == ST_LINE && !memcmp(&p, &q, sizeof p);
        frame();
        ok &= ux.style == ST_LINE;
        ui_style = 2; frame();                          /* (a live PIXEL byte, a restore: LINE) */
        ok &= ui_style == ST_LINE && ux.style == ST_LINE;
        ui_style = ST_LINE;
        settings_export(&p);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;                          /* settings from before STYLE (PER3) */
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && ui_style == ST_FLAT;
        frame();
        ok &= ux.style == ST_FLAT && ux.surf != ux.bg;
        bad += check("  STYLE saved with the settings and read back; older settings and unknown values: FLAT, PIXEL: LINE (idempotent)", ok);
    }
    ui_power_on(); frame();
    return bad;
}

/* the header (ui_draw.c draw_head, 1.2's three slots): its elements measured in the render (every pixel off BG; an
 * element is a run of inked columns, split at 2 empty columns and at the battery's cell), each one's ink centred on row
 * 12 (top + bottom + 1 within 1 px of H_HEAD); the track cushion's ink from x 4; the battery's in 214..236, nothing
 * between 208 and it; with the centre shown the BPM's ink within 2 px of x 120; a message / a label centred on x 120
 * (its ink between 40 and 205, within 3 px); GLO with the tempo turned: the label right of the BPM and its lock, no
 * battery. Every state of the header, FLAT and LINE, every palette */
#define HEAD_EL 32
static int32_t hel[HEAD_EL][4], nhel;                  /* x0 x1 (last inked column) top bot */
static int head_centres(const char *what, int print)
{
    uint16_t bg = swap16(T_BG);
    int32_t x = 0, ok = 1;
    nhel = 0;
    while (x < 240) {
        int32_t x0, x1, top = 99, bot = -1, gap = 0, y, ink;
        for (; x < 240; x++) {                        /* the next inked column */
            for (y = 0, ink = 0; y < H_HEAD; y++) ink |= host_screen[(Y_HEAD + y) * 240 + x] != bg;
            if (ink) break;
        }
        if (x >= 240) break;
        x0 = x1 = x;
        for (; x < 240 && gap < 2; x++) {
            if (x == HEAD_BAT_X && x != x0) break;     /* the battery's cell ends the element */
            for (y = 0, ink = 0; y < H_HEAD; y++)
                if (host_screen[(Y_HEAD + y) * 240 + x] != bg) {
                    ink = 1;
                    if (y < top) top = y;
                    if (y > bot) bot = y;
                }
            gap = ink ? 0 : gap + 1;
            if (ink) x1 = x;
        }
        x = x1 + 1;
        if (print) printf("    head %-10s x %3d..%3d ink rows %2d..%2d centre %4.1f (%+.1f)\n", what, x0, x1, top, bot,
                          (top + bot + 1) / 2.0, (top + bot + 1) / 2.0 - H_HEAD / 2);
        ok &= top + bot + 1 >= H_HEAD - 2 && top + bot + 1 <= H_HEAD + 2;
        if (nhel < HEAD_EL) { hel[nhel][0] = x0; hel[nhel][1] = x1; hel[nhel][2] = top; hel[nhel][3] = bot; nhel++; }
    }
    return ok;
}
/* the elements inside columns a .. b as one span (x0 .. x1): 2 x its centre, 0 none */
static int32_t head_span2(int32_t a, int32_t b, int32_t *x0, int32_t *x1)
{
    int32_t i;
    *x0 = 999; *x1 = -1;
    for (i = 0; i < nhel; i++)
        if (hel[i][0] >= a && hel[i][1] <= b) {
            if (hel[i][0] < *x0) *x0 = hel[i][0];
            if (hel[i][1] > *x1) *x1 = hel[i][1];
        }
    return *x1 < 0 ? 0 : *x0 + *x1 + 1;
}
enum { HS_STOPPED, HS_PLAY, HS_MSG, HS_OCT, HS_USB, HS_KEYCAP, HS_BPM_TURN, HS_BPM_LOCK,
       HS_NOFILE, HS_LAYER_LOCK, HS_GLO_TURN, HS_MSG2, HS_BEAT2, HS_BATT1, HS_BATT0, HS_BPM90,
       HS_N };
static const char *const HS_NAME[HS_N] = {"stopped", "play", "message", "OCT", "USB", "keycap",
    "BPM turn", "BPM LOCK", "NO FILE", "EDIT lock", "GLO turn", "msg2", "beat 2", "battery 1",
    "battery 0", "BPM 90"};
static void head_state(uint32_t s)
{
    song.playing = s == HS_PLAY || s == HS_BEAT2 || s == HS_OCT;

    clk_pos = 0; clk_step = s == HS_BEAT2 ? 4u : 8u;    /* (the 2nd beat: the metronome mirrored; else the 3rd) */
    if (s == HS_MSG) ui_message("SAVED 03 BASS");
    if (s == HS_MSG2) ui_say("LOADED ", "07 A VERY LONG PATTERN NAME");
    if (s == HS_OCT) song.octave = 2;
    usb.config = s == HS_USB; usb.suspended = 0;
    song.batt_raw = s == HS_BATT1 ? 540 : s == HS_BATT0 ? 500 : 600;
    if (s == HS_KEYCAP) ui_message("[OCT+] SAVE");
    if (s == HS_BPM_TURN || s == HS_GLO_TURN) ui.bpm_t = 40;
    if (s == HS_BPM_LOCK || s == HS_GLO_TURN) ui_prefs |= PREF_BPM_LOCK;
    if (s == HS_NOFILE) ui_message(MSG_NOFILE "NO FILE");
    if (s == HS_LAYER_LOCK) ui.layer = ui.lock = LAYER_EDIT;
    if (s == HS_GLO_TURN) ui.layer = LAYER_GLO;
    song.g[G_BPM] = s == HS_BPM90 ? 90 : 124;
    ui.roll_bpm = song.g[G_BPM];
}
static void head_clear(void)
{
    ui.msg_t = 0; song.octave = 0; usb.config = 0; song.playing = 0; ui.bpm_t = 0;
    ui_prefs &= ~PREF_BPM_LOCK; ui.layer = ui.lock = 0; song.batt_raw = 600;
}
static int test_head_centres(void)
{
    int bad = 0, ok = 1, place = 1;
    uint32_t st, p, s;
    for (st = ST_FLAT; st <= ST_LINE; st++)
        for (p = 0; p < NPALETTES; p++)
            for (s = 0; s < HS_N; s++) {
                int32_t a, b, c2, msg = s == HS_MSG || s == HS_KEYCAP || s == HS_NOFILE || s == HS_LAYER_LOCK ||
                                        s == HS_MSG2;
                int pr = !st && !p, okp;
                ui_power_on();
                ui_style = (uint8_t)st; style_apply();
                settings.palette = p; palette_set(p);
                head_state(s);
                memset(host_screen, 0, sizeof host_screen);
                ui.force = 1; draw_head(); ui.force = 0;
                ok &= head_centres(HS_NAME[s], pr);
                okp = nhel > 0 && hel[0][0] == 4;      /* the track cushion first, its ink from x 4 */
                if (s != HS_GLO_TURN) {                /* the battery last, in its cell; nothing between 208 and it */
                    okp &= head_span2(HEAD_BAT_X, 239, &a, &b) && a >= 214 && b <= 236;
                    okp &= !head_span2(HEAD_GRP_R, HEAD_BAT_X - 1, &a, &b);
                } else {                               /* GLO, the tempo turned: the BPM, its lock, the label after */
                    head_geo_t g;
                    head_geo(&g, "124");
                    okp &= head_span2(g.bx + g.b[2] + 20, 239, &a, &b) && a >= 142;
                }
                if (!msg) {                            /* the BPM's ink on x 120 (between the metronome and the lock) */
                    head_geo_t g;
                    head_geo(&g, s == HS_BPM90 ? "90" : "124");
                    c2 = head_span2(g.mx1, g.bx + g.b[2] + HEAD_GAP - 1, &a, &b);
                    okp &= c2 >= 236 && c2 <= 244;
                } else {                               /* the message centred on x 120 */
                    c2 = head_span2(40, 205, &a, &b);
                    okp &= c2 >= 234 && c2 <= 246 && a >= 40 && b <= 205;
                }
                if (pr && !okp) printf("    head %s: misplaced\n", HS_NAME[s]);
                place &= okp;
                head_clear();
            }
    ui_style = ST_FLAT; style_apply();
    bad += check("header: every element's ink centred on row 12 (+-1 px), every state, FLAT and LINE, every palette", ok);
    bad += check("header: three slots: track at 4, BPM / message on x 120, octave to 208, battery 214..236 (GLO with "
                 "the tempo turned: the label right of the BPM and its lock)", place);
    ui_power_on();
    return bad;
}

/* the header's metronome (ui_draw.c head_metro): it swings on the beats the click is on (seq.c click_beat_now): the
 * bar's 2nd and 4th beats mirrored, the 1st the accent's, the 3rd as stopped; a beat redraws its ink only (the rest
 * of the screen untouched, the header's signature kept) */
static int test_head_metronome(void)
{
    static uint16_t shot[4][H_HEAD * 240], still[H_HEAD * 240];
    head_geo_t g;
    int bad = 0, ok = 1, only = 1, b;
    int32_t x, y, w;
    ui_power_on();
    settings.palette = 0; palette_set(0);
    song.g[G_BPM] = 124; ui.roll_bpm = 124;
    head_geo(&g, "124");
    w = g.mx1 - g.mx0;
    ui.force = 1; draw_head(); ui.force = 0;           /* stopped */
    memcpy(still, host_screen, sizeof still);
    song.playing = 1; clk_pos = 0;
    for (b = 0; b < 4; b++) {
        uint32_t sig;
        clk_step = (uint32_t)b * 4u + (b & 1u) * 2u;   /* (inside the beat: its 1st or 3rd 1/16) */
        ui.force = 1; draw_head(); ui.force = 0;
        memcpy(shot[b], host_screen, sizeof shot[b]);
        sig = ui.head_sig;
        clk_step = (uint32_t)((b + 1) % 4) * 4u;       /* the next beat: only the metronome's ink changes */
        draw_head();
        only &= ui.head_sig == sig;
        for (y = 0; y < H_HEAD; y++)
            for (x = 0; x < 240; x++)
                if (host_screen[y * 240 + x] != shot[b][y * 240 + x]) only &= x >= g.mx0 && x < g.mx1;
    }
    for (b = 0; b < 4; b++)                            /* 1.1.5: no swing: every beat as stopped */
        for (y = 0; y < H_HEAD; y++)
            for (x = 0; x < w; x++)
                ok &= shot[b][y * 240 + g.mx0 + x] == still[y * 240 + g.mx0 + x];
    bad += check("header metronome: still on every beat, as stopped", ok);
    bad += check("header metronome: a beat changes nothing in the header", only);
    song.playing = 0;
    ui_power_on();
    return bad;
}



/* #181: a sound load (PRESETS) or another track (ALGORITHM) draws the page again (ui.force) with no blank frame on the
 * way: MIXER cleared its panel to the background before its four columns landed (HOME > TRACKS its rows), a flash on
 * every detent. Every pixel goes from what it showed straight to what it shows next (host_fl_*), on MIXER, HOME (the
 * scope), HOME > TRACKS and ENV, FLAT and LINE, MENU > LARGE off and on; a full redraw still covers the screen */
static int test_no_flash(void)
{
    static const char *const VIEW[4] = {"MIXER", "HOME", "HOME TRACKS", "ENV"};
    static const uint8_t KN[2] = {EN_PRESET, EN_ALGO};
    int bad = 0, ok = 1;
    uint32_t v, st, lg, k, n, miss;
    for (v = 0; v < 4u; v++)
        for (st = 0; st < 2u; st++)
            for (lg = 0; lg < 2u; lg++)
                for (k = 0; k < 2u; k++) {
                    ui_power_on();
                    ui_style = st ? ST_LINE : ST_FLAT;
                    ui_prefs = lg ? PREF_LARGE : 0u;
                    ui_home_view = v == 2u;
                    if (v == 1u || v == 2u) {
                        ui.home = 1;
                        ui.force = 1;
                    } else {
                        go_title(VIEW[v]);
                    }
                    frames(100);
                    miss = song.sel * 65536u + TSEL->eng_req * 256u + TSEL->preset;
                    host_fl_begin();
                    turn(KN[k], 1);                     /* (a sound loaded / the next track: the page drawn again) */
                    n = host_fl_end(0, 240);
                    ok &= miss != song.sel * 65536u + TSEL->eng_req * 256u + TSEL->preset;
                    if (n)
                        printf("    #181 %s %s%s, %s: %u pixels flashed\n", VIEW[v], st ? "LINE" : "FLAT",
                               lg ? " LARGE" : "", k ? "ALGORITHM" : "PRESETS", n);
                    ok &= n == 0u && ui.force == 0;
                    for (n = 0; n < 240u * 240u; n++)    /* and the redraw still covers the whole screen */
                        host_screen[n] = 0x5A5A;
                    ui.force = 1;
                    frame();
                    for (n = miss = 0; n < 240u * 240u; n++)
                        miss += host_screen[n] == 0x5A5A;
                    if (miss)
                        printf("    #181 %s %s%s: %u pixels not drawn\n", VIEW[v], st ? "LINE" : "FLAT", lg ? " LARGE" : "", miss);
                    ok &= miss == 0u;
                }
    ui_style = ST_FLAT;
    ui_prefs = 0;
    ui_home_view = 0;
    bad += check("#181 PRESETS / ALGORITHM on MIXER, HOME, HOME > TRACKS, ENV (FLAT, LINE, LARGE): redrawn, no blank frame", ok);
    return bad;
}

/* 1.4.1 ENGINE QUICK ACTIONS (ui_layer.c EQA): the EDIT layer's black keys are the selected track's engine's. Since the
 * kits (whose lanes' mutes they were) were archived, 2026-10-16, no engine has any: the keys are dark and do nothing */
static int test_edit_quick_actions(void)
{
    uint32_t e;
    int ok = 1;
    ui_power_on();
    for (e = 0; e < NENGINES; e++) {
        set_engine_of(TSEL, e);
        ok &= eqa_count(TSEL) == 0u && !eqa_of(TSEL, 0) && !eqa_put_back();
    }
    return check("EDIT layer quick actions: none on any engine (the kits archived), nothing to put back", ok);
}

#if NTRK == 8
/* (Blue Hare, TRACKS 8) the view: the 4-wide places show the selected track's four (1 .. 4 or 5 .. 8); GLO's knobs, SOLO
 * keys and cards follow it, its 8 black keys mute every track; FX's 8 black keys too; MIDI IN in blocks of 8; tracks 5 .. 8
 * at power-on */
static int test_eight_tracks(void)
{
    int bad = 0, ok;
    uint32_t k;
    int16_t l4;
    ui_power_on();
    for (k = 4, ok = 1; k < NTRK; k++)
        ok &= trk[k].eng_req == eng_for_track(&trk[k], UT_DEF[k][0]);
    bad += check("8 tracks: power-on, tracks 5 .. 8 their sounds", ok);
    ok = trk_view() == 0u;
    track_select(5);
    frame();
    ok &= song.sel == 5u && trk_view() == 4u;
    track_select(3);
    ok &= trk_view() == 0u;
    track_select(4);
    ok &= trk_view() == 4u;
    bad += check("  the view: track 6 selected shows 5 .. 8, track 4 1 .. 4, track 5 5 .. 8", ok);
    l4 = trk[4].p[P_LEVEL];
    btn_down(B_GLO); frame();
    turn(EN_K1, -10);
    ok = trk[4].p[P_LEVEL] == l4 - 10 && trk[0].p[P_LEVEL] != l4 - 10 && ui.layer == LAYER_GLO;
    key_down(black(5)); frame(); key_up(black(5)); frame();
    ok &= trk[5].p[P_MUTE] == 1 && !trk[1].p[P_MUTE];
    key_down(white(1)); frame();
    ok &= song.sel == 5u;                               /* G3: the view's second, layer 6 */
    key_up(white(1)); frame();
    key_down(white(0)); frame();
    ok &= song.sel == 4u;                               /* F3: the view's first, layer 5 */
    key_up(white(0)); frame();
    oct_back();
    ok &= trk[4].p[P_LEVEL] == l4 && !trk[5].p[P_MUTE];   /* OCT-: all 8 tracks' levels and mutes as it opened */
    btn_up(B_GLO); frame();
    bad += check("  GLO with track 5 selected: KNOB 1 T5 LEVEL, black key 6 T6 MUTE, G3 / F3 layer 6 / 5; OCT- puts the 8 back", ok);
    btn_down(B_FX); frame();                            /* (the layer open, then the key) */
    key_down(black(4)); frame();
    ok = (perf_held >> (PF_M1 + 4u)) & 1u;
    key_up(black(4)); btn_up(B_FX); frame();
    bad += check("  FX + black key 5 (D#4): T5 muted while held", ok && !((perf_held >> (PF_M1 + 4u)) & 1u));
    song.g[G_ROUTE] = 0;
    ok = midi_track(5) == &trk[5] && midi_track(7) == &trk[7] && midi_track(8) == TSEL;
    song.g[G_ROUTE] = 2;
    ok &= midi_track(13) == &trk[5] && midi_track(5) == TSEL && trk_midi_ch(5) == 13u;
    song.g[G_ROUTE] = 0;
    bad += check("  MIDI IN CH1-8: channel 6 plays track 6, 9 the selected; CH9-16: channel 14 track 6", ok &&
                 str_eq(N_ROUTE[0], "CH1-8") && str_eq(N_ROUTE[2], "CH9-16") && GP[G_ROUTE].max == 2);
    return bad;
}
#endif

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int bad = 0;
    bad += test_large_face();
    bad += test_sound_loads();
    bad += test_presets_knob();
    bad += test_midi();
    bad += test_save();
    bad += test_actions();
    bad += test_tracks();
    bad += test_screen();
    bad += test_favorites();
    bad += test_categories();
    bad += test_display_preferences();
    bad += test_menu_tabs();
    bad += test_menu_project_rows();
    bad += test_information();
    bad += test_product_ux();
    bad += test_page_cycles();
    bad += test_mono_screens();
    bad += test_roll();
    bad += test_soak_cards();
    bad += test_panel();
    bad += test_layer();
    bad += test_layer_knob_leak();
    bad += test_layer_knob_race();
    bad += test_fx_latch();
    bad += test_fx_map();
    bad += test_layer_lock();
    bad += test_edit_quick_actions();
    bad += test_menu_prefs();
    bad += test_style();
    bad += test_head_centres();
    bad += test_head_metronome();
    bad += test_div_order();
    bad += test_knob_accel();
    bad += test_usb_level();
    bad += test_click_menu();
    bad += test_usb_serial();
    bad += test_bpm_lock();
    bad += test_large();
    bad += test_browse_no_pattern();
    bad += test_name();
    bad += test_edit_cycle();
    bad += test_quick_layers();
    bad += test_chord_page();
    bad += test_bughunt_ui();
    bad += test_bughunt_ui2();
    bad += test_midi_leds();
    bad += test_idle_glow();
    bad += test_breath();
    bad += test_scale_leds();
    bad += test_screen_off();
    bad += test_idle_leds();
    bad += test_momentary();
    bad += test_no_flash();
    bad += test_retired_engines();
#if NTRK == 8
    bad += test_eight_tracks();
#endif

    printf(bad ? "ui: %d FAILED\n" : "ui: all passed\n", bad);
    return bad ? 1 : 0;
}
#endif
