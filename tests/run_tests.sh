#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md
# Host tests of the Felucca sources (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
#   PLUGIN=0   skip the plugin (its cores, the host, the VST3 / AU / CLAP bundles, pluginval)
#   NATIVE=0   skip the native image's build and boot (its host tests, native_test.c, still run)
#   SANITIZE=0 skip the sanitizer runs
#   JOBS_SUITE=n  steps at once (default: the CPU count); each step's output is printed whole, in the script's order
#   QUICK=1    the fast tier: the unit tests, no golden renders, UI renders, sanitizers, emulators, plugin or native image
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, the GM map on KIT, voice modes, FX sends, a 4-track mix: one hash
#                   each in tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             instructions / sample per preset and mix (tests/cpu_baseline.txt, +25 %), ns printed;
#                   target: loop instructions of the render functions in the pi32v2 disassembly
#                   (tests/target_budget.txt, +10 %; exact, static).
#   voices          the budget of 8, steal fades, MONO / LEGATO / UNISON keep their note, a 4-voice cap,
#                   no hanging notes on any MIDI / key routing.
# UI renders (tests/ui_render.c): every screen in every palette from the real drawing code: the layout lint (no text
#                   off the screen, cut, hidden, overlapping or spilling out of its cell / card; only free text
#                   ellipsised), GREY gray, MONO neutral, the draw cost, the text audit (build/ui_new/text_audit.tsv);
#                   PNGs of GREY MONO GREEN PAPER NIGHT in build/ui_new (tests/ui_render.py), the findings in build/ui_new/report.txt;
#                   every frame of the rolling digits (lint, GREY, MONO), their filmstrips in build/ui_slot;
#                   the alignment: every text / icon / keycap meant to be centred, or on its neighbours' line, by its
#                   ink against its box (cells, chips, buttons, rows, knobs, the roll's strip, the keycaps' pills), every
#                   screen in FLAT and LINE and every palette, over every value it can show; 1 px off or more fails
#                   (build/ui_new/align.txt). MENU > LARGE: every screen again (FLAT, LINE, every palette) and the page / value
#                   sweep with the tall cards, the same lint and alignment; sheet_LARGE_GREY.png, sheet_LARGE_MONO.png.
# UI (tests/ui_test.c): the UI sources against stub display / buttons / knobs: sound loads keep the track's
#                   ARP / SCL / SLICER, the sound's one-step undo (SAVE held), REC (arms only) on every page, ARP while
#                   recording, MIDI IN ROUT, saves refused while playing and the OVERWRITE? dialog, MUTE on TRACKS KNOB 1.
# Audio / persistence / editor: the real C paths against simulated DMA and NOR flash: bounded overload
#                   fades, shared-voice limits, deferred settings and retries, failed-save rollback, the autosave
#                   (1.2: when it writes, the power-on restore, damaged copies, a write cut short, wear over a session),
#                   malformed transfers, transport-stop timeouts, MIDI and UART recovery; the MENU settings over the
#                   editor (MENU_DESC / MENU_SET: every item, clamping, unknown ids, saving, USB SERIAL applied later).
# CHORD (tests/chord_test.c): the chord keys (src/chord.c): diatonic triads / sevenths of several scales and roots,
#                   the fixed shapes and voicings (at most 4 notes), names, MONO plays the root, a release ends
#                   exactly what its key / MIDI note started, recording, the ARP, MIDI IN, kits ignore CHRD.
# LFO 2 (tests/lfo2_test.c, 1.2): SYNC one cycle per note value at several tempi and on the external clock, OFF bit for bit
#                   as before; TRIG NOTE / FREE; POL UNI 0..+1, the matrix's AMP.
# TRACKER (1.5, in tests/ui_test.c test_tracker_*): LOOP and SONG playback (rows of a 1/16, swing, notes ringing to the next
#                   or OFF, the pattern cued, the order list round), a row's instrument and its one-row FX, the tracks' CUT,
#                   live recording into the rows, the pages' knobs / PRESETS / EDIT / keys, the cursor following the
#                   playing row a page of 8 at a time, the song saved and loaded whole (too big: refused; damaged: refused).
# MOD (tests/mod_test.c): the modulation matrix: slots that do nothing are bit-identical, every source on each
#                   kind of destination, clamping, MIDI CC1 / CC11 / aftertouch routing, the cost of 4 active
#                   slots (at most +5 %), demos in build/mod_demo/.
# SOUND (tests/sound14_test.c; #132 #104 #148): the matrix's S&H / SLEW sources (a value per LFO cycle, the glide) and
#                   DEPTH destination (the LFO's gain; none = untouched), (ANALOG's WAVE SYNC / SUB: gone with it;
#                   the band-limited restart), SPREAD (SPRD 0 / MONO / LEGATO bit for bit, a note hard left / right, the
#                   mono sum, UNISON, the SLICER's gate and the mute key on both sides, mono sends), the cost; demos in
#                   build/sound14_demo/.
# PERFORM (tests/perform_test.c): the FX hold layer (src/perform.c): 1/16 starts, stereo buffer effects, the
#                   too-long REPEAT, the SLICER interplay, silent layer keys, idle bit-identical, cost; build/perform_demo/;
#                   OCT UP / DN (the harmonizer): pitch, stereo, clicks, the shimmer bounded, cost; the key
#                   map (any effect or none on any key, a key remapped while held, FX LATCH); build/fx_demo/.
# REVERB (tests/reverb_test.c): REVERB TYPE (src/fx.c): ROOM bit for bit as before, SPRING's decay against SIZE,
#                   its chirp (group delay rising with frequency), stability at the corners, level, a model change
#                   without a click, its cost against ROOM (+30 % at most); demos in build/fx_demo/. 1.2's HALL: SPRING
#                   bit for bit as before, decay against SIZE and DAMP, stability, exact silence after the tail (27
#                   settings and levels), no offset, level, stereo, echo density / ringing / flutter against ROOM, SPRING
#                   and COMB8 (8 combs + 4 allpasses, host only, for comparison), model changes both ways, cost; the
#                   listening set in build/reverb_demo/ (drums, pluck, piano: dry and each model at SIZE 40 / 90 / 127).
#                   DELAY TYPE (Fel-Tracker): CLEAN as before, TAPE / WORN wobble, the runaway bounded and settling,
#                   TIME and type changes without a click, cost; build/fx_demo/delay_{clean,tape,worn}_pluck.wav.
# INPUT (tests/input_test.c): the key / button debounce of hal/fm1_input.h against the TIMER5 scan and bouncing
#                   contacts: a press within 2 scans (<= 2.3 ms), one note per bouncy press, no early or hanging
#                   release, stray samples ignored, fast repeats, the encoders' detents; the LED scan: lit LEDs every
#                   frame, dim ones a short pulse (the second line write) every frame, each only on its own column;
#                   the breath (#119): dark .. ~60 % of lit (DIM LO ~30 %), smooth, no dark run over ~10 ms near its peak;
#                   the power-on sweep (1.1, hal/fm1_led_anim.h): its length, every LED every frame at its level, the
#                   head left to right with its tail, the buttons, the end on the idle glow, the tick as before after;
#                   the idle animation (1.2, MENU > ANIM IDLE): endless, every LED every frame, the light there and back,
#                   the buttons breathing, a key down ends it before its next frame.
# CLICK (tests/click_test.c, 1.1): the metronome (src/click.c, src/seq.c) through audio.c: each beat at the first
#                   sample of its block, with a 1/16 track's steps 1 5 9 13 (internal and external clock, DIV 1/8 and
#                   SWING too), the accent, the frequencies, length, LEVEL, MASTER, OFF / REC / ON, USB audio bit for bit
#                   without it; 1.5: no count-in (PLAY armed plays at once); build/click_demo/.
# USB audio (tests/uac_test.c): the UAC1 descriptors as a host parses them (with and without CDC; 44100 and
#                   48000 Hz), the ring and packetiser: 44.1 frames per packet, every frame in order (bit for bit),
#                   underrun / overrun, restart; SET_CUR; the 44.1 -> 48 kHz resampler (every frame against a
#                   double-precision one, response, SNR, cost); the stream at 48 kHz (47..49 frames per packet,
#                   fast and slow I2S clocks, switching rates mid-stream).
# web (web/test_web.mjs): the editor protocol against its mock device, whose tables must equal the
#                   firmware's (tests/descdump.c -> build/host/desc.json; the MENU settings: tests/editor_test.c -> build/host/menu.json),
#                   the package builder, the updater.
# Browser emulator (web/emu, when emcc is there): the firmware in WebAssembly (build/emu) boots, plays keys and MIDI,
#                   draws, lights its LEDs, keeps a save across instances, plays a song bit for bit as the same file
#                   built with cc (web/emu/native_check.c); the cost of 1 s of a heavy song against real time.
# ENGINES (tests/engines_test.c): the IDM distribution (FELUCCA_DIST=idm, no HOOVER BYTE .. nor FOUNDRY's): its engines
#                   numbered from 0 in its order, the ids both ways, a left-out engine's sound loading as its substitute
#                   (projects, G_ENGSEL / PRESET, the song's instruments), its user presets kept but not listed, every
#                   engine left rendering.
# ROBUST (tests/robust_test.c): damaged or crafted stored data and editor requests: a song's fields out of range refused (an engine past the
#                   last, no position, an unknown FX) under a valid hash, values loaded inside their ranges, user preset
#                   patterns inside their fields, malformed requests never stop PLAY.
# Sanitizers (ASan + UBSan, when the compiler has them; SANITIZE=0 skips): the stored-data and protocol tests again
#                   (loader, M-UPGRADE entry, editor, projects, backup, ROBUST) and three short fuzz runs with fixed
#                   seeds: tests/fuzz_ed.c (editor SysEx and raw USB-MIDI packets), tests/fuzz_proj.c (random songs, their
#                   stored chunks mutated -> find -> load -> play -> render). Longer runs: build/host/asan/fuzz_ed 300000 7 (iterations, seed), the same for the others.
#                   UBSan leaves out the DSP's intended wraps (signed overflow, shifts, bounds, object-size,
#                   pointer-overflow).
# Change baseline entries only for reviewed, intentional differences in sound or cost;
# retain every unaffected golden / CPU / target entry. VERBOSE=1: every render.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
# (Fel-Tracker) the steps run in the background, JOBS_SUITE at once; their output is printed whole and in order as they
# end (report). A step that reads what an earlier one wrote follows a barrier (every running step ended); run_now: a
# step alone (the builds, the emulators' benchmarks against real time). Compiles stay in the script, in order.
JOBS_SUITE=${JOBS_SUITE:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}
LOGS="$OUT/logs"
rm -rf "$LOGS"
mkdir -p "$LOGS"
nj=0
shown=0
pids=""
report() {
    while [ $shown -lt $nj ] && [ -f "$LOGS/$((shown + 1)).rc" ]; do
        shown=$((shown + 1))
        echo "== $(cat "$LOGS/$shown.name")"
        cat "$LOGS/$shown.log"
        if [ "$(cat "$LOGS/$shown.rc")" != 0 ]; then
            fail=1
            echo "== FAILED: $(cat "$LOGS/$shown.name")"
        fi
    done
}
pool() {                                                # (a free slot: at most JOBS_SUITE steps and compiles unfinished)
    while [ $((nj + nc - $(ls "$LOGS" | grep -c '\.rc$'))) -ge "$JOBS_SUITE" ]; do
        sleep 0.05
    done
}
nc=0
cpend=""
compile() {                                             # a compile in the background; a step waits for the binaries it names
    pool
    nc=$((nc + 1))
    o=""
    prev=""
    for a in "$@"; do
        [ "$prev" = "-o" ] && o=$a
        prev=$a
    done
    printf '%s\n' "$o" > "$LOGS/c$nc.name"
    (if "$@" > "$LOGS/c$nc.log" 2>&1; then echo 0; else echo 1; fi > "$LOGS/c$nc.rc.tmp"; mv "$LOGS/c$nc.rc.tmp" "$LOGS/c$nc.rc") &
    cpend="$cpend $nc:$o"
}
run() {
    pool
    nj=$((nj + 1))
    printf '%s\n' "$1" > "$LOGS/$nj.name"
    shift
    deps=""
    rest=""
    for e in $cpend; do                                 # (the compiles of the binaries it names: awaited in the step)
        case " $* " in
            *"${e#*:}"*) deps="$deps ${e%%:*}" ;;
            *) rest="$rest $e" ;;
        esac
    done
    cpend=$rest
    after=${AFTER:-}                                    # (AFTER=n: the step waits for step n's end too)
    AFTER=""
    (if (while [ -n "$after" ] && [ ! -f "$LOGS/$after.rc" ]; do sleep 0.05; done
         for k in $deps; do
             while [ ! -f "$LOGS/c$k.rc" ]; do sleep 0.05; done
             [ "$(cat "$LOGS/c$k.rc")" = 0 ] || { echo "(its compile failed: $(cat "$LOGS/c$k.name"))"; cat "$LOGS/c$k.log"; exit 1; }
         done) > "$LOGS/$nj.log" 2>&1 && "$@" >> "$LOGS/$nj.log" 2>&1; then echo 0; else echo 1; fi > "$LOGS/$nj.rc.tmp"
     mv "$LOGS/$nj.rc.tmp" "$LOGS/$nj.rc") &
    report
}
barrier() {
    wait
    for e in $cpend; do                                 # (compiles no step ran: their failures)
        k=${e%%:*}
        [ "$(cat "$LOGS/c$k.rc")" = 0 ] || { fail=1; echo "== FAILED: compile $(cat "$LOGS/c$k.name")"; cat "$LOGS/c$k.log"; }
    done
    cpend=""
    report
    [ $shown -eq $nj ] || { fail=1; echo "== FAILED: $((nj - shown)) steps did not report"; }
}
run_now() { barrier; run "$@"; barrier; }
trap 'wait; report' EXIT
QUICK=${QUICK:-0}
slow() { [ "$QUICK" = 1 ] || "$@"; }              # (a heavy step: left out of QUICK=1)

