/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-24, the user's "pong as an easter egg. you enter in it holding play/stop, supports both single player and
 * two players. p1 moves with select. p2 with knob 4. follow the classic for default settings. also follow the colour
 * palette of the distro"; "redraw only what changes") PONG: PLAY held a second opens it (ui_input.c; the transport is
 * put back as it was before the press), HOME closes it; the music goes on, the keys are silent meanwhile.
 *   start screen  SELECT: 1 PLAYER (against the FM-1) or 2 PLAYERS; PLAY: serve
 *   SELECT        player 1's paddle (left) up and down
 *   KNOB 4        player 2's paddle (right; 1 PLAYER: the FM-1's)
 *   PLAY          pause, and again: on; after a game: the start screen
 * As the 1972 arcade game: two paddles, a square ball, the net, the walls top and bottom, blocky score digits; the
 * paddle in eight parts, the outer ones sending the ball off steepest; the ball faster after the 4th and the 12th hit of
 * a rally; a point served from the net toward the one who lost it; the first to 11 wins.
 * The colours: the palette's (MENU > COLOR): the field T_BG, walls and net T_LINE, the scores T_MID, the paddles T_TEXT,
 * the ball T_THEME (in GREY and MONO the palettes are gray already).
 * Drawn only where something changed (pg_draw): the ball's and each paddle's last and new places, the scores when one
 * changes; the start, pause and end screens whole, once. pg_region draws any rectangle of the screen from scratch (the
 * field, then what lies in it), so a small canvas is all a frame needs. Runs in the UI's frame (main.c), no audio time. */
#define PG_WALL 4                                       /* the walls' thickness, top and bottom */
#define PG_BALL 6
#define PG_PW 4                                         /* a paddle: width, height, its x (left edge) */
#define PG_PH 30
#define PG_X1 14
#define PG_X2 (240 - PG_X1 - PG_PW)
#define PG_STEP 10                                      /* px a detent of SELECT / KNOB 4 */
#define PG_WIN 11u
#define PG_SERVE_MS 1000u                               /* the ball away after a point, then served */
#define PG_DIG 5                                        /* the digits' block: 3 x 5 blocks */
#define PG_DIG_Y 12
enum { PG_READY, PG_RUN, PG_PAUSE, PG_OVER };
static const int16_t PG_VY[8] = {-170, -120, -70, -30, 30, 70, 120, 170};   /* px/s at the first speed, a paddle's part */
static const uint16_t PG_DIGITS[10] = {0x7B6Fu, 0x2C97u, 0x73E7u, 0x73CFu, 0x5BC9u, 0x79CFu, 0x79EFu, 0x7249u, 0x7BEFu,
                                       0x7BCFu};   /* 3 x 5, row by row from the top, bit 14 the top left */
static struct {
    uint8_t on, state, players, drawn_state, drawn_players, hits, serve_to, ball_on;
    uint8_t score[2], drawn_score[2];
    int32_t py[2];                                      /* the paddles' top, px << 8 */
    int32_t bx, by, vx, vy;                             /* the ball: top left px << 8, px/s */
    int32_t aim;                                        /* the FM-1's aim on its paddle (1 PLAYER), px */
    uint32_t t_last, serve_t, rnd;
    int16_t dpy[2], dbx, dby;                           /* where they were drawn (dbx < 0: no ball drawn) */
} pong;

static uint32_t pg_rand(void)
{
    pong.rnd = pong.rnd * 1664525u + 1013904223u;
    return pong.rnd >> 16;
}

/* the speed now: the arcade's three, by the hits of the rally */
static int32_t pg_speed(void) { return pong.hits >= 12u ? 260 : pong.hits >= 4u ? 200 : 150; }

