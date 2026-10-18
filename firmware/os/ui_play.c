/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (Blue Hare, 2026-10-18) The play views: the whole screen, nothing on it that playing does not need (the user's "if
 * I am playing I don't need more than an oscilloscope, the name of the patch and the number of the track"; after Baud
 * Girl's screens: big clean type, a label left and its value right over a thin bar). No header, no cards, no footer.
 *   HOME: the scope across the middle; at the bottom the patch's number (the accent) and its sound's name, in L (M when
 *   L lacks a glyph or it is too wide). TECHNO (the ARP pages) while it plays: STROBE's picture, the whole screen, no
 *   text; stopped: the piece's name and how to start it.
 *   Both: the top band is empty until something happens there: a knob turned shows its label and value (and a bar
 *   when it has a range) for as long as its card would have lit (ui.hot_t), a notice its text (ui.msg_t), then it is
 *   empty again. MENU > HOME TRACKS keeps the four tracks' page.
 *   MIXER (its first page, LEVEL PAN REV MUTE): a row a patch, its number and sound left, its level right, a thin bar
 *   under them, PAN and REV small; the selected one marked in the accent, a muted one dim.
 *   CLOCK (MIXER's second page): retro-futuristic, the user's: the tempo in seven-segment figures as a vacuum
 *   fluorescent display shows them (the unlit segments a ghost behind), four beat squares under them stepping with
 *   TECHNO (small, they move: nothing flashes), SWING and CLOCK as rows */
enum { PV_NONE, PV_HOME, PV_GEN, PV_MIX, PV_CLOCK };
#define PV_TOP 44                                       /* the top band */
#define PV_SCOPE_Y 44                                   /* HOME: the scope (124 rows) */
#define PV_NAME_Y 168                                   /* HOME: the name's band, to the bottom (the three tile the
                                                         * screen: each redraw covers its rows, no blank frame between) */
static uint32_t play_view(void)
{
    if (ui.home)
        return home_tracks() ? PV_NONE : PV_HOME;
    if (cur_page() >= &PAGES[0] && cur_page() < &PAGES[NPAGES] && cur_page()->fam == FAM_TRK && cur_page()->scope == SC_GLOBAL &&
        cur_page()->id[0] == G_BPM)
        return PV_CLOCK;                                /* (the CLOCK page itself, not a page the tests put over it) */
    return cur_page()->graph == GR_GEN ? PV_GEN : cur_page()->graph == GR_TRK && NTRK == 4u ? PV_MIX : PV_NONE;
}
/* what the top band says: a notice, or the knob just turned (label, value, its place 0 .. 1000 or -1); 0: nothing */
static int play_band(char *label, char *val, int32_t *ratio)
{
    int16_t *vp;
    const param_desc_t *d;
    const char *unit;
    if (ui.msg_t) {
        str_cpy(label, ui.msg, 24);
        val[0] = 0;
        *ratio = -1;
        return 1;
    }
    if (!ui.hot_t)
        return 0;
    if (!ui.home && cur_page()->graph == GR_TRK) {     /* (MIXER's cards: the selected patch's own) */
        static const uint8_t ID[4] = {P_LEVEL, P_PAN, P_REV, P_MUTE};
        vp = &TSEL->p[ID[ui.hot_col & 3u]];
        d = &TP[ID[ui.hot_col & 3u]];
    } else
        d = ui.home ? home_param(ui.hot_col, &vp) : page_desc(cur_page(), ui.hot_col, &vp);
    if (!d || !vp || !d->label || d->label[0] == '-')
        return 0;
    str_cpy(label, d->label, 24);
    param_format(d, *vp, val, &unit);
    if (unit && unit[0] && str_len(val) + str_len(unit) < 15u) {
        str_cpy(val + str_len(val), " ", 4);
        str_cpy(val + str_len(val), unit, 8);
    }
    *ratio = d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, enum_rank(d, *vp));
    return 1;
}
/* the band on the canvas's top PV_TOP rows (the canvas cleared to bg): the label M left (MID), the value M right (the
 * accent), the bar under them (a LINE track, the accent's length); a notice alone, centred, in TEXT */
