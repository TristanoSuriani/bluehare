/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-24, the user's "a frogger game that is activated when someone holds seq for one second"; then "niglio and
 * the cars 2x the size", SELECT and KNOB 4 to hop, PLAY to start and pause) HARE CROSSING: get Niglio across the road
 * and the river into the three burrows at the top. SEQ held for a second opens it (ui_input.c), HOME or SEQ closes it;
 * the music goes on (MIDI, the loops), the keys are silent meanwhile.
 *   PLAY          a new game (from the start screen or GAME OVER); in a game: pause, and again: on
 *   SELECT        a hop forward (right) or back (left)
 *   KNOB 4        a hop right or left
 * The road's cars run Niglio over; in the river only the logs carry him (the water and the edge of the screen do not).
 * A burrow filled is 50 points (and 10 for every row further than before, a point a second left), all three fill the
 * next level. Each life has 30 seconds (the bar at the top); three lives.
 * The difficulty (2026-10-24, the user's): every level the lanes 20 % faster, at most twice (level 6); every third level
 * the logs a cell shorter (one at least) with more water between, the cars' gaps a cell narrower (two at least); the
 * time a life 2 s less (20 s at least); from level 4 the second river lane's logs sink now and then (darker half a
 * second before: under for 1.5 s of every 6) and the first road lane's cars 60 % faster. The intermezzo says what is new.
 * The screen: a 16 px line on top (the score, the time, the lives), then 7 rows of 32 px: 0 the burrows, 1..2 the
 * river, 3 the bank, 4..5 the road, 6 the start; Niglio and the cars a 32 px cell (Niglio from the splash's picture,
 * ui_niglio.h, scaled down). Drawn in two canvases (gfx.c's: 240 x 124 at most). Its own colours, but in GREY and MONO
 * their grays (frog_c, as ui_strobe.c st_c). Runs in the UI's frame (main.c), no audio time. */
#define FR_TOP 16                                       /* the line on top */
#define FR_CELL 32
#define FR_ROWS 7u
#define FR_X0 8                                         /* Niglio's places across: 8 + 32 k */
#define FR_LANES 4u                                     /* rows 1, 2 the river, 4, 5 the road */
#define FR_LIFE_MS 30000u
enum { FR_READY, FR_RUN, FR_PAUSE, FR_OVER, FR_LEVEL };   /* (FR_LEVEL: the intermezzo between two levels) */
#define FR_LEVEL_MS 3000u
enum { FR_C_WATER = 0x1A7Fu, FR_C_ROAD = 0x2104u, FR_C_GRASS = 0x2C64u, FR_C_LOG = 0x8A22u, FR_C_HUD = 0x0000u,
       FR_C_HOME = 0x0180u, FR_C_TEXT = 0xFFFFu, FR_C_LINE = 0xFFE0u };
static const uint16_t FR_CAR[4] = {0xF8A3u, 0xFE60u, 0x07FFu, 0xF81Fu};
/* a lane: its row, speed (px per second, + right), the length of its things (cells), the gap between */
typedef struct { uint8_t row; int16_t speed; uint8_t len, gap; } fr_lane_t;
static const fr_lane_t FR_LANE[FR_LANES] = {
    {1, 34, 3, 2}, {2, -46, 2, 2},                      /* the logs */
    {4, -60, 1, 3}, {5, 42, 2, 3},                      /* the cars */
};
static struct {
    uint8_t on, state, lives, level, home[3], best_row, dead_t;
    int16_t x, row;                                     /* Niglio: px (left edge), row */
    int32_t xf;                                         /* (his px << 8, carried by a log) */
    int32_t off[FR_LANES];                              /* each lane's offset, px << 8 */
    uint32_t score, hi, t_last, life_t, paused_t, level_t, gt;   /* gt: the game's clock (stops in a pause) */
    uint8_t sprite[FR_CELL * FR_CELL];                  /* Niglio at 32 x 32: NG_PAL indices, 0 the ground */
} frog;

/* the level's lanes: speed (px a second), length and gap (cells); the time a life; the sinking logs' state */
static int32_t fr_speed(uint32_t i)
{
    int32_t sp = FR_LANE[i].speed * (int32_t)(4u + (frog.level < 6u ? frog.level : 6u)) / 5;
    return i == 2u && frog.level >= 4u ? sp * 8 / 5 : sp;   /* (from level 4: the fast lane) */
}
static int32_t fr_len(uint32_t i)
{
    int32_t n = (int32_t)FR_LANE[i].len - (i < 2u ? (int32_t)(frog.level - 1u) / 3 : 0);
    return n < 1 ? 1 : n;
}
static int32_t fr_gap(uint32_t i)
{
    int32_t g = (int32_t)FR_LANE[i].gap, d = (int32_t)(frog.level - 1u) / 3;
    if (i < 2u)
        return g + d > 4 ? 4 : g + d;                   /* (the river: more water) */
    return g - d < 2 ? 2 : g - d;                       /* (the road: closer cars) */
}
static uint32_t fr_life_ms(void)
{
    uint32_t lv = frog.level < 6u ? frog.level : 6u;
    return FR_LIFE_MS - 2000u * (lv - 1u);              /* 30 s .. 20 s */
}
static uint32_t fr_sink(uint32_t i)                     /* 0 afloat, 1 sinking (the warning), 2 under */
{
    uint32_t ph = frog.gt % 6000u;
    if (i != 1u || frog.level < 4u)
        return 0;
    return ph >= 4500u ? 2u : ph >= 4000u ? 1u : 0u;
}

static uint16_t frog_c(uint16_t c) { return ux.mono || ux.bw ? ux_gray(ux_luma(c) * 31u / 255u) : c; }

static void frog_sprite(void)                           /* Niglio's 152 x 152 to 32 x 32, nearest, once */
{
    uint32_t x, y;
    for (y = 0; y < FR_CELL; y++)
        for (x = 0; x < FR_CELL; x++) {
            uint32_t sx = x * NG_W / FR_CELL + NG_W / FR_CELL / 2, sy = y * NG_H / FR_CELL + NG_H / FR_CELL / 2;
            uint32_t k = sy * NG_W + sx;
            frog.sprite[y * FR_CELL + x] = (uint8_t)((NG_PX[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u);
        }
}

static void frog_start(void)                            /* Niglio at the start, a new life */
{
    frog.row = 6;
    frog.x = FR_X0 + 3 * FR_CELL;
    frog.xf = frog.x << 8;
    frog.best_row = 6;
    frog.life_t = fm1_ms;
    frog.dead_t = 0;
}
static void frog_new(void)                              /* PLAY: a new game */
{
    memset(frog.home, 0, sizeof frog.home);
    frog.lives = 3;
    frog.level = 1;
    frog.score = 0;
    frog.state = FR_RUN;
    frog.t_last = fm1_ms;
    frog_start();
}
static void frog_open(void)
{
    uint32_t i;
    frog_sprite();
    for (i = 0; i < FR_LANES; i++)
        frog.off[i] = (int32_t)(i * 53u % 64u) << 8;
    memset(frog.home, 0, sizeof frog.home);
    frog.on = 1;
    frog.state = FR_READY;
    frog.lives = 3;
    frog.level = 1;
    frog.score = 0;
    frog.t_last = fm1_ms;
    frog_start();
    ui_message("HARE CROSSING");
}
static void frog_close(void)
{
    frog.on = 0;
    ui.force = 1;                                       /* (the page drawn again whole) */
}

/* the things of lane i: is pixel column x covered? (the pattern repeats every period, offset by the lane's motion) */
static int frog_covered(uint32_t i, int32_t x)
{
    int32_t period = (fr_len(i) + fr_gap(i)) * FR_CELL, p = (x - (frog.off[i] >> 8)) % period;
    if (p < 0)
        p += period;
    return p < fr_len(i) * FR_CELL && fr_sink(i) != 2u;
}

static void frog_die(void)
{
    if (frog.dead_t)
        return;
    frog.dead_t = 30;                                   /* (frames of the flash) */
    if (frog.lives)
        frog.lives--;
}

static void frog_hop(int dx, int dy)
{
    if (frog.state != FR_RUN || frog.dead_t)
        return;
    frog.x = (int16_t)clamp(frog.x + dx * FR_CELL, 0, 240 - FR_CELL);
    frog.xf = frog.x << 8;
    frog.row = (int16_t)clamp(frog.row + dy, 0, 6);
    if (frog.row < frog.best_row) {                     /* further than before this life: 10 a row */
        frog.score += 10u * (uint32_t)(frog.best_row - frog.row);
        frog.best_row = (uint8_t)frog.row;
    }
}

/* ui_input.c: everything while the game is on. The knobs' turns: one hop a detent */
static void frog_input(uint32_t pressed)
{
    int32_t s = panel_enc(EN_SELECT), k = panel_enc(EN_K4);
    uint32_t e;
    for (e = 0; e < NE; e++)                            /* (the other knobs: no turn left over for after) */
        if (e != EN_SELECT && e != EN_K4)
            panel_enc(e);
    if (((pressed >> panel.btn[B_HOME]) & 1u) || ((pressed >> panel.btn[B_SEQ]) & 1u)) {
        frog_close();
        return;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {          /* PLAY: a new game; in one, pause and on (never the song) */
        if (frog.state == FR_READY || frog.state == FR_OVER)
            frog_new();
        else if (frog.state == FR_LEVEL)
            frog.level_t = fm1_ms - FR_LEVEL_MS;        /* (the intermezzo cut short) */
        else if (frog.state == FR_RUN) {
            frog.state = FR_PAUSE;
            frog.paused_t = fm1_ms;
        } else {
            frog.state = FR_RUN;
            frog.life_t += fm1_ms - frog.paused_t;      /* (the pause not counted against the time) */
            frog.t_last = fm1_ms;
        }
        return;
    }
    for (; s > 0; s--)
        frog_hop(0, -1);
    for (; s < 0; s++)
        frog_hop(0, 1);
    for (; k > 0; k--)
        frog_hop(1, 0);
    for (; k < 0; k++)
        frog_hop(-1, 0);
}

static void frog_step(void)                             /* the lanes, the log's ride, the rules: once a frame */
{
    uint32_t i, dt = fm1_ms - frog.t_last, b;
    frog.t_last = fm1_ms;
    if (frog.state == FR_PAUSE)
        return;
    dt = dt > 100u ? 100u : dt;
    frog.gt += dt;
    for (i = 0; i < FR_LANES; i++)
        frog.off[i] += fr_speed(i) * (int32_t)dt * 256 / 1000;
    if (frog.state == FR_LEVEL && fm1_ms - frog.level_t >= FR_LEVEL_MS) {   /* the intermezzo over: the next level */
        memset(frog.home, 0, sizeof frog.home);
        frog.state = FR_RUN;
        frog_start();
    }
    if (frog.state != FR_RUN)
        return;
    if (frog.dead_t) {                                  /* the flash, then the next life or the end */
        if (!--frog.dead_t) {
            if (frog.lives)
                frog_start();
            else {
                frog.state = FR_OVER;
                frog.hi = frog.score > frog.hi ? frog.score : frog.hi;
            }
        }
        return;
    }
    if (fm1_ms - frog.life_t > fr_life_ms()) {
        frog_die();
        return;
    }
    for (i = 0; i < FR_LANES; i++) {
        if (FR_LANE[i].row != (uint32_t)frog.row)
            continue;
        if (i < 2u) {                                   /* the river: on a log, carried; else in the water */
            if (!frog_covered(i, frog.x + FR_CELL / 2))
                frog_die();
            else {
                frog.xf += fr_speed(i) * (int32_t)dt * 256 / 1000;
                frog.x = (int16_t)(frog.xf >> 8);
                if (frog.x < -8 || frog.x > 240 - FR_CELL + 8)
                    frog_die();                         /* (off the screen with the log) */
            }
        } else if (frog_covered(i, frog.x + 6) || frog_covered(i, frog.x + FR_CELL - 7)) {
            frog_die();                                 /* a car */
        }
    }
    if (frog.row == 0 && !frog.dead_t) {                /* the top: into a burrow (columns 1, 3, 5), or not */
        int32_t c = (frog.x - FR_X0 + FR_CELL / 2) / FR_CELL;
        b = (uint32_t)(c - 1) / 2u;
        if (c >= 1 && c <= 5 && (c & 1) && !frog.home[b]) {
            frog.home[b] = 1;
            frog.score += 50u + (fr_life_ms() - (fm1_ms - frog.life_t)) / 1000u;
            if (frog.home[0] && frog.home[1] && frog.home[2]) {   /* all three: the intermezzo, then the next level */
                frog.level++;
                frog.score += 200u;
                frog.state = FR_LEVEL;
                frog.level_t = fm1_ms;
            }
            frog_start();
        } else
            frog_die();
    }
}

static void frog_niglio(int32_t x, int32_t y, int hit)  /* Niglio in a cell at (x, y) of the canvas */
{
    uint32_t i, j;
    for (j = 0; j < FR_CELL; j++)
        for (i = 0; i < FR_CELL; i++) {
            uint32_t k = frog.sprite[j * FR_CELL + i];
            if (k)
                cv_rect(x + (int32_t)i, y + (int32_t)j, 1, 1, frog_c(hit ? FR_CAR[0] : NG_PAL[k]));
        }
}

static void frog_car(int32_t x, int32_t y, int32_t w, uint16_t c, int right)   /* a car, its windscreen, its wheels */
{
    cv_rect(x + 7, y + 3, 6, 3, frog_c(0x0000u));
    cv_rect(x + w - 13, y + 3, 6, 3, frog_c(0x0000u));
    cv_rect(x + 7, y + FR_CELL - 6, 6, 3, frog_c(0x0000u));
    cv_rect(x + w - 13, y + FR_CELL - 6, 6, 3, frog_c(0x0000u));
    cv_rect(x + 3, y + 6, w - 6, FR_CELL - 12, frog_c(c));
    cv_rect(right ? x + w - 13 : x + 7, y + 9, 6, FR_CELL - 18, frog_c(0xFFFFu));
}

/* a centred line of text at screen row sy, in the band starting at screen row y0 (h rows) */
static void frog_line(int32_t y0, int32_t h, int32_t sy, const aafont_t *f, const char *t, uint16_t c, uint16_t bg)
{
    int32_t y = sy - y0;
    if (y + (int32_t)f->h <= 0 || y >= h)
        return;
    cv_text_on(120 - text_w(f, t) / 2, y, f, t, frog_c(c), frog_c(bg));
}

/* the start screen: how to play, on a panel over the field */
static void frog_howto(int32_t y0, int32_t h)
{
    static const char *const L[] = {"GET NIGLIO HOME", "", "SELECT: HOP UP / DOWN", "KNOB 4: HOP LEFT / RIGHT",
                                    "DODGE THE CARS", "RIDE THE LOGS, NOT THE WATER", "FILL THE 3 BURROWS",
                                    "30 S A LIFE, 3 LIVES", "", "PLAY: START / PAUSE", "HOME: BACK"};
    const int32_t top = 26, bottom = 226;
    uint32_t i;
    cv_rect(10, top - y0, 220, bottom - top, frog_c(FR_C_HUD));
    cv_rect(10, top - y0, 220, 2, frog_c(FR_C_LINE));
    cv_rect(10, bottom - 2 - y0, 220, 2, frog_c(FR_C_LINE));
    frog_line(y0, h, top + 8, &AF_M, "HARE CROSSING", FR_C_LINE, FR_C_HUD);
    for (i = 0; i < NELEM(L); i++)
        if (L[i][0])
            frog_line(y0, h, top + 32 + (int32_t)i * 15, &AF_S, L[i],
                      i == 9u ? FR_C_LINE : FR_C_TEXT, FR_C_HUD);
}

/* between two levels: the level reached, its bonus, Niglio hopping for joy (the lanes go on behind) */
static void frog_intermezzo(int32_t y0, int32_t h)
{
    char t[24];
    uint32_t el = fm1_ms - frog.level_t, k;
    const int32_t top = 52, bottom = 204;
    cv_rect(20, top - y0, 200, bottom - top, frog_c(FR_C_HUD));
    cv_rect(20, top - y0, 200, 2, frog_c(FR_C_LINE));
    cv_rect(20, bottom - 2 - y0, 200, 2, frog_c(FR_C_LINE));
    str_cpy(t, "LEVEL ", sizeof t);
    fmt_int(t + 6, frog.level);
    frog_line(y0, h, top + 10, &AF_L, t, FR_C_LINE, FR_C_HUD);
    frog_line(y0, h, top + 46, &AF_S, "ALL THREE HOME!  +200", FR_C_TEXT, FR_C_HUD);
    frog_line(y0, h, top + 62, &AF_S, frog.level == 4u ? "LOGS SINK! A FAST LANE!" : frog.level == 7u ?
              "SHORTER LOGS, CLOSER CARS" : frog.level < 7u ? "FASTER, LESS TIME" : "AS HARD AS IT GETS",
              FR_C_TEXT, FR_C_HUD);                     /* (what is new: fr_speed, fr_len, fr_gap, fr_sink) */   /* (Niglio hops below, from
                                                     * top + 84: 15 px up at most from bottom - 6 - 32) */
    for (k = 0; k < 3u; k++) {                          /* three Niglios hopping in turn */
        uint32_t ph = (el / 4u + k * 120u) % 360u;
        int32_t up = ph < 180u ? (int32_t)(ph < 90u ? ph : 180u - ph) / 6 : 0;
        frog_niglio(56 + (int32_t)k * 48, bottom - 6 - FR_CELL - up - y0, 0);
    }
}

/* screen rows y0 .. y0 + h - 1 into one canvas, blitted */
static void frog_band(int32_t y0, int32_t h)
{
    uint32_t r, i;
    char s[24];
    cv_begin(240, (uint32_t)h, frog_c(FR_C_HUD));
    if (y0 == 0) {                                      /* the top line: the score, the level, the time, the lives */
        uint32_t used = frog.state == FR_PAUSE ? frog.paused_t - frog.life_t : fm1_ms - frog.life_t;
        uint32_t left = frog.state == FR_READY || frog.state == FR_OVER || frog.dead_t ? 0u :
                        fr_life_ms() - (uint32_t)clamp((int32_t)used, 0, (int32_t)fr_life_ms());
        str_cpy(s, "SCORE ", sizeof s);
        fmt_int(s + 6, (int32_t)frog.score);
        cv_text_on(4, 2, &AF_S, s, frog_c(FR_C_TEXT), frog_c(FR_C_HUD));
        str_cpy(s, "LV ", sizeof s);
        fmt_int(s + 3, frog.level);
        cv_text_on(96, 2, &AF_S, s, frog_c(FR_C_LINE), frog_c(FR_C_HUD));
        cv_rect(140, 5, (int32_t)(left * 60u / FR_LIFE_MS), 6, frog_c(left < 8000u ? FR_CAR[0] : FR_C_LINE));   /* (the
                                                       * bar's length: of 30 s, so a shorter life a shorter bar) */
        for (i = 0; i < frog.lives; i++)
            cv_rect(210 + (int32_t)i * 9, 4, 6, 8, frog_c(NG_PAL[2]));
    }
    for (r = 0; r < FR_ROWS; r++) {
        int32_t y = FR_TOP + (int32_t)r * FR_CELL - y0, x;
        uint16_t bg = r == 0 ? FR_C_HOME : r <= 2u ? FR_C_WATER : r == 3u || r == 6u ? FR_C_GRASS : FR_C_ROAD;
        if (y + FR_CELL <= 0 || y >= h)
            continue;
        cv_rect(0, y, 240, FR_CELL, frog_c(bg));
        if (r == 0) {                                   /* the burrows */
            for (i = 0; i < 3u; i++) {
                x = FR_X0 + (int32_t)(1u + 2u * i) * FR_CELL;
                cv_rect(x - 3, y + 2, FR_CELL + 6, FR_CELL - 3, frog_c(FR_C_GRASS));
                if (frog.home[i])
                    frog_niglio(x, y, 0);
            }
        } else if (r == 5u) {                           /* the line between the road's two lanes */
            for (x = 4; x < 240; x += 24)
                cv_rect(x, y, 12, 1, frog_c(FR_C_LINE));
        }
        for (i = 0; i < FR_LANES; i++) {                /* the logs, the cars */
            const fr_lane_t *l = &FR_LANE[i];
            int32_t period = (fr_len(i) + fr_gap(i)) * FR_CELL, x0 = (frog.off[i] >> 8) % period;
            uint32_t sink = fr_sink(i);
            if (l->row != r)
                continue;
            for (x = x0 - period; x < 240; x += period) {
                int32_t w = fr_len(i) * FR_CELL;
                if (i < 2u && sink == 2u)               /* (under: its ripples only) */
                    cv_rect(x + 4, y + 15, w - 8, 2, frog_c(0x5DDFu));
                else if (i < 2u) {
                    cv_rect(x, y + 4, w, FR_CELL - 8, frog_c(sink ? 0x4100u : FR_C_LOG));   /* (sinking: darker) */
                    cv_rect(x + 3, y + 10, w - 6, 2, frog_c(0x5180u));
                    cv_rect(x + 3, y + 20, w - 6, 2, frog_c(0x5180u));
                } else
                    frog_car(x, y, w, FR_CAR[i % 4u], l->speed > 0);   /* (a colour a lane) */
            }
        }
        if ((uint32_t)frog.row == r && frog.state != FR_OVER && frog.state != FR_LEVEL && frog.state != FR_READY)
            frog_niglio(frog.x, y, frog.dead_t && (frog.dead_t & 4u));
        if (r == 3u && (frog.state == FR_PAUSE || frog.state == FR_OVER)) {   /* the bank: what PLAY does now */
            const char *m = frog.state == FR_PAUSE ? "PAUSE - PLAY: ON" : "GAME OVER - PLAY: AGAIN";
            cv_text_on(120 - text_w(&AF_S, m) / 2, y + 10, &AF_S, m, frog_c(FR_C_TEXT), frog_c(FR_C_GRASS));
        }
    }
    if (frog.state == FR_READY)
        frog_howto(y0, h);
    else if (frog.state == FR_LEVEL)
        frog_intermezzo(y0, h);
    cv_blit(0, (uint32_t)y0);
}

/* ui_draw (ui.c): the whole screen, every frame while it is on */
static void frog_draw(void)
{
    frog_step();
    frog_band(0, 120);
    frog_band(120, 120);
}