static void pg_serve_wait(uint32_t to)                  /* the ball away; served toward player `to` in a second */
{
    pong.ball_on = 0;
    pong.serve_to = (uint8_t)to;
    pong.serve_t = fm1_ms;
    pong.hits = 0;
}
static void pg_serve(void)
{
    int32_t s = pg_speed();
    pong.ball_on = 1;
    pong.bx = (120 - PG_BALL / 2) << 8;
    pong.by = (int32_t)(60u + pg_rand() % 120u) << 8;
    pong.vx = pong.serve_to ? s : -s;
    pong.vy = PG_VY[2u + pg_rand() % 4u] * s / 150;
}

static void pong_new(void)                              /* PLAY on the start screen: a game */
{
    pong.score[0] = pong.score[1] = 0;
    pong.py[0] = pong.py[1] = (120 - PG_PH / 2) << 8;
    pong.state = PG_RUN;
    pong.t_last = fm1_ms;
    pg_serve_wait(pg_rand() & 1u);
}
static void pong_open(void)
{
    pong.on = 1;
    pong.state = PG_READY;
    pong.players = pong.players ? pong.players : 1u;
    pong.drawn_state = 0xFF;                            /* (the first frame: all of it) */
    pong.rnd ^= fm1_ms * 2654435761u;
    pong.ball_on = 0;
    pong.score[0] = pong.score[1] = 0;
    pong.py[0] = pong.py[1] = (120 - PG_PH / 2) << 8;
    ui_message("PONG");
}
static void pong_close(void)
{
    pong.on = 0;
    ui.force = 1;                                       /* (the page drawn again whole) */
}

static void pg_move(uint32_t p, int32_t det)            /* a paddle by detents (+ up) */
{
    pong.py[p] = clamp(pong.py[p] - det * (PG_STEP << 8), PG_WALL << 8, (240 - PG_WALL - PG_PH) << 8);
}

