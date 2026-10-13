/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md */
#define FELUCCA_FAVORITES 1
/* Stable engine/preset references: by engine id (core.h EID_*; EID_COUNT: a user preset), whatever the build has. */
typedef struct {
    uint8_t factory[16][32]; /* engine id 0..15, preset 0..255 (the stored layout: [15][10..31] hold settings) */
    uint32_t user, filter;
} favorites_t;
static favorites_t favorites;
/* ids 16 and 17 (1.5: WEST, HOOVER) have no row of their own in the stored layout: their presets 0..7 star bits of a
 * byte BYTE's presets never reach (BYTE has 6; [15][8] id 16, [15][9] 17; [15][10..31] are settings,
 * settings_persist.c). Ids 18 .. 21 take the rows of the retired ids 1, 4, 8, 13 (whose old stars an import clears
 * once: settings_persist.c FAV_ROWS_FREED), their first halves (presets 0 .. 127); ids 22 .. 25 the second halves of
 * the same rows (presets 0 .. 127); ids 26 .. 36 the last 8 bytes of the rows of ids 0 .. 14 still in the layout
 * (FAV_ROW26: presets 0 .. 63; those rows' own engines keep presets 0 .. 127 since ids 36 .., more than any has). WEST and HOOVER
 * (16, 17) star presets 8 .. 63 in the last two of those tails (their 0 .. 7 stay in [15][8], [15][9]). (Fel-Tracker)
 * 35 (VECTOR) takes the tail of 27, retired (ACID: its stars of before show on VECTOR's first presets); ids 36 .. 46
 * bytes 16 .. 23 of the same rows as 26 .. (presets 0 .. 63), ids 47 .. 57 their bytes 8 .. 15, whose own engines
 * keep presets 0 .. 63 (bytes 8 .. 23 were their presets 64 .. 191: none has). An id past 57 needs a new settings
 * layout */
#define FAV_E16 (&favorites.factory[15][8])
#define FAV_ROWS_FREED 1u       /* [15][12] bit 0: settings_persist.c */
static const uint8_t FAV_ROW18[4] = {1, 4, 8, 13};
static const uint8_t FAV_ROW26[11] = {0, 2, 3, 5, 6, 7, 9, 10, 11, 12, 14};
typedef char fav_ids_fit[EID_COUNT <= 58u && (EID_COUNT <= 35u || EID_RETIRED(27u)) ? 1 : -1];   /* (ids 26 .. 34: FAV_ROW26[0 .. 8], 35
                                                                            * 27's; [9], [10]: 16, 17) */
static uint8_t *favorite_byte(uint32_t engine, uint32_t preset)
{
    if (engine >= EID_COUNT || preset >= 256u || EID_RETIRED(engine))
        return 0;
    if (engine >= 47u)
        return preset < 64u ? &favorites.factory[FAV_ROW26[engine - 47u]][8u + preset / 8u] : 0;
    if (engine >= 36u)
        return preset < 64u ? &favorites.factory[FAV_ROW26[engine - 36u]][16u + preset / 8u] : 0;
    if (engine >= 26u)
        return preset < 64u ? &favorites.factory[FAV_ROW26[(engine == 35u ? 27u : engine) - 26u]][24u + preset / 8u] : 0;
    if (engine < 16u)
        return engine == 15u || preset < 64u ? &favorites.factory[engine][preset / 8u] : 0;
    if (engine >= 22u)
        return preset < 128u ? &favorites.factory[FAV_ROW18[engine - 22u]][16u + preset / 8u] : 0;
    if (engine >= 18u)
        return preset < 128u ? &favorites.factory[FAV_ROW18[engine - 18u]][preset / 8u] : 0;
    if (preset < 8u)
        return FAV_E16 + (engine - 16u);              /* WEST, HOOVER: 0 .. 7 their bytes in [15]; 8 .. 63 a tail */
    return preset < 64u ? &favorites.factory[FAV_ROW26[9u + engine - 16u]][24u + preset / 8u] : 0;
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
