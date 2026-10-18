/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* Blue Hare user interface.
 * Flat: SURF cards and panels on the palette's background, no rules, one type family (Inter Tight, three sizes), tracks
 * named by their numbers 1..4 on a cushion (icons.c trk_icon). Four columns <-> KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
static int project_save(uint32_t slot);
static void panel_setup(void);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
static int up_used(uint32_t k);              /* user presets: upreset.c */
static int up_load(uint32_t k);
static uint32_t up_count(void);
static uint32_t up_nth(uint32_t n);
static uint32_t up_rank(uint32_t slot);
static uint32_t up_engine(uint32_t k);
static void up_name(uint32_t k, char *b);
static void up_slot_label(char *b, uint32_t k);
static void up_ui(uint32_t op, uint32_t k);
static void up_auto_name(char *b, uint32_t e, uint32_t k);   /* naming (ui_name.c) */
static void up_ui_named(uint32_t op, uint32_t k, const char *name, uint32_t cat);
static uint32_t up_cat(uint32_t k);
static int project_save_as(uint32_t slot, const char *name);
static int project_name(uint32_t slot, char *b);
static int project_rename(uint32_t slot, const char *name);
static void project_cur_name(char *b);
static void project_new(void);
static uint32_t user_of(const track_t *t)    /* user preset slot its sound came from, UP_SLOTS = none */
{
    return t->user && up_used(t->user - 1u) ? t->user - 1u : UP_SLOTS;
}
static uint32_t up_gen;                      /* bumped on every user bank change (redraws) */
#include "favorites.c"
#include "category.c"                   /* sound categories (Discussion #90): PRESETS > LIST */
/* MENU's two-valued settings (ui_menu.c MENU_FLAGS), a bit each in a byte no engine uses (as ui_layer.c layer_seen):
 * saved with the settings; 0 in older ones = every setting's default (append-only: a new setting takes a new bit,
 * its default is 0) */
#define ui_prefs (favorites.factory[15][30])
#define PREF_LATCH 1u                          /* MENU > FX LATCH ON (#40) */
#define PREF_ANIM_OFF 2u                       /* MENU > ANIM OFF (#46): values snap (no rolling digits, no glide) */
#define PREF_ACCEL 4u                          /* MENU > KNOB ACCEL ON (#52): fast turns of wide values x2..x8 */
#define PREF_USB_FIXED 8u                      /* MENU > USB LEVEL FIXED: USB audio at the full level, MASTER after */
#define PREF_BPM_LOCK 16u                      /* MENU > BPM LOCK ON (#58): SELECT sets the tempo only with GLO held */
#define PREF_LARGE 32u                         /* MENU > LARGE ON (#15, Discussion #80): big knob labels and values
                                                * (ui_draw.c large_kind); clear in every older setting = OFF */
#define PREF_SERIAL_OFF 64u                    /* MENU > USB SERIAL OFF (#67): no serial console, the device enumerates as
                                                * audio + MIDI only (usb.c usb_cdc_switch); clear in every older setting = ON */
#define PREF_RESTORE_OFF 128u                  /* MENU > RESTORE LAST OFF (1.2, Discussion #130): no autosave written, none
                                                * restored at power-on (project.c); clear in every older setting = ON */
#define fx_latch ui_prefs
/* MENU > AUDIO CLICK, CLICK LEVEL (1.1, Discussion #131; click.c, seq.c): two bits each in another byte no engine
 * uses, saved with the settings: bits 0-1 CLICK (OFF ON; 2, the retired REC, reads as ON), 2-3 CLICK LEVEL (stored 0
 * MID, 1 LOW, 2 HIGH); 0 in older settings = OFF / MID, so nothing clicks that did not; 3 (unknown) reads as 0
 * (settings_persist.c puts it back). Bits
 * 4-5 were COUNT-IN (1.1 .. 1.4.1; 1.5 has none): ignored, kept as saved. Bit 6 (1.2): MENU > SCALE LEDS ON (Discussion #127,
 * ui_input.c scale_leds; 0 in older settings = OFF). Bit 7 free (append-only) */
#define ui_rec_prefs (favorites.factory[15][28])
#define PREF_REC 0xFF00u                       /* (menu_items.c MENU_FLAGS: a bit of ui_rec_prefs, << 8) */
#define PREF_SCALE_LEDS (0x40u << 8)
/* 1.2: three more two-valued MENU settings, bits of another byte no engine uses, saved with the settings; 0 in older
 * settings = today's behaviour (append-only, as ui_prefs): bit 0 DISPLAY > SCOPE MIX (Discussion #165, fx.c scope_mix),
 * bit 1 was CONTROL > STEP PREVIEW (Discussion #169, the tracker's, gone with it); bit 2 was CONTROL > CHORD ENTRY ADD
 * (#155, the step sequencer's, 1.2 .. 1.4.1): both ignored, kept as saved. Bits 3-7 free */
#define ui_prefs2 (favorites.factory[15][24])
#define PREF_X 0xFF0000u                       /* (menu_items.c MENU_FLAGS: a bit of ui_prefs2, << 16) */
#define PREF_SCOPE_MIX (0x01u << 16)
#define PREF_EXPERT (0x08u << 16)                /* (2026-10-17) MENU > CONTROL > EXPERT ON: EDIT shows the engine's
                                                * own pages (EDIT 1, EDIT 2), else its plain one (SOUND) */
static int expert_on(void) { return (ui_prefs2 & (PREF_EXPERT >> 16)) != 0u; }
/* 1.3 (Discussions #112, #134): MENU > DISPLAY > HOME, what the HOME page shows under its four cards: 0 SCOPE (the
 * oscilloscope, as before), 1 TRACKS (the four tracks as rows: ui_graph.c draw_home_tracks). A byte of its own no engine
 * uses, saved with the settings; 0 in older settings = SCOPE; an unknown value (a later firmware's view) reads as SCOPE
 * and is kept as saved */