compile $CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"

compile $CC -Ibuild/gen -o "$OUT/upreset_test" tests/upreset_test.c   # (core.h: build/gen/dist_default.h)
run "user presets (UP_PUT parser, bank round trip, versions, grid records, retired engines refused)" "$OUT/upreset_test"

compile $CC -o "$OUT/input_test" tests/input_test.c
run "keys and buttons: fast press, long release, bouncy contacts (one note each), glitches, encoders" "$OUT/input_test"

compile $CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"

HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/src/core.h)
compile $CC -O2 -DT_CDC=1 -DHALF_FRAMES=$HALF -o "$OUT/uac_test" tests/uac_test.c -lm
run "USB audio input: descriptors (with CDC), ring and packets, 44.1 -> 48 kHz resampler, 48 kHz stream" "$OUT/uac_test"
compile $CC -O2 -DT_CDC=0 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_nocdc" tests/uac_test.c -lm
run "USB audio input: descriptors (without CDC), ring and packets, resampler, 48 kHz stream" "$OUT/uac_test_nocdc"
# USB descriptor layouts (#67): CDC UAC LAYOUT CDC-presented 48K; layout 0 and the console left out = 1.0's bytes
# (+ the 48 kHz rate with 48K = 1, the default; 48K = 0: byte for byte)
for v in 1.1.0.1.1 1.1.1.1.1 1.1.2.1.1 1.1.3.1.1 1.1.0.0.1 1.1.2.0.1 0.1.0.1.1 1.1.0.1.0 1.1.0.0.0 1.1.2.1.0 0.1.0.1.0 \
         1.0.0.1.1 1.0.1.1.1 1.0.0.0.1 0.0.0.1.1; do
    IFS=. read -r t_cdc t_uac t_lay t_on t_48 <<EOF
