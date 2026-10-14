/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* Fel-Tracker as a native desktop application: the firmware, as the browser emulator runs it (web/emu/felucca_web.c:
 * the simulated FM-1, its clock driven by the audio asked for), in a window of its own (SDL2), its sound out of the
 * computer's (SDL audio, 44.1 kHz), MIDI in from every source (CoreMIDI on macOS), its saves in a file.
 * It is the base the heavier things are built on: no device's CPU, RAM or flash under it, only the computer's. Built
 * FELUCCA_NATIVE (core.h): every track has its own 16 voices (no budget shared, no note taken from another track), the
 * engines' voice caps that were the device's CPU are lifted, and MENU > INFO's CPU is the computer's load.
 *   native/build.sh [DIST]  ->  build/native/fel-tracker-<dist> (and Fel-Tracker <DIST>.app on macOS)
 * The window: the screen (3x), the knobs (the mouse wheel over one turns it; MASTER the level), the buttons (a click
 * holds one while the mouse button is down, LEDs as the device lights them) and the 27 keys. The computer keyboard as
 * on the emulator page: F3 .. A#4 on the Z and A rows, B4 .. G5 on Q .. O, the twelve buttons on the number row in the
 * panel's order, [ ] OCT- OCT+, the arrows KNOB 1 (up, down) and ALGORITHM (left, right), Shift + up / down SELECT,
 * \ turns Q E, A D, Z C into KNOB 2, 3, 4. Esc quits.
 * Threads: the audio callback runs the device (web_render); the window's thread hands it the buttons and keys through
 * a queue (each change held HOLD_MS of the device's time, as worklet.js does: a click's down and up never fall within
 * one scan) and reads the screen and the LEDs with the audio device locked; MIDI arrives in a ring the callback empties.
 *   fel-tracker [--selftest [FILE.bmp]]   --selftest: boots with dummy video and audio drivers, plays 3 s, checks the
 *                              screen drew and the sound runs, exits 0 or 1 (tests/run_tests.sh); FILE.bmp: the
 *                              window as drawn, saved */
#include <stdint.h>
#include <time.h>
#define FELUCCA_NATIVE 1                                  /* (core.h: every track its own 16 voices, the engines' caps
                                                           * lifted; audio.c: the load meter the computer's) */
static uint32_t native_ticks(void)                        /* the computer's clock in the device's TIMER4 ticks (24 MHz) */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 24000000u + (uint64_t)ts.tv_nsec * 24u / 1000u);
}
#include "../web/emu/felucca_web.c"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <CoreMIDI/CoreMIDI.h>
#endif

#define HOLD_MS 24u
#define SCALE 3
#define SCR 240
#define WIN_W (SCR * SCALE + 40 + 520)
#define WIN_H (SCR * SCALE + 40)

static SDL_AudioDeviceID audio_dev;

/* ---------------------------------------------------------------- input queue (window -> audio) --- */
typedef struct { uint8_t keys; uint32_t mask; } in_ev_t;   /* keys 0: buttons by label, 1: keys */
static in_ev_t in_q[256];
static volatile uint32_t in_w, in_r;
static uint32_t next_ok;
static void push_input(int keys, uint32_t mask)          /* (the window's thread, the audio device locked) */
{
    if (in_w - in_r < 256u) {
        in_q[in_w % 256u].keys = (uint8_t)keys;
        in_q[in_w % 256u].mask = mask;
        in_w++;
    }
}
static void apply_input(void)                             /* (the audio callback) */
{
    uint32_t now = web_now_ms();
    while (in_r != in_w && (int32_t)(now - next_ok) >= 0) {
        in_ev_t e = in_q[in_r % 256u];
        in_r++;
        if (e.keys)
            web_keys(e.mask);
        else
            web_buttons(e.mask);
        next_ok = now + HOLD_MS;
    }
}

