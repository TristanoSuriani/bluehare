# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Blue Hare)
# Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# The baseline's tests (2026-10-17, the architecture's step 8): the sound, firmware/baseline. Sourced by
# tests/run_tests.sh (its harness: compile, run, slow, barrier; $CC, $OUT); a change here runs every layer above too.
if [ ! -f build/gen/bluehare_tables.h ]; then
    echo "== skip the baseline's tests (run ./build.sh once)"
    return 0 2>/dev/null || exit 0
fi
# (2026-10-17, the second review's D) built on firmware/baseline alone: the harness's HOST_BASELINE mode (no USB, no
# TRS MIDI, no keys: tests/lib/hostsim.c), nothing of the OS or the FM-1 on the include path
I="-DHOST_BASELINE -Ibuild/gen -Ifirmware/baseline -Itests/lib"
run "the component registry (firmware/components.txt): AUTHORS.md's table is its" python3 tools/gen_dist.py --credits-check .
compile $CC -O1 -w -Ibuild/gen -Ifirmware/baseline -o "$OUT/baseline_alone" tests/baseline/baseline_alone.c -lm
run "the baseline alone (firmware/baseline only on the include path): every engine, MIDI IN, GEN" "$OUT/baseline_alone"
slow compile $CC -O2 -w $I -o "$OUT/hostsim" tests/lib/hostsim.c -lm
slow run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
mkdir -p build/tracks_demo
slow run "TRACKS: 4-track pattern, voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
compile $CC -O2 -w $I -o "$OUT/speaker_test" tests/baseline/speaker_test.c -lm
run "SPEAKER EQ: FLAT / LOWCUT / BASS+ responses, BASS+ harmonics of the bass, the sub cut, no offset after, headroom on a full-scale kick (#180)" "$OUT/speaker_test"
compile $CC -O2 -w $I -o "$OUT/click_test" tests/baseline/click_test.c -lm
run "metronome: beats sample for sample (internal and external clock), accent, level, MASTER, not in USB, PLAY at once (no count-in)" "$OUT/click_test"
compile $CC -O2 -w $I -o "$OUT/float_fx_test" tests/baseline/float_fx_test.c -lm
run "floating point (2026-10-17): fastmath against libm, the EQ's bands, PLATE; the global reverb's types (2026-10-18)" "$OUT/float_fx_test"
compile $CC -O2 -w $I -o "$OUT/techno_test" tests/baseline/techno_test.c -lm
mkdir -p build/techno_demo
run "TECHNO, the player on ARP (2026-10-18): the arrangement to the DROP, every role, the scale, four on the floor, MOTION, DENSITY, a key, stop, the seed, every piece (demos)" "$OUT/techno_test" build/techno_demo
slow compile $CC -O2 -w $I -o "$OUT/mod_test" tests/baseline/mod_test.c -lm
mkdir -p build/mod_demo
slow run "modulation matrix: off = bit-identical, the math, MIDI CC1 / CC11 / aftertouch, cost, demos" "$OUT/mod_test" build/mod_demo
slow compile $CC -O2 -w $I -o "$OUT/sound14_test" tests/baseline/sound14_test.c -lm
mkdir -p build/sound14_demo
slow run "sound items: matrix S&H / SLEW / DEPTH, SPREAD (bit for bit at 0, L / R, mono sum), cost, demos" \
    "$OUT/sound14_test" build/sound14_demo
compile $CC -O2 -w $I -o "$OUT/swing_test" tests/baseline/swing_test.c -lm
run "SWING: track + global at most 100, the song player's step lengths, the SWG display" "$OUT/swing_test"
compile $CC -O2 -w $I -o "$OUT/patchfx_test" tests/baseline/patchfx_test.c -lm
run "the patches' delays (2026-10-18): the echo at TIME, FEED's next quieter, panned as the patch, one line each, asleep when silent, FEED 120 bounded" "$OUT/patchfx_test"
compile $CC -O1 -w $I -Itests/lib -o "$OUT/monolows_test" tests/baseline/monolows_test.c -lm
run "MONO LOWS: the master's side under ~120 Hz in mono (40 Hz hard left on both), the highs' width kept, silence exact" "$OUT/monolows_test"
compile $CC -O2 -w $I -Itests/lib -o "$OUT/gated_test" tests/baseline/gated_test.c -lm
run "REVERB TYPE GATED: HALL's level while it holds, cut after SIZE's hold, a longer SIZE longer, a hit opens it again" "$OUT/gated_test"
slow compile $CC -O2 -w $I -o "$OUT/regress" tests/baseline/regress.c -lm
slow run "regression, HARE FM1 (the primary) built with 4 tracks: golden renders, health, voices, CPU budget" "$OUT/regress" tests/baseline/golden.txt tests/baseline/cpu_baseline.txt