$v
EOF
    compile $CC -DT_CDC="$t_cdc" -DT_UAC="$t_uac" -DT_LAYOUT="$t_lay" -DT_ON="$t_on" -DT_48K="$t_48" -o "$OUT/usb_desc_test_$v" \
        tests/usb_desc_test.c
    run "USB descriptors: CDC $t_cdc (presented $t_on), UAC $t_uac (48 kHz $t_48), layout $t_lay" "$OUT/usb_desc_test_$v"
done

[ -f build/felucca.fwsc ] || { echo "run ./build.sh first"; exit 1; }

compile $CC -DOWN_PKG=1 -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry (own loader)" "$OUT/ota_test" build/felucca.fwsc

head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
compile $CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc

if [ -f build/gen/felucca_tables.h ]; then
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/hostsim" tests/hostsim.c -lm
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/scale_test" tests/scale_test.c -lm
    run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
    compile $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/chord_test" tests/chord_test.c -lm
    run "chord keys: diatonic and fixed chords, voicings, MONO root, releases, recording, ARP, MIDI IN, kits" "$OUT/chord_test"
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/speaker_test" tests/speaker_test.c -lm
    run "SPEAKER EQ: FLAT / LOWCUT / BASS+ responses, BASS+ harmonics of the bass, the sub cut, no offset after, headroom on a full-scale kick (#180)" "$OUT/speaker_test"
    slow run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
    mkdir -p build/tracks_demo
    slow run "TRACKS: 4-track pattern, live recording (lengths, swing), voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
    compile $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/lfo2_test" tests/lfo2_test.c -lm
    run "1.2 LFO 2: SYNC / TRIG / POL" "$OUT/lfo2_test"
    compile $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/midi_control_test" tests/midi_control_test.c -lm
    run "USB/TRS clock, bend, sustain, ownership and panic recovery" "$OUT/midi_control_test"
    compile $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/theme_test" tests/theme_test.c -lm
    run "themes: contrast, text blending and font metrics" "$OUT/theme_test"
    compile $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/text_ref_test" tests/text_ref_test.c -lm
    run "text: pens, kerning and pixels equal the reference renderer" "$OUT/text_ref_test"
    if python3 -c "import PIL" 2>/dev/null; then
        run "text spacing: glyph gaps against the font's own (16x supersampled), S M <= 0.25 px, L <= 0.5 px" \
            python3 tests/text_spacing_test.py build/gen/ui_fonts.h
    fi
    compile $CC -O1 -w -Ibuild/gen -o "$OUT/settings_test" tests/settings_test.c
    run "settings: PER1..PER4 migration, palette ids and preference preservation" "$OUT/settings_test"
    compile $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test" tests/ui_test.c -lm
    run "UI: sounds, undo, recording, MIDI overflow, pending saves, panel recovery, the tracker (play, pages, song store), GREY gray, MONO neutral, retired engines" "$OUT/ui_test"
    compile $CC -w -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/tkplay_test" tests/tkplay_test.c -lm
    run "tracker PLAY commands: RETRIG, NOTE DELAY, ARPEGGIO, SLIDE UP / DOWN, CHANCE in their row; a row without them as before" "$OUT/tkplay_test"
    FV=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c)   # (the splash, ABOUT: the real version)
    slow compile $CC -O1 -w "-DFELUCCA_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/ui_render" tests/ui_render.c -lm
    mkdir -p build/ui_new/ppm build/ui_slot
    slow run "UI renders: layout lint (every screen and palette, every page, engine and column value), GREY gray, MONO neutral, alignment by ink (1 px fails), draw cost" \
        sh -c '"$1" build/ui_new build/ui_slot && { ! python3 -c "import PIL" 2>/dev/null || python3 tests/ui_render.py build/ui_new build/ui_slot; }' _ "$OUT/ui_render"
    slow compile $CC -O1 -w -DFELUCCA_NATIVE -DFELUCCA_DIST=idm_native "-DFELUCCA_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/src -Itests \
        -o "$OUT/ui_render_native" tests/ui_render.c -lm
    mkdir -p build/ui_native/ppm build/ui_native_slot
    slow run "UI renders, IDM NATIVE (the native image's): its engines, CONCERT CAVERN SHIMMER, its version line: the same lint and alignment" \
        "$OUT/ui_render_native" build/ui_native build/ui_native_slot
    compile $CC -w -DFELUCCA_NTRK=8 -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test8" tests/ui_test.c -lm
    run "UI built with 8 tracks (step 3c): every check above, the view 1..4 / 5..8, GLO / FX on 8 tracks, MIDI IN blocks of 8" "$OUT/ui_test8"
    slow compile $CC -O1 -w "-DFELUCCA_VERSION=\"$FV\"" -DFELUCCA_NTRK=8 -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/ui_render8" tests/ui_render.c -lm
    mkdir -p build/ui_8/ppm build/ui_8slot
    slow run "UI renders built with 8 tracks (step 3c): the layout lint on every screen, the tracker on tracks 5..8" "$OUT/ui_render8" build/ui_8 build/ui_8slot
    compile $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/audio_test" tests/audio_test.c -lm
    run "audio: overload protection, bounded fades and DMA diagnostics" "$OUT/audio_test"
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/click_test" tests/click_test.c -lm
    run "metronome: beats sample for sample (internal and external clock), accent, level, MASTER, not in USB, PLAY armed at once (no count-in)" "$OUT/click_test"
    compile $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/persistence_test" tests/persistence_test.c -lm
    run "persistence: deferred settings, retry and failed-save rollback" "$OUT/persistence_test"
    compile $CC -w -DFELUCCA_NTRK=8 -Ibuild/gen -Ifirmware/src -o "$OUT/persistence_test8" tests/persistence_test.c -lm
    run "persistence built with 8 tracks (step 3b): the 16-chunk song store, the autosave of many chunks, cut anywhere" "$OUT/persistence_test8"
    compile $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/backup_test" tests/backup_test.c -lm
    run "full backup: CRC before writes, stale runtime, USB reset / timeout, malformed objects, older projects" "$OUT/backup_test"
    compile $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/editor_test" tests/editor_test.c -lm
    run "editor: real C protocol, malformed transfers, queue recovery, MENU settings (writes build/host/menu.json)" \
        env MENU_JSON="$OUT/menu.json" "$OUT/editor_test"
    EDITOR_JOB=$nj
    compile $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/robust_test" tests/robust_test.c -lm
    run "robustness: engine ids, retained old projects, preset patterns, malformed requests" \
        "$OUT/robust_test"
    for n in 4 8; do
        compile $CC -w -DFELUCCA_NTRK=$n -Ibuild/gen -Ifirmware/src -o "$OUT/width_test$n" tests/width_test.c -lm
        run "WIDTH and LOW CUT ($n tracks): the Haas effect on the last tracks, the lows whole, the cut, the store, the editor" "$OUT/width_test$n"
        compile $CC -w -DFELUCCA_NTRK=$n -Ibuild/gen -Ifirmware/src -o "$OUT/duck_test$n" tests/duck_test.c -lm
        run "DUCK ($n tracks): the key ducks a pad at once and lets it back, as deep as its DUCK; none without a key; the store, the editor" "$OUT/duck_test$n"
    done
    for n in 4 8; do
        compile $CC -w -DFELUCCA_NTRK=$n -Ibuild/gen -Ifirmware/src -o "$OUT/variation_test$n" tests/variation_test.c -lm
        run "variations ($n tracks): tempo, mutes, chance, sends and SLICER over the tracks', STOP, the store, bounds" "$OUT/variation_test$n"
    done
    compile $CC -w -DFELUCCA_DIST=ref -DFELUCCA_NTRK=4 -Ibuild/gen -Ifirmware/src -o "$OUT/engines_test4" tests/engines_test.c -lm
    run "a build without some engines (the ref build, 4 tracks): ids, substitutes, protocol, songs, presets; a version-2 song for the next" "$OUT/engines_test4"
    compile $CC -w -DFELUCCA_DIST=ref -DFELUCCA_NTRK=8 -Ibuild/gen -Ifirmware/src -o "$OUT/engines_test" tests/engines_test.c -lm
    AFTER=$nj                                           # (it reads the 4-track run's song_v2.bin)
    run "a build without some engines, 8 tracks: the same checks, its power-on, a 4-track song on it" "$OUT/engines_test"
    compile $CC -w -DFELUCCA_DIST=idm -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/idm_test" tests/idm_test.c -lm
    run "IDM: its 8 engines in their order, 4 tracks, power-on, where the engines it lacks load" "$OUT/idm_test"
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/mod_test" tests/mod_test.c -lm
    mkdir -p build/mod_demo
    slow run "modulation matrix: off = bit-identical, the math, MIDI CC1 / CC11 / aftertouch, cost, demos" "$OUT/mod_test" build/mod_demo
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/sound14_test" tests/sound14_test.c -lm
    mkdir -p build/sound14_demo
    slow run "sound items: matrix S&H / SLEW / DEPTH, SPREAD (bit for bit at 0, L / R, mono sum), cost, demos" \
        "$OUT/sound14_test" build/sound14_demo
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/slicer_test" tests/slicer_test.c -lm
    mkdir -p build/slicer_demo
    slow run "SLICER: no clicks, timing, sync with the sequencer, STUT, cost, demos" "$OUT/slicer_test" build/slicer_demo
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/swing_test" tests/swing_test.c -lm
    run "SWING: track + global at most 100, sequencer and SLICER step lengths, the SWG display" "$OUT/swing_test"
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/perform_test" tests/perform_test.c -lm
    mkdir -p build/perform_demo build/fx_demo
    slow run "FX layer effects: on the 1/16, stereo, too-long REPEAT, SLICER, silent keys, idle bit-identical, clicks, OCT UP / DN, the key map, cost, demos" "$OUT/perform_test" build/perform_demo build/fx_demo
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/reverb_test" tests/reverb_test.c -lm
    mkdir -p build/reverb_demo
    slow run "REVERB TYPE: ROOM / SPRING bit-identical, SPRING and HALL decay / stability / level, HALL silence / stereo / ringing, model changes, DELAY TYPE, cost, demos" "$OUT/reverb_test" build/fx_demo build/reverb_demo
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/gated_test" tests/gated_test.c -lm
    run "REVERB TYPE GATED: HALL's level while it holds, cut after SIZE's hold, a longer SIZE longer, a hit opens it again" "$OUT/gated_test"
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/regress" tests/regress.c -lm
    slow run "regression, the IDM distribution (the primary) built with 4 tracks: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
    slow compile $CC -O2 -w -DFELUCCA_DIST=idm -Ibuild/gen -Ifirmware/src -o "$OUT/regress_idm" tests/regress.c -lm
    slow run "regression, the IDM distribution with its 8 tracks: its presets' renders and CPU" "$OUT/regress_idm" tests/golden.txt tests/cpu_baseline.txt
    slow compile $CC -O2 -w -DFELUCCA_DIST=foundry -Ibuild/gen -Ifirmware/src -o "$OUT/regress_foundry" tests/regress.c -lm
    slow run "regression, the FOUNDRY distribution: its presets' golden renders and CPU" "$OUT/regress_foundry" tests/golden.txt tests/cpu_baseline.txt
    compile $CC -w -DFELUCCA_DIST=foundry -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/foundry_test" tests/foundry_test.c -lm
    run "FOUNDRY: its engines and order, the clean ones among them, 24 presets and more, power-on, where the ones that left load" "$OUT/foundry_test"
    slow compile $CC -O2 -w -DFELUCCA_DIST=warehouse -Ibuild/gen -Ifirmware/src -o "$OUT/regress_warehouse" tests/regress.c -lm
    slow run "regression, the WAREHOUSE distribution: its presets' golden renders and CPU" "$OUT/regress_warehouse" tests/golden.txt tests/cpu_baseline.txt
    compile $CC -w -DFELUCCA_DIST=warehouse -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/warehouse_test" tests/warehouse_test.c -lm
    run "WAREHOUSE: its engines and order, power-on, SIREN8 on every track" "$OUT/warehouse_test"
    slow compile $CC -O2 -w -DFELUCCA_DIST=chip -Ibuild/gen -Ifirmware/src -o "$OUT/regress_chip" tests/regress.c -lm
    slow run "regression, the CHIP distribution (4 tracks): its presets' golden renders (CHIPKIT) and CPU" "$OUT/regress_chip" tests/golden.txt tests/cpu_baseline.txt
    compile $CC -w -DFELUCCA_DIST=chip -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/chip_test" tests/chip_test.c -lm
    run "CHIP: its engines and order, 4 tracks, its palette, power-on; CHIPKIT's drums sound and end, a console each, RATE" "$OUT/chip_test"
    for d in idm foundry warehouse chip idm_native; do
        slow compile $CC -O2 -w -DFELUCCA_NATIVE -DFELUCCA_DIST=$d -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/native_test_$d" tests/native_test.c -lm
        slow run "native image ($d): 16 voices a track, the device's engines as they are, NEBULA CANYON, CONCERT CAVERN SHIMMER, the long delay" "$OUT/native_test_$d"
    done
    slow compile $CC -O2 -w -DFELUCCA_NATIVE -DFELUCCA_DIST=idm_native -Ibuild/gen -Ifirmware/src -o "$OUT/regress_idm_native" tests/regress.c -lm
    slow run "regression, IDM NATIVE (the native image's): its presets' golden renders (NEBULA, CANYON; the rest as the device's)" "$OUT/regress_idm_native" tests/golden.txt tests/cpu_baseline.txt
    if ${CC%% *} -fsyntax-only -w -DFELUCCA_DIST=idm_native -Ibuild/gen -Ifirmware/src tests/hostsim.c 2>/dev/null; then
        echo "== FAILED: a NATIVE distribution builds without FELUCCA_NATIVE"; fail=1
    else
        echo "== a NATIVE distribution refuses a device build (core.h: FELUCCA_NATIVE only)"
    fi
    slow compile $CC -O2 -w -DFELUCCA_DIST=chip -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/speech_test" tests/speech_test.c -lm
    mkdir -p build/speech_demo
    slow run "SPEECH: every word speaks and ends, pitch, RATE, FORM, END, WORDS keys; demos (each bank's words)" "$OUT/speech_test" build/speech_demo
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/descdump" tests/descdump.c -lm
    echo "== parameter and engine tables as JSON (for the editor mock test)"
    "$OUT/descdump" > "$OUT/desc.json" || fail=1