/* ---------------------------------------------------------------- MIDI (CoreMIDI thread -> audio) --- */
static uint32_t midi_q[512];
static volatile uint32_t midi_w, midi_r;
static void midi_push(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (midi_w - midi_r < 512u) {
        midi_q[midi_w % 512u] = st | d1 << 8 | d2 << 16;
        __sync_synchronize();
        midi_w++;
    }
}
#ifdef __APPLE__
static MIDIClientRef midi_client;
static MIDIPortRef midi_port;
static void midi_read(const MIDIPacketList *pl, void *ref, void *src)
{
    const MIDIPacket *p = &pl->packet[0];
    uint32_t i, k;
    (void)ref; (void)src;
    for (i = 0; i < pl->numPackets; i++, p = MIDIPacketNext(p))
        for (k = 0; k < p->length;) {
            uint8_t st = p->data[k];
            uint32_t len = (st & 0xF0u) == 0xC0u || (st & 0xF0u) == 0xD0u ? 2u : 3u;
            if (st < 0x80u || st >= 0xF0u) {          /* (system messages, running status: not here) */
                k++;
                continue;
            }
            midi_push(st, k + 1u < p->length ? p->data[k + 1u] : 0u, len == 3u && k + 2u < p->length ? p->data[k + 2u] : 0u);
            k += len;
        }
}
static ItemCount midi_connected;
static void midi_scan(void)                               /* every source, again when their number changes */
{
    ItemCount n = MIDIGetNumberOfSources(), i;
    if (!midi_port || n == midi_connected)
        return;
    for (i = 0; i < n; i++)
        MIDIPortConnectSource(midi_port, MIDIGetSource(i), 0);
    midi_connected = n;
    if (n)
        fprintf(stderr, "fel-tracker: MIDI in from %lu source(s)\n", (unsigned long)n);
}
static void midi_open(void)
{
    if (MIDIClientCreate(CFSTR("Fel-Tracker"), 0, 0, &midi_client) == 0)
        MIDIInputPortCreate(midi_client, CFSTR("in"), midi_read, 0, &midi_port);
    midi_scan();
}
#else
static void midi_open(void) {}
static void midi_scan(void) {}
#endif

/* ---------------------------------------------------------------- audio --- */
static volatile uint32_t audio_blocks;
static void audio_cb(void *u, Uint8 *stream, int len)
{
    float *o = (float *)stream;
    uint32_t n = (uint32_t)len / (2u * sizeof(float)), done = 0, i;
    (void)u;
    while (midi_r != midi_w) {
        uint32_t m = midi_q[midi_r % 512u];
        web_midi(m & 255u, (m >> 8) & 255u, (m >> 16) & 255u);
        midi_r++;
    }
    while (done < n) {
        uint32_t k = n - done < 128u ? n - done : 128u;  /* (128 frames a step: the queue's pace as in the browser) */
        apply_input();
        web_render(k);
        for (i = 0; i < k; i++) {
            o[2u * (done + i)] = out_l[i];
            o[2u * (done + i) + 1u] = out_r[i];
        }
        done += k;
    }
    audio_blocks++;
}

/* ---------------------------------------------------------------- the flash in a file --- */
static char flash_path[1024];
static uint32_t flash_writes;
static void flash_load(void)
{
    FILE *f = fopen(flash_path, "rb");
    if (f) {
        size_t r = fread(web_nor(), 1, web_nor_size(), f);
        fclose(f);
        if (r != web_nor_size())
            web_nor_erase();                          /* (a file cut short: as new) */
    }
}
static void flash_save_if_written(void)
{
    static uint8_t copy[0x100000];
    uint32_t w;
    SDL_LockAudioDevice(audio_dev);
    w = web_flash_writes_count();
    if (w != flash_writes)
        memcpy(copy, web_nor(), sizeof copy);         /* (a save is whole within one main-loop pass) */
    SDL_UnlockAudioDevice(audio_dev);
    if (w != flash_writes) {
        char tmp[1100];
        FILE *f;
        snprintf(tmp, sizeof tmp, "%s.new", flash_path);
        if ((f = fopen(tmp, "wb"))) {
            int ok = fwrite(copy, 1, sizeof copy, f) == sizeof copy;
            ok &= fclose(f) == 0;
            if (ok)
                rename(tmp, flash_path);              /* (a save cut short leaves the one before it) */
        }
        flash_writes = w;
    }
}

