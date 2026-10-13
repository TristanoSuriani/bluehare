/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md */
/* Firmware save flows against NOR flash: deferred settings, retries, and failed-save rollback. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static uint32_t irq_start_race, irq_races;
static void fm1_irq_off(void)
{
    if (irq_start_race) {                         /* audio serviced PLAY immediately before main masks it */
        irq_start_race = 0;
        events_block(CTL);
        irq_races++;
    }
}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }   /* (MENU > SCREEN OFF: lcd.c) */
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define FELUCCA_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int erase_error, fail_after = -1;
static uint32_t erases, as_erases;               /* (as_erases: the autosave's two sectors, 0xE5000 / 0xE6000) */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    erases++;
    as_erases += off >= 0xE5000u && off < 0xE7000u;
    if (erase_error) return -8;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0) return -9;
    if (fail_after > 0) fail_after--;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

enum { ED_UI_STATE = 34, ED_UI_SET, ED_UI_PALETTES, ED_FAV_GET, ED_FAV_SET };
static uint8_t ed_rep[64];                        /* the reply's bytes (MENU_SET) */
static uint32_t ed_repn;
static void ed_b(uint32_t n) { if (ed_repn < sizeof ed_rep) ed_rep[ed_repn++] = (uint8_t)(n & 127u); }
static void ed_v(int32_t n) { ed_b((uint32_t)(n + 8192)); ed_b((uint32_t)(n + 8192) >> 7); }
static int32_t ed_rv(const uint8_t *a) { return (int32_t)(a[0] | (uint32_t)a[1] << 7) - 8192; }
static void ed_str(const char *s, uint32_t n) { (void)s; (void)n; }
#include "../firmware/src/editor_preferences.c"
#include "../firmware/src/editor_menu.c"

