/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define FELUCCA_FAVORITES 1
/* Stable engine/preset references: by engine id (core.h EID_*; EID_COUNT: a user preset), whatever the build has. */
typedef struct {
    uint8_t factory[16][32]; /* engine id 0..15, preset 0..255 (the stored layout: [15][10..31] hold settings) */
    uint32_t user, filter;
} favorites_t;
static favorites_t favorites;
/* ids 16 on (1.5: WEST, HOOVER) have no row of their own in the stored layout: their presets 0..7 star bits of a
 * byte BYTE's presets never reach (BYTE has 6; [15][8] engine 16, [15][9] 17; [15][10..31] are settings,
 * settings_persist.c) */
#define FAV_E16 (&favorites.factory[15][8])
static uint8_t *favorite_byte(uint32_t engine, uint32_t preset)
{
    if (engine >= EID_COUNT || preset >= 256u)
        return 0;
    if (engine < 16u)
        return &favorites.factory[engine][preset / 8u];
    return engine <= 17u && preset < 8u ? FAV_E16 + (engine - 16u) : 0;
}
static int favorite_has(uint32_t engine, uint32_t preset)
{
    const uint8_t *b;
    if (engine == EID_COUNT)
        return preset < UP_SLOTS && ((favorites.user >> preset) & 1u);
    b = favorite_byte(engine, preset);
    return b && ((*b >> (preset % 8u)) & 1u);
}
static int favorite_set(uint32_t engine, uint32_t preset, int on)
{
    uint8_t *b = engine == EID_COUNT ? 0 : favorite_byte(engine, preset);
    if (engine == EID_COUNT ? preset >= UP_SLOTS : !b)
        return 0;
    if (favorite_has(engine, preset) == !!on) return 0;
    if (engine == EID_COUNT) {
        if (on) favorites.user |= 1u << preset;
        else favorites.user &= ~(1u << preset);
    } else {
        if (on) *b |= (uint8_t)(1u << (preset % 8u));
        else *b &= (uint8_t)~(1u << (preset % 8u));
    }
    return 1;
}