#define ui_home_view (favorites.factory[15][26])
enum { HV_SCOPE, HV_TRACKS, HV_COUNT };
static int home_tracks(void) { return ui_home_view == HV_TRACKS; }
enum { RP_CLICK, RP_LEVEL };
static uint32_t rp_get(uint32_t f)                     /* a field as the MENU steps it: CLICK 0..1, LEVEL 0..2 */
{
    uint32_t v = (ui_rec_prefs >> (2u * f)) & 3u;
    v = v == 3u ? 0u : v;
    return f == RP_LEVEL ? (v == 0u ? 1u : v == 1u ? 0u : 2u) : v != 0u;   /* (LEVEL: LOW MID HIGH, MID stored as 0) */
}
static void rp_apply(void)                             /* the settings into click.c / seq.c (every frame: ui_input) */
{
    click_mode = (uint8_t)rp_get(RP_CLICK);
    click_lvl = (uint8_t)rp_get(RP_LEVEL);
}
static void rp_put(uint32_t f, uint32_t v)
{
    uint32_t st = f == RP_LEVEL ? (v == 1u ? 0u : v == 0u ? 1u : 2u) : v != 0u;
    ui_rec_prefs = (uint8_t)((ui_rec_prefs & ~(3u << (2u * f))) | st << (2u * f));
    rp_apply();
}
/* MENU > STYLE (ui_menu.c): ST_FLAT ST_LINE (gfx.c) in another byte no engine uses, saved with the settings;
 * 0 in older ones = FLAT; 2, the retired PIXEL (1.0.1), reads as LINE; anything else unknown as FLAT (settings_persist.c). gfx.c draws from its copy, ux.style
 * (ui_draw.c style_apply) */
#define ui_style (favorites.factory[15][29])
static void draw_gaps(uint32_t y, uint32_t h);        /* (ui_draw.c) */

static uint8_t sync_reload;                  /* engine / preset / project / user preset loaded: editor RELOAD push */

#define ACC T_THEME                /* values, curves */
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? T_ACCENT : T_THEME)   /* the knob just turned: accent */
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* layout: header 0..24, four cards 28..72 (57 px at x 3 + 59 c),
 * the panel 76..198 (a SURF area; the graphs live in it), footer 202..240; BG between them */
#define Y_HEAD 0
#define H_HEAD 24
#define Y_LABEL 28                    /* the cards */
#define Y_SEP_END 72
#define CARD_W 57
#define CARD_H 44
#define CARD_X(c) (3 + 59 * (int32_t)(c))
#define Y_GRAPH 76                    /* the panel */
#define H_GRAPH 122
#define Y_FOOT 202
#define H_FOOT 38
/* MENU > LARGE (large_kind below): on the value pages the cards are tall (28..132) and the panel a strip
 * (136..198); elsewhere (lists, rolls, layers) the layout above, its card labels in M */
#define LG_CARD_H 104
#define LG_Y_GRAPH 136
#define LG_H_GRAPH 62

static struct {
    uint8_t home;
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about + credits, 3 info (1.2) (HOME held) */
    uint8_t menu_sel;            /* MENU: the row (menu_items.c MI_*; its tab MI_TAB), kept while the device runs */
    uint8_t menu_row[5];         /* MENU: the row last picked in each tab, from its first (ALGORITHM comes back to it) */
    uint16_t menu_scroll;        /* continuous ABOUT + CREDITS position, pixels */
    uint32_t menu_sig, home_t0;  /* HOME press time (btn_hold) */
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t bpm_t;               /* frames the BPM stays highlighted after a SELECT turn */
    uint8_t act;                 /* action pages: the column whose action OCT+ does, + 1; 0 = none (act_col) */
    uint32_t save_t0;           /* SAVE press time (btn_hold: held = UNDO) */
    uint8_t confirm;             /* the OCT- / OCT+ dialog: CF_*, 0 = none */
    uint8_t confirm_trk;         /* the track it clears, the slot it overwrites */
    uint8_t uslot;               /* SAVE > USER: the selected user preset slot */
    uint8_t uboot;               /* main.c: seconds left before UPDATE MODE (OCT- + OCT+ held), 0 = none */
    uint32_t ly_t0;              /* the layer button's press time | 1, LY_* bits (ui_layer.c layer_gesture) */
    uint8_t ly;                  /* the layer whose button is down (LAYER_*), 0 = none */
    uint8_t layer;               /* the layer whose map is shown (LAYER_*), 0 = none */
    uint8_t lock;                /* #83: the layer locked open by a double tap (LAYER_*), 0 = none (ui_layer.c) */
    uint16_t pg_down;            /* page buttons down (panel ids) that act when let go */
    uint32_t layer_sig;          /* drawn-state cache of the map */
    char msg[24];
    char msg2[24];               /* a second message, shown when the first is over */
    uint32_t enc_t[NE];
    /* drawn-state cache */
    char col[4][32];
    uint32_t graph_sig, head_sig, foot_sig, frame;
    /* rolling digits (ui_draw.c roll_*): the four card values and the header BPM */
    struct {
        char from[7];            /* the value rolling out; "" = idle */
        uint8_t t0;              /* (ui.frame) of the value's last change */
        int8_t dir;              /* +1: it went up (the old digit leaves upward), -1: down */
        uint8_t sig;             /* a card: what its value is of (label, unit, track, engine, palette) */
    } roll[5];
    int16_t roll_bpm;            /* the BPM last drawn */
} ui;

enum { CF_NONE, CF_OVR_PROJ, CF_OVR_USER, CF_INIT_SOUND, CF_ERASE_USER };   /* ui.confirm: SAVE over a used slot;
                                   * TOOLS' INIT; USER ERASE */