/* ui_input.c: everything while the game is on */
static void pong_input(uint32_t pressed)
{
    int32_t s = panel_enc(EN_SELECT), k = panel_enc(EN_K4);
    uint32_t e;
    for (e = 0; e < NE; e++)                            /* (the other knobs: no turn left over for after) */
        if (e != EN_SELECT && e != EN_K4)
            panel_enc(e);
    if ((pressed >> panel.btn[B_HOME]) & 1u) {
        pong_close();
        return;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {          /* PLAY: serve, pause and on, after a game the start screen */
        if (pong.state == PG_READY)
            pong_new();
        else if (pong.state == PG_OVER)
            pong.state = PG_READY;
        else if (pong.state == PG_RUN)
            pong.state = PG_PAUSE;
        else {
            pong.state = PG_RUN;
            pong.t_last = fm1_ms;
        }
        return;
    }
    if (pong.state == PG_READY) {                       /* the start screen: SELECT picks the players */
        if (s)
            pong.players = s > 0 ? 1u : 2u;
        return;
    }
    if (pong.state != PG_RUN)
        return;
    pg_move(0, s);
    if (pong.players == 2u)
        pg_move(1, k);
}

/* the FM-1's paddle (1 PLAYER): toward the ball while it comes, at a paddle's pace it cannot always keep up with;
 * back to the middle while it goes away */
static void pg_cpu(uint32_t dt)
{
    int32_t want, d, max = (int32_t)(dt * 140u) << 8;   /* (px/s, << 8, / 1000 below) */
    if (pong.ball_on && pong.vx > 0)
        want = (pong.by >> 8) + PG_BALL / 2 - PG_PH / 2 + pong.aim;
    else
        want = 120 - PG_PH / 2;
    d = (want << 8) - pong.py[1];
    max /= 1000;
    d = d > max ? max : d < -max ? -max : d;
    pong.py[1] = clamp(pong.py[1] + d, PG_WALL << 8, (240 - PG_WALL - PG_PH) << 8);
}

/* the ball against paddle p as it crosses the paddle's face: 1 returned */
static int pg_hit(uint32_t p, int32_t ox, int32_t nx)
{
    int32_t face = p ? PG_X2 << 8 : (PG_X1 + PG_PW) << 8, by = pong.by >> 8, py = pong.py[p] >> 8, part, s;
    int crossed = p ? (ox + (PG_BALL << 8) <= face && nx + (PG_BALL << 8) > face) : (ox >= face && nx < face);
    if (!crossed || by + PG_BALL <= py || by >= py + PG_PH)
        return 0;
    part = (by + PG_BALL / 2 - py) * 8 / PG_PH;         /* the paddle's part it met, 0 (top) .. 7 */
    part = part < 0 ? 0 : part > 7 ? 7 : part;
    pong.hits++;
    s = pg_speed();
    pong.vx = p ? -s : s;
    pong.vy = PG_VY[part] * s / 150;
    pong.bx = p ? face - (PG_BALL << 8) : face;
    if (p == 0u)
        pong.aim = (int32_t)(pg_rand() % 25u) - 12;     /* (the FM-1's next return: somewhere on its paddle) */
    return 1;
}

static void pong_step(void)                             /* the ball, the FM-1's paddle, the points: once a frame */
{
    uint32_t dt = fm1_ms - pong.t_last;
    int32_t ox, nx;
    pong.t_last = fm1_ms;
    if (pong.state != PG_RUN)
        return;
    dt = dt > 50u ? 50u : dt;
    if (pong.players == 1u)
        pg_cpu(dt);
    if (!pong.ball_on) {
        if (fm1_ms - pong.serve_t >= PG_SERVE_MS)
            pg_serve();
        return;
    }
    ox = pong.bx;
    nx = ox + pong.vx * (int32_t)dt * 256 / 1000;
    pong.by += pong.vy * (int32_t)dt * 256 / 1000;
    if (pong.by < PG_WALL << 8) {                       /* the walls */
        pong.by = (2 * PG_WALL << 8) - pong.by;
        pong.vy = -pong.vy;
    } else if (pong.by > (240 - PG_WALL - PG_BALL) << 8) {
        pong.by = (2 * (240 - PG_WALL - PG_BALL) << 8) - pong.by;
        pong.vy = -pong.vy;
    }
    if (pong.vx < 0 ? pg_hit(0, ox, nx) : pg_hit(1, ox, nx))
        return;
    pong.bx = nx;
    if (nx + (PG_BALL << 8) < 0 || nx > 240 << 8) {     /* out: a point to the other side, served to the loser */
        uint32_t won = nx > 0 ? 0u : 1u;                /* (out right: player 1's point) */
        pong.score[won]++;
        if (pong.score[won] >= PG_WIN) {
            pong.state = PG_OVER;
            pong.ball_on = 0;
        } else
            pg_serve_wait(won ^ 1u);
    }
}

/* ---- drawing: any rectangle of the screen from scratch (pg_region), into a canvas of its size */
static int32_t pg_rx, pg_ry;                            /* the region's top left on the screen */
static void pg_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x - pg_rx, y - pg_ry, w, h, c);             /* (cv_rect clips to the canvas) */
}
static int32_t pg_digits_x(uint32_t p, uint32_t v)      /* the left edge of player p's score */
{
    int32_t w = v >= 10u ? 7 * PG_DIG : 3 * PG_DIG;
    return p ? 120 + 6 * PG_DIG : 120 - 6 * PG_DIG - w;
}
static void pg_score(uint32_t p, uint32_t v)            /* the arcade's blocky digits */
{
    int32_t x = pg_digits_x(p, v), d, r, c;
    for (d = v >= 10u ? 0 : 1; d < 2; d++) {
        uint32_t g = PG_DIGITS[d ? v % 10u : v / 10u % 10u];
        for (r = 0; r < 5; r++)
            for (c = 0; c < 3; c++)
                if ((g >> (14 - 3 * r - c)) & 1u)
                    pg_rect(x + c * PG_DIG, PG_DIG_Y + r * PG_DIG, PG_DIG, PG_DIG, T_MID);
        x += 4 * PG_DIG;
    }
}
static void pg_field(void)                              /* everything in the field (clipped to the region) */
{
    int32_t y;
    pg_rect(0, 0, 240, PG_WALL, T_LINE);
    pg_rect(0, 240 - PG_WALL, 240, PG_WALL, T_LINE);
    for (y = PG_WALL + 4; y < 240 - PG_WALL; y += 16)   /* the net */
        pg_rect(119, y, 2, 8, T_LINE);
    pg_score(0, pong.score[0]);
    pg_score(1, pong.score[1]);
    pg_rect(PG_X1, pong.py[0] >> 8, PG_PW, PG_PH, T_TEXT);
    pg_rect(PG_X2, pong.py[1] >> 8, PG_PW, PG_PH, T_TEXT);
    if (pong.ball_on)
        pg_rect(pong.bx >> 8, pong.by >> 8, PG_BALL, PG_BALL, T_THEME);
}
static void pg_region(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 240) w = 240 - x;
    if (y + h > 240) h = 240 - y;
    if (w <= 0 || h <= 0)
        return;
    while (h > 0) {                                     /* (a canvas holds 240 x 124 at most: tall ones in parts) */
        int32_t part = (int32_t)(CV_MAX / (uint32_t)w) < h ? (int32_t)(CV_MAX / (uint32_t)w) : h;
        cv_begin((uint32_t)w, (uint32_t)part, T_BG);
        pg_rx = x;
        pg_ry = y;
        pg_field();
        cv_blit((uint32_t)x, (uint32_t)y);
        y += part;
        h -= part;
    }
}
/* the rectangles a and b as one if they overlap or touch, else each alone */
static void pg_two(int32_t ax, int32_t ay, int32_t aw, int32_t ah, int32_t bx, int32_t by, int32_t bw, int32_t bh)
{
    int32_t x0 = ax < bx ? ax : bx, y0 = ay < by ? ay : by;
    int32_t x1 = ax + aw > bx + bw ? ax + aw : bx + bw, y1 = ay + ah > by + bh ? ay + ah : by + bh;
    if ((x1 - x0) * (y1 - y0) <= aw * ah + bw * bh + 64) {
        pg_region(x0, y0, x1 - x0, y1 - y0);
        return;
    }
    pg_region(ax, ay, aw, ah);
    pg_region(bx, by, bw, bh);
}