else
    echo "== skip hostsim (run ./build.sh once)"
fi

# ASan + UBSan: the stored-data and protocol paths, and short fuzz runs (fixed seeds: the same inputs every run)
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize=signed-integer-overflow,shift,bounds,object-size,pointer-overflow"
SAN="$SAN -fno-sanitize-recover=undefined -w"
san_ok() {
    printf 'int main(void){return 0;}\n' > "$OUT/san_probe.c" &&
        ${CC%% *} $SAN -o "$OUT/san_probe" "$OUT/san_probe.c" 2>/dev/null && "$OUT/san_probe"
}
if [ "${SANITIZE:-1}" = 0 ] || [ "$QUICK" = 1 ]; then
    echo "== skip the sanitizer runs (SANITIZE=0 or QUICK=1)"
elif [ ! -f build/gen/felucca_tables.h ]; then
    echo "== skip the sanitizer runs (run ./build.sh once)"
elif ! san_ok; then
    echo "== skip the sanitizer runs (the compiler has no ASan / UBSan)"
else
    mkdir -p "$OUT/asan"
    A="$OUT/asan"
    SCC="${CC%% *} $SAN -Ibuild/gen -Ifirmware/src"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    $SCC -o "$A/ldr_test" tests/ldr_test.c
    run "ASan/UBSan: update loader (other app -> this build)" "$A/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc
    $SCC -DOWN_PKG=1 -o "$A/ota_test" tests/ota_test.c
    run "ASan/UBSan: M-UPGRADE entry (own loader)" "$A/ota_test" build/felucca.fwsc
    for t in editor_test backup_test persistence_test robust_test; do
        $SCC -o "$A/$t" tests/$t.c -lm
        run "ASan/UBSan: $t" "$A/$t"
    done
    $SCC -o "$A/fuzz_ed" tests/fuzz_ed.c -lm
    run "ASan/UBSan fuzz: editor SysEx and raw USB-MIDI packets (20000, seed 7)" "$A/fuzz_ed" 20000 7
    $SCC -o "$A/fuzz_proj" tests/fuzz_proj.c -lm
    run "ASan/UBSan fuzz: song stores mutated -> find -> load -> play -> render (2000, seed 13)" "$A/fuzz_proj" 2000 13
