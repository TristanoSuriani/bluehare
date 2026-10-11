/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host-side render of the FELUCCA DSP (engines, voices, FX, sequencer) to a WAV,
 * for debugging sound without hardware. Same sources as the firmware.
 *   tests/run_tests.sh builds it into build/host/;
 *   build/host/hostsim ENGINE PRESET MONO OUT.wav [CHORUS]
 * env: SECS=n renders n s (the 2 s note pattern repeats), CHORD[=k] holds k keys
 * (default 4, up to 8), DRUMS=1 adds GM drum hits (part 4: DRUM), BENCH=1 prints the render time,
 * DIST=d, LEVEL=l, SENDS=c,d,r, SWEEP=1, NOTE=k, OCT=o, PSET=id:v,..., VSWEEP=1, PSWEEP=id:a:b,
 * PRESET=1 (the whole preset: sends, voice mode), STEPS=n,n,... (a pattern, 0 = rest; see below).
 * TRACKS=DIR: the 4-track test (tracks_demo below): a bass / pad / lead / drums (DRUM) pattern
 * with live recording into DIR (mix + solos), checks, and the cost against one track. */
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define __attribute__(x)
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "felucca_tables.h"
#include "../firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
static struct { volatile uint32_t notes, buttons; } fm1_in;
#include "../firmware/src/core.h"
#include "../firmware/src/engines.c"
#include "../firmware/src/params.c"
#include "../firmware/src/mod.c"
#include "../firmware/src/voice.c"
#include "../firmware/src/slicer.c"
#include "../firmware/src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../firmware/src/usb.c"
#include "../firmware/src/midi_uart.c"                /* TRS MIDI IN: its parser (um_byte) feeds midi_in_q */
#include "../firmware/src/seq.c"
#define inst (trk[0])                   /* the single-part renders below: part 1 */

static void wav_hdr(FILE *f, uint32_t frames)
{
    uint32_t v;
    fwrite("RIFF", 1, 4, f); v = 36 + frames * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    uint16_t a = 1, ch = 2, ba = 4, bits = 16; uint32_t sr = FS, br = FS * 4;
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4; fwrite(&v, 4, 1, f);
}

static void wav_put(FILE *f, int32_t l, int32_t r)
{
    int16_t s[2] = {(int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l),
                    (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r)};
    fwrite(s, 2, 2, f);
}

/* ---------------------------------------------------------------- TRACKS --- */
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static uint32_t host_rows;                        /* rows put_step wrote (host_tracks_init: none) */
static void host_tracks_init(void)                /* as felucca_init: defaults, empty patterns */
{
    uint32_t i, k;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    song.g[G_RTYPE] = 0;                          /* ROOM, the power-on reverb until 1.2: the renders (goldens) as
                                                   * before, ROOM bit for bit; HALL, the default since: reverb_test.c */
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_E0; i++)
            trk[k].p[i] = TP[i].def;
    tk_defaults();                                /* (1.5: the tracker's song, empty) */
    host_rows = 0;
    song.master_q12 = 4096;
}

/* the factory preset as ui.c apply_preset_to sets it (sound, sends; not the steps, not the track's own:
 * the mix, ARP, SCL, the pattern parameters, the SLICER);
 * the engine as the UI asks for it (eng_req: the audio side switches; host_preset: at once) */
static void host_preset_values(track_t *t, uint32_t e, uint32_t pi, const preset_t *p)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    uint32_t i;
    t->eng_req = (uint8_t)e;
    t->preset = (uint8_t)pi;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = p->e[i];
    t->p[P_ATK] = p->env[0];
    t->p[P_DEC] = p->env[1];
    t->p[P_SUS] = p->env[2];
    t->p[P_REL] = p->env[3];
    t->p[P_ED_FLT] = p->fenv;
    t->p[P_VOICE] = p->mono ? V_LEGATO : V_POLY;
    for (i = 0; i < 4u; i++)
        t->p[P_DIST + i] = (int16_t)(p->fx[i] ? p->fx[i] - 1 : FX_DEF[i]);
    fm6_track_loaded(t);                          /* FM6: the preset's patch (ui.c apply_preset_to) */
}
static void host_preset_req(track_t *t, uint32_t e, uint32_t pi)
{
    uint32_t n = ENGINES[e]->npresets;
    if (!n) {
        t->eng_req = (uint8_t)e;
        t->preset = 0;
        for (uint32_t i = 0; i < 8u; i++)
            t->p[P_E0 + i] = ENGINES[e]->edit[i].def;
        return;
    }
    pi %= n;
    host_preset_values(t, e, pi, &ENGINES[e]->presets[pi]);
}
static void host_preset(track_t *t, uint32_t e, uint32_t pi)
{
    host_preset_req(t, e, pi);
    t->engine = t->eng_req;
}

/* the drums of the render tests: DRUM's kit (the power-on track 4; until 1.0.2 these used SAMPLE PERC) */
static void host_drums(track_t *t) { host_preset(t, ENGI_DRUM, 0); }

/* a step of the renders' songs, written as they were for the step sequencer of 1.4 (the goldens keep their songs): into
 * row i of the tracker's pattern 00, which plays them: its first note, a REST an OFF, a TIE nothing (the note rings);
 * the pattern as long as the longest track written. The other notes of a chord and the flags (accent, slide) are not
 * played (the tracker has no such cell); a row past TK_ROWS nothing */
enum { ST_NOTE, ST_TIE, ST_REST };                 /* (the 1.4 steps' kinds: the render songs' vocabulary) */
#define SF_ACCENT 1u
static void put_step(track_t *t, uint32_t i, uint32_t n, const uint8_t *notes, uint32_t time, uint32_t flags)
{
    (void)flags;
    if (i >= TK_ROWS)
        return;
    *tk_at(0, i, (uint32_t)(t - trk)) = tk_pack(time == ST_NOTE && n ? notes[0] : time == ST_REST ? TK_OFF : 0u, 0, 0, 0);
    if (i + 1u > host_rows)
        host_rows = i + 1u;
    tk.len[0] = (uint8_t)host_rows;
}