static void play_band_draw(const char *label, const char *val, int32_t ratio, uint16_t bg)
{
    int32_t y = CAP_IN(M, PV_TOP - 8) + 4;
    cv_rect(0, 0, 240, PV_TOP, bg);
    cv_bg = bg;
    if (!val[0]) {
        GFX_HOOK_ALIGN(0, 0, 240, PV_TOP, AL_H, "play band notice centred");
        cv_text_in(12, y, 216, &AF_M, label, T_TEXT, bg);
        return;
    }
    cv_text(16, y, &AF_M, label, T_MID);
    cv_text(224 - text_w(&AF_M, val), y, &AF_M, val, T_ACCENT);
    if (ratio >= 0) {
        cv_rect(16, PV_TOP - 6, 208, 2, T_LINE);
        cv_rect(16, PV_TOP - 6, 208 * clamp(ratio, 0, 1000) / 1000, 2, T_ACCENT);
    }
}
static uint32_t play_band_sig(void)
{
    char a[24], b[24];
    int32_t r;
    if (!play_band(a, b, &r))
        return 1u;
    return str_hash(str_hash(77u, a), b) + (uint32_t)r * 7u;
}

/* HOME: the band, the scope (every other frame), the number and the name */
static void play_home(void)
{
    static uint32_t band_sig, name_sig;
    char nm[24], num[4], a[24], b[24];
    int32_t r, nx, y;
    const aafont_t *f = &AF_L;
    uint32_t s;
    if (ui.force)
        band_sig = name_sig = 0;
    s = play_band_sig();
    if (s != band_sig) {                               /* the top band */
        band_sig = s;
        cv_begin(240, PV_TOP, T_BG);
        if (play_band(a, b, &r))
            play_band_draw(a, b, r, T_BG);
        cv_blit(0, 0);
    }
    if (ui.force || (ui.frame & 1u)) {                 /* the scope */
        cv_begin(240, 124, T_BG);
        cv_bg = T_BG;
        graph_scope(ACC);
        cv_blit(0, PV_SCOPE_Y);
    }
    sound_name(TSEL, nm);
    fmt_int(num, (int32_t)song.sel + 1);
    s = str_hash(str_hash(91u, nm), num) + ux.accent * 3u;
    if (s == name_sig)
        return;
    name_sig = s;
    cv_begin(240, 240 - PV_NAME_Y, T_BG);               /* the number and the name, on one baseline */
    cv_bg = T_BG;
    nx = 16 + text_w(&AF_L, num) + 14;
    if (!large_face_has(nm) || text_w(&AF_L, nm) > 224 - nx)
        f = &AF_M;
    y = CAP_IN(L, 240 - PV_NAME_Y - 20);
    GFX_HOOK_ALIGN(0, 0, 0, 240 - PV_NAME_Y - 20, AL_V, "play: the number centred in its band");
    cv_text(16, y, &AF_L, num, T_ACCENT);
    cv_text(nx, f == &AF_L ? y : y + AF_L_CAP_Y + AF_L_CAP_H - AF_M_CAP_Y - AF_M_CAP_H, f, nm, T_TEXT);
    cv_blit(0, PV_NAME_Y);
}

/* TECHNO: playing, the picture (two halves) with the band over its top when there is one; stopped, the piece */
static void play_gen(void)
{
    char a[24], b[24];
    int32_t r;
    int band = play_band(a, b, &r);
#if BLUEHARE_UI_STROBE
    if (player_playing()) {
        uint32_t half;
        if (!ui.force && !(ui.frame & 1u))
            return;
        for (half = 0; half < 2u; half++) {
            cv_begin(240, ST_H / 2, RGB(0, 0, 0));
            st_frame_half(half);
            if (!half && band)
                play_band_draw(a, b, r, st_c(RGB(0, 0, 0)));
            cv_blit(0, (uint32_t)st_y0);
        }
        st_y0 = 0;
        return;
    }
    st.ready = 0;                                       /* (the next start: a fresh floor) */
#endif
    {
        static uint32_t sig;
        const player_t *p = player();
        const char *piece = p && p->piece_name ? p->piece_name((uint32_t)clamp(song.g[p->piece_id], 0, (int32_t)p->npieces - 1)) : "TECHNO";
        uint32_t s = play_band_sig() + str_hash(5u, piece) * 3u;
        const aafont_t *f = large_face_has(piece) && text_w(&AF_L, piece) <= 216 ? &AF_L : &AF_M;
        if (!ui.force && s == sig)
            return;
        sig = s;
        cv_begin(240, 120, T_BG);
        if (band)
            play_band_draw(a, b, r, T_BG);
        cv_blit(0, 0);
        cv_begin(240, 120, T_BG);
        cv_bg = T_BG;
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H, "play: the piece centred");
        cv_text_in(0, f == &AF_L ? CAP_IN(L, 40) : CAP_IN(M, 40), 240, f, piece, T_TEXT, T_BG);
        cv_text_in(0, 52 + CAP_IN(S, 20), 240, &AF_S, "ARP TO PLAY", T_MID, T_BG);
        cv_blit(0, 120);
    }
}