/* a panel's centred line at screen row sy (the panels are drawn in the two whole bands) */
static void pg_line(int32_t sy, const aafont_t *f, const char *t, uint16_t c)
{
    int32_t y = sy - pg_ry;
    if (y + (int32_t)f->h <= 0 || y >= (int32_t)cv_h)
        return;
    cv_text_on(120 - text_w(f, t) / 2, y, f, t, c, T_SURF);
}
static void pg_panel(void)                              /* the start, pause and end screens over the field */
{
    static const char *const L[] = {"SELECT: PLAYER 1 (LEFT)", "KNOB 4: PLAYER 2 (RIGHT)", "FIRST TO 11 WINS", "",
                                    "PLAY: SERVE / PAUSE", "HOME: BACK"};
    static const char *const CPU = "THE FM-1 PLAYS RIGHT";
    int32_t top = pong.state == PG_READY ? 44 : 84, bottom = pong.state == PG_READY ? 222 : 156, i;
    char t[24];
    pg_rect(16, top, 208, bottom - top, T_SURF);
    pg_rect(16, top, 208, 2, T_THEME);
    pg_rect(16, bottom - 2, 208, 2, T_THEME);
    if (pong.state == PG_PAUSE) {
        pg_line(top + 16, &AF_L, "PAUSE", T_THEME);
        pg_line(top + 48, &AF_S, "PLAY: ON   HOME: BACK", T_TEXT);
        return;
    }
    if (pong.state == PG_OVER) {
        str_cpy(t, pong.score[0] > pong.score[1] ? "PLAYER 1 WINS" : pong.players == 1u ? "THE FM-1 WINS" :
                "PLAYER 2 WINS", sizeof t);
        pg_line(top + 20, &AF_M, t, T_THEME);
        pg_line(top + 48, &AF_S, "PLAY: AGAIN   HOME: BACK", T_TEXT);
        return;
    }
    pg_line(top + 10, &AF_L, "PONG", T_THEME);
    pg_line(top + 46, &AF_M, pong.players == 1u ? "< 1 PLAYER >" : "< 2 PLAYERS >", T_ACCENT);
    pg_line(top + 66, &AF_S, "SELECT: 1 OR 2 PLAYERS", T_MID);
    for (i = 0; i < (int32_t)NELEM(L); i++)
        if (L[i][0])
            pg_line(top + 88 + i * 13, &AF_S, i == 1 && pong.players == 1u ? CPU : L[i], i == 4 ? T_THEME : T_TEXT);
}

