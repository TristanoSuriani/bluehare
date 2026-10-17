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
#   LAYER=all|baseline|os|fm1|native|auto   (2026-10-17) the layers' tests to run: a layer and those above it (below)
#
# Regression suite (tests/baseline/regress.c, tests/fm1/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, the GM map on KIT, voice modes, FX sends, a 4-track mix: one hash
#                   each in tests/baseline/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             instructions / sample per preset and mix (tests/baseline/cpu_baseline.txt, +25 %), ns printed;
#                   target: loop instructions of the render functions in the pi32v2 disassembly
#                   (tests/fm1/target_budget.txt, +10 %; exact, static).
#   voices          the budget of 8, steal fades, MONO / LEGATO / UNISON keep their note, a 4-voice cap,
#                   no hanging notes on any MIDI / key routing.
# UI renders (tests/os/ui_render.c): every screen in every palette from the real drawing code: the layout lint (no text
#                   off the screen, cut, hidden, overlapping or spilling out of its cell / card; only free text
#                   ellipsised), GREY gray, MONO neutral, the draw cost, the text audit (build/ui_new/text_audit.tsv);
#                   PNGs of GREY MONO GREEN PAPER NIGHT in build/ui_new (tests/os/ui_render.py), the findings in build/ui_new/report.txt;
#                   every frame of the rolling digits (lint, GREY, MONO), their filmstrips in build/ui_slot;
#                   the alignment: every text / icon / keycap meant to be centred, or on its neighbours' line, by its
#                   ink against its box (cells, chips, buttons, rows, knobs, the roll's strip, the keycaps' pills), every
#                   screen in FLAT and LINE and every palette, over every value it can show; 1 px off or more fails
#                   (build/ui_new/align.txt). MENU > LARGE: every screen again (FLAT, LINE, every palette) and the page / value
#                   sweep with the tall cards, the same lint and alignment; sheet_LARGE_GREY.png, sheet_LARGE_MONO.png.
# UI (tests/os/ui_test.c): the UI sources against stub display / buttons / knobs: sound loads keep the track's
#                   SCL, the sound's one-step undo (SAVE held), REC (nothing yet) on every page, SEQ's
#                   placeholder (TODO), MIDI IN ROUT, saves refused while playing and the OVERWRITE? dialog, MUTE on TRACKS
#                   KNOB 1.
# Audio / persistence / editor: the real C paths against simulated DMA and NOR flash: bounded overload
#                   fades, shared-voice limits, deferred settings and retries, failed-save rollback, the autosave
#                   (1.2: when it writes, the power-on restore, damaged copies, a write cut short, wear over a session),
#                   malformed transfers, transport-stop timeouts, MIDI and UART recovery; the MENU settings over the
#                   editor (MENU_DESC / MENU_SET: every item, clamping, unknown ids, saving, USB SERIAL applied later).
# CHORD (tests/os/chord_test.c): the chord keys (src/chord.c): diatonic triads / sevenths of several scales and roots,
#                   the fixed shapes and voicings (at most 4 notes), names, MONO plays the root, a release ends
#                   exactly what its key / MIDI note started, MIDI IN.
# LFO 2 (tests/os/lfo2_test.c, 1.2): SYNC one cycle per note value at several tempi and on the external clock, OFF bit for bit
#                   as before; TRIG NOTE / FREE; POL UNI 0..+1, the matrix's AMP.
# SEQUENCER: a placeholder (SEQ shows TODO). The renders' songs play through the tests' own player (tests/lib/hostsim.c
#                   host_seq, on seq.c's seq_hook): rows of a 1/16, swung, as the tracker played them, so the goldens stay.
# BASELINE ALONE (tests/baseline/baseline_alone.c, 2026-10-17): the sound built with only firmware/baseline on the include
#   path (nothing of Blue Hare OS or the FM-1 port) and run: the layering kept
# FLOAT FX (tests/baseline/float_fx_test.c, 2026-10-17): fastmath.h against libm; the EQ's four bands at and away from their
#   frequencies, 0 dB bit for bit, the glide; PLATE's stereo tail and SIZE; AMB's echo, FDBK 127 bounded, the switch
#   to and from it clearing the delay's line
# GEN (tests/baseline/gen_test.c, Blue Hare 2026-10): the generative engine on the ARP button: stopped silent; every role
#                   plays, in its range, on the scale and ROOT; chords every BARS bars; steps at the tempo; a scale
#                   change at once; MOTION; a fixed seed's notes; stop lets everything go; each preset (WAVs in
#                   build/gen_demo/).
# MOD (tests/baseline/mod_test.c): the modulation matrix: slots that do nothing are bit-identical, every source on each
#                   kind of destination, clamping, MIDI CC1 / CC11 / aftertouch routing, the cost of 4 active
#                   slots (at most +5 %), demos in build/mod_demo/.
# SOUND (tests/baseline/sound14_test.c; #132 #104 #148): the matrix's S&H / SLEW sources (a value per LFO cycle, the glide) and
#                   DEPTH destination (the LFO's gain; none = untouched), (ANALOG's WAVE SYNC / SUB: gone with it;
#                   the band-limited restart), SPREAD (SPRD 0 / MONO / LEGATO bit for bit, a note hard left / right, the
#                   mono sum, UNISON, the mute key on both sides, mono sends), the cost; demos in
#                   build/sound14_demo/.
# PERFORM (tests/baseline/perform_test.c): the FX hold layer (src/perform.c): starts at once, stereo FREEZE, the archived
#                   effects (REPEAT, REVERSE, TAPE STOP) holding nothing, silent layer keys, idle bit-identical, cost; build/perform_demo/;
#                   OCT UP / DN (the harmonizer): pitch, stereo, clicks, the shimmer bounded, cost; the key
#                   map (any effect or none on any key, a key remapped while held, FX LATCH); build/fx_demo/.
# REVERB (tests/baseline/reverb_test.c): REVERB TYPE (src/fx.c): ROOM bit for bit as before, SPRING's decay against SIZE,
#                   its chirp (group delay rising with frequency), stability at the corners, level, a model change
#                   without a click, its cost against ROOM (+30 % at most); demos in build/fx_demo/. 1.2's HALL: SPRING
#                   bit for bit as before, decay against SIZE and DAMP, stability, exact silence after the tail (27
#                   settings and levels), no offset, level, stereo, echo density / ringing / flutter against ROOM, SPRING
#                   and COMB8 (8 combs + 4 allpasses, host only, for comparison), model changes both ways, cost; the
#                   listening set in build/reverb_demo/ (drums, pluck, piano: dry and each model at SIZE 40 / 90 / 127).
#                   DELAY TYPE (Blue Hare): CLEAN as before, TAPE / WORN wobble, the runaway bounded and settling,
#                   TIME and type changes without a click, cost; build/fx_demo/delay_{clean,tape,worn}_pluck.wav.
# INPUT (tests/os/input_test.c): the key / button debounce of hal/fm1_input.h against the TIMER5 scan and bouncing
#                   contacts: a press within 2 scans (<= 2.3 ms), one note per bouncy press, no early or hanging
#                   release, stray samples ignored, fast repeats, the encoders' detents; the LED scan: lit LEDs every
#                   frame, dim ones a short pulse (the second line write) every frame, each only on its own column;
#                   the breath (#119): dark .. ~60 % of lit (DIM LO ~30 %), smooth, no dark run over ~10 ms near its peak;
#                   the power-on sweep (1.1, hal/fm1_led_anim.h): its length, every LED every frame at its level, the
#                   head left to right with its tail, the buttons, the end on the idle glow, the tick as before after;
#                   the idle animation (1.2, MENU > ANIM IDLE): endless, every LED every frame, the light there and back,
#                   the buttons breathing, a key down ends it before its next frame.
# CLICK (tests/baseline/click_test.c, 1.1): the metronome (src/click.c, src/seq.c) through audio.c: each beat at the first
#                   sample of its block, with a 1/16 track's steps 1 5 9 13 (internal and external clock, DIV 1/8 and
#                   SWING too), the accent, the frequencies, length, LEVEL, MASTER, OFF / REC / ON, USB audio bit for bit
#                   without it; 1.5: no count-in (PLAY armed plays at once); build/click_demo/.
# USB audio (tests/fm1/uac_test.c): the UAC1 descriptors as a host parses them (with and without CDC; 44100 and
#                   48000 Hz), the ring and packetiser: 44.1 frames per packet, every frame in order (bit for bit),
#                   underrun / overrun, restart; SET_CUR; the 44.1 -> 48 kHz resampler (every frame against a
#                   double-precision one, response, SNR, cost); the stream at 48 kHz (47..49 frames per packet,
#                   fast and slow I2S clocks, switching rates mid-stream).
# web (web/test_web.mjs): the editor protocol against its mock device, whose tables must equal the
#                   firmware's (tests/os/descdump.c -> build/host/desc.json; the MENU settings: tests/os/editor_test.c -> build/host/menu.json),
#                   the package builder, the updater.
# Browser emulator (web/emu, when emcc is there): the firmware in WebAssembly (build/emu) boots, plays keys and MIDI,
#                   draws, lights its LEDs, keeps a save across instances, plays a song bit for bit as the same file
#                   built with cc (web/emu/native_check.c); the cost of 1 s of a heavy song against real time.
#                   The sound design studio (web/studio: the same build with its exports, studio_test.mjs through the
#                   worklet the page runs): engines, presets, parameters, notes, user slots, files, the FM-1's UP_PUT.
# ENGINES (tests/os/engines_test.c): a build without some engines (BLUEHARE_DIST=test_subset: HARE FM1 without BLISS, STRINGS): its engines
#                   numbered from 0 in its order, the ids both ways, a left-out engine's sound loading as its substitute
#                   (projects, G_ENGSEL / PRESET, the song's instruments), its user presets kept but not listed, every
#                   engine left rendering.
# ROBUST (tests/os/robust_test.c): damaged or crafted stored data and editor requests: a song's fields out of range refused (an engine past the
#                   last, no position, an unknown FX) under a valid hash, values loaded inside their ranges, user preset
#                   patterns inside their fields, malformed requests never stop PLAY.
# Sanitizers (ASan + UBSan, when the compiler has them; SANITIZE=0 skips): the stored-data and protocol tests again
#                   (loader, M-UPGRADE entry, editor, projects, backup, ROBUST) and three short fuzz runs with fixed
#                   seeds: tests/os/fuzz_ed.c (editor SysEx and raw USB-MIDI packets), tests/os/fuzz_proj.c (random songs, their
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