static uint32_t busy_now(void)                   /* sounding voices of the parts (none fading after a block) */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active;
    return n;
}

/* drum hits on part 4 (DRUM, the GM map): kick on the beats, snare on 2 and 4, hats on the 16ths
 * (k: 16ths since the start), each released at the next (a one-shot plays on through its release) */
static void drum_hit(uint32_t k)
{
    static uint8_t last;
    uint32_t n = k % 4u == 0u ? 36u : k % 8u == 4u ? 38u : 42u;
    if (last)
        trk_note_off(&trk[3], last);
    trk_note_on(&trk[3], n, 100);
    last = (uint8_t)n;
}

/* t's voices started since the voice clock (vage) was a0 */
static uint32_t started_since(const track_t *t, uint32_t a0)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].age > a0;
    return n;
}

/* the demo song (1.5: the tracker's pattern 00, 16 rows): T1 ANALOG ACID, T2 the power-on pad (TRK_DEF: FM6 PAD) holding
 * a note for 14 rows, T3 LOFI PULSE LD, T4 DRUM (a hit a row: kick on the beats, snare on 5 and 13, hats between);
 * 120 BPM, 8 bars. Bars 5..6: live recording: a clap into the drums late in row 3 (into row 4, not triggered twice),
 * MIDI ch 3 into the lead's row 1, two keys at once into the pad's empty row 6 (the first one recorded). solo: 0 = the
 * mix, 1..4 = that track only. Writes DIR/NAME; returns the number of failed checks. */
static void put_row(uint32_t k, uint32_t r, uint32_t note) { *tk_at(0, r, k) = tk_pack(note, 0, 0, 0); }
static int tracks_demo(const char *dir, const char *name, uint32_t solo)
{
    static const uint8_t ACID[16] = {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50};
    static const uint8_t LEAD[16] = {76, 0, 0, 79, 0, 0, 81, 0, 79, 0, 76, 0, 74, 0, 72, 0};
    const uint32_t frames = 16u * FS, bar = 2u * FS;
    char path[512];
    FILE *w;
    uint32_t f, i, bmax = 0, clips = 0, fail = 0, clap_done = 0, lead_done = 0, keys_t = 0, keys_on = 0, keys_done = 0;
    uint32_t clap_age = 0, clap_hits = 0xFFFFu, clap_pass = 0, kills0;
    track_t *t1 = &trk[0], *t2 = &trk[1], *t3 = &trk[2], *td = &trk[3];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(w = fopen(path, "wb"))) {
        fprintf(stderr, "tracks: cannot write %s\n", path);
        return 1;
    }
    host_tracks_init();
    song.g[G_BPM] = 120;
    host_preset(t1, 0, 4);
    host_preset(t2, TRK_DEF[1][0], TRK_DEF[1][1]);
    host_preset(t3, 3, 0);
    host_drums(td);
    tk.len[0] = 16;
    for (i = 0; i < 16u; i++) {
        if (ACID[i]) put_row(0, i, ACID[i]);
        if (LEAD[i]) put_row(2, i, LEAD[i]);
        put_row(3, i, i % 4u == 0u ? 36u : i == 4u || i == 12u ? 38u : i % 2u == 0u ? 42u : 0u);
    }
    put_row(1, 0, 57);
    put_row(1, 14, TK_OFF);
    if (solo) {                                    /* the other tracks silent */
        for (i = 0; i < NPART; i++)
            if (i + 1u != solo)
                trk[i].p[P_LEVEL] = 0;
    }
    kills0 = voice_kills;
    transport_req = 1;
    wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        uint32_t barn = f / bar, period = tk_row_len(tk_pl.row);
        if (barn == 4u && !song.rec)
            song.rec = 0x0Eu;                      /* bars 5..6: tracks 2, 3, 4 armed */
        if (barn == 6u)
            song.rec = 0;
        /* (a) a clap into the drums, late in row 3: lands on row 4, sounds now, row 4 does not play it again */
        if (song.rec && !clap_done && tk_pl.row == 3u && tk_pl.row_pos > period * 3u / 4u) {
            input_on(td, 39, 110);
            clap_done = 1;
            clap_age = vage;
            clap_pass = 1;
        } else if (clap_pass == 1u && tk_pl.row == 4u) {
            clap_hits = started_since(td, clap_age);   /* what row 4 triggered right after (nothing: the clap sounds) */
            clap_pass = 2;
        }
        /* (b) MIDI ch 3 into the lead, early in row 1 */
        if (song.rec && !lead_done && tk_pl.row == 1u && tk_pl.row_pos < period / 4u) {
            midi_in_q[mi_w++ % MQ] = 0x09u | 0x92u << 8 | 84u << 16 | 100u << 24;
            lead_done = 1;
        } else if (lead_done == 1u && tk_pl.row == 2u) {
            midi_in_q[mi_w++ % MQ] = 0x08u | 0x82u << 8 | 84u << 16;
            lead_done = 2;
        }
        /* (c) the keys, track 2 selected: two keys at once into the pad's empty row 6 */
        if (song.rec && !keys_done && tk_pl.row == 6u && tk_pl.row_pos < period / 4u) {
            song.sel = 1;
            fm1_in.notes = (1u << 7) | (1u << 11);
            keys_on = keys_done = 1;
            keys_t = f;
        } else if (keys_on && f - keys_t > period / 2u) {
            fm1_in.notes = 0;
            keys_on = 0;
        }
        mix_block(o, CTL);
        if (busy_now() > bmax)
            bmax = busy_now();
        for (i = 0; i < CTL; i++) {
            int32_t l = o[2 * i];
            if (l >= 32700 || l <= -32700)
                clips++;
            wav_put(w, o[2 * i], o[2 * i + 1]);
        }
    }
    fclose(w);
    if (solo)
        return bmax > NVOICE;
    {
        int ok_clap = tk_note(tk_at(0, 4, 3)) == 39u && clap_hits == 0u;
        int ok_lead = tk_note(tk_at(0, 1, 2)) == 84u;
        int ok_keys = tk_note(tk_at(0, 6, 1)) == kb_map(t2, 7);
        printf("tracks: recording: drums row 4 = %u (want the clap, 39), hits when row 4 played right after: %u (want 0: "
               "not triggered twice) %s\n", tk_note(tk_at(0, 4, 3)), clap_hits, ok_clap ? "ok" : "FAIL");
        printf("tracks: recording: MIDI ch 3 -> lead row 1 = %u (want 84) %s; two keys -> pad row 6 = %u (the first) %s\n",
               tk_note(tk_at(0, 1, 2)), ok_lead ? "ok" : "FAIL", tk_note(tk_at(0, 6, 1)), ok_keys ? "ok" : "FAIL");
        printf("tracks: voices sounding at most %u (budget %u) %s; given up to another part %u; %u samples near full "
               "scale\n", bmax, NVOICE, bmax <= NVOICE ? "ok" : "FAIL", voice_kills - kills0, clips);
        fail = !ok_clap + !ok_lead + !ok_keys + (bmax > NVOICE) + (clips > 0);
    }
    return (int)fail;
}

