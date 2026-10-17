#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md
# Host tests of the Blue Hare sources (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
#   NATIVE=0   skip the native image's build and boot (its host tests, native_test.c, still run)
#   SANITIZE=0 skip the sanitizer runs
#   JOBS_SUITE=n  steps at once (default: the CPU count); each step's output is printed whole, in the script's order
#   QUICK=1    the fast tier: the unit tests, no golden renders, UI renders, sanitizers, emulators or native image
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
#                   SCL, the sound's one-step undo (SAVE held), REC (nothing yet) on every page, SEQ's
#                   placeholder (TODO), MIDI IN ROUT, saves refused while playing and the OVERWRITE? dialog, MUTE on TRACKS
#                   KNOB 1.
# Audio / persistence / editor: the real C paths against simulated DMA and NOR flash: bounded overload
#                   fades, shared-voice limits, deferred settings and retries, failed-save rollback, the autosave
#                   (1.2: when it writes, the power-on restore, damaged copies, a write cut short, wear over a session),
#                   malformed transfers, transport-stop timeouts, MIDI and UART recovery; the MENU settings over the
#                   editor (MENU_DESC / MENU_SET: every item, clamping, unknown ids, saving, USB SERIAL applied later).
# CHORD (tests/chord_test.c): the chord keys (src/chord.c): diatonic triads / sevenths of several scales and roots,
#                   the fixed shapes and voicings (at most 4 notes), names, MONO plays the root, a release ends
#                   exactly what its key / MIDI note started, MIDI IN.
# LFO 2 (tests/lfo2_test.c, 1.2): SYNC one cycle per note value at several tempi and on the external clock, OFF bit for bit
#                   as before; TRIG NOTE / FREE; POL UNI 0..+1, the matrix's AMP.
# SEQUENCER: a placeholder (SEQ shows TODO). The renders' songs play through the tests' own player (tests/hostsim.c
#                   host_seq, on seq.c's seq_hook): rows of a 1/16, swung, as the tracker played them, so the goldens stay.
# BASELINE ALONE (tests/baseline_alone.c, 2026-10-17): the sound built with only firmware/baseline on the include
#   path (nothing of Blue Hare OS or the FM-1 port) and run: the layering kept
# FLOAT FX (tests/float_fx_test.c, 2026-10-17): fastmath.h against libm; the EQ's four bands at and away from their
#   frequencies, 0 dB bit for bit, the glide; PLATE's stereo tail and SIZE; AMB's echo, FDBK 127 bounded, the switch
#   to and from it clearing the delay's line
# GEN (tests/gen_test.c, Blue Hare 2026-10): the generative engine on the ARP button: stopped silent; every role
#                   plays, in its range, on the scale and ROOT; chords every BARS bars; steps at the tempo; a scale
#                   change at once; MOTION; a fixed seed's notes; stop lets everything go; each preset (WAVs in
#                   build/gen_demo/).
# MOD (tests/mod_test.c): the modulation matrix: slots that do nothing are bit-identical, every source on each
#                   kind of destination, clamping, MIDI CC1 / CC11 / aftertouch routing, the cost of 4 active
#                   slots (at most +5 %), demos in build/mod_demo/.
# SOUND (tests/sound14_test.c; #132 #104 #148): the matrix's S&H / SLEW sources (a value per LFO cycle, the glide) and
#                   DEPTH destination (the LFO's gain; none = untouched), (ANALOG's WAVE SYNC / SUB: gone with it;
#                   the band-limited restart), SPREAD (SPRD 0 / MONO / LEGATO bit for bit, a note hard left / right, the
#                   mono sum, UNISON, the mute key on both sides, mono sends), the cost; demos in
#                   build/sound14_demo/.
# PERFORM (tests/perform_test.c): the FX hold layer (src/perform.c): starts at once, stereo FREEZE, the archived
#                   effects (REPEAT, REVERSE, TAPE STOP) holding nothing, silent layer keys, idle bit-identical, cost; build/perform_demo/;
#                   OCT UP / DN (the harmonizer): pitch, stereo, clicks, the shimmer bounded, cost; the key
#                   map (any effect or none on any key, a key remapped while held, FX LATCH); build/fx_demo/.
# REVERB (tests/reverb_test.c): REVERB TYPE (src/fx.c): ROOM bit for bit as before, SPRING's decay against SIZE,
#                   its chirp (group delay rising with frequency), stability at the corners, level, a model change
#                   without a click, its cost against ROOM (+30 % at most); demos in build/fx_demo/. 1.2's HALL: SPRING
#                   bit for bit as before, decay against SIZE and DAMP, stability, exact silence after the tail (27
#                   settings and levels), no offset, level, stereo, echo density / ringing / flutter against ROOM, SPRING
#                   and COMB8 (8 combs + 4 allpasses, host only, for comparison), model changes both ways, cost; the
#                   listening set in build/reverb_demo/ (drums, pluck, piano: dry and each model at SIZE 40 / 90 / 127).
#                   DELAY TYPE (Blue Hare): CLEAN as before, TAPE / WORN wobble, the runaway bounded and settling,
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
#                   The sound design studio (web/studio: the same build with its exports, studio_test.mjs through the
#                   worklet the page runs): engines, presets, parameters, notes, user slots, files, the FM-1's UP_PUT.
# ENGINES (tests/engines_test.c): a build without some engines (BLUEHARE_DIST=test_subset: HARE FM1 without BLISS, STRINGS): its engines
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
# (Blue Hare) the steps run in the background, JOBS_SUITE at once; their output is printed whole and in order as they
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

HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/baseline/core.h)
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

[ -f build/bluehare.fwsc ] || { echo "run ./build.sh first"; exit 1; }

compile $CC -DOWN_PKG=1 -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry (own loader)" "$OUT/ota_test" build/bluehare.fwsc

head -c 200000 build/bluehare.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
compile $CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/bluehare.fwsc

if [ -f build/gen/bluehare_tables.h ]; then
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/hostsim" tests/hostsim.c -lm
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/scale_test" tests/scale_test.c -lm
    run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
    compile $CC -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/chord_test" tests/chord_test.c -lm
    run "chord keys: diatonic and fixed chords, voicings, MONO root, releases, MIDI IN" "$OUT/chord_test"
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/speaker_test" tests/speaker_test.c -lm
    run "SPEAKER EQ: FLAT / LOWCUT / BASS+ responses, BASS+ harmonics of the bass, the sub cut, no offset after, headroom on a full-scale kick (#180)" "$OUT/speaker_test"
    slow run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
    mkdir -p build/tracks_demo
    slow run "TRACKS: 4-track pattern, voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
    compile $CC -O1 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/lfo2_test" tests/lfo2_test.c -lm
    run "1.2 LFO 2: SYNC / TRIG / POL" "$OUT/lfo2_test"
    compile $CC -O1 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/midi_control_test" tests/midi_control_test.c -lm
    run "USB/TRS clock, bend, sustain, ownership and panic recovery" "$OUT/midi_control_test"
    compile $CC -O1 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/theme_test" tests/theme_test.c -lm
    run "themes: contrast, text blending and font metrics" "$OUT/theme_test"
    compile $CC -O1 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/text_ref_test" tests/text_ref_test.c -lm
    run "text: pens, kerning and pixels equal the reference renderer" "$OUT/text_ref_test"
    if python3 -c "import PIL" 2>/dev/null; then
        run "text spacing: glyph gaps against the font's own (16x supersampled), S M <= 0.25 px, L <= 0.5 px" \
            python3 tests/text_spacing_test.py build/gen/ui_fonts.h
    fi
    compile $CC -O1 -w -Ibuild/gen -o "$OUT/settings_test" tests/settings_test.c
    run "settings: PER1..PER4 migration, palette ids and preference preservation" "$OUT/settings_test"
    compile $CC -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/ui_test" tests/ui_test.c -lm
    run "UI: sounds, undo, MIDI overflow, pending saves, panel recovery, SEQ's placeholder, GREY gray, MONO neutral, retired engines" "$OUT/ui_test"
    FV=$(sed -n 's/^#define BLUEHARE_VERSION "\([^"]*\)".*/\1/p' firmware/fm1/bluehare.c)   # (the splash, ABOUT: the real version)
    slow compile $CC -O1 -w "-DBLUEHARE_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -Itests -o "$OUT/ui_render" tests/ui_render.c -lm
    mkdir -p build/ui_new/ppm build/ui_slot
    slow run "UI renders: layout lint (every screen and palette, every page, engine and column value), GREY gray, MONO neutral, alignment by ink (1 px fails), draw cost" \
        sh -c '"$1" build/ui_new build/ui_slot && { ! python3 -c "import PIL" 2>/dev/null || python3 tests/ui_render.py build/ui_new build/ui_slot; }' _ "$OUT/ui_render"
    slow compile $CC -O1 -w -DBLUEHARE_NATIVE -DBLUEHARE_DIST=all "-DBLUEHARE_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -Itests \
        -o "$OUT/ui_render_native" tests/ui_render.c -lm
    mkdir -p build/ui_native/ppm build/ui_native_slot
    slow run "UI renders, Native (every component: dist_all.h): CONCERT CAVERN SHIMMER, its version line: the same lint and alignment" \
        "$OUT/ui_render_native" build/ui_native build/ui_native_slot
    compile $CC -w -DBLUEHARE_NTRK=8 -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/ui_test8" tests/ui_test.c -lm
    run "UI built with 8 tracks (step 3c): every check above, the view 1..4 / 5..8, GLO / FX on 8 tracks, MIDI IN blocks of 8" "$OUT/ui_test8"
    slow compile $CC -O1 -w "-DBLUEHARE_VERSION=\"$FV\"" -DBLUEHARE_NTRK=8 -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -Itests -o "$OUT/ui_render8" tests/ui_render.c -lm
    mkdir -p build/ui_8/ppm build/ui_8slot
    slow run "UI renders built with 8 tracks (step 3c): the layout lint on every screen" "$OUT/ui_render8" build/ui_8 build/ui_8slot
    compile $CC -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/audio_test" tests/audio_test.c -lm
    run "audio: overload protection, bounded fades and DMA diagnostics" "$OUT/audio_test"
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/click_test" tests/click_test.c -lm
    run "metronome: beats sample for sample (internal and external clock), accent, level, MASTER, not in USB, PLAY at once (no count-in)" "$OUT/click_test"
    compile $CC -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/persistence_test" tests/persistence_test.c -lm
    run "persistence: deferred settings, retry and failed-save rollback" "$OUT/persistence_test"
    compile $CC -w -DBLUEHARE_NTRK=8 -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/persistence_test8" tests/persistence_test.c -lm
    run "persistence built with 8 tracks (step 3b): the song store and the autosave, cut anywhere" "$OUT/persistence_test8"
    compile $CC -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/backup_test" tests/backup_test.c -lm
    run "full backup: CRC before writes, stale runtime, USB reset / timeout, malformed objects, older projects" "$OUT/backup_test"
    compile $CC -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/editor_test" tests/editor_test.c -lm
    run "editor: real C protocol, malformed transfers, queue recovery, MENU settings (writes build/host/menu.json)" \
        env MENU_JSON="$OUT/menu.json" "$OUT/editor_test"
    EDITOR_JOB=$nj
    compile $CC -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/robust_test" tests/robust_test.c -lm
    run "robustness: engine ids, retained old projects, preset patterns, malformed requests" \
        "$OUT/robust_test"
    for n in 4 8; do
        compile $CC -w -DBLUEHARE_NTRK=$n -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/width_test$n" tests/width_test.c -lm
        run "LOW CUT ($n tracks): the power-on values, the cut, silence after it, the store (WIDTH removed)" "$OUT/width_test$n"
    done
    compile $CC -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/panmode_test" tests/panmode_test.c -lm
    run "PAN MODE: ALT at power-on (SPREAD in turn), RND a side at random (POLY, and MONO at half width), the store" "$OUT/panmode_test"
    compile $CC -w -DBLUEHARE_DIST=test_subset -DBLUEHARE_NTRK=4 -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/engines_test4" tests/engines_test.c -lm
    run "a build without some engines (TEST SUBSET, 4 tracks): ids, substitutes, protocol, projects, presets; a 4-track project for the next" "$OUT/engines_test4"
    compile $CC -w -DBLUEHARE_DIST=test_subset -DBLUEHARE_NTRK=8 -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/engines_test" tests/engines_test.c -lm
    AFTER=$nj                                           # (it reads the 4-track run's song_4trk.bin)
    run "a build without some engines, 8 tracks: the same checks, its power-on, a 4-track project on it" "$OUT/engines_test"
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/mod_test" tests/mod_test.c -lm
    mkdir -p build/mod_demo
    slow run "modulation matrix: off = bit-identical, the math, MIDI CC1 / CC11 / aftertouch, cost, demos" "$OUT/mod_test" build/mod_demo
    run "the component registry (firmware/components.txt): AUTHORS.md's table is its" python3 tools/gen_dist.py --credits-check .
    compile $CC -O1 -w -Ibuild/gen -Ifirmware/baseline -o "$OUT/baseline_alone" tests/baseline_alone.c -lm
    run "the baseline alone (firmware/baseline only on the include path): every engine, MIDI IN, GEN" "$OUT/baseline_alone"
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/float_fx_test" tests/float_fx_test.c -lm
    run "floating point (2026-10-17): fastmath against libm, the EQ's bands, PLATE, AMB and its switch" "$OUT/float_fx_test"
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/gen_test" tests/gen_test.c -lm
    mkdir -p build/gen_demo
    run "GEN, the generative engine: roles, scale, chords, tempo, MOTION, the seed, stop, every preset (demos)" "$OUT/gen_test" build/gen_demo
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/sound14_test" tests/sound14_test.c -lm
    mkdir -p build/sound14_demo
    slow run "sound items: matrix S&H / SLEW / DEPTH, SPREAD (bit for bit at 0, L / R, mono sum), cost, demos" \
        "$OUT/sound14_test" build/sound14_demo
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/swing_test" tests/swing_test.c -lm
    run "SWING: track + global at most 100, the song player's step lengths, the SWG display" "$OUT/swing_test"
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/perform_test" tests/perform_test.c -lm
    mkdir -p build/perform_demo build/fx_demo
    slow run "FX layer effects: at once, stereo, the archived ones nothing, silent keys, idle bit-identical, clicks, OCT UP / DN, the key map, cost, demos" "$OUT/perform_test" build/perform_demo build/fx_demo
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/reverb_test" tests/reverb_test.c -lm
    mkdir -p build/reverb_demo
    slow run "REVERB TYPE: ROOM / SPRING bit-identical, SPRING and HALL decay / stability / level, HALL silence / stereo / ringing, model changes, DELAY TYPE, cost, demos" "$OUT/reverb_test" build/fx_demo build/reverb_demo
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -Itests -o "$OUT/concert_test" tests/concert_test.c -lm
    run "REVERB TYPE CONCERT (the device's): HALL's level, longer with SIZE, stable at the corners, exact silence, stereo, model changes" "$OUT/concert_test"
    compile $CC -O1 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -Itests -o "$OUT/dual_test" tests/dual_test.c -lm
    run "DELAY TYPE DUAL: the left at TIME, the right at 3/4 of it, each its side, the echoes die away, FDBK 127 bounded" "$OUT/dual_test"
    compile $CC -O1 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -Itests -o "$OUT/monolows_test" tests/monolows_test.c -lm
    run "MONO LOWS: the master's side under ~120 Hz in mono (40 Hz hard left on both), the highs' width kept, silence exact" "$OUT/monolows_test"
    compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -Itests -o "$OUT/gated_test" tests/gated_test.c -lm
    run "REVERB TYPE GATED: HALL's level while it holds, cut after SIZE's hold, a longer SIZE longer, a hit opens it again" "$OUT/gated_test"
    slow compile $CC -O2 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/regress" tests/regress.c -lm
    slow run "regression, HARE FM1 (the primary) built with 4 tracks: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
    for d in hare_fm1 all; do
        slow compile $CC -O2 -w -DBLUEHARE_NATIVE -DBLUEHARE_DIST=$d -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -Itests -o "$OUT/native_test_$d" tests/native_test.c -lm
        slow run "native image ($d): 16 voices a track, the device's engines as they are, CONCERT CAVERN SHIMMER, the long delay" "$OUT/native_test_$d"
    done
    slow compile $CC -O2 -w -DBLUEHARE_NATIVE -DBLUEHARE_DIST=all -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/regress_native" tests/regress.c -lm
    slow run "regression, Native (every component): its presets' golden renders (as the device's)" "$OUT/regress_native" tests/golden.txt tests/cpu_baseline.txt
    $CC -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/descdump" tests/descdump.c -lm
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
elif [ ! -f build/gen/bluehare_tables.h ]; then
    echo "== skip the sanitizer runs (run ./build.sh once)"