/* ---------------------------------------------------------------- a tiny font for the labels (5 x 7) --- */
static const struct { char c; uint8_t r[7]; } FONT[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}}, {'G', {14, 17, 16, 23, 17, 17, 15}},
    {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {14, 4, 4, 4, 4, 4, 14}}, {'K', {17, 18, 20, 24, 20, 18, 17}},
    {'L', {16, 16, 16, 16, 16, 16, 31}}, {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}}, {'Q', {14, 17, 17, 17, 21, 18, 13}},
    {'R', {30, 17, 17, 30, 20, 18, 17}}, {'S', {15, 16, 16, 14, 1, 1, 30}}, {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}}, {'W', {17, 17, 17, 21, 21, 21, 10}},
    {'X', {17, 17, 10, 4, 10, 17, 17}}, {'Y', {17, 17, 10, 4, 4, 4, 4}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}}, {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}}, {'4', {2, 6, 10, 18, 31, 2, 2}}, {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'+', {0, 4, 4, 31, 4, 4, 0}}, {'/', {1, 1, 2, 4, 8, 16, 16}}, {'.', {0, 0, 0, 0, 0, 12, 12}},
};
static void text(SDL_Renderer *r, int x, int y, int px, const char *s)
{
    for (; *s; s++, x += 6 * px) {
        uint32_t g, row, col;
        for (g = 0; g < sizeof FONT / sizeof FONT[0] && FONT[g].c != *s; g++)
            ;
        if (g == sizeof FONT / sizeof FONT[0])
            continue;
        for (row = 0; row < 7u; row++)
            for (col = 0; col < 5u; col++)
                if ((FONT[g].r[row] >> (4u - col)) & 1u) {
                    SDL_Rect q = {x + (int)col * px, y + (int)row * px, px, px};
                    SDL_RenderFillRect(r, &q);
                }
    }
}

/* ---------------------------------------------------------------- the panel --- */
static const char *const BTN_NAME[14] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT-", "OCT+"};
static const uint8_t BTN_ORDER[14] = {B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY,
                                      B_REC, B_OCTDN, B_OCTUP};
