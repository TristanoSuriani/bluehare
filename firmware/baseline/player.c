/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (Blue Hare, 2026-10-17: the second review's I) A PLAYER: something that plays the layers on its own (a PLUGIN
 * component, firmware/components.txt), GEN the first. What the rest of Blue Hare knows of one is this interface: the
 * audio side calls block every block and gives it the keys while it plays; the OS starts and stops it (ARP), loads its
 * pieces (the knob whose global id is piece_id), shows its two status lines and follows its roles' notes (GEN's ocean:
 * by the roles' names). A new generator is a file that fills a player_t, a PLUGIN line in the registry and its entry
 * in PLAYERS below, beside GEN or in its place. Included by seq.c after the players' own files */
typedef struct {
    const char *name;                         /* "GEN" */
    uint8_t nroles;                           /* the layers it plays, from track 1 */
    const char *const *roles;                 /* their names ("PAD", "BASS", "BELL", ...: the ocean follows them) */
    uint8_t piece_id;                         /* the global that picks its piece (G_GPRE), 0xFF none */
    uint32_t npieces;
    void (*block)(uint32_t n);                /* (the audio ISR) every block: a start or stop asked for, its steps */
    void (*request)(int start);               /* (the main loop) start or stop at the next block */
    int (*playing)(void);
    int (*key_on)(uint32_t note, uint32_t vel);   /* a key while it plays: 1 taken (GEN: PLAY ALONG) */
    int (*key_off)(uint32_t note);
    void (*load)(uint32_t piece);             /* its piece: the sounds (os_load_sound), the key, the tempo, the FX */
    const char *(*piece_name)(uint32_t piece);
    void (*status)(char *a, char *b);         /* two short lines (8 bytes each): GEN the chord, BAR n */
    uint32_t (*notes)(uint32_t role);         /* notes the role played since the start */
} player_t;

/* ---- GEN (gen.c) as a player */
static const char *const GEN_ROLE_NAMES[GEN_ROLES] = {"PAD", "BASS", "BELL", "PULSE"};
static void gen_p_request(int start) { gen.req = start ? 1u : 2u; }
static int gen_p_playing(void) { return gen.on; }
static const char *gen_p_piece_name(uint32_t k) { return GEN_PRESETS[k % GEN_NPRESETS].name; }
static void gen_p_status(char *a, char *b)
{
    gen_chord_name(a);
    str_cpy(b, "BAR ", 8);
    fmt_int(b + 4, (int32_t)(gen.s.bar % 1000u) + 1);
}
static uint32_t gen_p_notes(uint32_t role) { return role < GEN_ROLES ? gen.s.played[role] : 0u; }
static const player_t GEN_PLAYER = {
    "GEN", GEN_ROLES, GEN_ROLE_NAMES, G_GPRE, GEN_NPRESETS, gen_block, gen_p_request, gen_p_playing, gen_play,
    gen_play_off, gen_load_piece, gen_p_piece_name, gen_p_status, gen_p_notes,
};

/* the players this build has (the distribution's PLUGIN components); the first is ARP's */
static const player_t *const PLAYERS[] = {
#if BLUEHARE_PLUGIN_GEN
    &GEN_PLAYER,
#endif
    0,
};
static const player_t *player(void) { return PLAYERS[0]; }
static int player_playing(void) { return player() && player()->playing(); }
static int player_key_on(uint32_t note, uint32_t vel) { return player() && player()->key_on(note, vel); }
static int player_key_off(uint32_t note) { return player() && player()->key_off(note); }
/* the role of the active player named name: its index, -1 none */
static int32_t player_role(const char *name)
{
    uint32_t r;
    for (r = 0; player() && r < player()->nroles; r++)
        if (str_eq(player()->roles[r], name))
            return (int32_t)r;
    return -1;
}