/* ns per 44.1 kHz sample of the whole mix (events, parts, buses, master) with notes held:
 * parts[k] = {engine, preset, notes} for part k, 0 notes = idle; part 4 DRUM_HITS: drum_hit on 16ths. 2 s to settle, then the
 * fastest of four 2 s stretches (the host's other load only ever adds). Runs in a child process
 * (a clean state each time); the result comes back through a pipe. */
#define DRUM_HITS 0xFFu
static double tracks_cost(const uint8_t parts[NPART][3], uint32_t *busy_max)
{
    int fd[2];
    pid_t pid;
    double r[2] = {-1, 0};
    if (pipe(fd))
        return -1;
    fflush(stdout);
    if (!(pid = fork())) {
        static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
        uint64_t ns = 0, best = ~0ull, k, nblk = 8u * FS / CTL, seg = nblk / 4u;   /* the fastest of 4 x 2 s: */
        uint32_t p, i, bm = 0;
        int32_t o[2 * CTL];
        host_tracks_init();
        for (p = 0; p < NPART; p++) {
            host_preset(&trk[p], parts[p][0], parts[p][1]);
            if (parts[p][2] == DRUM_HITS)
                continue;
            trk[p].p[P_VOICE] = V_POLY;
            trk[p].p[P_SUS] = 127;                  /* held notes keep sounding */
            trk[p].p[P_AMODE] = 0;
            for (i = 0; i < parts[p][2]; i++)
                trk_note_on(&trk[p], NOTES[i] + 12u * p, 100);
        }
        for (k = 0; k < 2u * FS / CTL + nblk; k++) {   /* 2 s to settle (attacks), then measure */
            uint64_t t0;
            if (parts[3][2] == DRUM_HITS && (k * CTL) % (FS / 8u) < CTL)   /* 16ths at 120 BPM */
                drum_hit((k * CTL) / (FS / 8u));
            t0 = now_ns();
            mix_block(o, CTL);
            if (k >= 2u * FS / CTL) {                  /* (the host's own noise only ever adds) */
                ns += now_ns() - t0;
                if ((k - 2u * FS / CTL) % seg == seg - 1u) {
                    best = ns < best ? ns : best;
                    ns = 0;
                }
            }
            if (busy_now() > bm)
                bm = busy_now();
        }
        r[0] = (double)best / (double)(seg * CTL);
        r[1] = bm;
        if (write(fd[1], r, sizeof r) != sizeof r)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], r, sizeof r) != sizeof r)
        r[0] = -1;
    close(fd[0]);
    waitpid(pid, 0, 0);
    if (busy_max)
        *busy_max = (uint32_t)r[1];
    return r[0];
}

/* stealing across parts must not click: three parts of plain sines (smooth: the largest sample
 * step is set by the pitches), 7 + 1 notes fill the budget, then notes on parts 2 and 3 take held
 * voices of part 1 (and part 2). Each taken voice fades over one block. Compares the largest sample
 * step around each take with the largest one in the 0.25 s before it; a hard cut would be several
 * times larger. Writes DIR/steal.wav. */
