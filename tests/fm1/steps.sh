# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Blue Hare)
# Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# Blue Hare FM1's tests (2026-10-17, the architecture's step 8): firmware/fm1, firmware/hal, firmware/loader (the
# audio interrupt, USB, TRS MIDI, updates, the package, the target's cost) and the FM-1 simulated in the browser (the
# emulator, the sound design studio on it). Sourced by tests/run_tests.sh.
compile $CC -o "$OUT/midi_uart_test" tests/fm1/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"
HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/baseline/core.h)
compile $CC -O2 -DT_CDC=1 -DHALF_FRAMES=$HALF -o "$OUT/uac_test" tests/fm1/uac_test.c -lm
run "USB audio input: descriptors (with CDC), ring and packets, 44.1 -> 48 kHz resampler, 48 kHz stream" "$OUT/uac_test"
compile $CC -O2 -DT_CDC=0 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_nocdc" tests/fm1/uac_test.c -lm
run "USB audio input: descriptors (without CDC), ring and packets, resampler, 48 kHz stream" "$OUT/uac_test_nocdc"
# USB descriptor layouts (#67): CDC UAC LAYOUT CDC-presented 48K; layout 0 and the console left out = 1.0's bytes
# (+ the 48 kHz rate with 48K = 1, the default; 48K = 0: byte for byte)
for v in 1.1.0.1.1 1.1.1.1.1 1.1.2.1.1 1.1.3.1.1 1.1.0.0.1 1.1.2.0.1 0.1.0.1.1 1.1.0.1.0 1.1.0.0.0 1.1.2.1.0 0.1.0.1.0 \
         1.0.0.1.1 1.0.1.1.1 1.0.0.0.1 0.0.0.1.1; do
    IFS=. read -r t_cdc t_uac t_lay t_on t_48 <<EOF
$v
EOF
    compile $CC -DT_CDC="$t_cdc" -DT_UAC="$t_uac" -DT_LAYOUT="$t_lay" -DT_ON="$t_on" -DT_48K="$t_48" -o "$OUT/usb_desc_test_$v" \
        tests/fm1/usb_desc_test.c
    run "USB descriptors: CDC $t_cdc (presented $t_on), UAC $t_uac (48 kHz $t_48), layout $t_lay" "$OUT/usb_desc_test_$v"
done

[ -f build/bluehare.fwsc ] || { echo "run ./build.sh first"; exit 1; }
compile $CC -DOWN_PKG=1 -o "$OUT/ota_test" tests/fm1/ota_test.c
run "M-UPGRADE entry (own loader)" "$OUT/ota_test" build/bluehare.fwsc
compile $CC -std=c99 -Wall -Wextra -Wno-unused-function -O1 -fsanitize=address,undefined -Ifirmware/baseline -o "$OUT/stub_test" tests/fm1/stub_test.c
run "boot stub (2026-10-24, from ChoralRoot FM-1): the CRC-32s, the marker, the rescue key, the requests, the staged loader, the record == ota.c's" "$OUT/stub_test"
# (2026-10-24, the boot stub) old: an app without the stub (the layout before it: the body alone, cut short), as on
# the units now; same: this stub with another body (an earlier build of the same stub)
tail -c +3809 build/bluehare.bin | head -c 200000 > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
head -c 300000 build/bluehare.bin > "$OUT/same_app.bin"
python3 tools/fm1pkg_make.py "$OUT/same_app.bin" build/loader/ota.bin "$OUT/same.fwsc" >/dev/null
compile $CC -o "$OUT/ldr_test" tests/fm1/ldr_test.c
run "update loader: other app -> this build; the stub's interruption model (a power cut after every flash operation)" \
    "$OUT/ldr_test" "$OUT/old.fwsc" build/bluehare.fwsc "$OUT/same.fwsc"
if [ -f build/gen/bluehare_tables.h ]; then
    compile $CC -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o "$OUT/audio_test" tests/fm1/audio_test.c -lm
    run "audio: overload protection, bounded fades and DMA diagnostics" "$OUT/audio_test"
fi
if san_wanted; then
    $SCC -o "$A/ldr_test" tests/fm1/ldr_test.c
    run "ASan/UBSan: update loader (other app -> this build), the interruption model" "$A/ldr_test" "$OUT/old.fwsc" \
        build/bluehare.fwsc "$OUT/same.fwsc"
    $SCC -DOWN_PKG=1 -o "$A/ota_test" tests/fm1/ota_test.c
    run "ASan/UBSan: M-UPGRADE entry (own loader)" "$A/ota_test" build/bluehare.fwsc
fi
run "regression: target cost of the render loops (pi32v2 disassembly)" python3 tests/fm1/target_budget.py \
    build/bluehare.dis tests/fm1/target_budget.txt
run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/fm1/install_test.py

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
    run_now "sound design studio: engines, presets, parameters, layers, crossfade, notes, user slots kept, sound files, the FM-1's UP_PUT" \
        node web/studio/studio_test.mjs build/studio/bluehare.wasm
fi
