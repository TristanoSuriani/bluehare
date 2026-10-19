#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani, Blue Hare: see AUTHORS.md
# The sound design studio: the firmware in the browser as the emulator builds it (web/emu/build.sh), with the
# studio's exports (web/studio/studio_web.c), and its page.
#   web/studio/build.sh                       ->  build/studio/{index.html, studio.js, sysex.js, worklet.js, bluehare.wasm, fonts/}
#   BLUEHARE_DIST=<name> web/studio/build.sh  ->  build/studio-<name>/
# Run after one ./build.sh (build/gen). EMU_OPT / EMU_CFLAGS as web/emu/build.sh.
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/bluehare_tables.h ] || { echo "studio: run ./build.sh once first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "studio: no emcc (Emscripten)"; exit 1; }
VERSION=$(sed -n 's/^#define BLUEHARE_VERSION "\([^"]*\)".*/\1/p' firmware/fm1/bluehare.c)
BUILD_DIST=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' distributions/BUILD | head -n 1 | tr -d ' ' | tr 'A-Z' 'a-z')
DIST=$(printf '%s' "${BLUEHARE_DIST:-$BUILD_DIST}" | tr 'A-Z' 'a-z')
[ -f "build/gen/dist_$DIST.h" ] || { echo "studio: no distribution $DIST"; exit 1; }
if [ "$DIST" = "$BUILD_DIST" ]; then OUTD=build/studio; else OUTD="build/studio-$DIST"; fi
mkdir -p "$OUTD/fonts"
# shellcheck disable=SC2086
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} \
    "-DBLUEHARE_VERSION=\"$VERSION\"" "-DBLUEHARE_DIST=$DIST" -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o "$OUTD/bluehare.wasm" web/studio/studio_web.c
sed -e "s/@VERSION@/$VERSION/g" -e "s/@DIST@/$DIST/g" web/studio/index.html > "$OUTD/index.html"
cp web/studio/studio.js web/studio/sysex.js web/emu/worklet.js "$OUTD/"
cp "assets/fonts/PixelifySans[wght].ttf" "$OUTD/fonts/PixelifySans.ttf"   # (2026-10-19: the small labels, as the firmware's)
cp assets/fonts/OFL-PixelifySans.txt "$OUTD/fonts/"
cp build/gen/BlueHareLED.ttf "$OUTD/fonts/"                 # (2026-10-18: the LED face, tools/gen_seg_font.py)
echo "studio: $OUTD ($(wc -c < "$OUTD/bluehare.wasm" | tr -d ' ') B wasm, $VERSION)"
