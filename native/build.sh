#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
# Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# The native image: Fel-Tracker as a desktop application (native/felucca_native.c: the firmware as the browser
# emulator runs it, in an SDL2 window, the computer's audio out, MIDI in).
#   native/build.sh [DIST]   (DIST: a distribution, distributions/<DIST>.txt; the BUILD one, IDM, without; idm_native:
#                             IDM with the native image's own engines, NEBULA and CANYON)
#     -> build/native/fel-tracker-<dist>          the program (SDL2 linked in: nothing else to install)
#     -> build/native/Fel-Tracker <DIST>.app      on macOS, the same as an application (double-click it)
# Needs SDL2 (macOS: brew install sdl2; Linux: the libsdl2-dev package) and one ./build.sh before (build/gen: the
# tables, the fonts, the distributions' headers).
set -e
cd "$(dirname "$0")/.."
DIST="${1:-$(grep -v '^#' distributions/BUILD | awk 'NF { print $1; exit }')}"
[ -f build/gen/felucca_tables.h ] || { echo "native: run ./build.sh once first (build/gen)"; exit 1; }
[ -f "build/gen/dist_$DIST.h" ] || { echo "native: no distribution $DIST (distributions/$DIST.txt; ./build.sh once after adding it)"; exit 1; }
command -v sdl2-config >/dev/null 2>&1 || { echo "native: no SDL2 (macOS: brew install sdl2)"; exit 1; }
VERSION=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c)   # ("v0.10.0")
PLIST_VERSION=${VERSION#v}
NAME=$(echo "$DIST" | tr 'a-z_' 'A-Z ')   # ("IDM NATIVE": the tag, gen_dist.py's)
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
${CC:-cc} -O2 -ffp-contract=off -std=gnu99 -w "-DFELUCCA_VERSION=\"$VERSION\"" "-DFELUCCA_DIST=$DIST" \
    -Ibuild/gen -Ifirmware/src $(sdl2-config --cflags) -o "$OUT/fel-tracker-$DIST" native/felucca_native.c $LIBS
echo "native: $OUT/fel-tracker-$DIST ($VERSION $NAME)"
if [ "$(uname -s)" = Darwin ]; then
    APP="$OUT/Fel-Tracker $NAME.app"
    rm -rf "$APP"
    mkdir -p "$APP/Contents/MacOS"
    cp "$OUT/fel-tracker-$DIST" "$APP/Contents/MacOS/fel-tracker"
    cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Fel-Tracker $NAME</string>
  <key>CFBundleDisplayName</key><string>Fel-Tracker $NAME</string>
  <key>CFBundleIdentifier</key><string>io.github.tristanosuriani.fel-tracker.$BUNDLE</string>
  <key>CFBundleVersion</key><string>$PLIST_VERSION</string>
  <key>CFBundleShortVersionString</key><string>$PLIST_VERSION</string>
  <key>CFBundleExecutable</key><string>fel-tracker</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF
    echo "native: $APP"
fi
