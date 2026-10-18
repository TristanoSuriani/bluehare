/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (Blue Hare, 2026-10-18) STROBE: the ARP pages while the player plays (ui_graph.c GR_GEN), in the ocean's place: the
 * user's "aggressive and electronic". The graph strip (the canvas, 240 x 122), redrawn every other UI frame (~33 a
 * second), flat: black, a neon floor grid rushing at you (a line a beat, at the tempo, to a vanishing point at the
 * centre), a ring at the centre breathing with the energy. It follows the music by the player's roles' names
 * (player.c): a drum hit (DRUMS, KICK, HATS) strobes the whole strip for a frame and jolts the floor; a bass note
 * (BASS) raises fat bars from the floor that fall back; anything else (STAB, LEAD, ...) tears the picture into
 * glitch slices for a few frames. The palette's THEME and ACCENT are the neon; GREY and MONO keep their screens
 * gray (st_c). Only the main loop draws it; the audio is untouched. The player's two status lines ride on top */
#define ST_NBAR 12u
static struct {
    uint8_t ready;
    uint32_t rng, t, ph;                    /* the floor's phase (Q16 of a line's spacing) */
    int32_t strobe, jolt, energy, glitch;   /* frames of strobe left; the floor's jolt (px); 0 .. 256; frames of tear */
    int16_t bar[ST_NBAR];                   /* the bass bars' heights (px) */
    uint32_t seen[8];                       /* the roles' notes counted at the last frame (player.c notes) */
} st;

