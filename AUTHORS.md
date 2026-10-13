# Authors and ownership

**Fel-Tracker** is a fork of **Felucca**, the multi-engine synthesizer firmware for the M-VAVE FM-1 by
Leo Kuroshita (Hügelton Instruments). Fel-Tracker is developed by **Tristano Suriani**
(<https://github.com/TristanoSuriani/fel-tracker>). It is an independent fork, not an official Felucca release.

Both are free software under the GNU General Public License, version 3 only (`LICENSE`). This file says whose work
is whose. The third-party parts that both build on (fonts, ported DSP, the JieLi SDK files) are listed in
[LICENSING.md](LICENSING.md).

The fork point is Felucca 1.4.1 (commit `f21d6d6`). Everything from commit `7aa4347` on is Fel-Tracker's, and
`git log` shows exactly which lines changed and who changed them.

## Felucca: Leo Kuroshita (Hügelton Instruments)

Everything in the tree at the fork point, except the third-party parts in LICENSING.md, is Leo Kuroshita's work.
This includes:

- **The firmware:** the HAL, the update loader, the voice and effects engine, the UI, the menus, storage, MIDI, USB
  and the editor protocol.
- **The engines ANALOG, FM6 (with its msfa port), PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE and DRUM**, with their
  factory presets.
- **The tools, the web pages and the tests:** the web editor, the installer and the browser emulator.
- **The name "Felucca"**, its documentation and its artwork.

A file Leo Kuroshita wrote carries his copyright line. Where Fel-Tracker changed it, a second line follows:
`Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md`. The file stays his, with Tristano Suriani's
changes in it. A few upstream files never had a header (for example `firmware/src/settings_persist.c` and some
tests); git history shows the changes to those.

## Fel-Tracker: Tristano Suriani

### New files

These carry `Copyright (C) 2026 Tristano Suriani (Fel-Tracker)`.

| What | Where |
| --- | --- |
| The tracker, which replaces the step sequencer: song, patterns, instruments, FX, its pages and its editor protocol | `firmware/src/tracker.c`, `firmware/src/ui_tracker.c`, `firmware/src/editor_tracker.c` |
| Stable engine ids, distributions, and builds of any set of up to 16 engines | `firmware/src/engine_ids.h`, `tools/gen_dist.py`, `distributions/` |
| WAVE (wavetables) | `firmware/src/eng_wave.c`, `tools/gen_wavetables.py` |
| BYTE (bytebeat) | `firmware/src/eng_byte.c` |
| WEST (west coast) | `firmware/src/eng_west.c` |
| HOOVER (rave lead) | `firmware/src/eng_hoover.c` |
| SPEECH (LPC words in English) | `firmware/src/eng_speech.c`, `tools/gen_speech.py`, `tools/speech/` |
| FMPERC (FM percussion) | `firmware/src/eng_fmperc.c` |
| KARPLUS (plucked string) | `firmware/src/eng_karplus.c` |
| VOSIM (formant pulse trains) | `firmware/src/eng_vosim.c` |
| PARTICLE (grain clouds) | `firmware/src/eng_particle.c` |
| CHAOS (chaotic oscillators) | `firmware/src/eng_chaos.c` |
| SID (a C64 sound-chip voice) | `firmware/src/eng_sid.c` |
| DUBSIREN (siren with tape echo) | `firmware/src/eng_dubsiren.c` |
| DUBSIREN8, SIREN8 on the device (its 8-bit cousin) | `firmware/src/eng_dubsiren8.c` |
| DUBCHORD (the dub-techno chord, a whole chord in one voice) | `firmware/src/eng_dubchord.c` |
| FMKEYS (FM keyboards) | `firmware/src/eng_fmkeys.c` |
| WASP (two oscillators, a gritty multimode filter) | `firmware/src/eng_wasp.c` |
| MODAL (a modal resonator; its resonators PHYS's, DaisySP's) | `firmware/src/eng_modal.c` |
| VECTOR (vector synthesis over WAVE's frames) | `firmware/src/eng_vector.c` |
| KIT (an FM drum kit on one part) | `firmware/src/eng_kit.c` |
| BREAKS (synthesized breakbeat player) | `firmware/src/eng_breaks.c` |
| RISERS (build-ups and landings) | `firmware/src/eng_risers.c` |
| Tests for distributions and SPEECH | `tests/engines_test.c`, `tests/speech_test.c` |

All of these engines are original implementations of published techniques, with no third-party code:

- Karplus-Strong (1983)
- VOSIM (Kaegi and Tempelaars, 1978)
- LPC lattice synthesis
- The Lorenz and Rössler systems, and the logistic and Hénon maps
- FM, wavetable and bytebeat synthesis

SID, WASP and DUBSIREN are written "in the manner of" the instruments they evoke; they contain no code or data from
emulators of those instruments. SPEECH's phonemes come from standard phonetic formant values, and its vocabularies
are Tristano Suriani's own. The breaks in BREAKS are his own patterns, written in the style of well-known breaks.

### Changes to Felucca's files

These changes are Tristano Suriani's (`git log 7aa4347^..`):

- The step sequencer is removed, and the tracker takes its place.
- SAMPLE, GRAIN, SLICE and DIGITAL are removed, along with the sample infrastructure.
- Engines are named by a stable id everywhere a sound is stored or sent, and by an index within the build.
- Distributions run through the build, the browser emulator, the regression tests and the device budget checks.
- Favourites are mapped for the new engine ids.
- The EDIT layer handles 16 engines.
- DRUM's special cases (lanes, mutes, MIDI) cover KIT too.
- FM6's voice bank is left out of builds without FM6.
- The documentation is updated to match.

### Files moved from Felucca

These are Felucca's code, moved out of the files they used to live in. They keep Leo Kuroshita's copyright, with
Fel-Tracker's modification line:

- `tests/factory_patterns.h`, from `firmware/src/engines.c`
- `tests/lfo2_test.c`, from `tests/fun10_test.c`

## AI assistance

Tristano Suriani develops Fel-Tracker with the help of AI coding agents (for coding, testing and documentation). He
decides the design, the features, the sound design and the direction. Felucca's own AI disclaimer is in its README
section.