fi

run "regression: target cost of the render loops (pi32v2 disassembly)" python3 tests/target_budget.py \
    build/felucca.dis tests/target_budget.txt

run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py

if command -v node >/dev/null 2>&1; then
    AFTER=${EDITOR_JOB:-}                               # (the web tests read the editor test's menu.json)
    run "web pages: editor protocol + samples, package builder, update protocol" node web/test_web.mjs
    run "web backup: capture, validation before writes, restore order" node web/test_backup.mjs
else
    echo "== skip web tests (no node)"
fi

if [ "$QUICK" = 1 ]; then
    echo "== skip the browser emulator (QUICK=1)"
elif ! command -v emcc >/dev/null 2>&1; then
    echo "== skip the browser emulator (no emcc: Emscripten builds web/emu)"
elif ! command -v node >/dev/null 2>&1 || [ ! -f build/gen/felucca_tables.h ]; then
    echo "== skip the browser emulator (needs node and build/gen)"
else
    run_now "browser emulator: the firmware to WebAssembly (web/emu/build.sh -> build/emu)" sh web/emu/build.sh
    BUILD_DIST=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' distributions/BUILD | head -n 1 | tr -d ' ' | tr 'A-Z' 'a-z')
    # (build/emu holds the BUILD distribution: the native reference the same, as web/emu/build.sh reads it)
    cc -O2 -ffp-contract=off -w -DFELUCCA_DIST="$BUILD_DIST" -Ibuild/gen -Ifirmware/src -o "$OUT/emu_native" \
        web/emu/native_check.c -lm
    run_now "browser emulator: the same song from the native build" "$OUT/emu_native" "$OUT/emu_native.f32"
    run_now "browser emulator: boot, keys, MIDI, screen, LEDs, PLAY, a save kept across instances, = native, cost" \
        node web/emu/emu_test.mjs build/emu/felucca.wasm "$OUT/emu_native.f32"
    cc -O2 -ffp-contract=off -w -DFELUCCA_DIST=foundry -Ibuild/gen -Ifirmware/src -o "$OUT/emu_native_foundry" \
        web/emu/native_check.c -lm
    run_now "browser emulator: the same song from the native build, FOUNDRY" "$OUT/emu_native_foundry" "$OUT/emu_native_foundry.f32"
    run_now "browser emulator, the FOUNDRY distribution (web/emu/build.sh -> build/emu-foundry)" env FELUCCA_DIST=foundry sh web/emu/build.sh
    run_now "browser emulator, FOUNDRY: boot, keys, MIDI, screen, LEDs, PLAY, saves, = native, cost" \
        node web/emu/emu_test.mjs build/emu-foundry/felucca.wasm "$OUT/emu_native_foundry.f32"
    cc -O2 -ffp-contract=off -w -DFELUCCA_DIST=warehouse -Ibuild/gen -Ifirmware/src -o "$OUT/emu_native_warehouse" \
        web/emu/native_check.c -lm
    run_now "browser emulator: the same song from the native build, WAREHOUSE" "$OUT/emu_native_warehouse" "$OUT/emu_native_warehouse.f32"
    run_now "browser emulator, the WAREHOUSE distribution (web/emu/build.sh -> build/emu-warehouse)" env FELUCCA_DIST=warehouse sh web/emu/build.sh
    run_now "browser emulator, WAREHOUSE: boot, keys, MIDI, screen, LEDs, PLAY, saves, = native, cost" \
        node web/emu/emu_test.mjs build/emu-warehouse/felucca.wasm "$OUT/emu_native_warehouse.f32"
    cc -O2 -ffp-contract=off -w -DFELUCCA_DIST=chip -Ibuild/gen -Ifirmware/src -o "$OUT/emu_native_chip" \
        web/emu/native_check.c -lm
    run_now "browser emulator: the same song from the native build, CHIP" "$OUT/emu_native_chip" "$OUT/emu_native_chip.f32"
    run_now "browser emulator, the CHIP distribution (web/emu/build.sh -> build/emu-chip)" env FELUCCA_DIST=chip sh web/emu/build.sh
    run_now "browser emulator, CHIP: boot, keys, MIDI, screen, LEDs, PLAY, saves, = native, cost" \
        node web/emu/emu_test.mjs build/emu-chip/felucca.wasm "$OUT/emu_native_chip.f32"
