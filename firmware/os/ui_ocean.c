/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (Blue Hare, 2026-10-17) THE OCEAN: GEN's pages show it while GEN plays (ui_graph.c GR_GEN), after the "ocean
 * sanctuary" of Tristano's generative proof of concept (docs/excerpt_ocean_gen.html), adapted to the FM-1: the graph
 * strip (240 x 122, the canvas: no framebuffer of its own), redrawn every other UI frame as HOME's oscilloscope is
 * (~33 a second); no blur, no transparency: a deep-sea gradient, three slanted light rays, starfish on the sand, a
 * shoal of fish and bubbles, drawn flat. It follows the music: a bell lets bubbles rise from the sand, a bass note sends
 * the crowned king fish across, a breath of the pad brightens the rays, a pulse hit or a swell of WAVES flicks the
 * shoal's tails. Only the main loop draws it; the audio is untouched. The chord and the bar ride on top, small. */
#define OC_NFISH 8u
#define OC_NBUB 20u
#define OC_NSTAR 4u
typedef struct { int32_t x, y, vx; uint8_t size, kind, tail, live; } oc_fish_t;   /* x, vx: 1/16 px */
typedef struct { int32_t x, y, vy; uint8_t r, live; } oc_bub_t;                     /* y, vy: 1/16 px */
static struct {
    uint8_t ready;
    uint32_t rng, t;
    oc_fish_t fish[OC_NFISH], king;
    oc_bub_t bub[OC_NBUB];
    int16_t sx[OC_NSTAR];
    uint8_t ss[OC_NSTAR];
    int32_t glow, flick;                     /* the rays' brightness (0..256), the tails' flick (0..16) */
    uint32_t seen[GEN_ROLES];                /* GEN's notes counted at the last frame (gen.s.played) */
} oc;