static int steal_test(const char *dir)
{
    static const uint8_t N1[7] = {48, 52, 55, 59, 62, 65, 69};
    const uint32_t frames = 4u * FS;
    char path[512];
    FILE *w;
    int32_t prev = 0, *L = calloc(frames + CTL, sizeof *L);   /* (the last block may run past frames) */
    uint32_t f, i, p, k, events[6], nev = 0, bad = 0, kills0;
    double worst = 0;
    snprintf(path, sizeof path, "%s/steal.wav", dir);
    if (!L || !(w = fopen(path, "wb")))
        return 1;
    host_tracks_init();
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        host_preset(t, 0, 5);                      /* ANALOG SINE KEY, as a plain held sine: */
        t->p[P_E4] = 127;                          /* filter open, no resonance, no drive */
        t->p[P_E5] = t->p[P_E6] = 0;
        t->p[P_ED_FLT] = 0;
        t->p[P_ATK] = 40;                          /* slow attack, full sustain, no sends */
        t->p[P_SUS] = 127;
        t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
        t->p[P_VOICE] = V_POLY;
        t->p[P_LEVEL] = 90;
    }
    kills0 = voice_kills;
    wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        uint32_t ms = f * 1000u / FS, k0 = voice_kills;
        if (f == 0)
            for (i = 0; i < 7u; i++)
                trk_note_on(&trk[0], N1[i], 90);
        if (f == FS / 2u / CTL * CTL)
            trk_note_on(&trk[1], 74, 90);          /* 8: the budget is full */
        if (ms >= 1000u && ms % 500u == 0u && f % (FS / 2u) < CTL && ms < 3500u) {
            p = ms / 500u % 2u ? 1u : 2u;          /* parts 2, 3 in turn: each takes a voice */
            trk_note_on(&trk[p], 76u + ms / 250u, 90);
        }
        mix_block(o, CTL);
        if (voice_kills != k0 && nev < 6u)
            events[nev++] = f;
        for (i = 0; i < CTL; i++) {
            L[f + i] = o[2 * i];
            wav_put(w, o[2 * i], o[2 * i + 1]);
        }
    }
    fclose(w);
    for (k = 0; k < nev; k++) {                    /* around the take vs. the 0.25 s before */
        int32_t calm = 0, at = 0;
        uint32_t e = events[k];
        for (i = e - FS / 4u; i < e - 2u * CTL; i++)
            calm = abs(L[i] - L[i - 1]) > calm ? abs(L[i] - L[i - 1]) : calm;
        for (i = e; i < e + 2u * CTL; i++)
            at = abs(L[i] - L[i - 1]) > at ? abs(L[i] - L[i - 1]) : at;
        if ((double)at / calm > worst)
            worst = (double)at / calm;
        if (at > calm * 3 / 2)
            bad++;
    }
    (void)prev;
    printf("tracks: stealing across parts: %u voices taken; largest sample step at a take / before it: at most %.2f x "
           "(a hard cut: several x) %s\n", voice_kills - kills0, worst, nev >= 4u && !bad ? "ok" : "FAIL");
    free(L);
    return nev < 4u || bad;
}

/* a preset change across engines while notes sound (the PRESETS knob, the editor's PRESET) must not click:
 * part 1 as a plain sine pad (ANALOG SINE KEY, filter open, slow release, no sends: smooth, so the largest
 * sample step is set by the pitches), (a) a chord held and released, the switch to FM6 (its TINE EP) in its release
 * tail; (b) twice a chord held, the switch to PHASE / LOFI while it is held (the preset change releases
 * it); (c) a switch to WAVE with a note in its second block: that note must sound on the new engine. As ui.c
 * set_engine_of + apply_preset_to: eng_req, the new engine's values and panic_req at once (the main loop
 * writes them with the audio IRQ off). Compares the largest sample step in the 10 ms after (a) and (b)
 * with the largest one in the 0.1 s before (a hard cut of the voices: many times larger).
 * Writes DIR/engine_switch.wav. */
static void xfade_sine(track_t *t)
{
    host_preset(t, 0, 5);                          /* ANALOG SINE KEY, as a plain held sine */
    t->p[P_E4] = 127;
    t->p[P_E5] = t->p[P_E6] = 0;
    t->p[P_ED_FLT] = 0;
    t->p[P_ATK] = 40;
    t->p[P_SUS] = 127;
    t->p[P_REL] = 100;
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_LEVEL] = 100;
}
static int xfade_test(const char *dir)
{
    static const uint8_t CH[3] = {57, 60, 64};
    const uint32_t frames = 4u * FS, B = FS / 10u / CTL * CTL;   /* B: 0.1 s in whole blocks */
    /* switches: (a) in a release tail, (b) twice with a chord held, (c) with a note right after it */
    const uint32_t sw[4] = {11u * B, 23u * B, 29u * B + 7u * CTL, 33u * B};
    const uint32_t on[3] = {B, 15u * B, 25u * B}, sine[3] = {14u * B, 24u * B, 32u * B};
    const uint8_t to[4] = {ENGI_FM6, 2, 3, ENGI_WAVE};
    char path[512];
    FILE *w;
    int32_t *L = calloc(frames + CTL, sizeof *L);               /* (the last block may run past frames) */
    uint32_t f, i, k, bad = 0, late_ok = 0;
    double worst = 0;
    track_t *t = &trk[0];
    snprintf(path, sizeof path, "%s/engine_switch.wav", dir);
    if (!L || !(w = fopen(path, "wb")))
        return 1;
    host_tracks_init();
    xfade_sine(t);
    wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        for (k = 0; k < 3u; k++) {
            if (f == sine[k])
                xfade_sine(t);                     /* silent since the switch before: the sine again at once */
            if (f == on[k])
                for (i = 0; i < 3u; i++)
                    trk_note_on(t, CH[i] - 2u * k, 100);
        }
        if (f == 9u * B)
            for (i = 0; i < 3u; i++)
                trk_note_off(t, CH[i]);            /* (a): the release tail rings */
        for (k = 0; k < 4u; k++)
            if (f == sw[k]) {
                host_preset_req(t, to[k], 0);
                t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
                panic_req |= 1u;
            } else if (k == 3u && f == sw[k] + CTL) {
                trk_note_on(t, 60, 100);           /* (c): a note during the fade */
            }
        if (f == sw[3] + FS / 50u / CTL * CTL) {
            uint32_t v;
            for (v = 0; v < NVOICE; v++)
                late_ok |= t->v[v].active && t->v[v].note == 60u && t->v[v].gate;
            late_ok = late_ok && t->engine == to[3];
            trk_note_off(t, 60);
        }
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            L[f + i] = o[2 * i];
            wav_put(w, o[2 * i], o[2 * i + 1]);
        }
    }
    fclose(w);
    for (k = 0; k < 3u; k++) {
        int32_t calm = 1, at = 0;
        for (i = sw[k] - FS / 10u; i < sw[k]; i++)
            calm = abs(L[i] - L[i - 1]) > calm ? abs(L[i] - L[i - 1]) : calm;
        for (i = sw[k]; i < sw[k] + FS / 100u; i++)
            at = abs(L[i] - L[i - 1]) > at ? abs(L[i] - L[i - 1]) : at;
        printf("tracks: engine switch %s -> %s (%s): largest sample step %d in the 10 ms after, %d in the 0.1 s "
               "before (%.2f x)\n", ENGINES[0]->name, ENGINES[to[k]]->name, k ? "chord held" : "release tail", at,
               calm, (double)at / calm);
        worst = (double)at / calm > worst ? (double)at / calm : worst;
        bad += at > calm * 3 / 2;
    }
    printf("tracks: engine switch: at most %.2f x the step before (a hard cut: several x) %s\n", worst, bad ? "FAIL" : "ok");
    printf("tracks: a note during the switch sounds on the new engine (%s) %s\n", ENGINES[to[3]]->name,
           late_ok ? "ok" : "FAIL");
    free(L);
    return bad || !late_ok;
}