/* MENU > DISPLAY > SCREEN OFF (1.1.5): after a while with no input on the panel the screen goes dark (the backlight
 * stays on: lcd.c lcd_power)
 * (a picture held for hours can stay faintly on the panel); the sound, the sequencer, MIDI and USB go on, the LEDs
 * stay as they are. NEVER (the default since 1.1.5.1), 5, 15, 30, 60 minutes, kept in a byte no engine uses, saved
 * with the settings (SCR_CODE). 1.1.5 stored value ^ 3, 0 (its default and every older setting) being 30 MIN; 1.1.5.1
 * reads that 0 as NEVER (the new default: the panel went dark for good on 1.1.5, see lcd.c lcd_power) and keeps the
 * other codes, 30 MIN now 4. Unknown codes read as NEVER.
 * Input is the panel's only: a button or key held or pressed, a knob turned (not MIDI, the editor or MASTER, an
 * analogue pot). The press, key or turn that wakes it is swallowed: no note (seq.c kb_asleep), no button, no
 * value, until everything is let go and the knobs rest for SCR_EAT_MS. A dialog, the count-in and the UPDATE MODE
 * countdown keep it on (and wake it); the crash screen, UPDATE and UBOOT wake it at once (lcd_wake_now).
 * Off: lcd_power(0) (DISPOFF, SLPIN) and nothing is drawn. Waking, a frame at a time (never a wait in
 * the loop): 120 ms after the SLPIN, SLPOUT (lcd_power(1)); 120 ms after that one frame redraws everything
 * (ui.force), then DISPON (lcd_power(2)) */
#define ui_scr (favorites.factory[15][27])
/* MENU > ANIM IDLE (1.2, Discussion #135; menu_items.c, ui_input.c idle_leds): bit 0, the LEDs' idle animation, in a
 * byte no engine uses, kept with the settings; 0 in every older setting = off. The other bits: 0 (room for a delay) */
#define ui_idle (favorites.factory[15][10])
#define SCR_DEF 0u                                     /* NEVER (1.1.5.1; 1.1.5: 30 MIN) */
static const uint8_t SCR_CODE[5] = {0, 2, 1, 4, 7};    /* the stored byte of NEVER 5 15 30 60 MIN (1.1.5: 3 2 1 0 7) */
static const uint8_t SCR_DEC[8] = {0, 2, 1, 0, 3, 0, 0, 4};   /* .. and back (3: 1.1.5's NEVER; 5, 6 unknown) */
#define SCR_EAT_MS 300u
static const uint8_t SCR_MIN[5] = {0, 5, 15, 30, 60};  /* NEVER, minutes */
enum { SCR_ON, SCR_OFF, SCR_WAKE, SCR_SLPOUT, SCR_SHOW };
static struct {
    uint8_t st;                  /* SCR_*: SCR_WAKE asked, SCR_SLPOUT sent, SCR_SHOW the frame that redraws */
    uint8_t eat;                 /* the wake gesture: panel input swallowed (ui_input) */
    uint32_t idle;               /* fm1_ms of the last panel input (or blocker) */
    uint32_t t;                  /* fm1_ms of the last panel command (SLPIN, SLPOUT) */
    uint32_t eat_t;              /* fm1_ms of the last swallowed knob detent */
} scrn;
static uint32_t scr_get(void)                          /* the MENU's value: 0 NEVER .. 4 60 MIN */
{
    return ui_scr < NELEM(SCR_DEC) ? SCR_DEC[ui_scr] : SCR_DEF;
}
static void scr_put(uint32_t v)                        /* (the time counts from the change: the editor's too) */
{
    ui_scr = SCR_CODE[v < NELEM(SCR_MIN) ? v : SCR_DEF];
    scrn.idle = fm1_ms;
}
static void scr_wake(void)
{
    if (scrn.st == SCR_OFF)
        scrn.st = SCR_WAKE;
    scrn.eat = 1;
}
static void scr_wake_now(void)                         /* main.c: the crash screen, UPDATE, UBOOT */
{
    if (scrn.st != SCR_ON)
        lcd_wake_now();
    scrn.st = SCR_ON;
    scrn.idle = fm1_ms;
}
/* ui_input, every pass: 1 = the pass is swallowed (the screen dark or waking, or the wake gesture going on) */
static int scr_input(uint32_t pressed, uint32_t notes)
{
    uint32_t held = fm1_in.buttons | fm1_in.notes, k;
    int32_t moved = 0;
    if (pressed | notes | held | panel_moved)
        scrn.idle = fm1_ms;
    panel_moved = 0;
    if (scrn.st == SCR_ON && !scrn.eat)
        return 0;
    for (k = 0; k < NE; k++)                           /* (every knob's turns go) */
        moved |= panel_enc(k);
    panel_moved = 0;
    if (moved)
        scrn.eat_t = scrn.idle = fm1_ms;
    if (pressed | notes | held | (uint32_t)moved)
        scr_wake();
    if (scrn.st != SCR_ON || held || (int32_t)(fm1_ms - scrn.eat_t) < (int32_t)SCR_EAT_MS)
        return 1;
    scrn.eat = 0;                                       /* all let go, the knobs at rest: input again */
    kb_asleep = 0;
    return 0;
}
/* ui_draw, every frame: 1 = draw it */
static int scr_frame(void)
{
    uint32_t lim = SCR_MIN[scr_get()] * 60000u;
    if (ui.confirm || ui.uboot) {                      /* (never dark under these) */
        scrn.idle = fm1_ms;
        if (scrn.st == SCR_OFF)
            scr_wake();
    }
    switch (scrn.st) {
    case SCR_ON:
        if (!lim || (int32_t)(fm1_ms - scrn.idle) < (int32_t)lim)   /* (signed: robust to a wrap) */
            return 1;
        lcd_power(0);
        scrn.st = SCR_OFF;
        scrn.t = fm1_ms;
        scrn.eat = 1;
        kb_asleep = 1;
        break;
    case SCR_WAKE:
        if (fm1_ms - scrn.t >= 120u) {
            lcd_power(1);
            scrn.st = SCR_SLPOUT;
            scrn.t = fm1_ms;
        }
        break;
    case SCR_SLPOUT:
        if (fm1_ms - scrn.t >= 120u) {
            scrn.st = SCR_SHOW;
            ui.force = 1;
            return 1;
        }
        break;
    case SCR_SHOW:
        return 1;
    }
    ui.msg_t = 0;                                      /* (dark: what would have shown is over; a clean page back) */
    ui.msg2[0] = 0;
    ui.hot_t = ui.bpm_t = 0;
    return 0;
}
static void scr_shown(void)                            /* after the frame: the redrawn page on */
{
    if (scrn.st != SCR_SHOW)
        return;
    lcd_power(2);
    scrn.st = SCR_ON;
    scrn.idle = fm1_ms;
}