static int check(const char *what, int ok)
{
    printf("persist: %-66s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);
    tk_defaults();
    memset(&proj_good, 0, sizeof proj_good);
    proj_name[0] = 0;
    proj_cur = PROJ_NO_SLOT;
    memset(up_bank, 0, sizeof up_bank);
    memset(&persist_saved, 0, sizeof persist_saved);
    memset(&settings, 0, sizeof settings);
    memset(&ui, 0, sizeof ui);
    memset(&favorites, 0, sizeof favorites);
    panel = PANEL_DEFAULT;
    host_tracks_init();
    fm1_ms = 0;
    transport_req = 0;
    persist_pending = 0;
    persist_retry_ms = 0;
    flash_ok = 1;
    erase_error = 0;
    fail_after = -1;
    up_gen = erases = 0;
    irq_start_race = irq_races = 0;
}

/* ---- 1.2, Discussion #130: the autosave (project.c): written only when the music changed, stopped and idle 10 s, at
 * most one a minute; restored at power-on (RESTORE LAST ON, a clean boot); a damaged one ignored ---- */
static void as_run(uint32_t ms)                          /* the main loop for ms (autosave_poll checks every 250 ms) */
{
    uint32_t end = fm1_ms + ms;
    while ((int32_t)(end - fm1_ms) > 0) {
        fm1_ms += 50u;
        autosave_poll();
    }
}
static void as_power_on(void)                            /* RAM lost: the defaults, then main.c's restore */
{
    uint32_t k;
    memset(trk, 0, sizeof trk);
    memset(&as, 0, sizeof as);
    memset(&ui, 0, sizeof ui);
    proj_name[0] = 0;
    tk_defaults();
    host_tracks_init();
    for (k = 0; k < NTRK; k++)
        trk[k].eng_req = trk[k].engine = (uint8_t)TRK_DEF[k][0];
}
static void as_edit(uint32_t i)                          /* a change the project holds: a cell's note (always another) */
{
    static uint32_t n;
    tk_cell_t *c = tk_at(0, i % 16u, i % NTRK);
    *c = tk_pack(20u + (tk_note(c) + 1u + n++ % 50u) % 100u, 0, 0, 0);
}
static uint32_t as_cells(void)                           /* pattern 0's cells, hashed */
{
    uint32_t r, k, h = 2166136261u;
    for (r = 0; r < TK_ROWS; r++)
        for (k = 0; k < NTRK; k++)
            h = (h ^ tk_bits(tk_at(0, r, k))) * 16777619u;
    return h;
}
static int autosave_test(void)
{
    int bad = 0, ok;
    uint32_t w, k, b0, minute, st;
    static uint8_t raw[ST_PAYLOAD_MAX];
    reset();
    as_power_on();
    as_erases = 0;
    ok = !autosave_boot(1) && !as.writes;
    as_run(120000u);
    bad += check("autosave: power-on with none: nothing restored; 2 min unchanged: no write", ok && !as.writes && !as_erases);
    as_edit(0);
    as_run(9000u);
    ok = !as.writes;
    as_run(1500u);
    bad += check("autosave: a change: no write within 10 s, one once idle 10 s (one erase)",
                 ok && as.writes == 1u && as_erases == 1u && st_load(OBJ_AUTOSAVE, raw, sizeof raw) > (int)PROJ_HDR);
    as_run(600000u);
    bad += check("autosave: 10 min unchanged after it: no more writes", as.writes == 1u && as_erases == 1u);
    song.g[G_SLOT] = 3;                                  /* (the PROJECT page's pick, the selected track: not the music) */
    song.sel = 2;
    as_run(120000u);
    bad += check("autosave: the selected track / PROJECT's slot pick alone: no write", as.writes == 1u);
    song.g[G_BPM] = 133;
    song.playing = 1;
    as_run(300000u);
    ok = as.writes == 1u;
    song.playing = 0;
    as_run(9000u);
    ok &= as.writes == 1u;
    as_run(1500u);
    bad += check("autosave: changed while playing: no write until stopped and idle 10 s, then one", ok && as.writes == 2u);
    as_edit(1);
    fm1_in.notes = 1u << 7;                              /* a key held (playing the sound, stopped) */
    as_run(60000u);
    ok = as.writes == 2u;
    fm1_in.notes = 0;
    trk[1].v[0].active = 1;                              /* .. its release still sounding */
    as_run(20000u);
    ok &= as.writes == 2u;
    trk[1].v[0].active = 0;
    as_run(9000u);
    ok &= as.writes == 2u;
    as_run(1500u);
    bad += check("autosave: a key held, a voice sounding: no write (an erase stops the audio); after, idle 10 s: one",
                 ok && as.writes == 3u);
    as_run(60000u);
    as_edit(2);                                          /* written 10 s on; then edits 15 s apart: a minute's gap */
    as_run(11000u);
    w = as.writes;
    as_edit(3);
    as_run(15000u);
    ok = as.writes == w;
    as_edit(4);
    as_run(15000u);
    ok &= as.writes == w;
    as_run(40000u);
    bad += check("autosave: two writes at least 60 s apart", ok && as.writes == w + 1u);
    as_run(60000u);
    w = as.writes;
    as_edit(5);
    for (k = 0; k < 120u; k++) {                         /* an editor backup going on: holds it */
        as_run(500u);
        autosave_hold();
    }
    ok = as.writes == w;
    as_run(10500u);
    bad += check("autosave: none during an editor transfer (autosave_hold), one after", ok && as.writes == w + 1u);
    /* power-on: restored */
    as_edit(6);
    song.g[G_BPM] = 97;
    str_cpy(proj_name, "MY TUNE", sizeof proj_name);
    as_run(70000u);
    st = as_cells();
    b0 = as_erases;
    as_power_on();
    ok = st != as_cells() && song.g[G_BPM] != 97;
    ok &= autosave_boot(1) == 1 && st == as_cells() && song.g[G_BPM] == 97 && str_eq(proj_name, "MY TUNE") &&
          ui.msg_t && str_eq(ui.msg, "RESTORED");
    as_run(300000u);
    bad += check("autosave: power-on restores it (the song, tempo, name; RESTORED); no write after (unchanged)",
                 ok && !as.writes && as_erases == b0);
    as_power_on();
    ok = autosave_boot(0) == 0 && song.g[G_BPM] != 97;
    as_run(60000u);
    bad += check("autosave: after a crash / hang / failed boot: not restored, nothing written", ok && as_erases == b0);
    as_power_on();
    ui_prefs |= PREF_RESTORE_OFF;
    ok = autosave_boot(1) == 0 && song.g[G_BPM] != 97;
    as_edit(7);
    as_run(300000u);
    bad += check("autosave: RESTORE LAST OFF: none restored, none written", ok && !as.writes && as_erases == b0);
    ui_prefs &= (uint8_t)~PREF_RESTORE_OFF;
    {   /* a damaged copy: the other one (A/B); both damaged: none */
        st_hdr_t h;
        int cur, other;
        as_power_on();
        autosave_boot(1);
        song.g[G_BPM] = 111;                             /* a newer copy into the other sector */
        as_run(11000u);
        cur = st_current(OBJ_AUTOSAVE, &h);
        nor[st_sector(OBJ_AUTOSAVE, (uint32_t)cur) + ST_PAYLOAD_OFF + 100u] ^= 0x10u;   /* its payload: the CRC fails */
        other = st_current(OBJ_AUTOSAVE, &h);
        as_power_on();
        ok = cur >= 0 && other == !cur && autosave_boot(1) == 1 && song.g[G_BPM] == 97;
        bad += check("autosave: the newest copy damaged: the copy before it is restored", ok);
        nor[st_sector(OBJ_AUTOSAVE, (uint32_t)other) + 12u] ^= 0x01u;   /* the other one's header too */
        as_power_on();
        ok = autosave_boot(1) == 0 && song.g[G_BPM] != 97 && song.g[G_BPM] != 111;
        bad += check("autosave: both copies damaged: ignored, power-on as new", ok);
        memset(nor + 0xE5000u, 0xFF, 0x2000u);
        st_save(OBJ_AUTOSAVE, "NOT A PROJECT", 13u);     /* a valid record that is no project */
        as_power_on();
        bad += check("autosave: a record that is no project: ignored", autosave_boot(1) == 0 && song.g[G_BPM] != 97);
    }
    /* a power cut in the middle of a write: the copy before stays in charge */
    memset(nor + 0xE5000u, 0xFF, 0x2000u);
    as_power_on();
    autosave_boot(1);
    as_edit(8);
    as_run(11000u);                                      /* (written: copy A) */
    song.g[G_BPM] = 150;
    fail_after = 1;                                      /* the next write stops after its first program (1.5: a
                                                          * small song is a page or two, the commit record last) */
    as_run(70000u);
    fail_after = -1;
    ok = as.err;
    as_power_on();
    ok &= autosave_boot(1) == 1 && song.g[G_BPM] != 150 && tk_note(tk_at(0, 8, 0)) != 0u;
    bad += check("autosave: a write cut short (power off): the copy before is restored", ok);
    {   /* (Fel-Tracker) a song of several chunks: autosaved whole and restored whole; a write cut after any of its flash
         * programs (chunks 1 .. first, chunk 0 the commit last): power-on restores the song before it or the new one,
         * whole, never a mix */
        uint32_t p, r, cut, cuts = 0, mixed = 0, done = 0;
        char what[160];
        st_hdr_t sh;
        proj_hdr_t ph;
        as_power_on();
        autosave_boot(1);
        for (p = 0; p < 8u; p++)
            for (r = 0; r < TK_ROWS; r++)
                for (k = 0; k < NTRK; k++)
                    *tk_at(p, r, k) = tk_pack(30u + (r + k) % 60u, 1, 0, 0);
        w = as.writes;
        as_run(70000u);
        ok = as.writes == w + 1u && st_current(OBJ_AUTOSAVE, &sh) >= 0;
        memcpy(&ph, st_buf, sizeof ph);
        ok &= ph.count > 1u;
        as_power_on();
        ok &= autosave_boot(1) == 1 && tk_note(tk_at(7, 63, NTRK - 1u)) == 30u + (63u + NTRK - 1u) % 60u;
        snprintf(what, sizeof what, "autosave: a song of %u chunks: written whole, power-on restores it whole", ph.count);
        bad += check(what, ok);
        for (cut = 1; cut < 400u && !done; cut++) {
            int32_t b0;
            as_power_on();
            if (autosave_boot(1) != 1 || tk_note(tk_at(7, 63, NTRK - 1u)) != 30u + (63u + NTRK - 1u) % 60u) {
                mixed++;
                break;
            }
            b0 = song.g[G_BPM];
            song.g[G_BPM] = (int16_t)(b0 < 200 ? b0 + 1 : 60);
            fail_after = (int)cut;
            as_run(70000u);
            done = fail_after > 0;                       /* (the write finished before the cut) */
            fail_after = -1;
            cuts += !done;
            as_power_on();
            if (autosave_boot(1) != 1 || (song.g[G_BPM] != b0 && song.g[G_BPM] != (b0 < 200 ? b0 + 1 : 60)) ||
                tk_note(tk_at(7, 63, NTRK - 1u)) != 30u + (63u + NTRK - 1u) % 60u)
                mixed++;
        }
        snprintf(what, sizeof what, "  a write cut after each of its %u flash programs: the song before or the new one, whole, each time", cuts);
        bad += check(what, done && !mixed && cuts > 30u);
    }
    /* wear: a 4-hour session: 3 h of editing (a change every 3 s), pauses and playing it back, then an hour away */
    as_power_on();
    autosave_boot(1);
    as_erases = 0;
    for (minute = 0; minute < 240u; minute++) {
        uint32_t s;
        if (minute >= 180u) {
            as_run(60000u);
            continue;
        }
        song.playing = minute % 10u >= 7u;               /* 3 minutes in 10 playing */
        for (s = 0; s < 60u; s += 3u) {
            if (minute % 5u != 4u)                       /* (every fifth minute a pause, no edits) */
                as_edit(minute * 20u + s);
            as_run(3000u);
        }
    }
    song.playing = 0;
    as_run(20000u);
    printf("persist:   a 4-hour session (3 h editing and playing, 1 h idle): %u autosave writes\n", (unsigned)as_erases);
    bad += check("autosave wear: a session's writes stay far below one a minute", as_erases > 0u && as_erases <= 60u);
    {   /* worst case: a change, then just enough idle, over and over, for 8 hours */
        uint32_t i;
        as_erases = 0;
        for (i = 0; i < 8u * 3600u / 11u; i++) {
            as_edit(i);
            as_run(11000u);
        }
        printf("persist:   worst case, 8 h of a change every 11 s: %u writes (%u per sector)\n", (unsigned)as_erases,
               (unsigned)(as_erases / 2u));
        bad += check("autosave wear, worst case: at most one write a minute (480 in 8 h)", as_erases <= 8u * 60u + 1u);
    }
    return bad;
}

/* 1.5: the song store (project.c): the song in up to four chunks, each an A/B storage object, a save's generation in
 * every header. A save cut short anywhere (any erase, any program) leaves the save before it whole, even two cut short in
 * a row; a damaged chunk of the newest save: the one before; names through flash; a failed rename keeps everything */
static uint32_t song_hash(void)
{
    uint32_t p, r, k, h = 2166136261u;
    for (p = 0; p < TK_PATS; p++)
        for (r = 0; r < TK_ROWS; r++)
            for (k = 0; k < NTRK; k++)
                h = (h ^ tk_bits(tk_at(p, r, k))) * 16777619u;
    h = (h ^ tk.norder) * 16777619u;
    return (h ^ (uint32_t)song.g[G_BPM]) * 16777619u;
}
static void song_fill(uint32_t seed, uint32_t pats)      /* a song over `pats` patterns (two or more chunks from 4) */
{
    uint32_t p, r, k;
    tk_defaults();
    for (p = 0; p < pats; p++)
        for (r = 0; r < TK_ROWS; r += 1u + (p + seed) % 2u)
            for (k = 0; k < NTRK; k++)
                *tk_at(p, r, k) = tk_pack(24u + (r * 7u + k * 5u + seed) % 90u, (k + seed) % 4u, (r + seed) % 3u, r);
    tk.order[1] = (uint8_t)(seed % TK_PATS);
    tk.norder = 2;
    song.g[G_BPM] = (int16_t)(60 + seed);
}
static void power_cycle(void)                            /* RAM lost: the store found again, the song from it */
{
    tk_defaults();
    song.g[G_BPM] = 120;
    proj_find();
    project_load(0);
}
static int song_store_test(void)
{
    static uint8_t nor1[0x100000];
    int bad = 0, ok;
    uint32_t h1, h2, k, ops, cuts = 0, twice = 0, gen;
    char n[16];
    reset();
    song_fill(1, 6);
    h1 = song_hash();
    ok = project_save(0) == 0 && !strcmp(ui.msg, "SAVED") && proj_good.have && proj_good.count >= 2u;
    power_cycle();
    bad += check("song store: a song of several chunks saved, found and loaded back whole", ok && song_hash() == h1 &&
                  !strcmp(ui.msg, "LOADED"));
    memcpy(nor1, nor, sizeof nor1);
    song_fill(2, 7);
    h2 = song_hash();
    erases = 0;
    project_save(0);
    ops = erases;                                        /* (an erase each chunk; its programs after it) */
    for (k = 0; k < 4000u; k++) {                        /* cut short after k programs (a power cut) */
        uint32_t got;
        memcpy(nor, nor1, sizeof nor);
        song_fill(2, 7);
        fail_after = (int)k;
        project_save(0);
        fail_after = -1;
        power_cycle();
        got = song_hash();
        if (got != h1 && got != h2)
            break;
        cuts += got == h1;
        if (got == h2)
            break;                                       /* (the whole save went through: past the last program) */
    }
    bad += check("song store: a save cut short after any program: the song before (whole), never a mix",
                  k < 4000u && song_hash() == h2 && cuts > 0u);
    for (k = 0; k < 40u; k++) {                          /* two saves, either cut short: the newest complete one loads */
        uint32_t h3, r2, r3, both = 0;
        memcpy(nor, nor1, sizeof nor);
        song_fill(2, 7);
        fail_after = (int)(k * 9u + 5u);
        r2 = (uint32_t)project_save(0);
        song_fill(3, 5);
        h3 = song_hash();
        fail_after = (int)(k * 5u + 3u);
        r3 = (uint32_t)project_save(0);
        fail_after = -1;
        power_cycle();
        both += r2 && r3;
        twice += song_hash() == (!r3 ? h3 : !r2 ? h2 : h1);
        cuts += both;
    }
    bad += check("song store: two saves in a row, either cut short: the newest complete one loads (the first when both)",
                 twice == 40u);
    memcpy(nor, nor1, sizeof nor);
    erase_error = 1;
    song_fill(2, 7);
    bad += check("song store: a failing erase: SAVE ERROR, the stored song as it was",
                  project_save(0) == 2 && !strcmp(ui.msg, "SAVE ERROR") && (power_cycle(), song_hash() == h1));
    erase_error = 0;
    /* a damaged chunk of the newest save: the save before it */
    memcpy(nor, nor1, sizeof nor);
    song_fill(2, 7);
    project_save(0);
    gen = proj_good.gen;
    nor[st_sector(OBJ_PROJECT0 + 1u, proj_good.copy[1]) + ST_PAYLOAD_OFF + 300u] ^= 0x40u;
    power_cycle();
    bad += check("song store: the newest save's chunk 2 damaged: the save before loads", song_hash() == h1 &&
                  proj_good.gen == gen - 1u);
    /* the PROJECT page: a pending PLAY, names, rename */
    memcpy(nor, nor1, sizeof nor);
    erases = 0;
    transport_req = 1;
    ok = project_save(0) != 0 && !strcmp(ui.msg, "STOP TO SAVE") && !erases;
    transport_req = 0;
    bad += check("song store: a pending PLAY: no save, no erase", ok);
    song_fill(4, 2);
    project_save_as(0, "LOFI JAM");
    proj_name[0] = 0;
    power_cycle();
    bad += check("song store: a song's name goes to flash and comes back", project_name(0, n) && !strcmp(n, "LOFI JAM") &&
                  !strcmp(proj_name, "LOFI JAM"));
    h1 = song_hash();
    memcpy(nor1, nor, sizeof nor1);
    fail_after = 1;
    ok = project_rename(0, "DUB") == 2 && !strcmp(ui.msg, "SAVE ERROR");
    fail_after = -1;
    power_cycle();
    bad += check("song store: a failed rename: SAVE ERROR, the name and the song as they were", ok && project_name(0, n) &&
                  !strcmp(n, "LOFI JAM") && song_hash() == h1);
    song_fill(9, 3);                                     /* (the music now: another song) */
    ok = project_rename(0, "DUB") == 0 && !strcmp(ui.msg, "RENAMED") && project_name(0, n) && !strcmp(n, "DUB");
    power_cycle();
    bad += check("song store: rename: the store has the new name, the song in it as it was", ok && song_hash() == h1 &&
                  !strcmp(proj_name, "DUB"));
    return bad;
}

int main(void)
{
    int bad = 0, ok;
    uint32_t before;
    persist_t p;
    up_bank_t bank;
    up_rec_t r;
    reset();
    settings_init();
    {
        const uint8_t amber[] = {0, 2};
        song.playing = 1;
        bad += check("editor display change while playing is applied and reports deferred saving",
                      ed_ui_set(amber, sizeof amber) == 4 && settings.palette == 2u && persist_pending == 1 && !erases);
        song.playing = 0;
        settings_poll();
        bad += check("STOP commits the deferred palette setting",
                      !persist_pending && erases == 1 && persist_saved.palette == palette_to_stored(2));
        before = erases;
        bad += check("repeating an unchanged editor preference succeeds without erasing",
                      ed_ui_set(amber, sizeof amber) == 0 && erases == before);
        erase_error = 1;
        const uint8_t mono[] = {0, 0};
        bad += check("editor reports failed saving while preserving the previous flash setting",
                      ed_ui_set(mono, sizeof mono) == 3 && settings.palette == 0u && persist_pending == 2 &&
                      persist_saved.palette == palette_to_stored(2));
        erase_error = 0;
        fm1_ms += 1000;
        settings_poll();
        bad += check("failed editor preference retries through the existing persistence path",
                      !persist_pending && persist_saved.palette == palette_to_stored(0));
        const uint8_t bold[] = {1, 1};
        bad += check("the retired font weight is refused without a write", ed_ui_set(bold, sizeof bold) == 2);
    }
    {   /* MENU_SET (editor_menu.c): the same settings record, the same deferral, retry and idempotence */
        const uint8_t leds_off[] = {4, 0, 64}, large_on[] = {2, 1, 64}, hold_06[] = {5, 3, 64}, eq_bass[] = {9, 2, 64};
        persist_t q;
        reset();
        settings_init();
        song.playing = 1;
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, leds_off, 3);
        bad += check("MENU_SET while playing: applied, rc 4 (queued until STOP), no erase",
                     ed_repn == 4 && ed_rep[0] == 4 && ed_rep[1] == 4 && settings_leds == LEDS_OFF && persist_pending == 1 && !erases);
        song.playing = 0;
        settings_poll();
        bad += check("STOP commits the MENU setting (LEDS OFF in the settings record)",
                     !persist_pending && erases == 1 && st_load(OBJ_SETTINGS, &q, sizeof q) == (int)sizeof q &&
                     leds_from_stored(q.zoom) == LEDS_OFF);
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, large_on, 3);
        ok = ed_rep[0] == 0 && (ui_prefs & PREF_LARGE);
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, hold_06, 3);
        ok &= ed_rep[0] == 0 && settings_hold == 3u;
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, eq_bass, 3);
        ok &= ed_rep[0] == 0 && st_load(OBJ_SETTINGS, &q, sizeof q) == (int)sizeof q && q.lowcut == 2u &&
              hold_from_stored(q.bold) == 3u && (q.favorites.factory[15][30] & PREF_LARGE) && leds_from_stored(q.zoom) == LEDS_OFF;
        bad += check("MENU_SET stopped: rc 0, saved at once (LARGE, HOLD, SPEAKER EQ), the others kept", ok);
        before = erases;
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, hold_06, 3);
        bad += check("repeating an unchanged MENU_SET: rc 0, no erase", ed_rep[0] == 0 && erases == before);
        erase_error = 1;
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, leds_off, 3);   /* unchanged: still rc 0 (no write) */
        ok = ed_rep[0] == 0;
        {
            const uint8_t leds_inv[] = {4, 3, 64};
            ed_repn = 0; ed_menu_handle(ED_MENU_SET, leds_inv, 3);
        }
        ok &= ed_rep[0] == 3 && settings_leds == LEDS_INV && persist_pending == 2 && leds_from_stored(persist_saved.zoom) == LEDS_OFF;
        bad += check("MENU_SET with a failing flash: applied, rc 3, the saved setting kept", ok);
        erase_error = 0;
        fm1_ms += 1000;
        settings_poll();
        bad += check("... and retried by the settings path", !persist_pending && leds_from_stored(persist_saved.zoom) == LEDS_INV);
        settings_hold = HOLD_DEF; settings_leds = LEDS_DIM;
    }
    reset();
    settings.palette = 3;
    settings.lowcut = 2;
    settings.zoom = 1;
    persist_saved.bold = 1;                       /* (an earlier firmware's font weight: kept as saved) */
    favorites.factory[8][0] = 1;
    favorites.user = 1u << 31;
    song.playing = 1;
    settings_save();
    bad += check("playing: settings queued, no erase, runtime kept", persist_pending && !erases &&
                  settings.palette == 3u && settings.lowcut == 2u && settings.zoom == 1u);
    song.playing = 0;
    transport_req = 1;
    settings_poll();
    bad += check("pending PLAY: settings still queued, no erase", persist_pending && !erases);
    transport_req = 0;
    settings.palette = 2;
    settings_poll();
    bad += check("STOP: latest settings saved and request cleared", !persist_pending && erases == 1u &&
                  st_load(OBJ_SETTINGS, &p, sizeof p) == (int)sizeof p && p.palette == palette_to_stored(2) &&
                  p.lowcut == 2u && p.zoom == 1u && p.bold == 1u &&
                  p.favorites.factory[8][0] == 1 && p.favorites.user == (1u << 31) &&
                  !memcmp(&p.panel, &panel, sizeof panel));
    settings_save();
    bad += check("unchanged settings do not erase", !persist_pending && erases == 1u);
    settings.palette = 1;
    erase_error = 1;
    fm1_ms = 0xFFFFFF00u;
    settings_save();
    before = erases;
    settings_poll();
    fm1_ms += 999u;
    settings_poll();
    bad += check("failed settings retry waits through clock wrap", persist_pending && erases == before &&
                  persist_saved.palette == palette_to_stored(2) && settings.palette == 1u);
    erase_error = 0;
    fm1_ms++;
    settings_poll();
    bad += check("settings retry succeeds after 1000 ms", !persist_pending && erases == before + 1u &&
                  st_load(OBJ_SETTINGS, &p, sizeof p) == (int)sizeof p && p.palette == palette_to_stored(1));

    bad += song_store_test();
    seq_stop();
    transport_req = 0;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.ver = UP_VER;
    r.engine = EID_SID;                                 /* (id 0, ANALOG's, retired since Fel-Tracker removed it) */
    r.np = P_COUNT;
    memcpy(r.name, "First", 6);
    erase_error = 1;
    bank = up_bank[1];
    ok = up_put(17, &r) == 2;
    erase_error = 0;
    bad += check("failed first preset save restores empty bank", ok && !memcmp(&up_bank[1], &bank, sizeof bank));
    bad += check("preset initial save", up_put(3, &r) == 0 && up_used(3));
    bank = up_bank[0];
    before = up_gen;
    r.p[0] = 77;
    fail_after = 1;
    ok = up_put(3, &r) == 2;
    fail_after = -1;
    bad += check("failed preset save rolls back bank and generation", ok && up_gen == before &&
                  !memcmp(&up_bank[0], &bank, sizeof bank));
    favorite_set(EID_COUNT, 3, 1);
    fail_after = 1;
    ok = up_put(3, 0) == 2;
    fail_after = -1;
    bad += check("failed preset erasure retains both the sound and its favorite", ok && up_used(3) &&
                  favorite_has(EID_COUNT, 3) && !memcmp(&up_bank[0], &bank, sizeof bank));
    memset(&up_bank[0], 0, sizeof up_bank[0]);
    up_boot();
    bad += check("failed preset save keeps old flash", !memcmp(&up_bank[0], &bank, sizeof bank));
    trk[0].user = 4;
    erase_error = 1;
    ok = up_put(3, 0) == 2;
    erase_error = 0;
    bad += check("failed preset erase keeps record and loaded label", ok && trk[0].user == 4u && up_gen == before &&
                  !memcmp(&up_bank[0], &bank, sizeof bank));
    bad += check("successful preset erase clears loaded label", up_put(3, 0) == 0 && !up_used(3) && !trk[0].user);
    before = erases;
    transport_req = 1;
    bad += check("pending PLAY: preset save blocked", up_put(3, &r) == 2 && erases == before && !up_used(3));
    transport_req = 0;
    bad += check("preset invalid slot blocked before access", up_put(UP_SLOTS, &r) == 1 && erases == before);
    song.playing = 0; transport_req = 1; irq_start_race = 1;
    bad += check("PLAY consumed before the save snapshot still blocks a project write",
                  project_save(1) != 0 && irq_races == 1u && song.playing && !transport_req && erases == before);
    song.playing = 0; transport_req = 1; irq_start_race = 1;
    bad += check("PLAY consumed before the preset snapshot still blocks a bank write",
                  up_put(3, &r) == 2 && irq_races == 2u && song.playing && !transport_req && erases == before);
    song.playing = 0; transport_req = 1; irq_start_race = 1;
    settings.palette = 4;
    settings_save();
    bad += check("PLAY consumed before the settings snapshot defers the write",
                  irq_races == 3u && song.playing && !transport_req && persist_pending && erases == before);
    bad += autosave_test();
    printf("%s\n", bad ? "PERSISTENCE TEST FAILED" : "persistence test passed");
    return bad != 0;
}