static uint16_t st_c(uint16_t c) { return ux.mono || ux.bw ? ux_gray(ux_luma(c) * 31u / 255u) : c; }
static uint32_t st_rand(uint32_t n)
{
    st.rng = st.rng * 1664525u + 1013904223u;
    return n ? (st.rng >> 8) % n : 0;
}
static inline void st_px(int32_t x, int32_t y, uint16_t s)
{
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = s;
}
static void st_span(int32_t x0, int32_t x1, int32_t y, uint16_t s)
{
    int32_t x;
    if ((uint32_t)y >= cv_h)
        return;
    x0 = x0 < 0 ? 0 : x0;
    x1 = x1 > (int32_t)cv_w ? (int32_t)cv_w : x1;
    for (x = x0; x < x1; x++)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = s;
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
/* the roles' new notes since the last frame, by name: drums strobe, bass raises bars, the rest tear */
static void st_listen(void)
{
    const player_t *p = player();
    uint32_t r, n;
    for (r = 0; p && r < p->nroles && r < 8u; r++) {
        const char *nm = p->roles[r];
        uint32_t now = p->notes(r);
        n = now - st.seen[r];
        st.seen[r] = now;
        if (!n || n > 64u)
            continue;
        if (str_eq(nm, "DRUMS") || str_eq(nm, "KICK") || str_eq(nm, "HATS")) {
            st.strobe = 1;
            st.jolt = 6;
            st.energy = st.energy + 60 > 256 ? 256 : st.energy + 60;
        } else if (str_eq(nm, "BASS")) {
            uint32_t k;
            for (k = 0; k < ST_NBAR; k++)
                st.bar[k] = (int16_t)(st.bar[k] > 50 ? st.bar[k] : 18 + (int32_t)st_rand(34u));
            st.energy = st.energy + 30 > 256 ? 256 : st.energy + 30;
        } else {
            st.glitch = 4;
            st.energy = st.energy + 20 > 256 ? 256 : st.energy + 20;
        }
    }
}
/* one frame on the canvas (w x h), the status lines on top */
static void st_frame(void)
{
    uint32_t w = cv_w, h = cv_h, y, i;
    int32_t hz = (int32_t)h * 2 / 5, cx = (int32_t)w / 2, bpm = song.g[G_BPM] > 20 ? song.g[G_BPM] : 120;
    uint16_t bg, neon, hot, dim;
    if (!st.ready) {
        memset(&st, 0, sizeof st);
        st.rng = 0x5770Bu;
        st.ready = 1;
    }
    st_listen();
    st.t++;
    neon = st_c(ux.theme);
    hot = st_c(ux.accent);
    dim = ux_mix(st_c(RGB(0, 0, 0)), neon, 40);
    bg = st.strobe ? neon : st_c(RGB(0, 0, 0));
    for (y = 0; y < h; y++)
        st_span(0, (int32_t)w, (int32_t)y, swap16(bg));
    if (!st.strobe) {
        uint16_t sn = swap16(neon), sd = swap16(dim), sh = swap16(hot);
        int32_t k, j;
        /* the floor: lines rushing towards you, a line a beat (the frame ~30 ms: bpm / 60 lines a second) */
        st.ph += (uint32_t)(bpm * 65536 / 60 / 33);
        for (k = 0; k < 8; k++) {
            int32_t z = ((k << 16) + (int32_t)(st.ph & 0xFFFF)) >> 8;   /* 0 .. 2048: far .. near */
            int32_t yy = hz + (int32_t)((uint32_t)z * (uint32_t)z / 2048u * (uint32_t)(h - (uint32_t)hz) / 2048u) + st.jolt;
            st_span(0, (int32_t)w, yy, k > 5 ? sn : sd);
        }
        for (j = -6; j <= 6; j++)                         /* and running to the vanishing point */
            st_line(cx + j * 8, hz, cx + j * (int32_t)w / 6, (int32_t)h - 1, j % 3 ? sd : sn);
        st_span(0, (int32_t)w, hz, sh);                   /* the horizon */
        {                                                 /* the ring at the centre, breathing */
            int32_t r = 10 + st.energy * 22 / 256, a;
            for (a = 0; a < 64; a++) {
                int32_t x = cx + (int32_t)sine_i((uint32_t)a << 26) * r / 32767;
                int32_t yy = hz - 4 - r + (int32_t)sine_i(((uint32_t)a << 26) + 0x40000000u) * r / 32767;
                st_px(x, yy, sh);
                st_px(x + 1, yy, sh);
            }
        }
        for (i = 0; i < ST_NBAR; i++) {                   /* the bass bars, rising from the floor */
            int32_t bw = (int32_t)w / (int32_t)ST_NBAR, x0 = (int32_t)i * bw + 2, hb = st.bar[i];
            for (k = 0; k < hb; k++)
                st_span(x0, x0 + bw - 4, (int32_t)h - 1 - k, k < 3 ? sh : sn);
            st.bar[i] = (int16_t)(hb - hb / 6 - (hb > 0));
        }
        if (st.glitch) {                                  /* the tear: slices of rows shifted sideways */
            for (k = 0; k < 6; k++) {
                int32_t y0 = (int32_t)st_rand(h - 8u), th = 2 + (int32_t)st_rand(6u), off = (int32_t)st_rand(41u) - 20, r;
                for (r = y0; r < y0 + th && r < (int32_t)h; r++) {
                    uint16_t *row = &cv_px[(uint32_t)r * w];
                    int32_t x;
                    if (off > 0)                          /* (copied the way that never overwrites what it reads) */
                        for (x = (int32_t)w - 1; x >= off; x--)
                            row[x] = row[x - off];
                    else if (off < 0)
                        for (x = 0; x < (int32_t)w + off; x++)
                            row[x] = row[x - off];
                }
            }
            st.glitch--;
        }
    }
    st.strobe = 0;
    if (st.jolt)
        st.jolt--;
    st.energy -= st.energy / 16 + (st.energy > 0);
    {                                                     /* the player's status lines, small, on top */
        char a[8], b[8];
        player()->status(a, b);
        cv_bg = bg;
        cv_text(cv_text(10, 4, &AF_S, a, st_c(ux.text)) + 10, 4, &AF_S, b, st_c(ux.mid));
    }
}