static const page_t *page_over;   /* a quick layer's own four knobs (ui_layer.c), while it edits or draws them */
static const page_t *cur_page(void) { return page_over ? page_over : &PAGES[ui.page]; }

/* MENU > LARGE (#15, Discussion #80: what KNOB 1..4 do, in bigger type). Per page type:
 *   LK_TALL  HOME and the value pages (EDIT ENV LFO MOD FX SLICER SCL CHORD ARP VOICE, MIXER, CLOCK,
 *            TOOLS): tall cards (a K1..K4 keycap, the icon, the label in M, the value in L,
 *            in M or S when L is too wide or lacks a glyph, the unit under it, the gauge), the panel a strip: HOME's
 *            scope, ENV's ADSR, LFO's wave, MIXER's four tracks (name, state, meter) small; the
 *            page's title and number in L on the others (their charts cannot be read that small);
 *   LK_LABEL the list and graph pages (PRESETS, USER, PROJECT, SEQ's placeholder, SLICES),
 *            HOME with MENU > HOME TRACKS (1.3) and the quick layers' maps: the layout as it
 *            is, the card labels in M;
 *   LK_OFF   LARGE off; the menu (its own LARGE: ui_menu.c), the dialogs and NAME keep their own layout in every case. */
enum { LK_OFF, LK_LABEL, LK_TALL };
static uint32_t large_kind(void)
{
    uint32_t g;
    if (!(ui_prefs & PREF_LARGE))
        return LK_OFF;
    if (ui.layer)
        return LK_LABEL;
    if (ui.home)                                        /* (HOME > TRACKS: its rows need the panel, the cards as on */
        return home_tracks() ? LK_LABEL : LK_TALL;      /* a list page; the rows' names in M: draw_home_tracks) */
    g = cur_page()->graph;
    return g == GR_BROWSE || g == GR_SLOTS || g == GR_USER || g == GR_TODO ? LK_LABEL : LK_TALL;
}
/* the geometry of the page shown: the cards' height, the panel's top and height */
static uint32_t card_h(void) { return large_kind() == LK_TALL ? LG_CARD_H : CARD_H; }
static uint32_t graph_y(void) { return large_kind() == LK_TALL ? LG_Y_GRAPH : Y_GRAPH; }
static uint32_t graph_h(void) { return large_kind() == LK_TALL ? LG_H_GRAPH : H_GRAPH; }
static int large_face_has(const char *s)                /* every glyph of s in the sparse L face */
{
    for (; *s; s++)
        if (glyph_at(&AF_L, fold(&AF_L, (uint8_t)*s)) < 0)
            return 0;
    return 1;
}
static int32_t ink_w(const aafont_t *f, const char *s)
{
    int32_t b[4];
    text_ink(f, s, b);
    return b[2] > b[0] ? b[2] - b[0] : 0;
}

/* the quick layers (ui_layer.c): a button held, the keys and KNOB 1..4 are its shortcuts, its map over the page */
enum { LAYER_NONE, LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT, LAYER_N };
static void draw_layer(void);
static const char *layer_head(void);
static int layer_locked(void);
static uint32_t layer_leds(uint32_t *br);
static uint32_t layer_btn(void);

/* a page shown (2026-10-17: EDIT's engine pages by MENU > EXPERT: OFF the plain SOUND, ON EDIT 1 and 2; 2026-10-18 FX:
 * OFF the REVERB and DELAY pages only) */
static int page_visible(uint32_t i)
{
    if (PAGES[i].fam == FAM_EDIT && PAGES[i].scope == SC_ENGINE)
        return (PAGES[i].id[0] >= PX_PLAIN && PAGES[i].id[0] != 0xFFu) != expert_on();
    if (PAGES[i].fam == FAM_FX)                      /* (FX: REVERB DELAY CHORUS EQ; EXPERT adds the sends, CHORUS EDIT,
                                                    * EQ BANDS) */
        return (PAGES[i].scope == SC_GLOBAL && (PAGES[i].id[0] == G_RTYPE || PAGES[i].id[0] == G_EQPRE)) ||
               (PAGES[i].scope == SC_TRACK && (PAGES[i].id[0] == P_DLY || PAGES[i].id[1] == PX_GLOBAL + G_CPRE)) ||
               expert_on();
    return 1;
}

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam && page_visible(i))
            return i;
    return 0;
}

/* transient message in the top bar, right of the transport and the BPM: a + b */
static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
    ui.msg2[0] = 0;
}

static void ui_message(const char *s) { ui_say(s, ""); }
/* a message led by an icon: its first byte (ui_draw.c draw_head draws the icon, then the words) */
#define MSG_NOFILE "\x01"                      /* the no-file icon (ICON_X_NOFILE) */

/* Main loop only, with interrupts enabled. PLAY may be consumed between reads;
 * take one coherent snapshot before a flash operation or song preparation. */
static int transport_busy(void)
{
    int busy;
    fm1_irq_off();
    busy = song.playing || transport_req == 1u;
    fm1_irq_on();
    return busy;
}