/* live recording into the tracker's rows (1.5): the nearest row (swung), OFF where the note is let go (not over a
 * note, not in its own row), the first note of a row kept (a chord: its first; MONO legato: each note its row).
 * Blocks are rendered until row idx is at fraction q (1/100) of its (swung) length; the keys go in between */
static void rec_run_to(uint32_t idx, uint32_t q)
{
    int32_t o[2 * CTL];
    uint32_t guard = 0;
    do {
        mix_block(o, CTL);
    } while (++guard < 100000u && !(tk_pl.row == idx && tk_pl.row_pos >= tk_row_len(idx) * q / 100u));
}
static uint32_t rec_note_at(uint32_t k, uint32_t r) { return tk_note(tk_at(0, r, k)); }
static int rec_test(void)
{
    track_t *t1 = &trk[0], *t3 = &trk[2];
    int ok_len, ok_short, ok_half, ok_swing, ok_mono, ok_chord, fail;
    host_tracks_init();
    song.g[G_BPM] = 120;
    host_preset(t1, 0, 1);                          /* POLY pad */
    host_preset(t3, 0, 0);                          /* SAW LEAD */
    t1->p[P_VOICE] = V_POLY;
    t3->p[P_VOICE] = V_MONO;
    tk.len[0] = 16;
    put_row(0, 5, 48);                              /* an older take */
    song.rec = 0x05u;
    transport_req = 1;
    /* (a) held 3.3 rows from row 2: 2 the note, 5 (let go early in it) keeps its note: no OFF over it */
    rec_run_to(2, 10);
    input_on(t1, 60, 100);
    rec_run_to(5, 30);
    input_off(t1, 60);
    ok_len = rec_note_at(0, 2) == 60u && !rec_note_at(0, 3) && !rec_note_at(0, 4) && rec_note_at(0, 5) == 48u;
    /* (b) let go 0.3 row in: its own row, no OFF */
    rec_run_to(8, 5);
    input_on(t1, 62, 100);
    rec_run_to(8, 35);
    input_off(t1, 62);
    rec_run_to(9, 50);
    ok_short = rec_note_at(0, 8) == 62u && !rec_note_at(0, 9);
    /* (c) let go past the middle of the next row: OFF on the row after it */
    rec_run_to(10, 10);
    input_on(t1, 64, 100);
    rec_run_to(11, 60);
    input_off(t1, 64);
    rec_run_to(12, 50);
    ok_half = rec_note_at(0, 10) == 64u && !rec_note_at(0, 11) && rec_note_at(0, 12) == TK_OFF;
    /* (d) a two-key chord: the first note; OFF where it is let go */
    rec_run_to(13, 5);
    input_on(t1, 67, 100);
    input_on(t1, 71, 100);
    rec_run_to(14, 20);
    input_off(t1, 67);
    rec_run_to(15, 60);
    input_off(t1, 71);
    rec_run_to(0, 10);
    ok_chord = rec_note_at(0, 13) == 67u && rec_note_at(0, 14) == TK_OFF && !rec_note_at(0, 15);
    /* (e) MONO, legato: A held from row 10, B in row 12 while A is down, A up, B up late in 13: 10 A, 12 B, 14 OFF */
    rec_run_to(10, 10);
    input_on(t3, 60, 100);
    rec_run_to(12, 10);
    input_on(t3, 62, 100);
    rec_run_to(12, 40);
    input_off(t3, 60);
    rec_run_to(13, 70);
    input_off(t3, 62);
    rec_run_to(14, 50);
    ok_mono = rec_note_at(2, 10) == 60u && !rec_note_at(2, 11) && rec_note_at(2, 12) == 62u && rec_note_at(2, 14) == TK_OFF;
    /* (f) CLOCK's SWING 50 %: 0.55 into an even (long) row is before its middle (stays); 0.42 into an odd (short, 0.8)
     * row is past its middle (the next one) */
    song.g[G_SWING] = 50;
    rec_run_to(4, 46);                              /* 0.46 x 1.2 = 0.55 */
    input_on(t3, 72, 100);
    input_off(t3, 72);
    rec_run_to(7, 53);                              /* 0.53 x 0.8 = 0.42 */
    input_on(t3, 74, 100);
    input_off(t3, 74);
    ok_swing = rec_note_at(2, 4) == 72u && !rec_note_at(2, 5) && rec_note_at(2, 8) == 74u && !rec_note_at(2, 7);
    song.g[G_SWING] = 0;
    printf("tracks: recording: held 3.3 rows: its row, no OFF over a note %s; 0.3 row: its row alone %s; past the middle of "
           "the next: OFF after it %s\n", ok_len ? "ok" : "FAIL", ok_short ? "ok" : "FAIL", ok_half ? "ok" : "FAIL");
    printf("tracks: recording: a chord its first note %s; MONO legato A..B -> A, B, OFF %s\n", ok_chord ? "ok" : "FAIL",
           ok_mono ? "ok" : "FAIL");
    printf("tracks: recording with SWING 50 %%: the nearest swung row (0.55 into a long row stays, 0.42 into a short one "
           "moves on) %s\n", ok_swing ? "ok" : "FAIL");
    fail = !ok_len + !ok_short + !ok_half + !ok_chord + !ok_mono + !ok_swing;
    return fail;
}

