# Licensing: Blue Hare and Felucca

Blue Hare is a fork of Felucca by Leo Kuroshita (Hügelton Instruments), developed by Tristano Suriani. Both are
under the same licence, below. [AUTHORS.md](AUTHORS.md) says whose work is whose; this file covers the licences.
Most of this file is Felucca's own licensing text. Blue Hare's part is the section "Blue Hare: Tristano
Suriani's work".

Felucca is free software, licensed under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). That covers the code and its own assets. A few
bundled or ported parts keep their own licences; they are listed below, and their licence
texts are in `LICENSES/`.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca)
Copyright (C) 2026 Tristano Suriani (Blue Hare's additions and changes)

## Blue Hare: Tristano Suriani's work

Blue Hare's additions are GPL-3.0-only, as Felucca is. The new files carry
`Copyright (C) 2026 Tristano Suriani (Blue Hare)`. Felucca files that Blue Hare changed keep Leo Kuroshita's
copyright line, followed by `Modified by Tristano Suriani for Blue Hare (2026)`. That line is the notice of
modification GPL-3.0 section 5(a) asks for, and git history records the changes themselves.

| What | Licence | Where |
| --- | --- | --- |
| Engine ids and distributions | GPL-3.0-only | `firmware/baseline/engine_ids.h`, `tools/gen_dist.py`, `distributions/` |
| The engines WAVE, BYTE, WEST, HOOVER, SPEECH, FMPERC, KARPLUS, VOSIM, PARTICLE, CHAOS, SID, DUBSIREN, DUBSIREN8 (SIREN8), DUBCHORD, BLISS, WASP, MODAL, VECTOR, ECHOES, GLITCH, DARK, HONKS, RAZOR, GUTS, SLAG, VENOM, WAR, CHOKE, RUST, DRILL, ANVIL, SCREAM, FLUIDS, SCRATCH, ABYSS, SHRED, KIT, BREAKS, RISERS, LINNKIT, STRINGS, FLOOR, NEBULA, CANYON, CHIPKIT, SANDSTORM; the native image's reverbs | GPL-3.0-only | `firmware/baseline/eng_*.c` and `archive/engines/` (the list in AUTHORS.md), `firmware/baseline/fx_native.c`, `tools/gen_wavetables.py`, `tools/gen_speech.py`, `tools/speech/` |
| Changes to Felucca's files | GPL-3.0-only (the files stay Leo Kuroshita's) | the files marked "Modified by Tristano Suriani" |

Blue Hare's engines contain no third-party code. They are original implementations of published techniques; the
references are in AUTHORS.md.

## What is GPL-3.0-only

Every file in this tree that carries an `SPDX-License-Identifier: GPL-3.0-only` header:

- the firmware: `firmware/` (app, HAL, update loader)
- the build script and tools: `build.sh`, `tools/`
- the web pages (installer, editor, the browser emulator `web/emu/`) and their tests: `web/` (not the Fukiai and DotGothic16 fonts, below)
- the host tests: `tests/`

One source file is a port and keeps the licence of its original: `firmware/baseline/shared/phys_dsp.c` (DaisySP, MIT).
The firmware built with it is GPL-3.0-only as a whole. (Blue Hare removed the engines PHYS and FM6, and with them
the ports of Rings and msfa.)

You may use, study, change and share Felucca under the GPL. If you distribute Felucca, or
firmware derived from it, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with modified Felucca inside.

## Hügelton Instruments' own work

All by Hügelton Instruments (Leo Kuroshita), in this tree:

| What | Licence | Where |
| --- | --- | --- |
| Felucca: firmware, tools, web pages, tests | GPL-3.0-only | `LICENSE` |
| The DRUM engine's lanes and General MIDI map (the engine itself removed by Blue Hare) | GPL-3.0-only | `archive/engines/drum_map.c` |
| The Fukiai icon font (<https://github.com/hugelton/Fukiai>): the firmware's icons (rasterised at build time by `tools/gen_aa_icons.py`) and the web editor's | MIT | `web/fukiai.ttf`, `LICENSES/MIT-Fukiai.txt`, `web/FUKIAI-LICENSE.txt` |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Inter Tight font by The Inter Project Authors: the UI text, rasterised into the firmware at build time (`tools/gen_aa_font.py`; the generated tables are not offered as a font, and the font declares no Reserved Font Name) | SIL OFL 1.1 | `assets/fonts/InterTight[wght].ttf`, `LICENSES/OFL-InterTight.txt` (also `assets/fonts/OFL.txt`) |
| DaisySP by Electrosmith, Corp and Emilie Gillet (<https://github.com/electro-smith/DaisySP>): the modal resonator (PHYS's, now MODAL's and ANVIL's), ported to fixed point | MIT | `firmware/baseline/shared/phys_dsp.c`, `LICENSES/MIT-DaisySP.txt` |
| X0X by charlesvestal (<https://github.com/charlesvestal/fm1-x0x>), a Felucca fork: the design of the browser emulator (the worklet, the device clock driven by its audio, the exports); its files credit it in their headers | GPL-3.0 | `web/emu/` |
| DotGothic16 font by The DotGothic16 Project Authors (<https://github.com/fontworks-fonts/DotGothic16>): the browser emulator's knob labels, served with its page (cut to printable ASCII; the font declares no Reserved Font Name) | SIL OFL 1.1 | `web/emu/fonts/DotGothic16-subset.woff`, `LICENSES/OFL-DotGothic16.txt` (also `web/emu/fonts/OFL.txt`) |
| DPF, the DISTRHO Plugin Framework, by Filipe Coelho (<https://github.com/DISTRHO/DPF>): the plugin's VST3, AU and CLAP wrappers, built into the plugin (archived; a git submodule, not copied into this tree) | ISC | `archive/plugin/dpf`, `LICENSES/ISC-DPF.txt` |
| The CLAP headers by Alexandre Bique (<https://github.com/free-audio/clap>), as DPF carries them: the plugin's CLAP format and its test (archived) | MIT | `archive/plugin/dpf/distrho/src/clap`, `LICENSES/MIT-CLAP.txt` |
| JieLi AC79 SDK by JieLi Technology: three of its files go into every `.fwsc` package (below); none are in this tree | Apache-2.0 | `LICENSES/Apache-2.0.txt` |

On the device, HOME held > ABOUT opens the information screen; turning PRESETS scrolls on into
CREDITS, a short list of these authors, licences and source URLs.

## JieLi SDK files in the packages (Apache-2.0)

A `.fwsc` package made by `tools/build.py` (with `tools/fm1pkg_make.py`; this is the package the
web installer installs) holds three unmodified files from the JieLi AC79 SDK
(<https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>, `cpu/wl82/tools/`). They are read from your SDK
checkout at build time (see BUILDING.md); no SDK files are in this tree.

| File in the package | What it is |
| --- | --- |
| `uboot.boot` | the first-stage boot loader (SPL) |
| `cfg_tool.bin` | the chip configuration block |
| `eq_cfg_hw.bin` | the default hardware EQ table |

They are licensed under the Apache License, Version 2.0 (`LICENSES/Apache-2.0.txt`), not under
the GPL, and their copyright stays with JieLi Technology. The SDK has no NOTICE file. Apache-2.0
files may be distributed together with GPL-3.0 code; Felucca itself stays GPL-3.0-only.

Every distribution of a package carries the licence texts with it: the release folder
(`tools/build.py --release`) and the site (`web/make_site.py`, next to the package in
`firmware/` and linked from the installer page).

## No vendor material

No M-VAVE material is part of Felucca. The firmware links no vendor code and contains no vendor
data, and the packages hold only Felucca and the three SDK files above. The installer's
"Return to official V15" holds only the official file's size and SHA-256: you select the
official firmware file you downloaded yourself, and it is checked and installed in your browser,
never uploaded or redistributed.

## Contributions

Contributions are welcome under GPL-3.0-only.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.

"M-VAVE" and "FM-1" are trademarks of their respective owners. Felucca is independent
firmware that runs on FM-1 hardware. It is not affiliated with, endorsed by or supported
by those owners.

## Radio

Felucca never enables the Bluetooth / Wi-Fi radio of the hardware.