static void page_entered(void)
{
    const page_t *pg = cur_page();
    ui.hot_t = 0;                                /* clear the previous page's emphasis */
    ui.act = pg->graph == GR_USER ? 4u : 0u;     /* the save screen is ready for OCT+ */
    ui.force = 1;
}

/* a page button tapped (1.2, Discussion #153; SEQ and GLO the sequencer's placeholder, SAVE USER -> PROJECT ->
 * TOOLS -> PRESETS; HOME: home_tap). From another
 * family the family's page shown last (SAVE: USER); one that is not the family's any more (a page that moved, as GLOBAL
 * and SYSTEM in 1.2), or not shown for this track, gives the family's first */
static void open_family(uint32_t fam)
{
    if (!ui.home && cur_page()->fam == fam) {          /* same button again: next page */
        uint32_t n, i = ui.page;
        for (n = 0; n < NPAGES; n++) {
            i = (i + 1u) % NPAGES;
            if (PAGES[i].fam == fam && page_visible(i)) break;
        }
        ui.page = (uint8_t)i;
    } else if (fam == FAM_SEQ || fam == FAM_GLO) {       /* SEQ; GLO (no page of its own; it opened the song): the */
        ui.page = (uint8_t)page_first(FAM_SEQ);          /* sequencer's placeholder */
    } else if (fam == FAM_SAVE) {
        uint32_t i;
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].graph == GR_USER) break;
        ui.page = (uint8_t)i;                         /* SAVE enters the sound save screen directly */
    } else {
        uint32_t i = ui.fam_last[fam];
        ui.page = (uint8_t)(i && i < NPAGES && PAGES[i].fam == fam && page_visible(i) ? i : page_first(fam));
    }
    ui.fam_last[fam] = ui.page;
    ui.home = 0;
    page_entered();
}

static void go_home(void)
{
    ui.home = 1;
    ui.hot_t = 0;
    ui.force = 1;
}

/* HOME tapped (1.2): HOME -> MIXER (up to 1.1.5 GLO's first page) -> CLOCK (up to 1.1.5 GLO > GLOBAL) -> HOME; from any
 * other page HOME. HOME's pages are FAM_TRK's, in PAGES order, always from the first (none remembered). Held: the menu
 * (ui_input) */
static void home_tap(void)
{
    uint32_t i = ui.page + 1u;
    if (ui.home) {
        ui.fam_last[FAM_TRK] = 0;                     /* (open_family: the family's first, the MIXER) */
        open_family(FAM_TRK);
    } else if (cur_page()->fam == FAM_TRK && i < NPAGES && PAGES[i].fam == FAM_TRK) {
        open_family(FAM_TRK);                         /* (on HOME's page again: the next one) */
    } else {
        go_home();
    }
}

/* ------------------------------------------------------- track setup --- */
/* One-step UNDO of a sound load (a factory or user preset, an engine jump, TOOLS INIT, the editor's PRESET / G_ENGSEL
 * / UP_LOAD; FM6's operator bytes edited by hand: edit_undo_take). Each first copies the track's sound as it was;
 * SAVE held 0.7 s swaps it back (held again: the loads again). One copy for all tracks: the last load wins. Loads in a
 * row on one track with nothing changed in between (the PRESETS knob through the list, an editor audition) keep the
 * copy from before the first, so the undo goes back past the whole browse. Not snapshotted: power-on, projects. The
 * tracker's pattern edits have their own undo (ui_tracker.c) */
static struct {
    uint8_t trk;                 /* track + 1, 0 = nothing to undo */
    uint8_t keep;                /* the track is as the last load left it (undo.after): a next load keeps the copy */
    uint8_t eng, preset, user;
    int16_t p[P_COUNT];
    uint32_t after;              /* track_sig right after the last load */
    uint32_t t_ms;               /* time of the last load (the editor's SETs after it belong to it) */
} undo;
static uint8_t undo_depth;       /* loads nest (an engine jump loads its first preset): the outer one counts;
                                  * bluehare_init / project_load raise it to take no copy at all */