fi

# (Fel-Tracker) the plugin's cores (plugin/build_core.sh: a library a distribution) and their instances (plugin/core)
if [ "${PLUGIN:-1}" = 0 ] || [ "$QUICK" = 1 ]; then
    echo "== skip the plugin's cores and host (PLUGIN=0 or QUICK=1)"
elif [ -f build/gen/felucca_tables.h ]; then
    run_now "plugin cores: one a distribution, fel_core_api alone exported" \
        env FEL_CORE_OUT="$OUT/cores" FEL_ARCH="$( [ "$(uname -s)" = Darwin ] && echo "-arch $(uname -m)")" sh plugin/build_core.sh
    cc -O2 -ffp-contract=off -w -DFELUCCA_DIST=idm_native -Ibuild/gen -Ifirmware/src -Iweb/emu -o "$OUT/plugin_core_ref" \
        tests/plugin_core_ref.c -lm
    run_now "plugin cores: the reference (IDM NATIVE, the native image's build, linked in)" "$OUT/plugin_core_ref" "$OUT/plugin_core_ref.f32"
    cc -O2 -w -o "$OUT/plugin_core_test" tests/plugin_core_test.c plugin/core/fel_loader.c -lpthread
    run_now "plugin instances: = the native image, independent (in turn, two threads), flash apart, the four side by side, copies kept and reused" \
        "$OUT/plugin_core_test" "$OUT/cores" "$OUT/plugin_core_ref.f32"
    cc -O2 -std=gnu11 -w -o "$OUT/plugin_host_test" tests/plugin_host_test.c plugin/src/fel_host.c plugin/src/fel_resample.c \
        plugin/core/fel_loader.c -lpthread -lm -lz
    run_now "plugin host: the resampler, the chooser, notes at their frame, a swap while playing; the host's transport (start, 60 s at 48 kHz, a loop), track outputs, the song saved and loaded; the window's view and panel, a switch with the song" \
        "$OUT/plugin_host_test" "$OUT/cores"
