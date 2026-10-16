#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
# Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# pluginval (Tracktion, GPL-3.0), the plugin validator tests/run_tests.sh runs on the VST3 when it is there:
#   plugin/get_pluginval.sh -> build/tools/pluginval-dl/pluginval.app (macOS)
set -e
cd "$(dirname "$0")/.."
V=v1.0.4
D=build/tools/pluginval-dl
mkdir -p "$D"
curl -sSL -o "$D/pluginval_macOS.zip" "https://github.com/Tracktion/pluginval/releases/download/$V/pluginval_macOS.zip"
(cd "$D" && unzip -q -o pluginval_macOS.zip)
echo "pluginval $V: $D/pluginval.app/Contents/MacOS/pluginval"
