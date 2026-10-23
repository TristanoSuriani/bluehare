#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Blue Hare)
# Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# The plugin into this Mac's plug-in folders (the user's: no administrator needed), signed ad hoc so the system and the
# hosts load it (no developer certificate: hosts on another Mac may ask to allow it, right-click > Open has no
# equivalent for plug-ins: System Settings > Privacy & Security), then the AU checked by Apple's auval.
#   plugin/install.sh            build (universal), sign, install VST3, AU and CLAP, run auval
#   plugin/install.sh --no-build install what build/plugin holds
#   plugin/install.sh --remove   take them out again
# Then rescan the plug-ins in the host (Live: Settings > Plug-Ins > Rescan; Renoise: Preferences > Plug/Misc > Rescan).
set -e
cd "$(dirname "$0")/.."
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
AU="$HOME/Library/Audio/Plug-Ins/Components"
CLAP="$HOME/Library/Audio/Plug-Ins/CLAP"
if [ "$1" = "--remove" ]; then
    rm -rf "$VST3/BlueHare.vst3" "$AU/BlueHare.component" "$CLAP/BlueHare.clap"
    killall -q AudioComponentRegistrar 2>/dev/null || true
    echo "install: removed"
    exit 0
fi
[ "$1" = "--no-build" ] || sh plugin/build.sh
mkdir -p "$VST3" "$AU" "$CLAP"
for b in build/plugin/BlueHare.vst3 build/plugin/BlueHare.component build/plugin/BlueHare.clap; do
    for c in "$b"/Contents/Resources/cores/*.dylib; do  # (the cores first: --deep does not reach Resources)
        codesign --force --sign - --timestamp=none "$c"
    done
    codesign --force --deep --sign - --timestamp=none "$b"
    codesign --verify --deep --strict "$b"
done
rm -rf "$VST3/BlueHare.vst3" "$AU/BlueHare.component" "$CLAP/BlueHare.clap"
cp -R build/plugin/BlueHare.vst3 "$VST3/"
cp -R build/plugin/BlueHare.component "$AU/"
cp -R build/plugin/BlueHare.clap "$CLAP/"
xattr -dr com.apple.quarantine "$VST3/BlueHare.vst3" "$AU/BlueHare.component" "$CLAP/BlueHare.clap" 2>/dev/null || true
killall -q AudioComponentRegistrar 2>/dev/null || true   # (the AU list read again)
echo "install: $VST3/BlueHare.vst3"
echo "install: $AU/BlueHare.component"
echo "install: $CLAP/BlueHare.clap"
if command -v auval >/dev/null 2>&1; then
    for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do   # (the AU list read again: a few seconds)
        auval -a 2>/dev/null | grep -q "aumu FeTr FelT" && break
        sleep 1
    done
    if auval -v aumu FeTr FelT >build/plugin/auval.log 2>&1; then
        echo "install: auval passed (build/plugin/auval.log)"
    else
        tail -20 build/plugin/auval.log
        echo "install: auval FAILED (build/plugin/auval.log)"
        exit 1
    fi
fi
