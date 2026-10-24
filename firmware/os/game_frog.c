/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-24, the user's "a frogger game that is activated when someone holds seq for one second") HARE CROSSING:
 * get the hare across the road and the river into the five burrows at the top. SEQ held for a second opens it
 * (ui_input.c), HOME or SEQ closes it; the music goes on (MIDI, the loops), the keys play no notes meanwhile.
 *   OCT- / OCT+   a hop left / right
 *   a white key   a hop up (forward)        a black key   a hop down
 * The road's cars run you over; in the river only the logs carry you (the water and the edge of the screen do not).
 * A burrow filled is 50 points (and 10 for every row you got further than before), all five fill the next level: the
 * lanes faster. Each life has 30 seconds (the bar at the bottom); three lives.
 * The screen: 15 rows of 16 px: 0 the score, 1 the burrows, 2..6 the river, 7 the bank, 8..12 the road, 13 the start,
 * 14 the time; drawn in three canvases of five rows (gfx.c's canvas: 240 x 124 at most). Its own colours, but in GREY
 * and MONO their grays (frog_c, as ui_strobe.c st_c). Runs in the UI's frame (main.c), no audio time. */
#define FR_ROWS 15u
#define FR_CELL 16
#define FR_LANES 10u                                    /* rows 2..6 the river, 8..12 the road */
#define FR_LIFE_MS 30000u
enum { FR_C_WATER = 0x1A7Fu, FR_C_ROAD = 0x2104u, FR_C_GRASS = 0x2C64u, FR_C_LOG = 0x8A22u, FR_C_HUD = 0x0000u,
       FR_C_HOME = 0x0180u, FR_C_HARE = 0xFFDEu, FR_C_EAR = 0xFBB7u, FR_C_TEXT = 0xFFFFu, FR_C_LINE = 0xFFE0u };
static const uint16_t FR_CAR[5] = {0xF8A3u, 0xFE60u, 0x07FFu, 0xF81Fu, 0xAFE5u};
/* a lane: its row, speed (px per second, + right), the length of its things (cells), how many, the gap between */
typedef struct { uint8_t row; int16_t speed; uint8_t len, n, gap; } fr_lane_t;
static const fr_lane_t FR_LANE[FR_LANES] = {
    {2, 40, 4, 2, 4},  {3, -55, 3, 3, 2}, {4, 70, 5, 2, 3}, {5, -45, 2, 3, 3}, {6, 60, 3, 3, 2},   /* the logs */
    {8, -70, 2, 2, 5}, {9, 50, 1, 3, 4},  {10, -90, 1, 3, 3}, {11, 60, 2, 2, 5}, {12, -40, 1, 3, 3},   /* the cars */
};
static struct {
    uint8_t on, lives, level, home[5], best_row, dead_t, over;
    int16_t x, row;                                     /* the hare: px (left edge), row */
    int32_t xf;                                         /* (its px << 8, carried by a log) */
    int32_t off[FR_LANES];                              /* each lane's offset, px << 8 */
    uint32_t score, hi, t_last, life_t;
} frog;

static uint16_t frog_c(uint16_t c) { return ux.mono || ux.bw ? ux_gray(ux_luma(c) * 31u / 255u) : c; }

static void frog_start(void)                            /* the hare at the start, a new life */
{
    frog.row = 13;
    frog.x = 7 * FR_CELL;
    frog.xf = frog.x << 8;
    frog.best_row = 13;
    frog.life_t = fm1_ms;
    frog.dead_t = 0;
}
static void frog_open(void)
{
    uint32_t i;
    memset(frog.home, 0, sizeof frog.home);
    for (i = 0; i < FR_LANES; i++)
        frog.off[i] = (int32_t)(i * 37u % 16u) << 12;
    frog.on = 1;
    frog.over = 0;
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
    const fr_lane_t *l = &FR_LANE[i];
    int32_t period = (int32_t)(l->len + l->gap) * FR_CELL, p = x - (frog.off[i] >> 8), k;
    p %= period * (int32_t)l->n;
    if (p < 0)
        p += period * (int32_t)l->n;
    k = p % period;
    return k < (int32_t)l->len * FR_CELL;
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
    if (frog.dead_t || frog.over)
        return;
    frog.x = (int16_t)clamp(frog.x + dx * FR_CELL, 0, 240 - FR_CELL);
    frog.xf = frog.x << 8;
    frog.row = (int16_t)clamp(frog.row + dy, 1, 13);
    if (frog.row < frog.best_row) {                     /* further than before this life: 10 a row */
        frog.score += 10u * (uint32_t)(frog.best_row - frog.row);
        frog.best_row = (uint8_t)frog.row;
    }
}

/* ui_input.c: everything while the game is on. 1 = it took the frame's input */
static int frog_input(uint32_t pressed, uint32_t notes)
{
    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    uint32_t k;
    if (((pressed >> panel.btn[B_HOME]) & 1u) || ((pressed >> panel.btn[B_SEQ]) & 1u)) {
        frog_close();
        return 1;
    }
    if (frog.over) {                                    /* GAME OVER: any key again */
        if (notes || ((pressed >> panel.btn[B_OCTUP]) & 1u))
            frog_open();
        return 1;
    }
    if ((pressed >> panel.btn[B_OCTDN]) & 1u)
        frog_hop(-1, 0);
    if ((pressed >> panel.btn[B_OCTUP]) & 1u)
        frog_hop(1, 0);
    for (k = 0; k < 32u; k++)
        if ((notes >> k) & 1u)
            frog_hop(0, BLACK[(k + 5u) % 12u] ? 1 : -1);   /* (the first key is F: keyboard.c kb_map 53 + k) */
    return 1;
}

static void frog_step(void)                             /* the lanes, the log's ride, the rules: once a frame */
{
    uint32_t i, dt = fm1_ms - frog.t_last, b;
    int32_t sp;
    frog.t_last = fm1_ms;
    dt = dt > 100u ? 100u : dt;
    for (i = 0; i < FR_LANES; i++) {
        sp = FR_LANE[i].speed * (int32_t)(4u + frog.level) / 5;
        frog.off[i] += sp * (int32_t)dt * 256 / 1000;
    }
    if (frog.over)
        return;
    if (frog.dead_t) {                                  /* the flash, then the next life or the end */
        if (!--frog.dead_t) {
            if (frog.lives)
                frog_start();
            else {
                frog.over = 1;
                frog.hi = frog.score > frog.hi ? frog.score : frog.hi;
            }
        }
        return;
    }
    if (fm1_ms - frog.life_t > FR_LIFE_MS) {
        frog_die();
        return;
    }
    for (i = 0; i < FR_LANES; i++) {
        int32_t cx = frog.x + FR_CELL / 2;
        if (FR_LANE[i].row != (uint32_t)frog.row)
            continue;
        if (i < 5u) {                                   /* the river: on a log, carried; else in the water */
            if (!frog_covered(i, cx))
                frog_die();
            else {
                sp = FR_LANE[i].speed * (int32_t)(4u + frog.level) / 5;
                frog.xf += sp * (int32_t)dt * 256 / 1000;
                frog.x = (int16_t)(frog.xf >> 8);
                if (frog.x < -4 || frog.x > 240 - FR_CELL + 4)
                    frog_die();                         /* (off the screen with the log) */
            }
        } else if (frog_covered(i, frog.x + 3) || frog_covered(i, frog.x + FR_CELL - 4)) {
            frog_die();                                 /* a car */
        }
    }
    if (frog.row == 1 && !frog.dead_t) {                /* the top: into a burrow (at 1, 4, 7, 10, 13 cells), or not */
        b = (uint32_t)(frog.x + FR_CELL / 2) / (FR_CELL * 3);
        if (b < 5u && !frog.home[b] && (frog.x + FR_CELL / 2) % (FR_CELL * 3) >= FR_CELL / 2 &&
            (frog.x + FR_CELL / 2) % (FR_CELL * 3) < FR_CELL * 3 - FR_CELL / 2) {
            frog.home[b] = 1;
            frog.score += 50u + (FR_LIFE_MS - (fm1_ms - frog.life_t)) / 1000u;   /* (and a point a second left) */
            for (b = 0; b < 5u && frog.home[b]; b++)
                ;
            if (b == 5u) {                              /* all five: the next level */
                memset(frog.home, 0, sizeof frog.home);
                frog.level++;
                frog.score += 200u;
            }
            frog_start();
        } else
            frog_die();
    }
}

static void frog_hare(int32_t x, int32_t y, uint16_t c)   /* the hare in a cell at (x, y) of the canvas */
{
    cv_rect(x + 3, y + 7, 10, 7, frog_c(c));            /* body */
    cv_rect(x + 5, y + 1, 2, 7, frog_c(c));             /* ears */
    cv_rect(x + 9, y + 1, 2, 7, frog_c(c));
    cv_rect(x + 5, y + 2, 1, 4, frog_c(FR_C_EAR));
    cv_rect(x + 10, y + 2, 1, 4, frog_c(FR_C_EAR));
    cv_rect(x + 5, y + 9, 2, 2, frog_c(0x0000u));       /* eyes */
    cv_rect(x + 9, y + 9, 2, 2, frog_c(0x0000u));
}

static void frog_band(uint32_t r0)                      /* screen rows r0 .. r0 + 4 into one canvas, blitted */
{
    uint32_t r, i;
    char s[24];
    cv_begin(240, 5u * FR_CELL, frog_c(FR_C_GRASS));
    for (r = r0; r < r0 + 5u; r++) {
        int32_t y = (int32_t)(r - r0) * FR_CELL;
        uint16_t bg = r == 0 || r == 14 ? FR_C_HUD : r == 1 ? FR_C_HOME : r >= 2 && r <= 6 ? FR_C_WATER :
                      r >= 8 && r <= 12 ? FR_C_ROAD : FR_C_GRASS;
        cv_rect(0, y, 240, FR_CELL, frog_c(bg));
        if (r == 0) {
            str_cpy(s, "SCORE ", sizeof s);
            fmt_int(s + 6, (int32_t)frog.score);
            cv_text_on(4, y + 3, &AF_S, s, frog_c(FR_C_TEXT), frog_c(bg));
            str_cpy(s, "LV ", sizeof s);
            fmt_int(s + 3, frog.level);
            cv_text_on(110, y + 3, &AF_S, s, frog_c(FR_C_LINE), frog_c(bg));
            str_cpy(s, "HI ", sizeof s);
            fmt_int(s + 3, (int32_t)frog.hi);
            cv_text_on(170, y + 3, &AF_S, s, frog_c(FR_C_TEXT), frog_c(bg));
        } else if (r == 1) {                            /* the burrows */
            for (i = 0; i < 5u; i++) {
                int32_t bx = (int32_t)i * FR_CELL * 3 + FR_CELL;
                cv_rect(bx - 2, y + 1, FR_CELL + 4, FR_CELL - 1, frog_c(FR_C_GRASS));
                if (frog.home[i])
                    frog_hare(bx, y, FR_C_HARE);
            }
        } else if (r == 14) {                           /* the time and the lives */
            uint32_t left = frog.dead_t || frog.over ? 0u : FR_LIFE_MS - clamp(fm1_ms - frog.life_t, 0, FR_LIFE_MS);
            cv_rect(70, y + 5, (int32_t)(left * 160u / FR_LIFE_MS), 6, frog_c(left < 8000u ? FR_CAR[0] : FR_C_LINE));
            for (i = 0; i < frog.lives; i++)
                frog_hare(4 + (int32_t)i * 18, y, FR_C_HARE);
        } else if (r == 8 || r == 12) {                 /* the road's edge lines */
            cv_rect(0, r == 8 ? y : y + FR_CELL - 1, 240, 1, frog_c(FR_C_LINE));
        }
        for (i = 0; i < FR_LANES; i++) {                /* the logs, the cars */
            const fr_lane_t *l = &FR_LANE[i];
            int32_t period = (int32_t)(l->len + l->gap) * FR_CELL, x0 = (frog.off[i] >> 8) % period, x;
            if (l->row != r)
                continue;
            for (x = x0 - period; x < 240; x += period) {
                int32_t w = (int32_t)l->len * FR_CELL;
                if (i < 5u) {
                    cv_rect(x, y + 2, w, FR_CELL - 4, frog_c(FR_C_LOG));
                    cv_rect(x + 2, y + 5, w - 4, 1, frog_c(0x5180u));
                } else {
                    uint16_t c = FR_CAR[i % 5u];
                    cv_rect(x + 2, y + 3, w - 4, FR_CELL - 6, frog_c(c));
                    cv_rect(l->speed > 0 ? x + w - 5 : x + 2, y + 5, 3, 6, frog_c(0xFFFFu));   /* (its windscreen) */
                }
            }
        }
        if ((uint32_t)frog.row == r && !frog.over)
            frog_hare(frog.x, y, frog.dead_t && (frog.dead_t & 4u) ? FR_CAR[0] : FR_C_HARE);
        if (frog.over && r == 7) {
            cv_text_on(70, y + 3, &AF_S, "GAME OVER - A KEY", frog_c(FR_C_TEXT), frog_c(bg));
        }
    }
    cv_blit(0, r0 * FR_CELL);
}

/* ui_draw (ui.c): the whole screen, every frame while it is on */
static void frog_draw(void)
{
    frog_step();
    frog_band(0);
    frog_band(5);
    frog_band(10);
}
