# Blue Hare: effects, DSP algorithms and backbone engines

This document lists every effect and every DSP algorithm in Blue Hare's audio path, and names the sound engines the
rest of the system depends on. It describes the tree as of 2026-10-16, after the sequencer, the arpeggiator and the
plugin were removed. The engine list will shrink once the engine whitelist is applied; see
[What archiving engines touches](#what-archiving-engines-touches) for the dependencies that matter then.

Sources are named relative to `firmware/src/` unless a path says otherwise.

## 1. The frame: rates, blocks and numbers

| | Device (FM-1) | Native image (desktop) |
|---|---|---|
| Sample rate | 44.1 kHz, stereo, 24-bit out | 44.1 kHz |
| Processing block | 32 samples (`CTL`), inside a 128-frame I2S half buffer (2.9 ms) | the same code, the same blocks |
| Arithmetic | fixed point only (the pi32v2 core has no FPU): Q15 audio, Q12 to Q30 coefficients, 32x32 into 64-bit products where needed | the same fixed-point engines; the reverbs CONCERT, CAVERN and SHIMMER in floating point (`fx_native.c`) |
| Voices | 12 per track, 8 shared by every track (12 while the audio interrupt's measured load stays under 75 %) | 16 per track |
| Tracks | 4 (8 still buildable) | 4 |

Two rules run through the whole code base:

- **Silence in, silence out.** Every recursive filter keeps its rounding remainder (error feedback) or rounds towards
  zero in its feedback path, so tails end in exact zeros, with no DC offset and no limit cycles. The DC blocker, the
  send low-cuts, LOW CUT, HALL, CONCERT and the SVF modes all follow it.
- **Per-block coefficients, per-sample signals.** Filter coefficients, pitch increments and modulation are worked
  out once per 32-sample block; the amplitude is ramped across the block (`amp_at`), so block-rate control never
  clicks.

## 2. Signal flow

```
keys / MIDI (USB, TRS)
  -> note input: scale map (QNT SNAP / WHITE), TRN, octave; chord keys (CHRD, VOIC)       seq.c, chord.c
  -> voice allocation (POLY / MONO / LEGATO / UNISON, glide, the shared budget, stealing)  voice.c
  -> per voice: ADSR, LFO, modulation matrix -> the engine's render                         voice.c, mod.c, eng_*.c
  -> per track: DIST -> SLICER -> LOW CUT -> level, pan, SPREAD / PAN MODE, WIDTH, DUCK     fx.c, slicer.c
       sends -> chorus | delay (sends low-cut) | reverb (sends low-cut)                    fx.c
  -> mix -> FX layer (REPEAT, REVERSE, filters, TAPE STOP, FREEZE, OCT UP / DN, macros)     perform.c
  -> master: MONO LOWS -> bus compressor -> peak limiter -> soft clip -> DC blocker
             -> SPEAKER EQ (FLAT / LOWCUT / BASS+) -> MASTER                                fx.c, master.c
  -> USB audio (44.1 kHz, or 48 kHz through the resampler)                                  usb.c, uac_fir.h
  -> DAC, with the metronome's click added here only                                        audio.c, click.c
```

## 3. Effects

### 3.1 Per-track inserts

| Effect | Algorithm | Where |
|---|---|---|
| **DIST** | Low cut, then drive (1x to 8x, exponential), then an asymmetric soft clip (a tanh table with a small bias for even harmonics), then a tone low-pass that closes as the drive rises, then make-up gain. Its state is per track. | `fx.c` `dist` |
| **SLICER** | A tempo-synced 16-step insert after DIST and before the sends. GATE: a gain ramped at most 1/128 a sample (2.9 ms), its edges on the step boundaries. STUT: open steps play live and are recorded (22.05 kHz, 16-bit); closed steps loop the recording at the step length, windowed and cross-faded by DEPTH. Rates 1/8 to 32T, 16 patterns, swung by CLOCK's SWING. | `slicer.c` |
| **LOW CUT** | Two one-pole high-passes (12 dB/oct) at 40 to 320 Hz, after DIST and the SLICER, so the sends lose the lows too. | `fx.c` `track_lowcut` |
| **SPREAD** | POLY and UNISON voices alternate left and right of PAN by SPRD; the track carries a sum and a side signal (L - R), the side bypassing DIST so a distorted chord keeps its clean stereo. | `fx.c` `mix_spread`, `voice.c` |
| **PAN MODE** | ALT spreads voices in turn (as SPREAD); RND puts each new voice on a random side. | `voice.c` `voice_start` |
| **WIDTH** | Haas widening on the upper half of the tracks: the signal splits at ~250 Hz; one side plays it whole, the other plays the lows plus the highs delayed 1 to 20 ms. The mono sum keeps its lows. | `fx.c` `mix_haas` |
| **DUCK** | A track marked KEY (the kick) ducks the others by their depth (1 to 15, up to ~23 dB): an instant attack on the key's level, a ~90 ms release. | `fx.c` `duck_key`, `mix_ducked` |

### 3.2 Send buses (shared by all tracks)

| Effect | Algorithm | Where |
|---|---|---|
| **Chorus** | One modulated delay line (5 to 15 ms), read twice half a sine sweep apart: left and right move in opposite directions. Reads interpolate linearly. | `fx.c` `fx_buses` |
| **Delay: CLEAN** | A tempo-synced delay (TIME in note values) with a low-passed feedback loop (COLR, FDBK). | `fx.c` |
| **Delay: TAPE / WORN** | The same line read with wow (~0.5 Hz) and flutter (~6 Hz) between samples; TIME changes glide (the echoes bend in pitch); the loop is low-passed, high-passed and saturated, so high FDBK runs away into a bounded howl. WORN has more of each. | `fx.c` `dly_tape` |
| **Delay: PING** | Ping-pong: the left echo at TIME, the right at twice it, the loop round both (up to 0.74 s). | `fx.c` |
| **Delay: DUAL** | True stereo: two lines, left at TIME and right at 3/4 of it, each with its own low-passed loop, a quarter of each fed into the other. | `fx.c` |
| **Sends low-cut** | The delay and reverb sends lose everything under ~220 Hz (two one-pole high-passes), so the bass stays dry and out of the tails. | `fx.c` `send_lowcut` |
| **Reverb: ROOM** | Freeverb-like: 4 damped combs and 2 allpasses; the combs signed in turn (+ - + -) give a side signal for width. | `fx.c` `rev_room` |
| **Reverb: SPRING** | One spring of a tank (after Välimäki, Parker and Abel): a low cut (~110 Hz), a chain of stretched first-order allpasses `(a + z^-4)/(1 + a z^-4)` whose group delay rises with frequency (the chirp, the "boing"), a loop delay with one-pole damping; two pickups. | `fx.c` `rev_spring` |
| **Reverb: HALL** | A feedback delay network at half rate (22.05 kHz): a low cut, 2 allpass diffusers, 8 mutually prime lines (21 to 34 ms), four of them read through slow sine modulation, one-pole damping, and an 8x8 Paley conference matrix (each line feeds the 7 others, none itself). RT60 0.5 s to 9 s, set from SIZE through a 2^-x approximation. Stereo from split line sums. | `fx.c` `rev_hall` |
| **Reverb: GATED** | HALL cut off sharply: a hit opens the gate, it holds 60 to 500 ms after the last one, then closes over ~10 ms (the 80s gated snare). | `fx.c` |
| **Reverb: CONCERT** | HALL grown: half rate, a 12 ms pre-delay, 4 diffusers, 16 modulated lines (26 to 67 ms), mixed by a 16x16 Hadamard matrix computed as a fast Walsh transform (lossless). ~34 KB of the memory pool. | `fx.c` `rev_concert` |
| **Native-only reverbs** | CONCERT at full rate in floating point, CAVERN (a 70 ms gap, six discrete wall echoes, then a long dark FDN tail) and SHIMMER (a two-head pitch shifter an octave up inside the loop). On the device they load as HALL. | `fx_native.c` |

### 3.3 The performance layer (FX held)

On the master, between its level and its output. The SLICER's 32 KB recording memory is borrowed as one stereo loop
(8192 frames at 22.05 kHz, 371 ms).

| Effect | Algorithm |
|---|---|
| **REPEAT 1/8, 1/16, 1/32** | A beat repeat from the loop, starting on the next 1/16. |
| **REVERSE** | The loop played backwards. |
| **LPF / HPF** | A resonant filter over the mix; the FILTER macro sweeps it. |
| **TAPE STOP** | The read speed falls to zero (the pitch drops with it). |
| **FREEZE** | The loop held. |
| **OCT UP / OCT DN** | A rotating-delay harmonizer: two read taps sweep a 128 to 2176-sample delay half a sweep apart at the speed that gives 2x or 0.5x, cross-faded by a raised cosine. SHIMMER feeds the shifted sound back, so each pass climbs another octave. |
| **Macros** | FILTER, CRUSH (bit reduction), THROW (the dry mix into the delay and reverb), DEPTH (the effect's level, or SHIMMER while an octave plays). |

Black keys 1 to 4 mute tracks 1 to 4 while held. Every effect starts and ends with a 2.9 ms ramp.

### 3.4 Master and output

| Stage | Algorithm | Where |
|---|---|---|
| **MONO LOWS** | The mix's side signal high-passed at ~120 Hz (12 dB/oct); the lows of the side removed from both channels, so the bass is mono. | `fx.c` `mono_lows` |
| **Bus compressor** | 3:1 above a threshold 6 dB under the limiter's; level from the block peak, ~3 ms attack, ~190 ms release, the gain ramped across the block. | `fx.c` `bus_comp` |
| **Peak limiter** | ~0.1 ms attack, ~150 ms release, its threshold where tanh is still nearly linear, so chords get quieter instead of crushed. | `fx.c` |
| **Soft clip** | A 257-point tanh table, interpolated. | `dsp.c` `softclip` |
| **DC blocker** | A ~2 Hz leaky integrator subtracted from the signal, with error feedback (the state reaches the input exactly). | `fx.c` |
| **SPEAKER EQ** | LOWCUT: a 12 dB/oct high-pass at ~110 Hz. BASS+: the bass under ~150 Hz clipped at its own envelope (a level-following trapezoid, odd harmonics), band-passed 220 Hz to 1 kHz and added back (the missing fundamental, for the small speaker). | `fx.c` `spk_bass` |
| **MASTER** | The pot, square-law, smoothed per UI frame. | `master.c` |
| **USB audio at 48 kHz** | A polyphase FIR resampler 160/147: 24 taps per phase, Kaiser window (beta 9), cut-off 22 kHz, Q30 coefficients. | `uac_fir.h`, `usb.c` |
| **Click** | A "magic circle" sine oscillator (a sine and its quadrature at `k = 2 sin(pi f / FS)`): 1760 Hz on the downbeat, 1320 Hz otherwise, ~6 ms decay. Added after the master, to the DAC only, never to USB audio. | `click.c` |

## 4. DSP building blocks

Shared by the engines (`dsp.c`, `shared/`):

| Block | What it is |
|---|---|
| **Phase accumulators** | 32-bit phases; pitch from a 1/16-semitone table plus cents as a factor (`cents_inc`). |
| **Sine** | A 1024-point table, linearly interpolated (THD about -55 dB). |
| **Band-limited saw and pulse** | polyBLEP: a quadratic residual around each wrap (`blep`, `osc_saw`, `osc_pulse`). |
| **Triangle** | Naive (its harmonics fall fast enough). |
| **Noise** | xorshift32 generators, one per voice or engine; the shared `rng` for the LFO's S&H on tracks 1 to 3. |
| **State-variable filter** | Andrew Simper's trapezoidal SVF, unconditionally stable, low-pass output; coefficients per block in Q13 (`tsvf_coef`, `tsvf_lp`). Used by 13 engines. |
| **Soft clip and soft knee** | The tanh table (`softclip`, used by 16 engines) and a linear-then-soft knee for peaks only (`soft_knee`). |
| **2x oversampling** | A polyphase IIR half-band (two first-order allpass paths, after hiir's design for a 0.16 transition) up and down around a shaper; what would fold under 15 kHz is 46 dB down. Used by RAZOR, VENOM, GUTS and SANDSTORM. |
| **Physical models** | DaisySP's resonators ported to fixed point (MIT, `shared/phys_dsp.c`): a modal bank of up to 12 band-pass SVF modes with a strike exciter, a circular-membrane mode set, and an extended Karplus-Strong string (dispersion allpass or curved bridge, DC block, one-pole damping, a resampler for low notes). Q20 to Q24 signals, Q28 SVF coefficients, no float. |
| **Drum map** | The GM drum note to lane map shared by the kits (`shared/drum_map.c`). |

## 5. The voice layer

| Part | Algorithm | Where |
|---|---|---|
| **Allocation** | Per-track voices under a shared budget. When it's full, a new voice takes, in order: the oldest released voice, the oldest extra UNISON voice, then the oldest held voice of a POLY part that isn't its lowest note. A voice taken from another part fades over one block. | `voice.c` |
| **Voice modes** | POLY (ROTATE or REUSE), MONO and LEGATO (a mono stack with LAST, LOW or HIGH priority), UNISON (up to eight detuned voices). | `voice.c` |
| **Glide** | RATE (a fixed speed) or TIME (the same time for any interval). | `voice.c` |
| **ADSR** | One per voice, ticked per block; an engine can supply its own amplitude curve (`engine_t.amp`). | `voice.c` |
| **LFO** | Sine, triangle, saw, square, S&H; free RATE or SYNC to a note value of the tempo; TRIG NOTE or FREE; bipolar or unipolar; FADE in after note-on. | `voice.c` |
| **Fixed routings** | ENV DEST (filter, pitch, shape) and LFO DEST (pitch, filter, shape, amp). | `voice.c` |
| **Modulation matrix** | 4 slots of source, destination and amount. Sources: LFO, ENV, VEL, KEY, RAND, MODW, AT, EXPR, S&H, SLEW. Destinations: per voice PITCH, CUT, SHP and AMP; per track PAN, the sends (DIST, CHO, DLY, REV), the LFO's RATE and DEPTH, VIB and the engine's E1 to E8. Unused slots cost nothing and leave the output bit-identical. | `mod.c` |
| **Engine switch** | The old engine's voices fade out over a few blocks before the switch; notes played during the fade start on the new engine. | `voice.c` `engine_block` |
| **Chord keys** | Diatonic triads and sevenths of the track's scale, or fixed shapes, in five voicings, at most four notes. Each key or MIDI note releases exactly the notes it started. | `chord.c` |

## 6. The engines

47 engines are in the tree (`engine_ids.h`: each has a stable id and a substitute that loads in its place when a build
lacks it). Engines 0 to 13 and 27 were Felucca's and are retired; every engine below is Blue Hare's own. NEBULA and
CANYON exist only in the native image.

### Kits and percussion

| Engine | Algorithm |
|---|---|
| **FMPERC** | Two-operator FM made to be hit: a modulator at RATIO times the note moves the carrier's phase by INDEX; each hit sweeps pitch and index down. Its `fmp_run` is the voice of KIT, BREAKS and FLOOR. |
| **KIT** | A General MIDI drum kit on one track, eight lanes (kick to bell) of FMPERC voices, with lane levels and mutes. |
| **BREAKS** | A synthesized breakbeat: a key starts a one-bar pattern (kick, snare with accents and ghost notes, hats) at the song tempo, looping while held. |
| **FLOOR** | Four-on-the-floor grooves, made like BREAKS. |
| **WAR** | An industrial kit: a key a drum, each a pair of FM operators. |
| **LINNKIT** | The LinnDrum's kit, synthesized (no samples). |
| **CHIPKIT** | The 8-bit consoles' drums, made the way their sound chips made them. |
| **GLITCH** | Micro-sounds a key each: clicks, pops, sweeps, random-pitch squares, data noise. |
| **CHOKE** | Gated noise, hard-limited: resonant band-pass noise, a falling sine body, a snap. |

### Subtractive, virtual-analog and leads

| Engine | Algorithm |
|---|---|
| **WASP** | Two oscillators (band-limited saw, square, pulse, noise) into a gritty multimode filter, after the Wasp of 1978. |
| **SID** | The C64 chip's way of sounding: a 12-bit-quantised phase accumulator, its combined waves, a 23-bit noise shift register, ring mod and hard sync. |
| **RAZOR** | Up to 7 detuned band-limited saws, a sub square and noise, driven (2x oversampled) into a resonant low-pass and a high-pass edge. |
| **SANDSTORM** | Five detuned saws into two resonant low-passes in series (24 dB), oversampled drive: the late-90s trance lead. |
| **HOOVER** | Two detuned PWM saws plus a square an octave down, a pitch bend into the note, a low-pass: the rave lead. |
| **VENOM** | A low-pass and a band-pass that cross-modulate each other's cutoff at audio rate (caustic acid). |
| **GUTS** | A sine wavefolder made lopsided by an offset, then a resonant low-pass (filthy acid). |
| **DARK** | Sub-bass: a sine with sub-octaves through a sine folder. |
| **SLAG** | 2 to 4 drifting oscillators, saturated, through a low-pass that sinks over each note. |
| **STRINGS** | A string ensemble: 16', 8' and 4' saws plus a square per voice, filtered, with its own ensemble chorus as a post-voice stage. |
| **DUBCHORD** | A whole four-note chord in one voice through one filter (the dub-techno stab). |

### FM, wavetable and vector

| Engine | Algorithm |
|---|---|
| **WAVE** | Wavetables: 8 banks of 8 single-cycle frames in flash, POS crossfading neighbours (the shape routings move it). Its tables are VECTOR's sources. |
| **VECTOR** | Four sources at the corners of a square, mixed bilinearly as a point moves over it. |
| **BLISS** | Four-operator FM as two pairs, in the manner of 80s FM pianos and bells. |
| **WEST** | A complex oscillator (FM, feedback), a wavefolder and a low-pass gate, the Buchla way. |
| **DRILL** | A sine carrier pitch-ramped at audio rate and phase-modulated (power tools). |

### Physical models and resonators

| Engine | Algorithm |
|---|---|
| **MODAL** | DaisySP's modal resonator in fixed point: an exciter striking a bank of tuned modes (bar, marimba, bell, glass ...). Its `md_render` is ANVIL's voice. |
| **ANVIL** | Heavy metal struck, on MODAL's resonator with heavy materials (anvil, pipe, gong, chain, beam, tank, slab). |
| **KARPLUS** | Karplus-Strong: a period-long delay line filled with an excitation at the pluck, fed back through damping. |
| **ABYSS** | A feedback network of four delay lines, a cavern that rings (or sings the note with TUNE). |

### Voice and formant

| Engine | Algorithm |
|---|---|
| **VOSIM** | Kaegi and Tempelaars' VOSIM (1978) with two formants: trains of decaying sin^2 pulses per period. |
| **SPEECH** | LPC speech: a buzz or hiss through a 10-pole lattice filter at 8820 Hz whose reflection coefficients glide word by word (no recordings). |
| **SCREAM** | An unstable glottal pulse train (roughness, jitter, breaks) with formants. |
| **HONKS** | A clipped reed (saw) with breath noise through two formant resonances, each note scooping up into pitch. |

### Noise, chaos, grains and texture

| Engine | Algorithm |
|---|---|
| **CHAOS** | Chaotic systems as oscillators: Lorenz and Rössler flows integrated at the note's speed, logistic and Hénon maps per period. |
| **RUST** | Noise and a pulse, sample-rate reduced, bit-crushed and corrupted, through two resonances. |
| **FLUIDS** | A morphing oscillator through a chain of 2 to 8 first-order allpasses mixed back with the dry signal (liquid notches). |
| **PARTICLE** | A cloud of synthesized grains: random start times at DENS a second, SIZE long, scattered in pitch (quantisable), chirped. |
| **NEBULA** (native) | PARTICLE's cloud uncut: 64 grains a voice. |
| **SHRED** | A source written into a 35 ms ring, read back by two windowed grains that stretch, freeze or reverse it. |
| **SCRATCH** | A turntablist's hands: a worked-out "record" (stab, voice, kick or noise) read at the speed of scratch gestures. |
| **BYTE** | Bytebeat: one-line formulas of a counter t (shifts, products, logic), held at 8 kHz on C4. |

### Echoes, sirens and builds

| Engine | Algorithm |
|---|---|
| **ECHOES** | A note and its echoes as one instrument: every repeat played anew, each different from the last. |
| **CANYON** (native) | ECHOES' repeats uncut: 64 of them, all ringing. |
| **DUBSIREN** | The sound-system siren box: an oscillator swept by a shaped LFO, with its own echo (last track only). |
| **DUBSIREN8** | DUBSIREN in 8 bits, for any track. |
| **RISERS** | Build-ups timed to the tempo: noise or tone climbing RANGE semitones over LEN beats along CURVE. |

## 7. The backbone engines

"Backbone" here means an engine the rest of the system leans on, by any of four measures:

1. **It lends code to other engines.** Archiving it means moving that code into `shared/` or archiving its users too.
2. **Other engines fall back to it.** When a sound's engine isn't in a build, it loads as that engine's substitute
   (`engine_ids.h`), so every stored sound and preset has somewhere to land.
3. **It fills a power-on track role** (kit, bass, mid, high: `engines.c` `TRK_DEF`).
4. **Most builds carry it.**

| Engine | Lends code to | Engines falling back to it | Builds that carry it (of 6) | Power-on role |
|---|---|---|---|---|
| **WAVE** | VECTOR (its wavetables) | 8: PARTICLE, BLISS, DUBCHORD, VECTOR, ECHOES, SLAG, FLUIDS, STRINGS | 4 | mid (IDM) |
| **KIT** (with **FMPERC**) | FMPERC's `fmp_run` is the voice of KIT, BREAKS and FLOOR | 6: FMPERC, BREAKS, GLITCH, WAR, LINNKIT, CHIPKIT | 2 | kit (reference, native) |
| **SID** | none | 4: BYTE, HOOVER, WASP, DARK | 3 | bass and high (CHIP, reference) |
| **GLITCH** | none | 4: CHAOS, KIT, DRILL, SCRATCH | 3 | none |
| **MODAL** | its resonator (`md_render`, DaisySP's `phys_dsp.c`) is ANVIL's | 1: ANVIL | 3 | high (IDM) |
| **WASP** | none | 2: SID, RAZOR | 3 | bass (IDM) |
| **BREAKS** | none (plays FMPERC's voice) | 1: FLOOR | 4 | kit (IDM) |
| **ECHOES** | its repeats (`ec_*`) are CANYON's | 2: ABYSS, CANYON | 3 | none |
| **PARTICLE** | its grain helpers (`pt_*`) are NEBULA's | 1: NEBULA | 3 | none |
| **RAZOR** | none | 2: GUTS, VENOM | 1 | high (FOUNDRY) |

**The backbone, then:**

- **WAVE**: the most-substituted engine and the wavetable source. Every melodic family falls back to it.
- **KIT with FMPERC**: the percussion spine. Every kit falls back to KIT, and FMPERC's voice drives KIT, BREAKS
  and FLOOR.
- **SID**: the fallback for the bass and lead families, and the default bass in two builds.
- **WASP**: the subtractive core (SID and RAZOR land on it), the IDM bass.
- **MODAL**: the only home of the physical-modelling code (DaisySP's resonators), which ANVIL needs too.
- **GLITCH**: where the noise and micro-sound family lands (CHAOS, DRILL, SCRATCH, and KIT itself as a last
  resort).

These six cover the four track roles (kit: KIT or BREAKS; bass: SID or WASP; mid: WAVE; high: MODAL or SID) and give
every other engine a substitute within one or two hops.

## What archiving engines touches

When the whitelist is applied:

- **Shared code goes with its owner unless moved.** Archiving FMPERC breaks KIT, BREAKS and FLOOR; archiving MODAL
  breaks ANVIL; archiving WAVE breaks VECTOR; archiving PARTICLE or ECHOES breaks NEBULA or CANYON. Either keep the
  owner, or move `fmp_run`, `md_render` (with `shared/phys_dsp.c`), the wavetables, `pt_*` or `ec_*` into
  `shared/`.
- **Substitutes must stay inside the build.** Every kept engine's substitute chain has to end at a kept engine.
  Otherwise a stored sound of an archived engine has nowhere to load, and the project or preset is refused. The
  chains in `engine_ids.h` are then rewritten for the one distribution.
- **Ids stay.** An archived engine's id is retired, never reused, as Felucca's 0 to 13 and 27 were.
- **Golden renders and CPU baselines** of archived engines move out of `tests/golden.txt` and
  `tests/cpu_baseline.txt` with them; the target cost budget (`tests/target_budget.txt`) keeps only the kept render
  loops.
- **The effects, the voice layer and the building blocks in sections 3 to 5 don't depend on any one engine.** They
  stay whole whatever the whitelist is. Section 4's blocks lose users but not their purpose.