# (2026-10-17) the tests by layer (the architecture's step 8): tests/<layer>/steps.sh, each the steps of its layer.
# LAYER picks them: a layer and the layers above it (what builds on it), as a change there can break those:
#   baseline  baseline os fm1 native      os  os fm1 native      fm1  fm1      native  native
#   all (the default) every one; auto: the lowest layer the working tree changes (git diff HEAD, the untracked files):
#   firmware/baseline, firmware/components.txt, distributions, tools, tests/baseline, tests/lib: baseline; firmware/os,
#   tests/os, web/editor.html, web/test_*: os; firmware/fm1, firmware/hal, firmware/loader, tests/fm1, web/emu,
#   web/studio: fm1; native, tests/native: native; documentation only: nothing to run
LAYER=${LAYER:-all}
layer_of() {
    case "$1" in
        firmware/baseline/*|firmware/components.txt|distributions/*|tools/*|tests/baseline/*|tests/lib/*|tests/run_tests.sh|Makefile|build.sh) echo baseline ;;
        firmware/os/*|tests/os/*|web/editor.html|web/test_*|web/*.js) echo os ;;
        firmware/fm1/*|firmware/hal/*|firmware/loader/*|firmware/*.ld|firmware/*.S|tests/fm1/*|web/emu/*|web/studio/*) echo fm1 ;;
        native/*|tests/native/*) echo native ;;
        *.md|docs/*|LICENSES/*|archive/*|assets/*|.idea/*) echo none ;;
        *) echo baseline ;;
    esac
}
if [ "$LAYER" = auto ]; then
    seen=""
    for f in $(git diff --name-only HEAD 2>/dev/null) $(git ls-files --others --exclude-standard 2>/dev/null); do
        seen="$seen $(layer_of "$f")"
    done
    LAYER=none
    for l in native fm1 os baseline; do
        case " $seen " in *" $l "*) LAYER=$l ;; esac
    done
    echo "== LAYER=auto: the working tree's lowest layer changed: $LAYER"
fi
case "$LAYER" in
    all|baseline) LAYERS="baseline os fm1 native" ;;
    os) LAYERS="os fm1 native" ;;
    fm1) LAYERS="fm1" ;;
    native) LAYERS="native" ;;
    none) LAYERS="" ;;
    *) echo "LAYER: all, baseline, os, fm1, native or auto"; exit 2 ;;
esac
echo "== the layers' tests: ${LAYERS:-none}"

# ASan + UBSan (the stored-data and protocol paths, the loader; short fuzz runs): set up once, run by the layers' steps
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize=signed-integer-overflow,shift,bounds,object-size,pointer-overflow"
SAN="$SAN -fno-sanitize-recover=undefined -w"
san_ok() {
    printf 'int main(void){return 0;}\n' > "$OUT/san_probe.c" &&
        ${CC%% *} $SAN -o "$OUT/san_probe" "$OUT/san_probe.c" 2>/dev/null && "$OUT/san_probe"
}
SAN_ON=0
if [ "${SANITIZE:-1}" = 0 ] || [ "$QUICK" = 1 ]; then
    echo "== skip the sanitizer runs (SANITIZE=0 or QUICK=1)"
elif [ ! -f build/gen/bluehare_tables.h ]; then
    echo "== skip the sanitizer runs (run ./build.sh once)"
elif ! san_ok; then
    echo "== skip the sanitizer runs (the compiler has no ASan / UBSan)"
else
    SAN_ON=1
    A="$OUT/asan"
    mkdir -p "$A"
    SCC="${CC%% *} $SAN -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
fi
san_wanted() { [ "$SAN_ON" = 1 ]; }

for l in $LAYERS; do
    echo "== layer: $l (tests/$l/steps.sh)"
    . "tests/$l/steps.sh"
done

barrier
[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
