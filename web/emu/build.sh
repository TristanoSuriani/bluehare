#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md
# Blue Hare in the browser: the firmware's sources as the host tests build them (web/emu/bluehare_web.c), compiled
# to WebAssembly with Emscripten, plus the page. After X0X's web/emu/build.sh (charlesvestal/fm1-x0x, GPL-3.0).
#   web/emu/build.sh                       ->  build/emu/{index.html, worklet.js, bluehare.wasm, fonts/}: the distribution
#                                              distributions/BUILD names (as ./build.sh)
#   BLUEHARE_DIST=<name> web/emu/build.sh  ->  build/emu-<name>/ (another distribution: distributions/<name>.txt)
# Run after one ./build.sh (build/gen: the tables, the fonts, the distributions' headers).
# -ffp-contract=off: no fused multiply-adds, as the host build. EMU_OPT replaces -O2, EMU_CFLAGS adds flags
# (EMU_CFLAGS=-msimd128: let the compiler vectorise; every browser with AudioWorklet has wasm SIMD).
# The page shows the version (BLUEHARE_VERSION) marked "(preview)" before its release; EMU_PREVIEW=0 drops the mark.
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/bluehare_tables.h ] || { echo "emu: run ./build.sh once first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten)"; exit 1; }
VERSION=$(sed -n 's/^#define BLUEHARE_VERSION "\([^"]*\)".*/\1/p' firmware/fm1/bluehare.c)
BUILD_DIST=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' distributions/BUILD | head -n 1 | tr -d ' ' | tr 'A-Z' 'a-z')
DIST=$(printf '%s' "${BLUEHARE_DIST:-$BUILD_DIST}" | tr 'A-Z' 'a-z')
[ -f "build/gen/dist_$DIST.h" ] || { echo "emu: no distribution $DIST (distributions/$DIST.txt; run ./build.sh once after adding it)"; exit 1; }
if [ "$DIST" = "$BUILD_DIST" ]; then OUTD=build/emu; else OUTD="build/emu-$DIST"; fi
if [ "$DIST" = default ]; then TAG=''; else TAG=" $(printf '%s' "$DIST" | tr 'a-z' 'A-Z')"; fi
mkdir -p "$OUTD/fonts"
# shellcheck disable=SC2086
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} \
    "-DBLUEHARE_VERSION=\"$VERSION\"" "-DBLUEHARE_DIST=$DIST" -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o "$OUTD/bluehare.wasm" web/emu/bluehare_web.c
[ "${EMU_PREVIEW:-1}" = 0 ] && PREVIEW='' || PREVIEW=' (preview)'
# (2026-10-18) the page wears the distribution's palette (its PALETTE line: HARE FM1's IDM), not its name's
PAL=$(sed -n 's/^PALETTE[[:space:]]*\([A-Za-z0-9_-]*\).*/\1/p' "distributions/$DIST.txt" 2>/dev/null | head -n 1 | tr 'A-Z' 'a-z')
sed -e "s/@VERSION@/$VERSION$TAG/g" -e "s/@PREVIEW@/$PREVIEW/g" -e "s/@DIST@/${PAL:-$DIST}/g" web/emu/index.html > "$OUTD/index.html"
cp web/emu/worklet.js web/emu/shell.js web/emu/shell.css "$OUTD/"   # (2026-10-19: the instrument, shared with the studio)
cp web/emu/fonts/DotGothic16-subset.woff web/emu/fonts/OFL.txt "$OUTD/fonts/"
cp build/gen/BlueHareLED.ttf "$OUTD/fonts/"                 # (2026-10-18: the LED face, tools/gen_seg_font.py)
echo "emu: $OUTD ($(wc -c < "$OUTD/bluehare.wasm" | tr -d ' ') B wasm, $VERSION$TAG)"
