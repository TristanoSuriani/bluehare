# Building Fel-Tracker (and Felucca)

Fel-Tracker (Tristano Suriani's fork of Felucca by Leo Kuroshita) builds the same way Felucca does, and still under
Felucca's file names (`felucca.bin`, `felucca.fwsc`). The distributions (below) are Fel-Tracker's. Whose work is
whose: [AUTHORS.md](AUTHORS.md).

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font and icons are
  rasterised at build time)
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).
- Emscripten (optional, `emcc`: the browser emulator, `web/emu/`).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 1.0` makes a release build: the package identity becomes `FM-1_910`
and the version string `v1.0`; the package is `build/felucca-1.0.fwsc`, and
`build/release-1.0/` holds what a release ships: the package, the app
(`felucca-1.0-app.bin`), `SHA256SUMS`, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_CDC_DEFAULT` | 1 | `0`: the console is built in but left out of USB from boot (as MENU > USB SERIAL OFF) |
| `FELUCCA_UAC` | 1 | USB audio input (the master output, stereo, 44.1 or 48 kHz) |
| `FELUCCA_UAC_48K` | 1 | `0`: the USB audio input at 44.1 kHz only, as up to 1.0.5.2 (`firmware/src/usb.c`) |
| `FELUCCA_UART` | 1 | TRS MIDI IN |
| `FELUCCA_ICONS` | 1 | parameter icons on the knob cards |

`FELUCCA_USB_LAYOUT` (`0` to `3`, default `0`) picks other USB descriptor layouts for testing; see
`firmware/src/usb.c`.

## Distributions

The tree holds every engine (`firmware/src/engine_ids.h`); a **distribution** is the set one build holds, at most 16,
in the order the device lists them: `distributions/<name>.txt`, one engine a line, and optionally `TRACKS 4` or
`TRACKS 8`, the build's track count (4 without the line), and `PALETTE <name>`, the palette a device starts with
(tools/gen_ui_palettes.py: IDM's IDM, FOUNDRY's FOUNDRY; GREY without the line).

```
./build.sh                         # the distribution distributions/BUILD names: IDM (build/felucca.fwsc, "v0.10.0 IDM")
FELUCCA_DIST=foundry ./build.sh    # another one (build/felucca-foundry.fwsc)
```

| Distribution | Tracks | Engines |
| --- | --- | --- |
| `idm` | 8 | SID WASP WAVE BREAKS WEST VECTOR DUBSIREN MODAL DUBCHORD VOSIM PARTICLE CHAOS ECHOES KIT SPEECH GLITCH (16) |
| `foundry` | 8 | SCREAM DARK VOSIM WAR SCRATCH FLUIDS RAZOR DUBCHORD RUST ANVIL DRILL SLAG MODAL BLISS ECHOES WAVE |
| `warehouse` | 8 | LINNKIT KIT BREAKS FMPERC CHOKE GUTS VENOM HOOVER DUBCHORD VOSIM DUBSIREN8 ABYSS SHRED RISERS (14: STRINGS and FLOOR to come) |

With 8 tracks the places four wide (the tracker's columns, the MIXER, HOME's rows, the GLO and FX layers' cards) show the
selected track's four, 1 .. 4 or 5 .. 8; songs are stream version 3 (a 4-track build writes version 2 and refuses an
8-track song; an 8-track build loads both); the song store and the autosave hold 16 chunks; the SLICER's STUT records
on tracks 1 .. 4 only; DUBSIREN's echo is the last track's, 8 (elsewhere it plays dry); MIDI IN routes in blocks of 8 (CH1-8, SEL, CH9-16).

Felucca's own engines (ANALOG FM6 PHASE LOFI VOICE TRIO WHEEL PHYS NOISE DRUM) and its default distribution are gone
from Fel-Tracker: their ids are retired (`core.h` `EID_RETIRED`), what other engines used of them lives in
`firmware/src/shared/` (PHYS's resonators for MODAL and ANVIL, DRUM's lanes and GM map for KIT, WAR and GLITCH). The
host tests build the primary distribution (`distributions/BUILD`: IDM) with 4 tracks unless they name one.