static uint32_t fnv(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t track_sig(const track_t *t)      /* the sound */
{
    uint8_t id[3] = {t->eng_req, t->preset, t->user};
    return fnv(fnv(2166136261u, t->p, sizeof t->p), id, 3);
}

/* MOMENTARY (ui_input.c mom_turn: LFO held, the knobs' changes go back when it is let go): the values turned, where
 * they are (a track's p[], song.g[]) and what they were. In the main loop's pool (no .bss) */
#define MOM_N 8u
static struct {
    uint8_t n;                                    /* values held, 0 = none */
    int16_t *vp[MOM_N];
    int16_t v[MOM_N];
} mom __attribute__((section(".pool")));
static void mom_restore(void)                     /* LFO let go: every value back */
{
    while (mom.n) {
        mom.n--;
        *mom.vp[mom.n] = mom.v[mom.n];
    }
}
static void mom_back(const track_t *t)            /* a sound load into t (0: a project): its values back first */
{
    uint32_t i, k = 0;
    for (i = 0; i < mom.n; i++)
        if (!t || (mom.vp[i] >= t->p && mom.vp[i] < t->p + P_COUNT) || (mom.vp[i] >= song.g && mom.vp[i] < song.g + G_COUNT)) {
            *mom.vp[i] = mom.v[i];                /* (a global: back too, before the load's undo copy) */
        } else {
            mom.vp[k] = mom.vp[i];
            mom.v[k++] = mom.v[i];
        }
    mom.n = (uint8_t)k;
}
static void undo_copy(track_t *t)                 /* the track's sound as it is now */
{
    uint32_t i = trk_index(t);
    undo.trk = (uint8_t)(i + 1u);
    undo.eng = t->eng_req;
    undo.preset = t->preset;
    undo.user = t->user;
    memcpy(undo.p, t->p, sizeof undo.p);
}
static void load_begin(track_t *t)
{
    uint32_t i = trk_index(t);
    mom_back(t);                                  /* (MOMENTARY: the sound as it was, then the load) */
    if (undo_depth++)
        return;
    if (undo.keep && undo.trk == i + 1u && track_sig(t) == undo.after)
        return;                                   /* browsing on: the copy from before the first load stays */
    undo_copy(t);
}

static void load_end(track_t *t)
{
    if (--undo_depth)
        return;
    undo.after = track_sig(t);
    undo.keep = 1;
    undo.t_ms = fm1_ms;
}

/* the editor's SET right after a load on the selected track (an audition: G_ENGSEL, then the patch's
 * values): part of that load, the copy from before it stays */
static void load_extend(track_t *t)
{
    if (undo.keep && undo.trk == trk_index(t) + 1u && fm1_ms - undo.t_ms < 1500u) {
        undo.after = track_sig(t);
        undo.t_ms = fm1_ms;
    }
}

/* a sound edit by hand on track t is about to happen (FM6's operator bytes, ui_fm6op.c): the undo copy of the track as
 * it is. A knob turned on (the same tag, not 0: the same knob of the same gesture), on the same track, nothing else
 * changed and less than 1.5 s apart, shares the copy from before its first detent. edit_undo_done after each */
static uint32_t undo_tag;
static void edit_undo_take(track_t *t, uint32_t tag)
{
    if (tag && tag == undo_tag && undo.keep && undo.trk == trk_index(t) + 1u && track_sig(t) == undo.after &&
        fm1_ms - undo.t_ms < 1500u)
        return;
    undo_tag = tag;
    undo_copy(t);
    undo.keep = 0;
}
static void edit_undo_done(track_t *t)
{
    undo.after = track_sig(t);
    undo.keep = 1;
    undo.t_ms = fm1_ms;
}

static int param_kept(uint32_t i);

/* SAVE held: the sound the loads changed and the copy change places, so held again = the loads again. The engine,
 * preset and its parameters (not param_kept); the mix (LEVEL PAN MUTE) stays: no load changes it */
static void undo_swap(void)
{
    track_t *t;
    uint32_t i, tr;
    char b[4] = {'T', 0, 0, 0};
    if (!undo.trk) {
        ui_message("NOTHING TO UNDO");
        return;
    }
    t = &trk[(undo.trk - 1u) % NTRK];
    tr = trk_index(t);
    fm1_irq_off();                                /* the audio ISR must not see half a sound */
    {
        uint8_t e = t->eng_req, pr = t->preset, u = t->user;
        panic_req |= (uint8_t)(1u << tr);
        t->eng_req = undo.eng;
        t->preset = undo.preset;
        t->user = undo.user;
        undo.eng = e;
        undo.preset = pr;
        undo.user = u;
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i)) {
                int16_t x = t->p[i];
                t->p[i] = undo.p[i];
                undo.p[i] = x;
            }
    }
    fm1_irq_on();
    undo.keep = 0;                                /* the next load copies the track as it is now */
    if (t == TSEL)
        sync_reload = 1;
    b[1] = (char)('1' + tr);
    ui_say("UNDO/REDO ", b);
    ui.force = 1;
}

/* (param_kept, patch_preset, patch_engine: the baseline's, patch.c) */

#define preset_shown(e) (ENGINES[e]->npresets)

static void set_engine_of(track_t *t, uint32_t ei);
/* preset pi of the engine the track asked for: the sound (baseline patch.c), with the OS's own around it (the undo, the
 * editor's sync) */
static void apply_preset_to(track_t *t, uint32_t pi)
{
    load_begin(t);
    patch_preset(t, pi);
    if (t == TSEL)
        sync_reload = 1;
    load_end(t);
}

/* the engine's defaults and its first preset (patch.c). With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
static void set_engine_of(track_t *t, uint32_t ei)
{
    ei %= NENGINES;                                   /* (an index of this build: the stores map ids, engines.c) */
    if (eng_for_track(t, ei) != ei) {                 /* (DUBSIREN off the last track: DUBSIREN8) */
        if (t == TSEL)
            ui_say(ENGINES[ei]->name, ": LAST TRACK ONLY");
        ei = eng_for_track(t, ei);
    }
    load_begin(t);
    fm1_irq_off();
    patch_engine(t, ei);
    fm1_irq_on();
    if (t == TSEL)
        sync_reload = 1;
    load_end(t);
}

static void apply_preset(uint32_t pi) { apply_preset_to(TSEL, pi); }
static void set_engine(uint32_t ei) { set_engine_of(TSEL, ei); }

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
}

/* (Blue Hare, 2026-10) GEN's piece k (gen.c gen_load_piece: the baseline picks the sounds, the key, the tempo, the FX;
 * each sound through os_load_sound below, the OS's way: undo, sync, the IRQ off for an engine) */
static void os_load_sound(uint32_t track, uint32_t engine, uint32_t preset)
{
    track_t *t = &trk[track % NTRK];
    engine = eng_for_track(t, engine);
    if (t->eng_req != engine)
        set_engine_of(t, engine);
    apply_preset_to(t, preset);
}
static void gen_apply(uint32_t k)                 /* (the player's piece k: player.c load) */
{
    if (!player())
        return;
    player()->load(k);
    ui.force = 1;
}
/* the main loop: the piece knob (the player's: GEN's page, the editor, the studio) moved: its sounds. (Not at
 * power-on: the project's sounds stay until a piece is picked) */