/* TRS MIDI IN (midi_uart.c, untested on hardware): bytes through its parser into the same queue as
 * USB, routed by channel: ROUT CH1-4 1..4 -> parts 1..4, others (10 too) ignored; ROUT SEL all -> the
 * selected track; running
 * status, note-on velocity 0 = note-off; a note-off on a "selected track" channel reaches the track
 * its note-on went to after another track was selected; recording into an armed track. */
static int trs_held(const track_t *t, uint32_t note)
{
    uint32_t v;
    for (v = 0; v < NVOICE; v++)
        if (t->v[v].active && t->v[v].gate && t->v[v].note == note)
            return 1;
    return 0;
}
static void trs_bytes(const uint8_t *b, uint32_t n)
{
    int32_t o[2 * CTL];
    while (n--)
        um_byte(*b++);
    mix_block(o, CTL);
}
static int trs_test(void)
{
    uint32_t i;
    int ok_parts, ok_ch10, ok_sel, ok_off, ok_hang, ok_rec;
    host_tracks_init();
    for (i = 0; i < NPART; i++) {
        host_preset(&trk[i], 0, 1);
        trk[i].p[P_VOICE] = V_POLY;
        trk[i].p[P_SUS] = 127;
    }
    song.sel = 1;
    trs_bytes((const uint8_t[]){0x90, 60, 100, 65, 0xF8, 100, 0x91, 62, 100, 0x92, 64, 100, 0x93, 48, 100, 0x99, 36, 110,
                                0x94, 67, 90}, 21);
    ok_parts = trs_held(&trk[0], 60) && trs_held(&trk[0], 65) && trs_held(&trk[1], 62) && trs_held(&trk[2], 64) &&
               trs_held(&trk[3], 48) && !trs_held(&trk[0], 62);
    /* ROUT CH1-4 (the default): ch 10 and ch 5 are not heard at all (kept free for other instruments) */
    ok_ch10 = !trs_held(&trk[1], 36) && !trs_held(&trk[3], 36);  /* ch 10: no drum channel any more */
    ok_sel = !trs_held(&trk[1], 67) && !trs_held(&trk[0], 67);
    song.g[G_ROUTE] = 1;                            /* ROUT SEL: every channel -> the selected track */
    trs_bytes((const uint8_t[]){0x99, 36, 110, 0x94, 67, 90}, 6);
    ok_ch10 &= trs_held(&trk[1], 36) && !trs_held(&trk[3], 36);
    ok_sel &= trs_held(&trk[1], 67) && !trs_held(&trk[0], 67);
    trs_bytes((const uint8_t[]){0x90, 60, 0, 65, 0, 0x81, 62, 0, 0x82, 64, 64, 0x83, 48, 0, 0x89, 36, 0}, 17);   /* vel 0 = off, 0x8n */
    ok_off = !trs_held(&trk[0], 60) && !trs_held(&trk[0], 65) && !trs_held(&trk[1], 62) && !trs_held(&trk[2], 64) &&
             !trs_held(&trk[3], 48) && !trs_held(&trk[1], 36);
    song.sel = 2;                                   /* another track selected while ch 5's note is down */
    trs_bytes((const uint8_t[]){0x84, 67, 0}, 3);
    ok_hang = !trs_held(&trk[1], 67);
    song.rec = 1u << 2;                             /* track 3 armed, transport on: ch 3 records */
    transport_req = 1;
    trs_bytes((const uint8_t[]){0xF8}, 1);
    trs_bytes((const uint8_t[]){0x92, 72, 100}, 3);
    trs_bytes((const uint8_t[]){0x92, 72, 0}, 3);
    ok_rec = tk_note(tk_at(0, 0, 2)) == 72u && !tk_note(tk_at(0, 0, 1));   /* (1.5: the tracker's row 00) */
    printf("tracks: TRS MIDI IN: ch 1..4 -> parts %s, ch 10 and ch 5 ignored on CH1-4, the selected track on SEL %s %s; note-offs "
           "(running status, vel 0) %s\n", ok_parts ? "ok" : "FAIL", ok_ch10 ? "ok" : "FAIL", ok_sel ? "ok" : "FAIL",
           ok_off ? "ok" : "FAIL");
    printf("tracks: TRS MIDI IN: note-off after another track was selected reaches the note's track %s; ch 3 records "
           "into armed track 3 %s\n", ok_hang ? "ok" : "FAIL", ok_rec ? "ok" : "FAIL");
    return !ok_parts + !ok_ch10 + !ok_sel + !ok_off + !ok_hang + !ok_rec;
}

