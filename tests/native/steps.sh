# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Blue Hare)
# Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# Blue Hare Native's tests (2026-10-17, the architecture's step 8): native/ (built with every component, dist_all.h,
# and with HARE FM1's set): its sound (16 voices a track, the native reverbs, the long delay), its golden renders, its
# UI renders (for now the device's view: it runs the OS through the emulator), its build and boot. Sourced by
# tests/run_tests.sh.
if [ ! -f build/gen/bluehare_tables.h ]; then
    echo "== skip Native's tests (run ./build.sh once)"
    return 0 2>/dev/null || exit 0
fi
I="-Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1"
for d in hare_fm1 all; do
    slow compile $CC -O2 -w -DBLUEHARE_NATIVE -DBLUEHARE_DIST=$d $I -Itests/lib -o "$OUT/native_test_$d" tests/native/native_test.c -lm
    slow run "native image ($d): 16 voices a track, the device's engines as they are, CONCERT CAVERN SHIMMER, the long delay" "$OUT/native_test_$d"
done
slow compile $CC -O2 -w -DBLUEHARE_NATIVE -DBLUEHARE_DIST=all $I -o "$OUT/regress_native" tests/baseline/regress.c -lm
slow run "regression, Native (every component): its presets' golden renders (as the device's)" "$OUT/regress_native" tests/baseline/golden.txt tests/baseline/cpu_baseline.txt
FV=$(sed -n 's/^#define BLUEHARE_VERSION "\([^"]*\)".*/\1/p' firmware/fm1/bluehare.c)
slow compile $CC -O1 -w -DBLUEHARE_NATIVE -DBLUEHARE_DIST=all "-DBLUEHARE_VERSION=\"$FV\"" $I -Itests/os \
    -o "$OUT/ui_render_native" tests/os/ui_render.c -lm
mkdir -p build/ui_native/ppm build/ui_native_slot
slow run "UI renders, Native (every component: dist_all.h): CONCERT CAVERN SHIMMER, its version line: the same lint and alignment" \
    "$OUT/ui_render_native" build/ui_native build/ui_native_slot

# the application (native/build.sh: SDL2): built, then booted with dummy video and audio drivers
if [ "${NATIVE:-1}" = 0 ] || [ "$QUICK" = 1 ]; then
    echo "== skip the native image (NATIVE=0 or QUICK=1)"
elif ! command -v sdl2-config >/dev/null 2>&1; then
    echo "== skip the native image (needs SDL2: brew install sdl2)"
else
    run_now "native image: the desktop application builds, every component (native/build.sh -> build/native)" sh native/build.sh
    run_now "native image: boots (dummy drivers), the screen draws, the clock follows the audio, HOME lit, a key sounds, MASTER dragged and scrolled" \
        build/native/blue-hare-all --selftest
    run_now "native image, HARE FM1's set: builds (native/build.sh hare_fm1)" sh native/build.sh hare_fm1
    run_now "native image, HARE FM1's set: boots, the screen draws, the clock follows the audio, HOME lit, a key sounds" \
        build/native/blue-hare-hare_fm1 --selftest
fi