static uint8_t gen_loaded;                       /* a piece's sounds loaded since power-on */
static void gen_poll(void)
{
    static int16_t last = -1;
    const player_t *p = player();
    if (!p || p->piece_id == 0xFFu)
        return;
    if (last < 0)
        last = song.g[p->piece_id];
    if (song.g[p->piece_id] != last) {
        last = song.g[p->piece_id];
        gen_apply((uint32_t)clamp(last, 0, (int32_t)p->npieces - 1));
        gen_loaded = 1;
    }
}
/* ARP: the player (GEN) on or off. The first start since power-on loads the piece shown (its sounds, tempo, FX): it
 * plays what its page says; later starts keep what was changed since */
static void gen_toggle(void)
{
    const player_t *p = player();
    if (!p)
        return;
    if (!p->playing() && !gen_loaded && p->piece_id != 0xFFu) {
        gen_apply((uint32_t)clamp(song.g[p->piece_id], 0, (int32_t)p->npieces - 1));
        gen_loaded = 1;
    }
    ui_say(p->name, p->playing() ? " STOPPED" : " PLAYING");
    p->request(!p->playing());
}

/* switch engine (its defaults + first preset) and say so */
static void select_engine(uint32_t e)
{
    set_engine(e);
    ui_say("ENGINE ", ENGINES[TSEL->eng_req]->name);
    ui.force = 1;
}

/* the presets of every engine (in display order, engines.c ENGINES[]), then the used user presets, as one list (the PRESETS knob and the
 * PRESETS page browse it) */
static uint32_t preset_all_pos(uint32_t *total)          /* list index of the selected track's preset */
{
    uint32_t n = 0, cur = 0, e, r;
    for (r = 0; r < NENG_SHOWN; r++) {
        const engine_t *en = ENGINES[e = eng_vis(r)];
        if (e == TSEL->eng_req)
            cur = n + TSEL->preset % (en->npresets ? en->npresets : 1u);
        n += preset_shown(e);
    }
    if (user_of(TSEL) < UP_SLOTS)
        cur = n + up_rank(user_of(TSEL));
    *total = n + up_count();
    return cur;
}

/* list index n (< total) -> engine, *k its preset; NENGINES = user preset, *k its slot */
static uint32_t preset_all_at(uint32_t n, uint32_t *k)
{
    uint32_t e = 0, r;
    for (r = 0; r < NENG_SHOWN && n >= preset_shown(eng_vis(r)); r++)
        n -= preset_shown(eng_vis(r));
    if (r == NENG_SHOWN) {
        *k = up_nth(n);
        return NENGINES;
    }
    e = eng_vis(r);
    *k = n;
    return e;
}

/* PRESETS > LIST (KNOB 4): ALL, FAV (favorites.filter, as up to 1.1.5), or one category (1.2, Discussion #90: BASS ..
 * OTHER, category.c): the list mode 0 ALL, 1 FAV, 1 + CAT_* a category. The category is kept with the settings in a
 * byte no engine uses (ui_lcat: 0 = none; firmware before reads ALL there, and favorites.filter stays 0 / 1 for it, the
 * editor and backups); the PRESETS knob and page browse that list */
#define ui_lcat (favorites.factory[15][11])
#define LIST_N (1u + CAT_N)                              /* ALL FAV BASS LEAD PAD PLUCK KEYS DRUM FX OTHER */
static uint32_t list_mode(void) { return favorites.filter ? 1u : ui_lcat && ui_lcat < CAT_N ? 1u + ui_lcat : 0u; }
static void list_set(uint32_t m)
{
    favorites.filter = m == 1u;
    ui_lcat = (uint8_t)(m >= 2u && m < LIST_N ? m - 1u : 0u);
}
static const char *list_name(uint32_t m) { return m == 0u ? "ALL" : m == 1u ? "FAV" : CAT_NAME[(m - 1u) % CAT_N]; }
/* the favourites' engine of list entry e (preset_all_at: an index, NENGINES = a user preset): its id (favorites.c) */
static uint32_t fav_eng(uint32_t e) { return e >= NENGINES ? EID_COUNT : eng_id(e); }
/* the category of list entry e, k (preset_all_at: NENGINES = user preset k) */
static uint32_t sound_cat(uint32_t e, uint32_t k) { return e == NENGINES ? up_cat(k) : preset_cat(e, k); }
static int list_keep(uint32_t e, uint32_t k)
{
    uint32_t m = list_mode();
    return m == 1u ? favorite_has(fav_eng(e), k) : m ? sound_cat(e, k) == m - 1u : 1;
}
/* the category of the sound track t plays: its user preset's, else its factory preset's (the NAME screen's default) */
static uint32_t track_cat(const track_t *t)
{
    uint32_t u = user_of(t);
    return u < UP_SLOTS ? up_cat(u) : preset_cat(t->eng_req % NENGINES, t->preset);
}

static uint32_t preset_pos(uint32_t *total)
{
    uint32_t all, current = preset_all_pos(&all), n = 0, pos = 0xFFFFFFFFu;
    if (!list_mode()) { *total = all; return current; }
    for (uint32_t i = 0; i < all; i++) {
        uint32_t k, e = preset_all_at(i, &k);
        if (!list_keep(e, k)) continue;
        if (i == current) pos = n;
        n++;
    }
    *total = n;
    return pos == 0xFFFFFFFFu ? n : pos; /* current sound need not be in the list */
}
static uint32_t preset_at(uint32_t n, uint32_t *k)
{
    uint32_t all;
    if (!list_mode()) return preset_all_at(n, k);
    preset_all_pos(&all);
    for (uint32_t i = 0; i < all; i++) {
        uint32_t e = preset_all_at(i, k);
        if (list_keep(e, *k) && !n--) return e;
    }
    *k = UP_SLOTS;
    return NENGINES;
}
static int preset_favorite(void)
{
    uint32_t k = user_of(TSEL);
    return favorite_has(k < UP_SLOTS ? EID_COUNT : eng_id(TSEL->eng_req), k < UP_SLOTS ? k : TSEL->preset);
}
static void preset_mark(int on)
{
    uint32_t k = user_of(TSEL);
    if (favorite_set(k < UP_SLOTS ? EID_COUNT : eng_id(TSEL->eng_req), k < UP_SLOTS ? k : TSEL->preset, on)) {
        ui.force = 1;
        settings_save();
    }
}

