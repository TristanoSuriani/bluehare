# Authors and ownership

**Blue Hare** is a fork of **Felucca**, the multi-engine synthesizer firmware for the M-VAVE FM-1 by
Leo Kuroshita (Hügelton Instruments). Blue Hare is developed by **Tristano Suriani**
(<https://github.com/TristanoSuriani/fel-tracker>). It is an independent fork, not an official Felucca release.

Both are free software under the GNU General Public License, version 3 only (`LICENSE`). This file says whose work
is whose. The third-party parts that both build on (fonts, ported DSP, the JieLi SDK files) are listed in
[LICENSING.md](LICENSING.md).

The fork point is Felucca 1.4.1 (commit `f21d6d6`). Everything from commit `7aa4347` on is Blue Hare's, and
`git log` shows exactly which lines changed and who changed them.

## Felucca: Leo Kuroshita (Hügelton Instruments)

Everything in the tree at the fork point, except the third-party parts in LICENSING.md, is Leo Kuroshita's work.
This includes:

- **The firmware:** the HAL, the update loader, the voice and effects engine, the UI, the menus, storage, MIDI, USB
  and the editor protocol.
- **The engines ANALOG, FM6 (with its msfa port), PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE and DRUM**, with their
  factory presets (Blue Hare removed them; what other engines use of PHYS and DRUM is kept in
  `firmware/baseline/shared/`: `phys_dsp.c`, the resonators, and `drum_map.c`, the lanes and the General MIDI map).
- **The tools, the web pages and the tests:** the web editor, the installer and the browser emulator.
- **The name "Felucca"**, its documentation and its artwork.

A file Leo Kuroshita wrote carries his copyright line. Where Blue Hare changed it, a second line follows:
`Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md`. The file stays his, with Tristano Suriani's
changes in it. A few upstream files never had a header (for example `firmware/os/settings_persist.c` and some
tests); git history shows the changes to those.

## Components in the build

What a distribution can be made of today (`firmware/components.txt`, the registry; this table is generated from it by
`python3 tools/gen_dist.py --credits .`, do not edit it by hand). The archived engines are listed further down.

<!-- COMPONENTS-BEGIN -->
| Component | Kind | What | Where | Credit |
| --- | --- | --- | --- | --- |
| SID | engine | a C64 sound-chip voice: pulse, saw, noise, ring, sync | `firmware/baseline/eng_sid.c` | Tristano Suriani |
| SH101 | engine | the little monosynth: saw, pulse, sub, noise, a 24 dB ladder | `firmware/baseline/eng_sh101.c` | Tristano Suriani |
| WAVE | engine | wavetables, scanned and warped | `firmware/baseline/eng_wave.c` | Tristano Suriani |
| VOSIM | engine | two formants of pulse trains: voices, reeds, brass | `firmware/baseline/eng_vosim.c` | Tristano Suriani |
| BLISS | engine | FM keys: electric pianos, bells, mallets | `firmware/baseline/eng_bliss.c` | Tristano Suriani |
| WEST | engine | west coast: complex oscillator, folder, low-pass gate | `firmware/baseline/eng_west.c` | Tristano Suriani |
| MODAL | engine | struck, plucked and bowed resonators | `firmware/baseline/eng_modal.c`, `firmware/baseline/shared/phys_dsp.c` | Tristano Suriani; its resonators DaisySP's (MIT) |
| STRINGS | engine | a string ensemble, its ensemble chorus | `firmware/baseline/eng_strings.c` | Tristano Suriani |
| PLATE | fx | REVERB TYPE PLATE: Dattorro's plate, in float | `firmware/baseline/fx_amb.c`, `firmware/baseline/fastmath.h` | Charles Vestal (amb-1, X0X's fastmath); ported by Tristano Suriani |
| AMB | fx | DELAY TYPE AMB: a tape echo with two wandering heads, an Echoplex | `firmware/baseline/fx_amb.c`, `firmware/baseline/fastmath.h` | Charles Vestal (amb-1); ported by Tristano Suriani |
| EQ | fx | the patch's EQ: four bands, in float | `firmware/baseline/fx.c`, `firmware/baseline/fastmath.h` | Tristano Suriani |
| GEN | plugin | the generative ensemble on ARP: pad, bass, bells, pulse or waves | `firmware/baseline/gen.c` | Charles Vestal (amb-1's composition); Tristano Suriani |
| OCEAN | ui | the ocean on GEN's pages while it plays | `firmware/os/ui_ocean.c` | Tristano Suriani, after his generative proof of concept |
<!-- COMPONENTS-END -->

## Blue Hare: Tristano Suriani

### New files

These carry `Copyright (C) 2026 Tristano Suriani (Blue Hare)`.

| What | Where |
| --- | --- |
| Stable engine ids, distributions, and builds of any set of up to 16 engines | `firmware/baseline/engine_ids.h`, `tools/gen_dist.py`, `distributions/` |
| WAVE (wavetables) | `firmware/baseline/eng_wave.c`, `tools/gen_wavetables.py` |
| BYTE (bytebeat) | `firmware/src/eng_byte.c` |
| WEST (west coast) | `firmware/baseline/eng_west.c` |
| HOOVER (rave lead) | `firmware/src/eng_hoover.c` |
| SPEECH (LPC words in English) | `firmware/src/eng_speech.c`, `tools/gen_speech.py`, `tools/speech/` |
| FMPERC (FM percussion) | `firmware/src/eng_fmperc.c` |
| KARPLUS (plucked string) | `firmware/src/eng_karplus.c` |
| VOSIM (formant pulse trains) | `firmware/baseline/eng_vosim.c` |
| PARTICLE (grain clouds) | `firmware/src/eng_particle.c` |
| CHAOS (chaotic oscillators) | `firmware/src/eng_chaos.c` |
| SID (a C64 sound-chip voice) | `firmware/baseline/eng_sid.c` |
| DUBSIREN (siren with tape echo) | `firmware/src/eng_dubsiren.c` |
| DUBSIREN8, SIREN8 on the device (its 8-bit cousin) | `firmware/src/eng_dubsiren8.c` |
| DUBCHORD (the dub-techno chord, a whole chord in one voice) | `firmware/src/eng_dubchord.c` |
| BLISS (FM keyboards) | `firmware/baseline/eng_bliss.c` |
| WASP (two oscillators, a gritty multimode filter) | `firmware/src/eng_wasp.c` |
| MODAL (a modal resonator; its resonators PHYS's, DaisySP's) | `firmware/baseline/eng_modal.c` |
| VECTOR (vector synthesis over WAVE's frames) | `firmware/src/eng_vector.c` |
| ECHOES (a note and its repeats, each played anew) | `firmware/src/eng_echoes.c` |
| GLITCH (a kit of micro-sounds) | `firmware/src/eng_glitch.c` |
| DARK, HONKS, RAZOR, GUTS, SLAG, VENOM, WAR, CHOKE, RUST, DRILL, ANVIL, SCREAM, FLUIDS, SCRATCH, ABYSS, SHRED (written for FOUNDRY) | `firmware/src/eng_dark.c`, `eng_honks.c`, `eng_razor.c`, `eng_guts.c`, `eng_slag.c`, `eng_venom.c`, `eng_war.c`, `eng_choke.c`, `eng_rust.c`, `eng_drill.c`, `eng_anvil.c`, `eng_scream.c`, `eng_fluids.c`, `eng_scratch.c`, `eng_abyss.c`, `eng_shred.c` |
| KIT (an FM drum kit on one part) | `firmware/src/eng_kit.c` |
| BREAKS (synthesized breakbeat player) | `firmware/src/eng_breaks.c` |
| RISERS (build-ups and landings) | `firmware/src/eng_risers.c` |
| LINNKIT (the 80s sampled drum machine, after the LinnDrum; WAREHOUSE's) | `firmware/src/eng_linnkit.c` |
| STRINGS (a string ensemble, after the Waldorf Streichfett; WAREHOUSE's) | `firmware/baseline/eng_strings.c` |
| FLOOR (four-on-the-floor beats; WAREHOUSE's) | `firmware/src/eng_floor.c` |
| NEBULA (PARTICLE's cloud whole; the native image's) | `firmware/src/eng_nebula.c` |
| CANYON (ECHOES' repeats, all of them; the native image's) | `firmware/src/eng_canyon.c` |
| CHIPKIT (the drums of the 8-bit consoles; CHIP's) | `firmware/src/eng_chipkit.c` |
| SANDSTORM (the late-90s trance lead, named after Darude's track; WAREHOUSE's) | `firmware/src/eng_sandstorm.c` |
| The native image's reverbs CONCERT, CAVERN, SHIMMER | `firmware/baseline/fx_native.c` |
| Tests for distributions and SPEECH | `tests/os/engines_test.c`, `archive/tests/speech_test.c` |

*(2026-10-16)* The engines not in Blue Hare's one distribution, HARE FM1 (SID, SH101, WAVE, VOSIM, BLISS, WEST, MODAL, STRINGS since 2026-10-17;
ECHOES and CANYON archived then), are archived: their files are in `archive/engines/` (the paths above say `firmware/src/` for
where they lived), their tests in `archive/tests/`, SPEECH's generator and vocabularies in `archive/tools/`. The
SLICER is in `archive/fx/`.

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

- The step sequencer is removed. The tracker that took its place is removed too (2026-10): the sequencer is a
  placeholder for now (SEQ shows TODO).
- SAMPLE, GRAIN, SLICE and DIGITAL are removed, along with the sample infrastructure.
- Engines are named by a stable id everywhere a sound is stored or sent, and by an index within the build.
- Distributions run through the build, the browser emulator, the regression tests and the device budget checks.
- Favourites are mapped for the new engine ids.
- The EDIT layer handles 16 engines.
- DRUM's special cases (lanes, mutes, MIDI) cover KIT too.
- Felucca's own engines, its default distribution and FM6's patch editing (its web editor tab, voice bank and preset
  patches) are removed; their ids retired; the host tests build IDM.
- Blue Hare's groovebox parts are archived (2026-10-16): the SLICER (Felucca's), DUCK, and the FX layer's REPEAT,
  REVERSE and TAPE STOP (Felucca's); the FX layer has its own buffer instead of the SLICER's.
- GEN (2026-10), the generative engine on ARP (`firmware/baseline/gen.c`, its page, `tests/baseline/gen_test.c`): its composition
  (the pieces, the breathing pad, the bells' loops, the drift, the arc, the euclidean pulse) is **amb-1's, by Charles
  Vestal** (GPL-3.0; the excerpts in `docs/excerpt_amb1.c` and `docs/excerpt_fastmath_amb1.c`, the latter from his X0X),
  ported to fixed point and to Blue Hare's tracks and engines by Blue Hare.
  amb-1's sound processing (`firmware/baseline/fx_amb.c`: the plate reverb, the tape echo and Echoplex, the master) is
  his too, ported for the studio's comparison (EFFECTS > PROCESSING) and, since 2026-10-17, inside Blue Hare's chain on the device as REVERB TYPE PLATE and DELAY TYPE AMB; `firmware/baseline/fastmath.h` is his X0X's maths library, unchanged. GEN's amb-1 algorithm is kept for comparison
  only, until Blue Hare's own generative algorithms replace it.
- SH101 (2026-10) is Blue Hare's engine, its own design after the Roland SH-101 (no circuit modelled).
- The sound design studio (`web/studio/`, 2026-10-16) is Blue Hare's: its page, its exports and its test. It runs on
  the browser emulator's build and worklet (Felucca's, after X0X), which gained a "call" message for it.
- The documentation is updated to match.

### Files moved from Felucca

These are Felucca's code, moved out of the files they used to live in. They keep Leo Kuroshita's copyright, with
Blue Hare's modification line:

- `tests/lib/factory_patterns.h`, from `firmware/baseline/engines.c`
- `tests/os/lfo2_test.c`, from `tests/fun10_test.c`

## AI assistance

Tristano Suriani develops Blue Hare with the help of AI coding agents (for coding, testing and documentation). He
decides the design, the features, the sound design and the direction. Felucca's own AI disclaimer is in its README
section.