static int tracks_test(const char *dir)
{
    static const char *const SOLO[5] = {"tracks_demo.wav", "t1_bass.wav", "t2_pad.wav", "t3_lead.wav", "t4_drums.wav"};
    uint32_t e, pi, s, worst_e = 0, worst_p = 0, heavy[NENGINES] = {0}, bm = 0;
    double worst = 0, best_e[NENGINES] = {0}, four, four_full;
    int fail = 0;
    for (s = 0; s < 5u; s++) {
        pid_t pid;
        fflush(stdout);
        pid = fork();
        int st = 0;
        if (!pid)
            { int rc = tracks_demo(dir, SOLO[s], s); fflush(stdout); _exit(rc); }
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st))
            fail++;
    }
    {
        pid_t pid;
        int st = 0;
        fflush(stdout);
        pid = fork();
        if (!pid) {
            int rc = steal_test(dir);
            fflush(stdout);
            _exit(rc);
        }
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st))
            fail++;
        fflush(stdout);
        pid = fork();
        if (!pid) {
            int rc = trs_test();
            fflush(stdout);
            _exit(rc);
        }
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st))
            fail++;
        fflush(stdout);
        pid = fork();
        if (!pid) {
            int rc = rec_test();
            fflush(stdout);
            _exit(rc);
        }
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st))
            fail++;
        fflush(stdout);
        pid = fork();
        if (!pid) {
            int rc = xfade_test(dir);
            fflush(stdout);
            _exit(rc);
        }
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st))
            fail++;
    }
    printf("tracks: WAVs in %s: %s (mix), %s, %s, %s, %s, steal.wav, engine_switch.wav\n", dir, SOLO[0], SOLO[1], SOLO[2], SOLO[3], SOLO[4]);
    /* one part: every preset with 8 held notes (the engine's cap: VOICE 4); the worst one */
    for (e = 0; e < NENGINES; e++)
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            uint8_t parts[NPART][3] = {{(uint8_t)e, (uint8_t)pi, 8}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
            double c = tracks_cost(parts, 0);
            if (c > best_e[e]) {
                best_e[e] = c;
                heavy[e] = pi;
            }
            if (c > worst) {
                worst = c;
                worst_e = e;
                worst_p = pi;
            }
        }
    printf("tracks: one part, 8 notes held (host -O2, ns per sample, heaviest preset per engine):");
    for (e = 0; e < NENGINES; e++)
        if (eng_ok(e))
            printf(" %s %s %.1f%s", ENGINES[e]->name, ENGINES[e]->presets[heavy[e]].name, best_e[e],
                   e + 1u < NENGINES ? "," : "\n");
    {   /* FM6, PHASE, VOICE (their heaviest presets) and drums on part 4 at once: 3 + 3 + 2 notes and the hits in the
         * budget of 8 */
        const uint8_t fm = ENGI_FM6;
        uint8_t parts[NPART][3] = {{fm, (uint8_t)heavy[fm], 3}, {2, (uint8_t)heavy[2], 3}, {5, (uint8_t)heavy[5], 2},
                                   {ENGI_DRUM, 0, DRUM_HITS}};
        uint8_t full[NPART][3] = {{fm, (uint8_t)heavy[fm], 8}, {2, (uint8_t)heavy[2], 8}, {5, (uint8_t)heavy[5], 4},
                                  {ENGI_DRUM, 0, DRUM_HITS}};
        uint8_t vv[NPART][3] = {{5, (uint8_t)heavy[5], 4}, {5, (uint8_t)heavy[5], 4}, {0, 0, 0}, {0, 0, 0}};
        uint8_t idle[NPART][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {ENGI_DRUM, 0, DRUM_HITS}};
        uint32_t bm2 = 0, bm3 = 0;
        double two_voice, none;
        four = tracks_cost(parts, &bm);
        four_full = tracks_cost(full, &bm2);
        two_voice = tracks_cost(vv, &bm3);
        none = tracks_cost(idle, 0);
        printf("tracks: worst single part: %s %s %.1f ns\n", ENGINES[worst_e]->name, ENGINES[worst_e]->presets[worst_p].name, worst);
        printf("tracks: %s + PHASE + VOICE + drums, 3 + 3 + 2 notes: %.1f ns (%.2f x the worst single part), "
               "%u voices\n", ENGINES[fm]->name, four, four / worst, bm);
        printf("tracks: the same, 8 + 8 + 4 notes asked for (budget keeps %u): %.1f ns (%.2f x)\n", bm2, four_full,
               four_full / worst);
        printf("tracks: two VOICE parts, 4 + 4 voices (the heaviest 8 the budget allows): %.1f ns (%.2f x); "
               "no notes but the drums (buses, 3 idle parts): %.1f ns\n", two_voice, two_voice / worst, none);
        if (bm > NVOICE || bm2 > NVOICE || bm3 > NVOICE)
            fail++;
    }
    printf("tracks: %s\n", fail ? "FAILED" : "all checks ok");
    return fail;
}