static const char *const KNOB_NAME[8] = {"MASTER", "SELECT", "PRESETS", "ALGO", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"};
static const int8_t KNOB_ROLE[8] = {-1, EN_SELECT, EN_PRESET, EN_ALGO, EN_K1, EN_K2, EN_K3, EN_K4};
static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static const uint8_t BLACK_AFTER[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
#define PX (SCR * SCALE + 40)                             /* the panel's left edge */
static SDL_Rect knob_rect(int i) { SDL_Rect q = {PX + (i % 4) * 124, 30 + (i / 4) * 110, 100, 80}; return q; }
static SDL_Rect btn_rect(int i)
{
    SDL_Rect q = {PX + (i % 6) * 82, 270 + (i / 6) * 72, 74, 56};
    if (i >= 12)
        q = (SDL_Rect){PX + (i - 12) * 82, 270 + 2 * 72, 74, 56};
    return q;
}
static SDL_Rect key_rect(int k)                           /* key k (0 F3 .. 26 G5) */
{
    int i;
    for (i = 0; i < 16; i++)
        if (WHITE[i] == k)
            return (SDL_Rect){PX + i * 31, 520, 29, 200};
    for (i = 0; i < 11; i++)
        if (BLACK[i] == k)
            return (SDL_Rect){PX + BLACK_AFTER[i] * 31 + 19, 520, 22, 120};
    return (SDL_Rect){0, 0, 0, 0};
}
static int in_rect(SDL_Rect q, int x, int y) { return x >= q.x && x < q.x + q.w && y >= q.y && y < q.y + q.h; }
static int key_at(int x, int y)                           /* (the black keys over the white ones) */
{
    int i;
    for (i = 0; i < 11; i++)
        if (in_rect(key_rect(BLACK[i]), x, y))
            return BLACK[i];
    for (i = 0; i < 16; i++)
        if (in_rect(key_rect(WHITE[i]), x, y))
            return WHITE[i];
    return -1;
}

/* ---------------------------------------------------------------- the computer keyboard --- */
typedef struct { SDL_Scancode sc; int8_t key, btn; } kmap_t;
static const kmap_t KMAP[] = {
    {SDL_SCANCODE_Z, 0, -1}, {SDL_SCANCODE_X, 2, -1}, {SDL_SCANCODE_C, 4, -1}, {SDL_SCANCODE_V, 6, -1},
    {SDL_SCANCODE_B, 7, -1}, {SDL_SCANCODE_N, 9, -1}, {SDL_SCANCODE_M, 11, -1}, {SDL_SCANCODE_COMMA, 12, -1},
    {SDL_SCANCODE_PERIOD, 14, -1}, {SDL_SCANCODE_SLASH, 16, -1},
    {SDL_SCANCODE_S, 1, -1}, {SDL_SCANCODE_D, 3, -1}, {SDL_SCANCODE_F, 5, -1}, {SDL_SCANCODE_H, 8, -1},
    {SDL_SCANCODE_J, 10, -1}, {SDL_SCANCODE_L, 13, -1}, {SDL_SCANCODE_SEMICOLON, 15, -1}, {SDL_SCANCODE_APOSTROPHE, 17, -1},
    {SDL_SCANCODE_Q, 18, -1}, {SDL_SCANCODE_W, 19, -1}, {SDL_SCANCODE_E, 20, -1}, {SDL_SCANCODE_R, 21, -1},
    {SDL_SCANCODE_T, 22, -1}, {SDL_SCANCODE_Y, 23, -1}, {SDL_SCANCODE_U, 24, -1}, {SDL_SCANCODE_I, 25, -1},
    {SDL_SCANCODE_O, 26, -1},
    {SDL_SCANCODE_1, -1, B_FX}, {SDL_SCANCODE_2, -1, B_SCL}, {SDL_SCANCODE_3, -1, B_ENV}, {SDL_SCANCODE_4, -1, B_LFO},
    {SDL_SCANCODE_5, -1, B_EDIT}, {SDL_SCANCODE_6, -1, B_GLO}, {SDL_SCANCODE_7, -1, B_HOME}, {SDL_SCANCODE_8, -1, B_SAVE},
    {SDL_SCANCODE_9, -1, B_ARP}, {SDL_SCANCODE_0, -1, B_SEQ}, {SDL_SCANCODE_MINUS, -1, B_PLAY}, {SDL_SCANCODE_EQUALS, -1, B_REC},
    {SDL_SCANCODE_LEFTBRACKET, -1, B_OCTDN}, {SDL_SCANCODE_RIGHTBRACKET, -1, B_OCTUP},
};
static uint32_t kb_btn, kb_key, mouse_btn, mouse_key, sent_btn, sent_key;
static int letter_knobs;
static void sync_input(void)                              /* the union of the keyboard's and the mouse's, if changed */
{
    uint32_t b = kb_btn | mouse_btn, k = kb_key | mouse_key;
    SDL_LockAudioDevice(audio_dev);
    if (b != sent_btn)
        push_input(0, sent_btn = b);
    if (k != sent_key)
        push_input(1, sent_key = k);
    SDL_UnlockAudioDevice(audio_dev);
}
static uint32_t master = 700;
static void turn(int knob, int n)
{
    SDL_LockAudioDevice(audio_dev);
    if (KNOB_ROLE[knob] < 0) {
        int m = (int)master + n * 24;
        master = (uint32_t)(m < 0 ? 0 : m > 1023 ? 1023 : m);
        web_master(master);
    } else {
        web_enc((uint32_t)KNOB_ROLE[knob], n);
    }
    SDL_UnlockAudioDevice(audio_dev);
}

/* ---------------------------------------------------------------- drawing --- */
static void draw(SDL_Renderer *r, SDL_Texture *tex)
{
    static uint16_t fb[SCR * SCR];
    uint32_t lit, dim, keys_lit, keys_dim, i;
    SDL_Rect sq = {20, 20, SCR * SCALE, SCR * SCALE};
    SDL_LockAudioDevice(audio_dev);
    for (i = 0; i < SCR * SCR; i++) {
        uint16_t v = web_screen()[i];
        fb[i] = (uint16_t)(v >> 8 | v << 8);          /* (the panel's byte order -> RGB565) */
    }
    lit = web_lit_buttons();
    dim = web_dim_buttons() | web_breath_buttons();
    keys_lit = web_lit_keys();
    keys_dim = web_dim_keys() | web_breath_keys() | web_mid_keys();
    SDL_UnlockAudioDevice(audio_dev);
    SDL_UpdateTexture(tex, 0, fb, SCR * 2);
    SDL_SetRenderDrawColor(r, 22, 22, 26, 255);
    SDL_RenderClear(r);
    SDL_RenderCopy(r, tex, 0, &sq);
    for (i = 0; i < 8u; i++) {                         /* the knobs */
        SDL_Rect q = knob_rect((int)i);
        SDL_SetRenderDrawColor(r, 48, 48, 56, 255);
        SDL_RenderFillRect(r, &q);
        SDL_SetRenderDrawColor(r, 200, 200, 210, 255);
        text(r, q.x + 6, q.y + q.h - 18, 2, KNOB_NAME[i]);
    }
    for (i = 0; i < 14u; i++) {                        /* the buttons, lit or dim as the LEDs */
        SDL_Rect q = btn_rect((int)i);
        uint32_t b = BTN_ORDER[i], on = (lit >> b) & 1u, lo = (dim >> b) & 1u;
        if (b == B_PLAY && ((lit >> 14) & 1u))
            SDL_SetRenderDrawColor(r, 80, 220, 120, 255);
        else if (on)
            SDL_SetRenderDrawColor(r, 255, 236, 200, 255);
        else if (lo)
            SDL_SetRenderDrawColor(r, 130, 120, 100, 255);
        else
            SDL_SetRenderDrawColor(r, 58, 58, 66, 255);
        SDL_RenderFillRect(r, &q);
        SDL_SetRenderDrawColor(r, on ? 20 : 220, on ? 20 : 220, on ? 20 : 228, 255);
        text(r, q.x + 6, q.y + 20, 2, BTN_NAME[i]);
    }
    for (i = 0; i < 2u; i++) {                         /* the keys: white, then black over them */
        uint32_t k, n = i ? 11u : 16u;
        for (k = 0; k < n; k++) {
            uint32_t key = i ? BLACK[k] : WHITE[k], on = (keys_lit >> key) & 1u, lo = (keys_dim >> key) & 1u;
            SDL_Rect q = key_rect((int)key);
            if (on)
                SDL_SetRenderDrawColor(r, 255, 236, 200, 255);
            else if (lo)                                  /* (the glow: a scale's keys, the DRUM grid's beats) */
                SDL_SetRenderDrawColor(r, i ? 110 : 238, i ? 100 : 226, i ? 80 : 196, 255);
            else if (i)
                SDL_SetRenderDrawColor(r, 30, 30, 34, 255);
            else
                SDL_SetRenderDrawColor(r, 210, 210, 214, 255);
            SDL_RenderFillRect(r, &q);
        }
    }
    SDL_SetRenderDrawColor(r, 150, 150, 160, 255);
    text(r, PX, WIN_H - 26, 2, letter_knobs ? "QE AD ZC: KNOB 2 3 4" : "FEL-TRACKER");
    SDL_RenderPresent(r);
}

/* ---------------------------------------------------------------- the window's loop --- */
static int selftest(const char *shot);
static SDL_Renderer *r;
static SDL_Texture *tex;
int main(int argc, char **argv)
{
    SDL_Window *w;
    SDL_AudioSpec want = {0}, got;
    int quit = 0, test = argc > 1 && !strcmp(argv[1], "--selftest"), mouse_knob = -1;
    uint32_t t0, last_scan = 0;
    char *pref;
    if (test) {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "fel-tracker: %s\n", SDL_GetError());
        return 1;
    }
    pref = SDL_GetPrefPath("Fel-Tracker", FELUCCA_DIST_TAG[0] ? FELUCCA_DIST_TAG + 1 : "default");
    snprintf(flash_path, sizeof flash_path, "%sflash.bin", pref ? pref : "./");
    SDL_free(pref);
    web_nor_erase();
    if (!test)
        flash_load();
    web_master(master);
    web_boot();
    flash_writes = web_flash_writes_count();
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = audio_cb;
    audio_dev = SDL_OpenAudioDevice(0, 0, &want, &got, 0);
    if (!audio_dev || got.freq != 44100 || got.format != AUDIO_F32SYS || got.channels != 2) {
        fprintf(stderr, "fel-tracker: no 44.1 kHz stereo audio out (%s)\n", SDL_GetError());
        return 1;
    }
    w = SDL_CreateWindow("Fel-Tracker" FELUCCA_DIST_TAG, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
                         SDL_WINDOW_ALLOW_HIGHDPI);
    r = w ? SDL_CreateRenderer(w, -1, test ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : 0;
    tex = r ? SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, SCR, SCR) : 0;
    if (!tex) {
        fprintf(stderr, "fel-tracker: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(r, WIN_W, WIN_H);
    midi_open();
    SDL_PauseAudioDevice(audio_dev, 0);
    if (test)
        return selftest(argc > 2 ? argv[2] : 0);
    fprintf(stderr, "fel-tracker%s: saves in %s\n", FELUCCA_DIST_TAG, flash_path);
    t0 = SDL_GetTicks();
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            int i;
            if (e.type == SDL_QUIT)
                quit = 1;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                SDL_Scancode sc = e.key.keysym.scancode;
                int down = e.type == SDL_KEYDOWN, shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                if (sc == SDL_SCANCODE_ESCAPE && down) {
                    quit = 1;
                    continue;
                }
                if (down && sc == SDL_SCANCODE_BACKSLASH && !e.key.repeat) {
                    letter_knobs = !letter_knobs;
                    continue;
                }
                if (down && (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN)) {
                    turn(shift ? 1 : 4, sc == SDL_SCANCODE_UP ? -1 : 1);
                    continue;
                }
                if (down && (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT)) {
                    turn(3, sc == SDL_SCANCODE_LEFT ? -1 : 1);
                    continue;
                }
                if (letter_knobs && down) {           /* Q E, A D, Z C: KNOB 2, 3, 4 down and up */
                    static const struct { SDL_Scancode a, b; int k; } LK[3] = {
                        {SDL_SCANCODE_Q, SDL_SCANCODE_E, 5}, {SDL_SCANCODE_A, SDL_SCANCODE_D, 6}, {SDL_SCANCODE_Z, SDL_SCANCODE_C, 7}};
                    int hit = 0;
                    for (i = 0; i < 3; i++)
                        if (sc == LK[i].a || sc == LK[i].b) {
                            turn(LK[i].k, sc == LK[i].a ? -1 : 1);
                            hit = 1;
                        }
                    if (hit)
                        continue;
                }
                if (e.key.repeat)
                    continue;
                for (i = 0; i < (int)(sizeof KMAP / sizeof KMAP[0]); i++)
                    if (KMAP[i].sc == sc) {
                        if (KMAP[i].key >= 0)
                            kb_key = down ? kb_key | 1u << KMAP[i].key : kb_key & ~(1u << KMAP[i].key);
                        else
                            kb_btn = down ? kb_btn | 1u << KMAP[i].btn : kb_btn & ~(1u << KMAP[i].btn);
                    }
                sync_input();
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int x = e.button.x, y = e.button.y, k = key_at(x, y);
                for (i = 0; i < 14; i++)
                    if (in_rect(btn_rect(i), x, y))
                        mouse_btn = 1u << BTN_ORDER[i];
                if (k >= 0)
                    mouse_key = 1u << k;
                sync_input();
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                mouse_btn = mouse_key = 0;
                sync_input();
            } else if (e.type == SDL_MOUSEMOTION) {
                mouse_knob = -1;
                for (i = 0; i < 8; i++)
                    if (in_rect(knob_rect(i), e.motion.x, e.motion.y))
                        mouse_knob = i;
            } else if (e.type == SDL_MOUSEWHEEL && mouse_knob >= 0 && e.wheel.y) {
                turn(mouse_knob, e.wheel.y > 0 ? 1 : -1);
            }
        }
        draw(r, tex);
        flash_save_if_written();
        if (SDL_GetTicks() - last_scan > 2000u) {
            midi_scan();
            last_scan = SDL_GetTicks();
        }
        if (SDL_GetTicks() - t0 < 16u)
            SDL_Delay(4);
        t0 = SDL_GetTicks();
    }
    SDL_CloseAudioDevice(audio_dev);
    flash_save_if_written();
    SDL_Quit();
    return 0;
}