/* MIXER: the band, then the four rows (two canvases of two rows) */
#define PV_ROW 49
static void play_mix_row(uint32_t k, int32_t y0)
{
    const track_t *t = &trk[k];
    char nm[24], num[4], val[16], pr[24];
    const char *unit;
    uint32_t lvl = (uint32_t)t->p[P_LEVEL] & 127u, sel = k == song.sel, mute = t->p[P_MUTE] != 0;
    uint16_t ink = mute ? T_DIM : T_TEXT, vc = mute ? T_DIM : sel ? T_ACCENT : T_THEME;
    int32_t y = y0 + 4 + CAP_IN(M, 22), nx;
    sound_name(t, nm);
    fmt_int(num, (int32_t)k + 1);
    if (sel)
        cv_rect(0, y0 + 6, 4, PV_ROW - 12, T_ACCENT);  /* (the selected patch: a mark at the left edge) */
    cv_text(16, y, &AF_M, num, sel ? T_ACCENT : T_MID);
    nx = 16 + text_w(&AF_M, "4") + 10;
    if (mute)
        str_cpy(val, "MUTE", 16);
    else {
        param_format(&TP[P_LEVEL], (int32_t)lvl, val, &unit);
        if (unit && unit[0]) {
            str_cpy(val + str_len(val), " ", 4);
            str_cpy(val + str_len(val), unit, 8);
        }
    }
    cv_text(224 - text_w(&AF_M, val), y, &AF_M, val, vc);
    {                                                   /* the name: what is left of the row, ellipsised */
        int32_t room = 224 - text_w(&AF_M, val) - 10 - nx;
        while (str_len(nm) > 1u && text_w(&AF_M, nm) > room)
            nm[str_len(nm) - 1u] = 0;
        cv_text(nx, y, &AF_M, nm, ink);
    }
    cv_rect(16, y0 + 30, 208, 2, T_LINE);
    cv_rect(16, y0 + 30, 208 * (int32_t)lvl / 127, 2, mute ? T_DIM : vc);
    str_cpy(pr, "PAN ", 24);
    fmt_int(pr + str_len(pr), t->p[P_PAN]);
    str_cpy(pr + str_len(pr), "   REV ", 24 - str_len(pr));
    fmt_int(pr + str_len(pr), t->p[P_REV]);
    cv_text(16, y0 + 35 + CAP_IN(S, 12), &AF_S, pr, T_MID);
}
static void play_mix(void)
{
    static uint32_t band_sig, rows_sig;
    char a[24], b[24];
    int32_t r;
    uint32_t s, k;
    if (ui.force)
        band_sig = rows_sig = 0;
    s = play_band_sig();
    if (s != band_sig) {
        band_sig = s;
        cv_begin(240, PV_TOP, T_BG);
        cv_bg = T_BG;
        if (play_band(a, b, &r))
            play_band_draw(a, b, r, T_BG);
        else {
            GFX_HOOK_ALIGN(0, 0, 240, PV_TOP, AL_H, "play: the mixer's title centred");
            cv_text_in(0, CAP_IN(S, PV_TOP), 240, &AF_S, "MIXER", T_MID, T_BG);
        }
        cv_blit(0, 0);
    }
    s = song.sel * 7u + ux.accent;
    for (k = 0; k < NTRK; k++) {
        char nm[24];
        sound_name(&trk[k], nm);
        s = (s ^ str_hash(k, nm)) * 16777619u;
        s = (s ^ (uint32_t)(trk[k].p[P_LEVEL] + 128 * trk[k].p[P_PAN] + 32768 * trk[k].p[P_REV] + (trk[k].p[P_MUTE] << 24))) * 16777619u;
    }
    if (s == rows_sig)
        return;
    rows_sig = s;
    for (k = 0; k < 2u; k++) {
        cv_begin(240, 2 * PV_ROW, T_BG);
        cv_bg = T_BG;
        play_mix_row(2u * k, 0);
        play_mix_row(2u * k + 1u, PV_ROW);
        cv_blit(0, (uint32_t)(PV_TOP + 2 * PV_ROW * (int32_t)k));
    }
}