static void preset_go(uint32_t n)                    /* load list index n into the selected track (the sound only) */
{
    uint32_t k, e = preset_at(n, &k);
    if (e == NENGINES) {
        up_load(k);
    } else {
        if (e != TSEL->eng_req)
            select_engine(e);
        apply_preset(k);
    }
    ui.force = 1;
}

/* the EDIT layer's KNOB 2 (ui_layer.c): the selected track's engine only: its factory presets, then the used user presets
 * made with it (slot order). List index of the current sound; *total the length */
static uint32_t eng_list_pos(uint32_t *total)
{
    const engine_t *en = ENGINES[TSEL->eng_req % NENGINES];
    uint32_t e = TSEL->eng_req % NENGINES, np = preset_shown(e), u = user_of(TSEL), k, n = 0;
    uint32_t cur = np ? TSEL->preset % en->npresets : 0u;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && up_engine(k) == e) {
            if (k == u)
                cur = np + n;
            n++;
        }
    *total = np + n;
    return cur;
}

static void eng_list_step(int32_t direction)         /* the next / previous sound of the engine (wraps) */
{
    uint32_t total, cur = eng_list_pos(&total), e = TSEL->eng_req % NENGINES, np = preset_shown(e), k, n;
    if (total < 2u)
        return;
    n = (cur + (direction > 0 ? 1u : total - 1u)) % total;
    if (n < np) {
        apply_preset(n);
    } else {
        n -= np;
        for (k = 0; k < UP_SLOTS; k++)
            if (up_used(k) && up_engine(k) == e && !n--) {
                up_load(k);
                break;
            }
    }
    ui.force = 1;
}

static void preset_step(int32_t direction)
{
    uint32_t total, cur = preset_pos(&total);
    if (!total) { ui_message(list_mode() == 1u ? "NO FAVORITES" : "NO SOUNDS IN LIST"); return; }
    preset_go(cur >= total ? (direction > 0 ? 0 : total - 1) :
              (cur + (direction > 0 ? 1u : total - 1u)) % total);
}

/* Seven display rows. FAV and a category use a bounded window, not a repeating carousel.
 * Return total for an empty row; a current sound not in the list shows the start. */
static uint32_t preset_visible(uint32_t cur, uint32_t total, uint32_t row)
{
    uint32_t first, last;
    if (!total || row >= 7u) return total;
    if (!list_mode())
        return (cur + total * 4u + row - 3u) % total;
    first = cur < total && cur > 3u ? cur - 3u : 0u;
    last = total > 7u ? total - 7u : 0u;
    if (first > last) first = last;
    return first + row < total ? first + row : total;
}

/* HOME: what KNOB k edits: the engine's four main parameters */
static const param_desc_t *home_param(uint32_t k, int16_t **vp)
{
    uint32_t id = ENGINES[TSEL->eng_req % NENGINES]->knob[k & 3u];
    *vp = &TSEL->p[id];
    return track_desc(TSEL, id);
}

/* select track i (KNOB 1 on TRACKS, the editor): its sound, pages and pattern from now on */
static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel)
        return;
    song.sel = (uint8_t)i;
    ui.hot_t = 0;
    sync_reload = 1;
    ui.force = 1;
}

/* (Blue Hare, 8 tracks) the 4 tracks the 4-wide places show (MIXER, GLO's cards and keys, the FX layer's mutes): the selected track's four, 1 .. 4 or 5 .. 8 (selecting a track there brings its four in) */
#define VIEW_N 4u
static uint32_t trk_view(void) { return NTRK > VIEW_N ? song.sel / VIEW_N * VIEW_N : 0u; }

/* ---------------------------------------------------- action pages --- */
/* Pages whose purpose is an action (SAVE > USER, PROJECT, TOOLS): the knobs pick,
 * OCT+ does it, OCT- cancels the picked action or goes HOME (ui_input.c). There OCT- / OCT+ do not
 * shift the octave */
static int go_id(uint32_t id) { return id == G_LOAD || id == G_SAVE || id == G_INITSND; }

static uint32_t act_cols(void)                   /* the columns that are actions, a bit each; 0 = not such a page */
{
    const page_t *pg = cur_page();
    uint32_t c, m = 0;
    if (ui.home)
        return 0;
    if (pg->graph == GR_USER)
        return 14u;                              /* LOAD ERASE SAVE */
    if (pg->scope == SC_GLOBAL)
        for (c = 0; c < 4u; c++)
            if (go_id(pg->id[c]))
                m |= 1u << c;
    return m;
}

/* the action OCT+ does: its column + 1, 0 = none picked yet */
static uint32_t act_col(void) { return ui.act; }

static const char *act_name(uint32_t c)          /* column c's action (the footer hint) */
{
    static const char *const UP_GO[3] = {"LOAD", "ERASE", "SAVE"};
    uint32_t id = cur_page()->id[c & 3u];
    if (cur_page()->graph == GR_USER)
        return UP_GO[(c + 2u) % 3u];
    return id == G_INITSND ? "INIT" : id == G_LOAD ? "LOAD" : "SAVE";
}

/* the picked action would do something now (OCT+ breathes): a used slot, stopped for a flash write */
static int act_ready(void)
{
    uint32_t c = act_col(), id;
    if (!c--)
        return 0;
    if (cur_page()->graph == GR_USER)
        return c == 3u ? !song.playing : up_used(ui.uslot) && (c == 1u || !song.playing);
    id = cur_page()->id[c & 3u];
    if (id == G_LOAD)
        return project_used((uint32_t)song.g[G_SLOT] - 1u);
    if (id == G_SAVE)
        return !song.playing;
    return 1;
}
