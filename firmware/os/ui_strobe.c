/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (Blue Hare, 2026-10-18) STROBE, the player's visuals: the ARP pages while it plays (ui_graph.c GR_GEN), in the
 * ocean's place: the user's "aggressive and electronic". The graph strip (the canvas, 240 x 122), redrawn every other
 * UI frame (~33 a second), flat, on black. SAFE TO WATCH (docs/Identity.md rule 7): nothing flashes. No full-screen
 * changes of brightness: the music moves things (a jolt, a ring that thickens, bars that rise, slices that shift),
 * it never blinks the picture.
 * Five scenes: GRID (a neon floor rushing at you, a line a beat, a ring breathing at the horizon, the bass's bars),
 * TUNNEL (squares rushing outwards), STEPS (the bar's sixteen 1/16s, the one playing outlined, each role's hits a
 * row), WARP (a starfield streaming from the centre, faster with the energy), WAVES (lines rippling: the bass slow
 * and wide, the lead fast). The scene and the colour scheme (one of six neon pairs) follow the music's part
 * (player.c part: TECHNO's section, a new one every 32 bars), picked by the algorithm, not by a setting; GREY and MONO
 * keep their screens gray (st_c). It follows the music by the player's roles' names: a drum hit (DRUMS, KICK, HATS)
 * jolts the floor and thickens the ring, a bass note (BASS) raises the bars and swells the waves, anything else (STAB,
 * LEAD, ...) shifts slices of the picture sideways for a few frames. Only the main loop draws it; the audio is
 * untouched. (2026-10-18, the user's "no information is needed, I just want a hypnotic demo") The whole screen, no text:
 * a 240 x 240 picture drawn in two halves through the canvas (st_y0: the half's first row), its state stepped once a
 * frame (st_step) and drawn twice (st_draw: what moves, moves on the second half only, st_last) */
#define ST_NBAR 12u
#define ST_NSTAR 48u
#define ST_NSCENE 5u
enum { SC_GRID, SC_TUNNEL, SC_STEPS, SC_WARP, SC_WAVES };
static const uint16_t ST_SCHEME[6][2] = {          /* {neon, hot}: the algorithm's colour schemes */
    {RGB(0, 230, 255), RGB(255, 40, 200)},   /* cyan, magenta */
    {RGB(120, 255, 40), RGB(255, 230, 0)},   /* acid green, yellow */
    {RGB(255, 150, 0), RGB(255, 40, 40)},    /* amber, red */
    {RGB(120, 170, 255), RGB(235, 245, 255)},   /* ice blue, white */
    {RGB(170, 70, 255), RGB(255, 90, 180)},  /* violet, pink */
    {RGB(255, 60, 30), RGB(255, 200, 60)},   /* red, gold */
};
static struct {
    uint8_t ready, scene, scheme;
    uint32_t rng, t, ph, part;               /* the floor's phase (Q16 of a line's spacing); the part shown */
    int32_t jolt, energy, glitch, thick, swell;   /* px; 0 .. 256; frames of shift; the ring's extra; the waves' */
    int16_t bar[ST_NBAR], btgt[ST_NBAR];     /* the bass bars' heights and where they rise to (px: a few a frame, no jump) */
    int16_t sx[ST_NSTAR], sy[ST_NSTAR], sz[ST_NSTAR];   /* WARP's stars: direction (Q8), distance */
    uint16_t hits[4];                        /* STEPS: the 1/16s each role hit in this bar, a bit each */
    uint32_t seen[8];                        /* the roles' notes counted at the last frame (player.c notes) */
} st;

#define ST_W 240                                    /* the picture: the whole screen */
#define ST_H 240
static int32_t st_y0;                               /* the canvas's first row in the picture */
static uint8_t st_last;                             /* the last half: the state moves */
static uint8_t st_gn;                               /* the frame's shifted slices: rows, height, offset */
static int16_t st_gy[6], st_gt[6], st_go[6];
static uint16_t st_c(uint16_t c) { return ux.mono || ux.bw ? ux_gray(ux_luma(c) * 31u / 255u) : c; }
static uint32_t st_rand(uint32_t n)
{
    st.rng = st.rng * 1664525u + 1013904223u;
    return n ? (st.rng >> 8) % n : 0;
}
static inline void st_px(int32_t x, int32_t y, uint16_t s)
{
    y -= st_y0;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = s;
}
static void st_span(int32_t x0, int32_t x1, int32_t y, uint16_t s)
{
    int32_t x;
    y -= st_y0;
    if ((uint32_t)y >= cv_h)
        return;
    x0 = x0 < 0 ? 0 : x0;
    x1 = x1 > (int32_t)cv_w ? (int32_t)cv_w : x1;
    for (x = x0; x < x1; x++)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = s;
}
static void st_vline(int32_t x, int32_t y0, int32_t y1, uint16_t s)
{
    int32_t y;
    for (y = y0; y <= y1; y++)
        st_px(x, y, s);
}
static void st_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t s)   /* (Bresenham) */
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int32_t e = dx + dy, n = 0;
    for (;;) {
        st_px(x0, y0, s);
        if ((x0 == x1 && y0 == y1) || ++n > 600)
            break;
        if (2 * e >= dy) {
            e += dy;
            x0 += sx;
        }
        if (2 * e <= dx) {
            e += dx;
            y0 += sy;
        }
    }
}
static void st_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t s)   /* an outline */
{
    st_span(x0, x1 + 1, y0, s);
    st_span(x0, x1 + 1, y1, s);
    st_vline(x0, y0, y1, s);
    st_vline(x1, y0, y1, s);
}
static void st_star(uint32_t i)               /* a star from the centre, any direction */
{
    uint32_t a = st_rand(1024u) << 22;
    st.sx[i] = (int16_t)(sine_i(a) >> 7);
    st.sy[i] = (int16_t)(sine_i(a + 0x40000000u) >> 7);
    st.sz[i] = (int16_t)(1 + (int32_t)st_rand(20u));
}
/* the roles' new notes since the last frame, by name */
static void st_listen(void)
{
    const player_t *p = player();
    uint32_t r, n, s = p && p->step ? p->step() % 16u : 0u;
    if (s == 0u && st.hits[0] & 0x8000u)
        st.hits[0] = st.hits[1] = st.hits[2] = st.hits[3] = 0;   /* (a new bar: STEPS starts again) */
    for (r = 0; p && r < p->nroles && r < 8u; r++) {
        const char *nm = p->roles[r];
        uint32_t now = p->notes(r);
        n = now - st.seen[r];
        st.seen[r] = now;
        if (!n || n > 64u)
            continue;
        if (r < 4u)
            st.hits[r] |= (uint16_t)(1u << s);
        if (str_eq(nm, "DRUMS") || str_eq(nm, "KICK") || str_eq(nm, "HATS")) {
            st.jolt = 5;
            st.thick = 3;
            st.energy = st.energy + 50 > 256 ? 256 : st.energy + 50;
        } else if (str_eq(nm, "BASS")) {
            uint32_t k;
            for (k = 0; k < ST_NBAR; k++)
                st.btgt[k] = (int16_t)(st.btgt[k] > 40 ? st.btgt[k] : 14 + (int32_t)st_rand(28u));
            st.swell = 256;
            st.energy = st.energy + 30 > 256 ? 256 : st.energy + 30;
        } else {
            st.glitch = 4;
            st.energy = st.energy + 20 > 256 ? 256 : st.energy + 20;
        }
    }
}
/* the part of the music: a new one picks the scene and the colours (the algorithm's, from the part and the piece) */
static void st_part(void)
{
    const player_t *p = player();
    uint32_t part = p && p->part ? p->part() : 0u, h;
    if (st.ready && part == st.part)
        return;
    st.part = part;
    h = (part * 2654435761u) ^ ((uint32_t)song.g[G_GPRE] * 40503u);
    st.scene = (uint8_t)((part + (h >> 7)) % ST_NSCENE);
    st.scheme = (uint8_t)((h >> 13) % 6u);
}
static void st_grid(uint16_t sn, uint16_t sd, uint16_t sh, int32_t hz, int32_t cx)
{
    uint32_t w = ST_W, h = ST_H, i;
    int32_t k, j;
    for (k = 0; k < 8; k++) {
        int32_t z = ((k << 16) + (int32_t)(st.ph & 0xFFFF)) >> 8;   /* 0 .. 2048: far .. near */
        int32_t yy = hz + (int32_t)((uint32_t)z * (uint32_t)z / 2048u * (uint32_t)(h - (uint32_t)hz) / 2048u) + st.jolt;
        st_span(0, (int32_t)w, yy, k > 5 ? sn : sd);
    }
    for (j = -6; j <= 6; j++)
        st_line(cx + j * 8, hz, cx + j * (int32_t)w / 6, (int32_t)h - 1, j % 3 ? sd : sn);
    st_span(0, (int32_t)w, hz, sh);
    {
        int32_t r = 10 + st.energy * 22 / 256, a, t;
        for (t = 0; t <= st.thick; t++)
            for (a = 0; a < 64; a++) {
                int32_t x = cx + (int32_t)sine_i((uint32_t)a << 26) * (r + t) / 32767;
                int32_t yy = hz - 4 - r + (int32_t)sine_i(((uint32_t)a << 26) + 0x40000000u) * (r + t) / 32767;
                st_px(x, yy, sh);
            }
    }
    for (i = 0; i < ST_NBAR; i++) {
        int32_t bw = (int32_t)w / (int32_t)ST_NBAR, x0 = (int32_t)i * bw + 2, hb = st.bar[i];
        for (k = 0; k < hb; k++)
            st_span(x0, x0 + bw - 4, (int32_t)h - 1 - k, k < 3 ? sh : sn);
    }
}
static void st_tunnel(uint16_t sn, uint16_t sd, uint16_t sh, int32_t cx, int32_t cy)
{
    int32_t k;
    for (k = 0; k < 7; k++) {
        int32_t z = ((k << 16) + (int32_t)(st.ph & 0xFFFF)) >> 8;   /* 0 .. 1792: small .. big */
        int32_t hw = 4 + z * z / 14000, hh = hw * ST_H / ST_W + st.jolt;
        st_rect(cx - hw, cy - hh, cx + hw, cy + hh, k >= 5 ? sh : k >= 3 ? sn : sd);
    }
    st_rect(cx - 3 - st.thick, cy - 2 - st.thick, cx + 3 + st.thick, cy + 2 + st.thick, sh);
}
static void st_steps(uint16_t sn, uint16_t sd, uint16_t sh)
{
    const player_t *p = player();
    uint32_t s = p && p->step ? p->step() % 16u : 0u, i, r;
    int32_t cw = ST_W / 16, top = 40, rh = (ST_H - 2 * top) / 4;
    for (i = 0; i < 16u; i++) {
        int32_t x0 = (int32_t)i * cw + 1, x1 = x0 + cw - 3;
        for (r = 0; r < 4u; r++) {
            int32_t y0 = top + (int32_t)r * rh + 1, y1 = y0 + rh - 4, y;
            if ((st.hits[r] >> i) & 1u)
                for (y = y0 + 1; y < y1; y++)
                    st_span(x0 + 1, x1, y, r == 0u ? sh : sn);
            else
                st_rect(x0, y0, x1, y1, sd);
        }
        if (i == s)                                      /* (the 1/16 playing: outlined, not lit) */
            st_rect(x0 - 1, top - 1, x1 + 1, top + 4 * rh - 2, sh);
    }
    if (st_last)
        st.hits[0] |= (uint16_t)(s == 15u ? 0x8000u : 0u);
}
static void st_warp(uint16_t sn, uint16_t sh, int32_t cx, int32_t cy)
{
    uint32_t i;
    int32_t sp = 1 + st.energy / 48;
    if (!st.sz[0])
        for (i = 0; i < ST_NSTAR; i++)
            st_star(i);
    for (i = 0; i < ST_NSTAR; i++) {
        int32_t d = st.sz[i], x = cx + st.sx[i] * d / 64, y = cy + st.sy[i] * d / 64;
        int32_t x2 = cx + st.sx[i] * (d + sp + 2) / 64, y2 = cy + st.sy[i] * (d + sp + 2) / 64;
        if ((uint32_t)x >= ST_W || (uint32_t)y >= ST_H) {
            if (st_last)
                st_star(i);
            continue;
        }
        st_line(x, y, x2, y2, d > 60 ? sh : sn);
        if (st_last)
            st.sz[i] = (int16_t)(d + sp + d / 20);
    }
}
static void st_waves(uint16_t sn, uint16_t sd, uint16_t sh)
{
    int32_t k, x, h = ST_H;
    for (k = 0; k < 4; k++) {
        int32_t y0 = 30 + k * (h - 40) / 4, amp = 4 + (k == 3 ? st.swell * 14 / 256 : st.energy * 10 / 256), py = y0;
        uint32_t f = k == 3 ? 3u : 6u + (uint32_t)k * 5u;
        for (x = 0; x < ST_W; x += 2) {
            int32_t y = y0 + (int32_t)sine_i((uint32_t)x * f * 2000000u + st.t * (40000000u + (uint32_t)k * 9000000u)) * amp / 32767;
            st_line(x - 2, py, x, y, k == 3 ? sh : k == 0 ? sd : sn);
            py = y;
        }
    }
}
/* a frame's state, once a frame: the part, the music heard, the floor's phase, the slices, what decays */
static void st_step(void)
{
    uint32_t i, k;
    int32_t bpm = song.g[G_BPM] > 20 ? song.g[G_BPM] : 120;
    if (!st.ready) {
        memset(&st, 0, sizeof st);
        st.rng = 0x5770Bu;
        st_part();
        st.ready = 1;
    }
    st_part();
    st_listen();
    st.t++;
    st.ph += (uint32_t)(bpm * 65536 / 60 / 33);        /* (a line a beat: bpm / 60 a second, the frame ~30 ms) */
    st_gn = 0;
    if (st.glitch) {                                     /* the shift: slices of rows moved sideways */
        for (k = 0; k < 6u; k++) {
            st_gy[k] = (int16_t)st_rand(ST_H - 8u);
            st_gt[k] = (int16_t)(2 + st_rand(6u));
            st_go[k] = (int16_t)((int32_t)st_rand(41u) - 20);
        }
        st_gn = 6;
        st.glitch--;
    }
    for (i = 0; i < ST_NBAR; i++) {                      /* the bars rise 3 px a frame to where a bass note sent them, then */
        if (st.bar[i] < st.btgt[i])                       /* fall back as they did (safe to watch: no area lights at once) */
            st.bar[i] = (int16_t)(st.bar[i] + 3 > st.btgt[i] ? st.btgt[i] : st.bar[i] + 3);
        else
            st.bar[i] = (int16_t)(st.bar[i] - st.bar[i] / 6 - (st.bar[i] > 0)), st.btgt[i] = 0;
    }
    if (st.jolt)
        st.jolt--;
    if (st.thick)
        st.thick--;
    st.swell -= st.swell / 10 + (st.swell > 0);
    st.energy -= st.energy / 16 + (st.energy > 0);
}
/* the picture's rows st_y0 .. on the canvas */
static void st_draw(void)
{
    uint32_t w = cv_w, h = cv_h, y, k;
    int32_t hz = ST_H * 2 / 5, cx = ST_W / 2, cy = ST_H / 2;
    uint16_t bg, neon, hot, dim;
    neon = st_c(ST_SCHEME[st.scheme][0]);
    hot = st_c(ST_SCHEME[st.scheme][1]);
    bg = st_c(RGB(0, 0, 0));
    dim = ux_mix(bg, neon, 40);
    for (y = 0; y < h; y++)
        st_span(0, (int32_t)w, (int32_t)y + st_y0, swap16(bg));
    switch (st.scene) {
    case SC_TUNNEL: st_tunnel(swap16(neon), swap16(dim), swap16(hot), cx, cy); break;
    case SC_STEPS: st_steps(swap16(neon), swap16(dim), swap16(hot)); break;
    case SC_WARP: st_warp(swap16(neon), swap16(hot), cx, cy); break;
    case SC_WAVES: st_waves(swap16(neon), swap16(dim), swap16(hot)); break;
    default: st_grid(swap16(neon), swap16(dim), swap16(hot), hz, cx); break;
    }
    cv_bg = bg;
    for (k = 0; k < st_gn; k++) {
        int32_t r, off = st_go[k];
        for (r = st_gy[k]; r < st_gy[k] + st_gt[k]; r++) {
            uint16_t *row;
            int32_t x, cr = r - st_y0;
            if (cr < 0 || cr >= (int32_t)h)
                continue;
            row = &cv_px[(uint32_t)cr * w];
            if (off > 0)                                  /* (copied the way that never overwrites what it reads) */
                for (x = (int32_t)w - 1; x >= off; x--)
                    row[x] = row[x - off];
            else if (off < 0)
                for (x = 0; x < (int32_t)w + off; x++)
                    row[x] = row[x - off];
        }
    }
}
/* half 0 (the top, the frame's step first) or 1 (the bottom) of a frame on the canvas (240 x ST_H / 2) */
static void st_frame_half(uint32_t half)
{
    if (!half)
        st_step();
    st_y0 = (int32_t)half * (ST_H / 2);
    st_last = (uint8_t)(half == 1u);
    st_draw();
}