/* a colour of the ocean in the palette's terms: GREY and MONO keep their screens neutral (its luma, gray) */
static uint16_t oc_c(uint16_t c)
{
    return ux.mono || ux.bw ? ux_gray(ux_luma(c) * 31u / 255u) : c;
}
static uint32_t oc_rand(uint32_t n)
{
    oc.rng = oc.rng * 1664525u + 1013904223u;
    return n ? (oc.rng >> 8) % n : 0;
}
static void oc_fish_new(oc_fish_t *f, uint32_t w, uint32_t h, int king)
{
    int left = (int)oc_rand(2u);
    f->size = (uint8_t)(king ? 13u : 5u + oc_rand(6u));
    f->kind = (uint8_t)(king ? 2u : oc_rand(2u));
    f->x = left ? -(int32_t)(f->size * 3u) * 16 : (int32_t)(w + f->size * 3u) * 16;
    f->y = 18 + (int32_t)oc_rand(h > 50u ? h - 50u : 1u);
    f->vx = (left ? 1 : -1) * (king ? 14 : 10 + (int32_t)oc_rand(20u));
    f->tail = (uint8_t)oc_rand(64u);
    f->live = 1;
}
static void oc_init(uint32_t w, uint32_t h)
{
    uint32_t i;
    memset(&oc, 0, sizeof oc);
    oc.rng = 0x0CEA1u;
    for (i = 0; i < OC_NFISH; i++) {
        oc_fish_new(&oc.fish[i], w, h, 0);
        oc.fish[i].x = (int32_t)oc_rand(w) * 16;      /* (the shoal already there) */
    }
    for (i = 0; i < OC_NSTAR; i++) {
        oc.sx[i] = (int16_t)(20u + i * (w - 40u) / (OC_NSTAR - 1u) + oc_rand(16u) - 8u);
        oc.ss[i] = (uint8_t)(4u + oc_rand(3u));
    }
    for (i = 0; i < 8u; i++) {                         /* a few bubbles already rising */
        oc.bub[i].x = (int32_t)oc_rand(w);
        oc.bub[i].y = (int32_t)oc_rand(h) * 16;
        oc.bub[i].vy = 6 + (int32_t)oc_rand(10u);
        oc.bub[i].r = (uint8_t)(1u + oc_rand(3u));
        oc.bub[i].live = 1;
    }
    oc.ready = 1;
}
static void oc_bubble(int32_t x, uint32_t h)
{
    uint32_t i;
    for (i = 0; i < OC_NBUB; i++)
        if (!oc.bub[i].live) {
            oc.bub[i].x = x + (int32_t)oc_rand(9u) - 4;
            oc.bub[i].y = (int32_t)(h - 10u) * 16;
            oc.bub[i].vy = 10 + (int32_t)oc_rand(14u);
            oc.bub[i].r = (uint8_t)(1u + oc_rand(3u));
            oc.bub[i].live = 1;
            return;
        }
}
/* the music since the last frame: GEN's notes, role by role */
static void oc_listen(uint32_t w, uint32_t h)
{
    uint32_t r, n;
    for (r = 0; r < GEN_ROLES; r++) {
        n = gen.s.played[r] - oc.seen[r];
        oc.seen[r] = gen.s.played[r];
        if (!n || n > 64u)                               /* (a restart: counted from now) */
            continue;
        if (r == GR_BELL) {                              /* bells: bubbles from the sand, where a starfish lies */
            while (n--) {
                uint32_t k = oc_rand(OC_NSTAR);
                oc_bubble(oc.sx[k], h);
                oc_bubble(oc.sx[k], h);
            }
        } else if (r == GR_BASS) {                       /* the bass: the king fish crosses, if he is not already */
            if (!oc.king.live)
                oc_fish_new(&oc.king, w, h, 1);
        } else if (r == GR_PAD) {                        /* a breath: the light grows */
            oc.glow = oc.glow + 90 > 256 ? 256 : oc.glow + 90;
        } else {                                         /* a hit, a swell: the shoal's tails flick */
            oc.flick = 16;
        }
    }
}
static inline void oc_px(int32_t x, int32_t y, uint16_t s)   /* (s: already byte-swapped) */
{
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = s;
}
static void oc_span(int32_t x0, int32_t x1, int32_t y, uint16_t s)
{
    int32_t x;
    if ((uint32_t)y >= cv_h)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 > (int32_t)cv_w)
        x1 = (int32_t)cv_w;
    for (x = x0; x < x1; x++)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = s;
}
static uint32_t oc_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 14;
    while (b) {
        if ((r + b) * (r + b) <= v)
            r += b;
        b >>= 1;
    }
    return r;
}
static void oc_draw_fish(const oc_fish_t *f)
{
    static const uint16_t BODY[3] = {RGB(0, 230, 245), RGB(168, 85, 247), RGB(255, 205, 40)};
    int32_t cx = f->x >> 4, cy = f->y, a = f->size, b = f->size * 11 / 20 + 1, dir = f->vx < 0 ? -1 : 1, dy;
    int32_t sway = (int32_t)sine_i((uint32_t)f->tail << 26) * (a / 2 + oc.flick / 4) / 32767;
    uint16_t c = swap16(oc_c(BODY[f->kind % 3u])), eye = swap16(oc_c(RGB(255, 255, 255))), pupil = swap16(oc_c(RGB(0, 0, 0)));
    for (dy = -b; dy <= b; dy++) {                       /* the body: an ellipse, a span a row */
        int32_t hw = a * (int32_t)oc_isqrt((uint32_t)(b * b - dy * dy)) / b;
        oc_span(cx - hw, cx + hw + 1, cy + dy, c);
    }
    {                                                    /* the tail: a triangle behind, its tips swaying */
        int32_t x, x0 = cx - dir * a, len = a * 3 / 5 + 1;
        for (x = 0; x <= len; x++) {
            int32_t half = (a / 2) * x / len, mid = sway * x / len / 3;
            int32_t px = x0 - dir * x;
            int32_t y;
            for (y = -half; y <= half; y++)
                oc_px(px, cy + y + mid, c);
        }
    }
    oc_px(cx + dir * a / 2, cy - b / 3, eye);            /* the eye */
    oc_px(cx + dir * a / 2 + dir, cy - b / 3, eye);
    oc_px(cx + dir * (a / 2 + 1), cy - b / 3 + 1, pupil);
    if (f->kind == 2u) {                                 /* the king: his crown, three points */
        int32_t k, y;
        for (k = -1; k <= 1; k++)
            for (y = 0; y < 5 - (k != 0); y++)
                oc_span(cx + k * 4 - (4 - y) / 2, cx + k * 4 + (4 - y) / 2 + 1, cy - b - 1 - y, c);
        oc_px(cx, cy - b - 3, swap16(oc_c(RGB(239, 68, 68))));   /* (a jewel) */
    }
}
/* one frame of the ocean on the canvas (w x h), the chord and bar on top */
static void oc_frame(void)
{
    uint32_t w = cv_w, h = cv_h, y, i;
    if (!oc.ready)
        oc_init(w, h);
    oc_listen(w, h);
    oc.t++;
    for (y = 0; y < h; y++) {                            /* the deep: blue at the top, near black at the sand */
        uint32_t f = y * 256u / h;
        uint16_t base = oc_c(RGB(8 - 6 * f / 256, 90 - 80 * f / 256, 140 - 115 * f / 256));
        uint16_t lit = ux_mix(base, oc_c(RGB(160, 220, 255)), (int32_t)(6 + oc.glow * 10 / 256));
        uint16_t sb = swap16(base), sl = swap16(lit);
        int32_t k;
        oc_span(0, (int32_t)w, (int32_t)y, sb);
        for (k = 0; k < 3; k++) {                        /* three rays, slanting right as they go down */
            int32_t x0 = 30 + k * 80 + (int32_t)y * 2 / 5 + (int32_t)(sine_i(oc.t * 3000000u + (uint32_t)k * 900000000u) >> 12);
            oc_span(x0, x0 + 18 - k * 3, (int32_t)y, sl);
        }
    }
    oc_span(0, (int32_t)w, (int32_t)h - 4, swap16(oc_c(RGB(40, 34, 30))));   /* the sand */
    oc_span(0, (int32_t)w, (int32_t)h - 3, swap16(oc_c(RGB(48, 40, 34))));
    for (i = 0; i < OC_NSTAR; i++) {                     /* starfish, glowing slowly in turn */
        int32_t s = oc.ss[i], cx = oc.sx[i], cy = (int32_t)h - 7, k;
        uint32_t gl = (uint32_t)(sine_i(oc.t * 20000000u + i * 1100000000u) + 32768) >> 9;   /* 0..127 */
        uint16_t c = swap16(ux_mix(oc_c(RGB(120, 30, 50)), oc_c(RGB(244, 63, 94)), (int32_t)(40 + gl * 60 / 128)));
        for (k = 0; k < 5; k++) {
            int32_t dx = (int32_t)sine_i((uint32_t)k * 858993459u) * s / 32767;
            int32_t dy = -(int32_t)sine_i((uint32_t)k * 858993459u + 0x40000000u) * s / 32767;
            int32_t j;
            for (j = 0; j <= s; j++) {
                oc_px(cx + dx * j / s, cy + dy * j / s, c);
                oc_px(cx + dx * j / s + 1, cy + dy * j / s, c);
            }
        }
    }
    for (i = 0; i < OC_NBUB; i++) {                      /* bubbles: rings, rising, a little sideways */
        oc_bub_t *b = &oc.bub[i];
        int32_t cx, cy, r;
        uint16_t c = swap16(oc_c(RGB(200, 235, 255)));
        if (!b->live)
            continue;
        b->y -= b->vy;
        if (b->y < -64) {
            b->live = 0;
            continue;
        }
        cx = b->x + (int32_t)(sine_i((uint32_t)(b->y * 2000000)) >> 14);
        cy = b->y >> 4;
        r = b->r;
        oc_px(cx - r, cy, c);
        oc_px(cx + r, cy, c);
        oc_px(cx, cy - r, c);
        oc_px(cx, cy + r, c);
        if (r > 1) {
            oc_px(cx - r + 1, cy - r + 1, c);
            oc_px(cx + r - 1, cy - r + 1, c);
            oc_px(cx - r + 1, cy + r - 1, c);
            oc_px(cx + r - 1, cy + r - 1, c);
        }
    }
    for (i = 0; i < OC_NFISH; i++) {                     /* the shoal */
        oc_fish_t *f = &oc.fish[i];
        f->x += f->vx;
        f->y += (int32_t)(sine_i((uint32_t)f->x * 400000u) > 30000) - (int32_t)(sine_i((uint32_t)f->x * 400000u) < -30000);
        f->tail = (uint8_t)(f->tail + 3u + (uint32_t)oc.flick / 4u);
        if ((f->vx > 0 && (f->x >> 4) > (int32_t)w + f->size * 3) || (f->vx < 0 && (f->x >> 4) < -(int32_t)f->size * 3))
            oc_fish_new(f, w, h, 0);
        f->y = clamp(f->y, 14, (int32_t)h - 16);
        oc_draw_fish(f);
    }
    if (oc.king.live) {                                  /* the king, once across */
        oc_fish_t *k = &oc.king;
        k->x += k->vx;
        k->tail = (uint8_t)(k->tail + 2u);
        if ((k->vx > 0 && (k->x >> 4) > (int32_t)w + 40) || (k->vx < 0 && (k->x >> 4) < -40))
            k->live = 0;
        else
            oc_draw_fish(k);
    }
    oc.glow -= oc.glow / 24 + (oc.glow > 0);
    if (oc.flick)
        oc.flick--;
    {                                                    /* the chord and the bar, small, on the deep */
        char ch[8], b[16];
        gen_chord_name(ch);
        str_cpy(b, "BAR ", sizeof b);
        fmt_int(b + 4, (int32_t)(gen.s.bar % 1000u) + 1);
        cv_bg = oc_c(RGB(8, 90, 140));
        cv_text(cv_text(10, 4, &AF_S, ch, oc_c(RGB(235, 248, 255))) + 10, 4, &AF_S, b, oc_c(RGB(150, 200, 230)));
    }
}
