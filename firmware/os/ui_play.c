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
 *   empty again. MENU > HOME TRACKS keeps the four tracks' page */
enum { PV_NONE, PV_HOME, PV_GEN };
#define PV_TOP 44                                       /* the top band */
#define PV_SCOPE_Y 44                                   /* HOME: the scope (124 rows) */
#define PV_NAME_Y 168                                   /* HOME: the name's band, to the bottom (the three tile the
                                                         * screen: each redraw covers its rows, no blank frame between) */
static uint32_t play_view(void)
{
    if (ui.home)
        return home_tracks() ? PV_NONE : PV_HOME;
    return cur_page()->graph == GR_GEN ? PV_GEN : PV_NONE;
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
    else
        play_gen();
}
