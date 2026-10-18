/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared PER4 layout: each feature updates only its own fields, preserving
 * the other feature's saved preferences when either is built independently.
 * PER1/PER2 are upstream; PER3 added bold; PER4 added favorites; PER5 (Blue Hare, 2026-10-18) the user slots 33 .. 80's
 * stars (favorites.user_hi: 80 user slots), a PER4 imported with none of them.
 * palette: UI_PAL_TAG + index; an old id (below 20, earlier firmware) is migrated on import.
 * bold: no longer used (one font weight); kept as it was saved, unless it holds the HOLD setting (panel.c).
 * zoom: no longer used (the large readout); kept as it was saved, unless it holds the LEDS setting (panel.c).
 * favorites.factory[15][28..31]: CLICK / CLICK LEVEL (1.1; COUNT-IN's bits until 1.4.1), STYLE, MENU's flags, the layers seen (bytes no
 * engine uses; 0 in older settings = every one's default); [15][27]: SCREEN OFF (1.1.5; 0 = 30 MIN, the default);
 * [15][26]: MENU > DISPLAY > HOME (1.3; 0 = SCOPE, 1 TRACKS, ui.c ui_home_view);
 * [15][24]: MENU's 1.2 flags SCOPE MIX, STEP PREVIEW ON, (CHORD ENTRY ADD until 1.4.1) (bits 0-2, ui.c ui_prefs2; 0 = the defaults);
 * [15][10]: MENU > ANIM IDLE (1.2, Discussion #135, ui.c ui_idle: bit 0; 0 = off, ANIM as its bit in MENU's flags says);
 * [15][12]: bit 0 FAV_ROWS_FREED (1.5): the retired engine ids' favourite rows cleared (favorites.c: ids 18 .. 21 use them);
 * [15][11]: PRESETS > LIST's category (1.2, Discussion #90, ui.c ui_lcat: 0 none = ALL / FAV as favorites.filter says);
 * [15][14..23]: the FX layer's key map (1.2, ui_layer.c fx_keys: 16 white keys x 5 bits, perform.c perf_map_of; 0 = the
 * key's default, an unknown code too: kept as saved, a later firmware's effect survives a round trip here).
 * MENU's flags (ui.c PREF_*) are append-only bits whose 0 is the default: BPM LOCK (16) is clear in every older setting
 * = unlocked, LARGE (32) = OFF, RESTORE LAST OFF (128, 1.2) = ON, read as saved (nothing to migrate, so importing twice changes nothing). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
    uint32_t bold;
    struct { uint8_t factory[16][32]; uint32_t user, filter, user_hi[2]; } favorites;
} persist_t;
#define PERSIST_MAGIC 0x50455236u              /* (PER6, 2026-10-18: PER5's layout; a PER5 takes the distribution's
                                                * STYLE once, LED) */

/* Normalize in place; 1 = current, 2 = migrated, 0 = invalid. */
static int settings_import(persist_t *p, int n)
{
    int current = n == (int)sizeof *p && p->magic == PERSIST_MAGIC;
    int old5 = n == (int)sizeof *p && p->magic == 0x50455235u;
    int old4 = n == (int)(sizeof *p - sizeof p->favorites.user_hi) && p->magic == 0x50455234u;
    int old3 = n == (int)(sizeof *p - sizeof p->favorites) && p->magic == 0x50455233u;
    int old2 = n == (int)(sizeof *p - sizeof p->favorites - sizeof p->bold) && p->magic == 0x50455232u;
    int old1 = n == (int)(8u + sizeof(panel_t)) && p->magic == 0x50455231u;
    if (!(current || old5 || old4 || old3 || old2 || old1)) return 0;
    if (old1) {
        panel_t old;
        memcpy(&old, (uint8_t *)p + 8, sizeof old);
        p->panel = old;
        p->lowcut = p->zoom = 0;
    }
    if (old1 || old2) p->bold = 0;
    if (old4) memset(p->favorites.user_hi, 0, sizeof p->favorites.user_hi);
    else if (!current && !old5) memset(&p->favorites, 0, sizeof p->favorites);
    if (!current)                                  /* (2026-10-18) a setting of before PER6: the distribution's STYLE */
        p->favorites.factory[15][29] = (uint8_t)BLUEHARE_DIST_STYLE;
    if (p->favorites.factory[15][29] > 1u && p->favorites.factory[15][29] != 3u)   /* STYLE (ui.c ui_style): 2, the retired
        PIXEL = LINE; 3 LED (2026-10-18); unknown = FLAT */
        p->favorites.factory[15][29] = p->favorites.factory[15][29] == 2u ? 1u : 0u;
    {   /* CLICK, CLICK LEVEL, (COUNT-IN until 1.4.1) (ui.c ui_rec_prefs): two bits each, 3 is no value: the default (0) */
        uint32_t b = p->favorites.factory[15][28], f;
        for (f = 0; f < 3u; f++)
            if (((b >> (2u * f)) & 3u) == 3u)
                b &= ~(3u << (2u * f));
        p->favorites.factory[15][28] = (uint8_t)b;
    }
    if (p->favorites.factory[15][27] > 7u)         /* SCREEN OFF (1.1.5, ui.c SCR_CODE): unknown = 0 (NEVER since 1.1.5.1) */
        p->favorites.factory[15][27] = 0;
    if (p->favorites.factory[15][11] > 8u)         /* PRESETS LIST's category (1.2, ui.c ui_lcat, CAT_*): unknown = none */
        p->favorites.factory[15][11] = 0;
    /* [15][26] HOME (1.3, ui.c ui_home_view): kept as saved; a value this firmware does not know draws SCOPE */
    if (!current && !old5)                        /* (2026-10-18) a setting of before PER5: the distribution's palette once
                                                    * (IDM since then; ICE, the old default, saved with every setting) */
        p->palette = palette_to_stored(BLUEHARE_DIST_PALETTE);
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(palette_from_stored(p->palette));
    settings.magic = SETTINGS_MAGIC;
    settings.palette = palette_from_stored(p->palette);
    settings.lowcut = p->lowcut;
    settings.zoom = p->zoom;
    settings_hold = (uint8_t)hold_from_stored(p->bold);
    settings_leds = (uint8_t)leds_from_stored(p->zoom);
#ifdef BLUEHARE_FAVORITES
    memcpy(&favorites, &p->favorites, sizeof favorites);
    favorites.filter = favorites.filter == 1u;
    if (!(favorites.factory[15][12] & FAV_ROWS_FREED)) {   /* the retired ids' rows: their old stars cleared, once */
        uint32_t r;                                          /* (ids 18 .. 21 star there now: favorites.c) */
        for (r = 0; r < 4u; r++)
            memset(favorites.factory[FAV_ROW18[r]], 0, sizeof favorites.factory[0]);
        favorites.factory[15][12] |= FAV_ROWS_FREED;
    }
#endif
    if (p->panel.magic == PANEL_MAGIC) panel = p->panel;
    return current ? 1 : 2;
}

/* Start with the last imported/saved object, including fields owned by a
 * feature absent from this build. No save-on-boot or extra flash writes. */
static void settings_export(persist_t *p)
{
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(settings.palette);
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom = leds_to_stored(settings.zoom, settings_leds);
    p->panel = panel;
    p->bold = hold_to_stored(p->bold, settings_hold);
#ifdef BLUEHARE_FAVORITES
    memcpy(&p->favorites, &favorites, sizeof favorites);
#endif
}