fi

# (Fel-Tracker) the plugin (plugin/build.sh: DPF, plugin/dpf a git submodule): VST3, AU, CLAP; the CLAP driven as a host
# drives it; pluginval on the VST3 when it is there (build/tools/pluginval-dl/pluginval.app, or PLUGINVAL=path)
if [ "${PLUGIN:-1}" = 0 ] || [ "$QUICK" = 1 ]; then
    echo "== skip the plugin (PLUGIN=0 or QUICK=1)"
elif [ ! -f plugin/dpf/Makefile.plugins.mk ] || [ ! -f build/gen/felucca_tables.h ] || [ "$(uname -s)" != Darwin ]; then
    echo "== skip the plugin (needs macOS and plugin/dpf: git submodule update --init --recursive)"
else
    run_now "plugin: VST3, AU and CLAP build, the cores in their bundles (plugin/build.sh -> build/plugin)" \
        env FEL_ARCH="-arch $(uname -m)" sh plugin/build.sh
    cc -O2 -std=gnu11 -w -Iplugin/dpf/distrho/src -o "$OUT/plugin_clap_test" tests/plugin_clap_test.c -lm
    run_now "plugin, as a host loads it (CLAP): an instrument, its latency, 9 stereo buses, notes, a track on its own bus, its parameters, the state, two instances" \
        "$OUT/plugin_clap_test" build/plugin/Fel-Tracker.clap
    PV=${PLUGINVAL:-build/tools/pluginval-dl/pluginval.app/Contents/MacOS/pluginval}
    if [ -x "$PV" ]; then
        run_now "plugin: pluginval, strictness 8, on the VST3 (build/plugin/pluginval.log)" sh -c \
            "\"$PV\" --strictness-level 8 --validate-in-process --skip-gui-tests --timeout-ms 300000 --validate build/plugin/Fel-Tracker.vst3 >build/plugin/pluginval.log 2>&1"
    else
        echo "== skip pluginval (plugin/get_pluginval.sh fetches it)"
    fi
fi

# (Fel-Tracker) the native image (native/build.sh: SDL2): built, then booted with dummy video and audio drivers
if [ "${NATIVE:-1}" = 0 ] || [ "$QUICK" = 1 ]; then
    echo "== skip the native image (NATIVE=0 or QUICK=1)"
elif ! command -v sdl2-config >/dev/null 2>&1 || [ ! -f build/gen/felucca_tables.h ]; then
    echo "== skip the native image (needs SDL2: brew install sdl2)"
else
    run_now "native image: the desktop application builds (native/build.sh -> build/native)" sh native/build.sh
    run_now "native image: boots (dummy drivers), the screen draws, the clock follows the audio, HOME lit, a key sounds, MASTER dragged and scrolled" \
        build/native/fel-tracker-idm --selftest
    run_now "native image, IDM NATIVE: builds (native/build.sh idm_native)" sh native/build.sh idm_native
    run_now "native image, IDM NATIVE: boots, the screen draws, the clock follows the audio, HOME lit, a key sounds" \
        build/native/fel-tracker-idm_native --selftest
fi

barrier
[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