elif ! san_ok; then
    echo "== skip the sanitizer runs (the compiler has no ASan / UBSan)"
else
    mkdir -p "$OUT/asan"
    A="$OUT/asan"
    SCC="${CC%% *} $SAN -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    $SCC -o "$A/ldr_test" tests/ldr_test.c
    run "ASan/UBSan: update loader (other app -> this build)" "$A/ldr_test" "$OUT/old.fwsc" build/bluehare.fwsc
    $SCC -DOWN_PKG=1 -o "$A/ota_test" tests/ota_test.c
    run "ASan/UBSan: M-UPGRADE entry (own loader)" "$A/ota_test" build/bluehare.fwsc
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
    build/bluehare.dis tests/target_budget.txt

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
elif ! command -v node >/dev/null 2>&1 || [ ! -f build/gen/bluehare_tables.h ]; then
    echo "== skip the browser emulator (needs node and build/gen)"
else
    run_now "browser emulator: the firmware to WebAssembly (web/emu/build.sh -> build/emu)" sh web/emu/build.sh
    BUILD_DIST=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' distributions/BUILD | head -n 1 | tr -d ' ' | tr 'A-Z' 'a-z')
    # (build/emu holds the BUILD distribution: the native reference the same, as web/emu/build.sh reads it)
    cc -O2 -ffp-contract=off -w -DBLUEHARE_DIST="$BUILD_DIST" -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/emu_native" \
        web/emu/native_check.c -lm
    run_now "browser emulator: the same song from the native build" "$OUT/emu_native" "$OUT/emu_native.f32"
    run_now "browser emulator: boot, keys, MIDI, screen, LEDs, PLAY, a save kept across instances, = native, cost" \
        node web/emu/emu_test.mjs build/emu/bluehare.wasm "$OUT/emu_native.f32"
    run_now "sound design studio: the firmware with the studio's exports (web/studio/build.sh -> build/studio)" sh web/studio/build.sh
    run_now "sound design studio: engines, presets, parameters, notes, user slots kept, sound files, the FM-1's UP_PUT" \
        node web/studio/studio_test.mjs build/studio/bluehare.wasm
fi

# (the plugin, VST3 / AU / CLAP, and its tests: archived, archive/plugin)

# (Blue Hare) the native image (native/build.sh: SDL2): built, then booted with dummy video and audio drivers
if [ "${NATIVE:-1}" = 0 ] || [ "$QUICK" = 1 ]; then
    echo "== skip the native image (NATIVE=0 or QUICK=1)"
elif ! command -v sdl2-config >/dev/null 2>&1 || [ ! -f build/gen/bluehare_tables.h ]; then
    echo "== skip the native image (needs SDL2: brew install sdl2)"
else
    run_now "native image: the desktop application builds, every component (native/build.sh -> build/native)" sh native/build.sh
    run_now "native image: boots (dummy drivers), the screen draws, the clock follows the audio, HOME lit, a key sounds, MASTER dragged and scrolled" \
        build/native/blue-hare-all --selftest
    run_now "native image, HARE FM1's set: builds (native/build.sh hare_fm1)" sh native/build.sh hare_fm1
    run_now "native image, HARE FM1's set: boots, the screen draws, the clock follows the audio, HOME lit, a key sounds" \
        build/native/blue-hare-hare_fm1 --selftest
fi

barrier
[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
