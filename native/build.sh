#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Blue Hare)
# Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# The native image: Blue Hare as a desktop application (native/bluehare_native.c: the firmware as the browser
# emulator runs it, in an SDL2 window, the computer's audio out, MIDI in).
#   native/build.sh [DIST]   (2026-10-17: without DIST every component of the registry, gen_dist.py's dist_all.h: Native
#                             has them all; DIST: a distribution, distributions/<DIST>.txt, the FM-1's as it is)
#     -> build/native/blue-hare-<dist>          the program (SDL2 linked in: nothing else to install)
#     -> build/native/Blue Hare[ <DIST>].app    on macOS, the same as an application (double-click it)
# Needs SDL2 (macOS: brew install sdl2; Linux: the libsdl2-dev package) and one ./build.sh before (build/gen: the
# tables, the fonts, the distributions' headers).
set -e
cd "$(dirname "$0")/.."
DIST="${1:-all}"
[ -f build/gen/bluehare_tables.h ] || { echo "native: run ./build.sh once first (build/gen)"; exit 1; }
[ -f "build/gen/dist_$DIST.h" ] || { echo "native: no distribution $DIST (distributions/$DIST.txt; ./build.sh once after adding it)"; exit 1; }
command -v sdl2-config >/dev/null 2>&1 || { echo "native: no SDL2 (macOS: brew install sdl2)"; exit 1; }
VERSION=$(sed -n 's/^#define BLUEHARE_VERSION "\([^"]*\)".*/\1/p' firmware/fm1/bluehare.c)   # ("v0.40.0")
PLIST_VERSION=${VERSION#v}
NAME=$(echo "$DIST" | tr 'a-z_' 'A-Z ')   # ("HARE FM1": the tag, gen_dist.py's; all: none)
[ "$DIST" = all ] && NAME=""
BUNDLE=$(echo "$DIST" | tr '_' '-')
OUT=build/native
mkdir -p "$OUT"
case "$(uname -s)" in
Darwin)
    LIBS="$(sdl2-config --static-libs) -framework CoreMIDI -framework CoreFoundation" ;;   # (SDL2 in the program)
*)
    LIBS="$(sdl2-config --libs) -lm" ;;
esac
# shellcheck disable=SC2086
${CC:-cc} -O2 -ffp-contract=off -std=gnu99 -w "-DBLUEHARE_VERSION=\"$VERSION\"" "-DBLUEHARE_DIST=$DIST" \
    -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 $(sdl2-config --cflags) -o "$OUT/blue-hare-$DIST" native/bluehare_native.c $LIBS
echo "native: $OUT/blue-hare-$DIST ($VERSION $NAME)"
if [ "$(uname -s)" = Darwin ]; then
    APP="$OUT/Blue Hare${NAME:+ $NAME}.app"
    rm -rf "$APP"
    mkdir -p "$APP/Contents/MacOS"
    cp "$OUT/blue-hare-$DIST" "$APP/Contents/MacOS/blue-hare"
    cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Blue Hare $NAME</string>
  <key>CFBundleDisplayName</key><string>Blue Hare $NAME</string>
  <key>CFBundleIdentifier</key><string>io.github.tristanosuriani.blue-hare.$BUNDLE</string>
  <key>CFBundleVersion</key><string>$PLIST_VERSION</string>
  <key>CFBundleShortVersionString</key><string>$PLIST_VERSION</string>
  <key>CFBundleExecutable</key><string>blue-hare</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF
    echo "native: $APP"
fi