/* ui_draw (ui.c): every frame while it is on. Whole on a change of screen (state, players, ui.force); while a game
 * runs, only the ball's and the paddles' last and new places and a score that changed */
static void pong_draw(void)
{
    int32_t bx, by, p, y;
    pong_step();
    if (pong.state != pong.drawn_state || pong.players != pong.drawn_players || ui.force) {
        pong.drawn_state = pong.state;
        pong.drawn_players = pong.players;
        for (y = 0; y < 240; y += 120) {
            cv_begin(240, 120, T_BG);
            pg_rx = 0;
            pg_ry = y;
            pg_field();
            if (pong.state != PG_RUN)
                pg_panel();
            cv_blit(0, (uint32_t)y);
        }
        pong.dpy[0] = (int16_t)(pong.py[0] >> 8);
        pong.dpy[1] = (int16_t)(pong.py[1] >> 8);
        pong.dbx = pong.ball_on ? (int16_t)(pong.bx >> 8) : -1;
        pong.dby = (int16_t)(pong.by >> 8);
        pong.drawn_score[0] = pong.score[0];
        pong.drawn_score[1] = pong.score[1];
        return;
    }
    if (pong.state != PG_RUN)
        return;
    for (p = 0; p < 2; p++) {                           /* a score that changed: its digits' place, both widths */
        if (pong.score[p] != pong.drawn_score[p]) {
            pg_region(pg_digits_x((uint32_t)p, 10u), PG_DIG_Y, 7 * PG_DIG, 5 * PG_DIG);
            pong.drawn_score[p] = pong.score[p];
        }
    }
    for (p = 0; p < 2; p++) {                           /* the paddles: where they were and are, as one strip */
        y = pong.py[p] >> 8;
        if (y != pong.dpy[p]) {
            int32_t y0 = y < pong.dpy[p] ? y : pong.dpy[p], y1 = (y > pong.dpy[p] ? y : pong.dpy[p]) + PG_PH;
            pg_region(p ? PG_X2 : PG_X1, y0, PG_PW, y1 - y0);
            pong.dpy[p] = (int16_t)y;
        }
    }
    bx = pong.ball_on ? pong.bx >> 8 : -1;
    by = pong.by >> 8;
    if (bx != pong.dbx || (bx >= 0 && by != pong.dby)) {   /* the ball: its last place and its new one */
        if (pong.dbx >= 0 && bx >= 0)
            pg_two(pong.dbx, pong.dby, PG_BALL, PG_BALL, bx, by, PG_BALL, PG_BALL);
        else if (pong.dbx >= 0)
            pg_region(pong.dbx, pong.dby, PG_BALL, PG_BALL);
        else
            pg_region(bx, by, PG_BALL, PG_BALL);
        pong.dbx = (int16_t)bx;
        pong.dby = (int16_t)by;
    }
}