int main(int argc, char **argv)
{
    int eng = argc > 1 ? atoi(argv[1]) : 0, preset = argc > 2 ? atoi(argv[2]) : 4;
    int mono = argc > 3 ? atoi(argv[3]) : 1;
    const char *out = argc > 4 ? argv[4] : "out.wav";
    uint32_t i, frames = FS * (getenv("SWEEP") ? 4 : 2), f, nk = 0;
    clock_t c0;
    FILE *w = fopen(out, "wb");
    if (getenv("SECS"))
        frames = FS * (uint32_t)atoi(getenv("SECS"));
    if (getenv("CHORD"))
        nk = atoi(getenv("CHORD")) > 4 ? (uint32_t)atoi(getenv("CHORD")) : 4u;
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    for (i = 0; i < P_E0; i++) inst.p[i] = TP[i].def;
    inst.eng_req = inst.engine = (uint8_t)eng;
    for (i = 0; i < 8; i++) inst.p[P_E0 + i] = ENGINES[eng]->edit[i].def;
    if (ENGINES[eng]->npresets) {
        const preset_t *p = &ENGINES[eng]->presets[preset % ENGINES[eng]->npresets];
        for (i = 0; i < 8; i++) inst.p[P_E0 + i] = p->e[i];
        inst.p[P_ATK] = p->env[0]; inst.p[P_DEC] = p->env[1];
        inst.p[P_SUS] = p->env[2]; inst.p[P_REL] = p->env[3]; inst.p[P_ED_FLT] = p->fenv;
    }
    fm6_track_loaded(&inst);                            /* FM6: the preset's patch */
    inst.p[P_VOICE] = (int16_t)mono;
    inst.p[P_CHOR] = argc > 5 ? atoi(argv[5]) : 24;      /* as felucca_init */
    inst.p[P_DLY] = argc > 5 ? atoi(argv[5]) : 28;
    inst.p[P_REV] = argc > 5 ? atoi(argv[5]) : 36;
    if (getenv("PRESET"))                               /* PRESET=1: sends, voice mode too (then SENDS, PSET) */
        host_preset(&inst, (uint32_t)eng, (uint32_t)preset);
    if (getenv("SENDS"))                                 /* SENDS=chorus,delay,reverb */
        sscanf(getenv("SENDS"), "%hd,%hd,%hd", &inst.p[P_CHOR], &inst.p[P_DLY], &inst.p[P_REV]);
    if (getenv("OCT"))                                   /* OCT=o: keyboard octave shift (key 7 = C4 + 12 o) */
        song.octave = (int8_t)atoi(getenv("OCT"));
    if (getenv("PSET")) {                               /* PSET=id:value,... (track parameter ids, P_E0 = 61) */
        const char *s = getenv("PSET");
        int id, val, k;
        while (sscanf(s, "%d:%d%n", &id, &val, &k) == 2) {
            if (id >= 0 && id < P_COUNT)
                inst.p[id] = (int16_t)val;
            s += k;
            if (*s == ',')
                s++;
        }
    }
    if (getenv("STEPS")) {                              /* STEPS=n,n,...: a pattern (0 = rest) played by the sequencer */
        const char *s = getenv("STEPS");
        int nt, k, c = 0;
        while (c < TK_ROWS && sscanf(s, "%d%n", &nt, &k) == 1) {
            uint8_t u = (uint8_t)nt;
            put_step(&inst, (uint32_t)c++, nt ? 1u : 0u, &u, nt ? ST_NOTE : ST_REST, 0);
            s += k;
            if (*s == ',')
                s++;
        }
        inst.p[P_SLEN] = (int16_t)c;
        transport_req = 1;
    }
    song.master_q12 = 4096;
    if (getenv("TRACKS")) {
        fclose(w);
        remove(out);
        return tracks_test(getenv("TRACKS"));
    }
    wav_hdr(w, frames);
    c0 = clock();
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        uint32_t fp = f % (2u * FS);    /* position in the repeating 2 s pattern */
        {   /* C4 0.1-0.5 s, E4 0.45-0.8 s (overlap: legato/steal), G4 0.9-1.2 s */
            uint32_t n = 0, f = fp;
            if (f > FS / 10 && f < FS / 2) n |= 1u << 7;
            if (f > FS * 45 / 100 && f < FS * 8 / 10) n |= 1u << 11;
            if (f > FS * 9 / 10 && f < FS * 12 / 10) n |= 1u << 14;
            fm1_in.notes = getenv("STEPS") ? 0u : n;   /* STEPS: only the sequencer plays */
        }
        if (nk) {                       /* keys held 0.1-1.5 s (C E G B, then D F A C) */
            static const uint8_t K[8] = {7, 11, 14, 18, 9, 12, 16, 19};
            uint32_t n = 0, k;
            for (k = 0; k < nk && k < 8u; k++)
                n |= 1u << K[k];
            fm1_in.notes = (fp > FS / 10 && fp < FS * 3 / 2) ? n : 0;
        }
        if (getenv("DRUMS") && fp % (FS / 8u) < CTL) {   /* part 4 DRUM: drum_hit on 16ths */
            if (!f)
                host_drums(&trk[3]);
            drum_hit(f / (FS / 8u));
        }
        if (getenv("DIST"))
            inst.p[P_DIST] = (int16_t)atoi(getenv("DIST"));
        if (getenv("LEVEL"))
            inst.p[P_LEVEL] = (int16_t)atoi(getenv("LEVEL"));
        if (getenv("SWEEP")) {          /* cutoff and resonance sweeps while a note is held */
            uint32_t t = f * 1000 / FS;  /* ms */
            inst.p[P_E4] = (int16_t)((t / 4) % 256 < 128 ? (t / 4) % 128 : 127 - (t / 4) % 128);
            inst.p[P_E5] = (int16_t)((t / 7) % 256 < 128 ? (t / 7) % 128 : 127 - (t / 7) % 128);
            fm1_in.notes = (f > FS / 20) ? (1u << 7) : 0;
        }
        if (getenv("NOTE"))             /* NOTE=k: one key (7 = C4, see OCT) held to 0.5 s before the end */
            fm1_in.notes = (f > FS / 20 && f + FS / 2 < frames) ? 1u << atoi(getenv("NOTE")) : 0;
        if (getenv("VSWEEP"))           /* VSWEEP=1: EDIT 1 (P_E0) from 0 to 127 over the render */
            inst.p[P_E0] = (int16_t)((uint64_t)f * 128u / frames);
        if (getenv("PSWEEP")) {         /* PSWEEP=id:from:to: a track parameter from .. to over the render */
            int id, a, b;
            if (sscanf(getenv("PSWEEP"), "%d:%d:%d", &id, &a, &b) == 3 && id >= 0 && id < P_COUNT)
                inst.p[id] = (int16_t)(a + (int64_t)(b - a) * f / frames);
        }
        mix_block(o, CTL);
        {   /* VOICES=1: report the most voices active at once (engine voice caps) */
            static uint32_t vmax, hi;
            uint32_t k, a = 0;
            for (k = 0; k < NVOICE; k++)
                if (inst.v[k].active) {
                    a++;
                    hi |= 1u << k;
                }
            if (a > vmax)
                vmax = a;
            if (getenv("VOICES") && f + CTL >= frames)
                fprintf(stderr, "voices: at most %u active, slots used 0x%02X\n", vmax, hi);
        }
        for (i = 0; i < 2 * CTL; i++) {
            int16_t s = (int16_t)(o[i] > 32767 ? 32767 : o[i] < -32768 ? -32768 : o[i]);
            fwrite(&s, 2, 1, w);
        }
    }
    if (getenv("BENCH"))
        fprintf(stderr, "%.1f s audio in %.1f ms (%.2f %% of real time)\n", (double)frames / FS,
                (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC,
                (double)(clock() - c0) * 100.0 / CLOCKS_PER_SEC / ((double)frames / FS));
    fclose(w);
    return 0;
}