/* CLOCK: a seven-segment figure (0 .. 9; 10 blank) at x, y, W x H, the strokes T thick: the lit ones c, the others g */
#define SEG_W 46
#define SEG_H 80
#define SEG_T 8
static void seg_digit(int32_t x, int32_t y, uint32_t d, uint16_t c, uint16_t g)
{
    static const uint8_t SEG[11] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x00};   /* bits a..g */
    const int32_t W = SEG_W, H = SEG_H, T = SEG_T, h2 = H / 2;
    uint32_t m = SEG[d % 11u];
    cv_rect(x + T, y, W - 2 * T, T, m & 1u ? c : g);                       /* a */
    cv_rect(x + W - T, y + T, T, h2 - T - T / 2, m & 2u ? c : g);          /* b */
    cv_rect(x + W - T, y + h2 + T / 2, T, h2 - T - T / 2, m & 4u ? c : g); /* c */
    cv_rect(x + T, y + H - T, W - 2 * T, T, m & 8u ? c : g);               /* d */
    cv_rect(x, y + h2 + T / 2, T, h2 - T - T / 2, m & 16u ? c : g);        /* e */
    cv_rect(x, y + T, T, h2 - T - T / 2, m & 32u ? c : g);                 /* f */
    cv_rect(x + T, y + h2 - T / 2, W - 2 * T, T, m & 64u ? c : g);         /* g */
}
static void play_clock(void)
{
    static uint32_t band_sig, num_sig, rows_sig;
    char a[24], b[24];
    int32_t r;
    uint32_t s, k, bpm = (uint32_t)clamp(song.g[G_BPM], 0, 999), beat = 4u;
    if (ui.force)
        band_sig = num_sig = rows_sig = 0;
    s = play_band_sig();
    if (s != band_sig) {
        band_sig = s;
        cv_begin(240, PV_TOP, T_BG);
        cv_bg = T_BG;
        if (play_band(a, b, &r))
            play_band_draw(a, b, r, T_BG);
        else {
            GFX_HOOK_ALIGN(0, 0, 240, PV_TOP, AL_H, "play: the clock's title centred");
            cv_text_in(0, CAP_IN(S, PV_TOP), 240, &AF_S, "TEMPO", T_MID, T_BG);
        }
        cv_blit(0, 0);
    }
    if (player_playing() && player()->step)
        beat = (player()->step() % 16u) / 4u;
    s = bpm * 8u + beat + ux.theme * 1031u;
    if (s != num_sig) {                                 /* the figures and the beat squares (rows 44 .. 163) */
        uint16_t lit = T_THEME, ghost = ux_mix(T_BG, T_THEME, 14), x0 = (uint16_t)((240 - 3 * SEG_W - 2 * 12) / 2);
        num_sig = s;
        cv_begin(240, 120, T_BG);
        cv_bg = T_BG;
        for (k = 0; k < 3u; k++) {
            uint32_t d = k == 0u ? bpm / 100u : k == 1u ? bpm / 10u % 10u : bpm % 10u;
            if (k == 0u && !d)
                d = 10u;                                /* (no leading zero: its ghost alone) */
            seg_digit(x0 + (int32_t)k * (SEG_W + 12), 8, d, lit, ghost);
        }
        for (k = 0; k < 4u; k++)                        /* the beat */
            cv_rect(120 - 2 * 18 + (int32_t)k * 18 + 3, 102, 12, 6, k == beat ? T_ACCENT : ghost);
        cv_blit(0, PV_TOP);
    }
    s = (uint32_t)song.g[G_SWING] * 131u + (uint32_t)song.g[G_CLOCK] * 7u + ux.accent;
    if (s == rows_sig)
        return;
    rows_sig = s;
    cv_begin(240, 240 - PV_TOP - 120, T_BG);            /* SWING and CLOCK, rows of 38 */
    cv_bg = T_BG;
    for (k = 1; k < 3u; k++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(cur_page(), k, &vp);
        char val[16];
        const char *unit;
        int32_t y0 = (int32_t)(k - 1u) * 38, y = y0 + CAP_IN(M, 30);
        if (!d || !vp)
            continue;
        param_format(d, *vp, val, &unit);
        if (unit && unit[0]) {
            str_cpy(val + str_len(val), " ", 4);
            str_cpy(val + str_len(val), unit, 8);
        }
        cv_text(16, y, &AF_M, k == 1u ? "SWING" : "CLOCK", T_TEXT);
        cv_text(224 - text_w(&AF_M, val), y, &AF_M, val, T_THEME);
        if (d->fmt != F_ENUM) {
            cv_rect(16, y0 + 31, 208, 2, T_LINE);
            cv_rect(16, y0 + 31, 208 * RATIO(d, *vp) / 1000, 2, T_THEME);
        }
    }
    cv_blit(0, PV_TOP + 120);
}

static void draw_play(uint32_t v)
{
    static uint32_t last;
    if (v != last) {                                    /* (into a play view, or out of one: everything again) */
        last = v;
        ui.force = 1;
    }
    if (v == PV_NONE)
        return;
    if (v == PV_HOME)
        play_home();
    else if (v == PV_MIX)
        play_mix();
    else if (v == PV_CLOCK)
        play_clock();
    else
        play_gen();
}