/* --selftest: the dummy drivers' audio callback runs the device as the real one would; 3 s of it, then the screen
 * must have drawn (the splash, then HOME), the device's clock moved with the audio, HOME lit, a key sounding */
static int selftest(const char *shot)
{
    uint32_t i, t = SDL_GetTicks(), ms0, ms1, draws, lit, cpu;
    double e = 0;
    while (SDL_GetTicks() - t < 1500u)
        SDL_Delay(10);
    kb_key = 1u << 12;                                /* (a key: F4) */
    sync_input();
    SDL_Delay(400);
    SDL_LockAudioDevice(audio_dev);
    for (i = 0; i < 512u; i++)
        e += (double)out_l[i] * out_l[i];
    ms0 = web_now_ms();
    draws = web_screen_draws();
    lit = web_lit_buttons();
    SDL_UnlockAudioDevice(audio_dev);
    kb_key = 0;
    sync_input();
    SDL_Delay(1000);
    SDL_LockAudioDevice(audio_dev);
    ms1 = web_now_ms();
    cpu = song.cpu_q8 * 1000u / 256u;
    SDL_UnlockAudioDevice(audio_dev);
    if (shot) {                                       /* (--selftest FILE.bmp: the window as drawn now) */
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, WIN_W, WIN_H, 32, SDL_PIXELFORMAT_ARGB8888);
        draw(r, tex);
        if (s && !SDL_RenderReadPixels(r, 0, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch))
            SDL_SaveBMP(s, shot);
        SDL_FreeSurface(s);
    }
    printf("native: %u screen draws, the device's clock %u ms in 1 s, HOME %s, a key's sound %s, CPU %u.%u %%\n", draws,
           ms1 - ms0, (lit >> B_HOME) & 1u ? "lit" : "dark", e > 1e-6 ? "on" : "silent", cpu / 10u, cpu % 10u);
    SDL_CloseAudioDevice(audio_dev);
    SDL_Quit();
    return draws > 2u && ms1 - ms0 > 500u && ((lit >> B_HOME) & 1u) && e > 1e-6 ? 0 : 1;
}