The engines a distribution leaves out are not linked. A build numbers its engines 0, 1, .. in the distribution's
order (`core.h` `ENGI_*`, `engines.c` `ENGINES[]`), but everything stored and the editor protocol name an engine by its
fixed id (`EID_*`), so a project, a user preset, a song or a backup moves between distributions:

- a sound of an engine the distribution lacks loads as its substitute (`engine_ids.h`), with the substitute's first
  preset (a project's track keeps the rest of its sound); if that one is missing too, down the same chain, else the
  distribution's first engine;
- a user preset of such an engine is kept as it is but listed nowhere and loads nothing (the editor still reads and
  writes it);
- the power-on parts (SID BASS, VOSIM FORMANT PAD, SID SYNC LEAD, KIT FM KIT) whose engine is missing start as the first engine;
- the editor sees the missing engines as "-" (`INFO`), as it does the retired ids.

A new engine: its `eng_<name>.c` (an `ENG_<NAME>` with `.id`), a line in `engine_ids.h` (the next free id, its
substitute; raise `core.h` `EID_COUNT`), an include in `engines.c`, and its name in the distributions that hold it.
The favourites have rows for ids up to 21 (`favorites.c`). The EDIT layer has 16 white keys for engines; INIT is the
white key after them, or with 16 engines the last black key. Then `tools/gen_editor_mock.py build/host/desc.json
web/editor.html` (after the host tests) brings the editor's mock device up to date.

## Browser emulator

```
./build.sh
web/emu/build.sh
python3 -m http.server -d build/emu 8790     # open http://localhost:8790/
```

`web/emu/build.sh` builds the distribution `distributions/BUILD` names (IDM) into `build/emu/`;
`FELUCCA_DIST=foundry web/emu/build.sh` another one into `build/emu-foundry/` (serve that folder instead). The page
shows the distribution after the version.

The computer's keyboard: the bottom rows play the keys, the number row the buttons, [ ] OCT, ↑ ↓ KNOB 1 (with Shift
SELECT; up a step back, down a step on), ← → ALGORITHM; \ (or the QE AD ZC button) turns Q E, A D, Z C from notes
into KNOB 2, 3, 4 (each pair: down, up) and back. ` (or the Full screen button) shows the device alone, as large
as the screen allows (Escape leaves it); Shift ` (or Big screen) shows the screen large above a smaller panel, on the
page and in full screen. The two choices are remembered in the browser.

MIDI in: a keyboard plugged into the computer plays the emulator (Web MIDI: Chrome, Edge or Firefox, not Safari; the
page on localhost or https). Once switched on, the line under the key hints names the inputs found and blinks on what
they send. MIDI channel 1 plays track 1, 2 track 2 .. (MENU > MIDI > MIDI IN: CH1-8); SEL plays the selected track
from any channel, CH9-16 takes channels 9 .. 16 (a controller's pads, often on 10, then play track 2).

`web/emu/felucca_web.c` builds the firmware's sources as the host tests do, against a simulated FM-1, and
Emscripten compiles it to WebAssembly (`build/emu/felucca.wasm`, with `index.html`, `worklet.js` and the font in `fonts/`). The page
keeps the flash's storage sectors (projects, user presets, settings) in the browser. Not emulated: USB (audio,
MIDI out, the editor, updates) and TRS MIDI. A build of it is on the site:
<https://hugelton.github.io/Felucca/webapp/try/>.

## Tests

```
tests/run_tests.sh
```

Runs the host tests and, with Node.js, the web page tests (and, with Emscripten too, the browser emulator's). Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build). The suites cover flash storage,
user presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock,
control), USB audio, the update entry and loader, the command-line installer, the UI (the real
drawing code against stubs: every screen in every palette is rendered and checked for clipped or
overlapping text; PNGs land in `build/ui_new/`), every engine (SPEECH, the regression renders of each preset), the chord keys, the modulation matrix, the FX layer, the reverbs, the SLICER
and swing.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

Use the web installer in Chrome or Edge:
<https://hugelton.github.io/Felucca/webapp/installer/>. It installs the released package.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).
