# Blue Hare: effects, DSP algorithms and backbone engines

This document lists every effect and every DSP algorithm in Blue Hare's audio path, and names the sound engines the
rest of the system depends on. It describes the tree as of 2026-10-16, after the sequencer, the arpeggiator and the
plugin were removed and Milestone 1's first step ([Milestones.md](Milestones.md)) archived the groovebox's parts: every
engine but SID, SH101, WAVE, VOSIM, BLISS, WEST, MODAL and STRINGS (`archive/engines/`), the SLICER, DUCK and the FX layer's REPEAT,
REVERSE and TAPE STOP (`archive/fx/`). One distribution is built, HARE FM1.

Sources are named relative to `firmware/baseline/` (the sound; `firmware/os/` the instrument's system, `firmware/fm1/` the FM-1 port) unless a path says otherwise.

## 1. The frame: rates, blocks and numbers

| | Device (FM-1) | Native image (desktop) |
|---|---|---|
| Sample rate | 44.1 kHz, stereo, 24-bit out | 44.1 kHz |
| Processing block | 32 samples (`CTL`), inside a 128-frame I2S half buffer (2.9 ms) | the same code, the same blocks |
| Arithmetic | fixed point today: Q15 audio, Q12 to Q30 coefficients, 32x32 into 64-bit products where needed. The pi32v2 core has a single-precision FPU, on since 2026-10 (`-mfprev1`, tools/build.py). Since 2026-10-17 the effects ported from amb-1 (PLATE, AMB) and the EQ run in float on it, their maths X0X's `fastmath.h` (Charles Vestal's: no libm on the device); the engines stay fixed point | the same fixed-point engines; the reverbs CONCERT, CAVERN and SHIMMER in floating point (`fx_native.c`) |
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
  -> layers: the ON layers a note reaches (ranges, STACK / ROUND ROBIN, TRANS, crossfade)  seq.c
  -> per layer: DIST -> LOW CUT -> FILTER -> level (x WHEEL crossfade), pan, SPREAD         fx.c
       sends -> chorus | delay (sends low-cut) | reverb (sends low-cut)                    fx.c, fx_amb.c
  -> mix -> FX layer (filters, FREEZE, OCT UP / DN, macros, mutes)                        perform.c
  -> master: EQ (4 bands, float) -> MONO LOWS -> bus compressor -> peak limiter -> soft clip -> DC blocker
             -> SPEAKER EQ (FLAT / LOWCUT / BASS+) -> MASTER                                fx.c, master.c
  -> USB audio (44.1 kHz, or 48 kHz through the resampler)                                  usb.c, uac_fir.h
  -> DAC, with the metronome's click added here only                                        audio.c, click.c
```

## 3. Effects

### 3.1 Per-track inserts

| Effect | Algorithm | Where |
|---|---|---|
| **DIST** | Low cut, then drive (1x to 8x, exponential), then an asymmetric soft clip (a tanh table with a small bias for even harmonics), then a tone low-pass that closes as the drive rises, then make-up gain. Its state is per track. | `fx.c` `dist` |
| **LOW CUT** | Two one-pole high-passes (12 dB/oct) at 40 to 320 Hz, after DIST, so the sends lose the lows too. | `fx.c` `track_lowcut` |
| **SPREAD** | POLY and UNISON voices alternate left and right of PAN by SPRD; the track carries a sum and a side signal (L - R), the side bypassing DIST so a distorted chord keeps its clean stereo. | `fx.c` `mix_spread`, `voice.c` |
| **PAN MODE** | ALT spreads voices in turn (as SPREAD); RND puts each new voice on a random side. | `voice.c` `voice_start` |

Archived 2026-10-16 (`archive/fx/README.md`): the **SLICER** (a tempo-synced 16-step GATE / STUT insert) and **DUCK**
(a kick track ducking the others).

### 3.2 Send buses (shared by all tracks)

| Effect | Algorithm | Where |
|---|---|---|
| **Chorus** | One modulated delay line (5 to 15 ms), read twice half a sine sweep apart: left and right move in opposite directions. Reads interpolate linearly. | `fx.c` `fx_buses` |
| **Delay: CLEAN** | A tempo-synced delay (TIME in note values) with a low-passed feedback loop (COLR, FDBK). | `fx.c` |
| **Delay: TAPE / WORN** | The same line read with wow (~0.5 Hz) and flutter (~6 Hz) between samples; TIME changes glide (the echoes bend in pitch); the loop is low-passed, high-passed and saturated, so high FDBK runs away into a bounded howl. WORN has more of each. | `fx.c` `dly_tape` |
| **Delay: PING** | Ping-pong: the left echo at TIME, the right at twice it, the loop round both (up to 0.74 s). | `fx.c` |
| **Delay: DUAL** | True stereo: two lines, left at TIME and right at 3/4 of it, each with its own low-passed loop, a quarter of each fed into the other. | `fx.c` |
| **Delay: AMB** *(2026-10-17)* | amb-1's echoes (Charles Vestal's), in float: a 5.3 s tape loop at a quarter rate in the delay's own line, darker and thinner each pass (COLR, FDBK), wow, two heads that wander across the stereo field (GEN's MOTION), the second sliding to another place on the loop now and then; and a 160 ms Echoplex, fed back, its second tap right. The echoes feed the reverb's send, as amb-1's feed its plate. Switching to or from it clears the line 4096 samples a block (12 ms of silence). | `fx_amb.c` `dly_amb` |
| **Sends low-cut** | The delay and reverb sends lose everything under ~220 Hz (two one-pole high-passes), so the bass stays dry and out of the tails. | `fx.c` `send_lowcut` |
| **Reverb: ROOM** | Freeverb-like: 4 damped combs and 2 allpasses; the combs signed in turn (+ - + -) give a side signal for width. | `fx.c` `rev_room` |
| **Reverb: SPRING** | One spring of a tank (after Välimäki, Parker and Abel): a low cut (~110 Hz), a chain of stretched first-order allpasses `(a + z^-4)/(1 + a z^-4)` whose group delay rises with frequency (the chirp, the "boing"), a loop delay with one-pole damping; two pickups. | `fx.c` `rev_spring` |
| **Reverb: HALL** | A feedback delay network at half rate (22.05 kHz): a low cut, 2 allpass diffusers, 8 mutually prime lines (21 to 34 ms), four of them read through slow sine modulation, one-pole damping, and an 8x8 Paley conference matrix (each line feeds the 7 others, none itself). RT60 0.5 s to 9 s, set from SIZE through a 2^-x approximation. Stereo from split line sums. | `fx.c` `rev_hall` |
| **Reverb: GATED** | HALL cut off sharply: a hit opens the gate, it holds 60 to 500 ms after the last one, then closes over ~10 ms (the 80s gated snare). | `fx.c` |
| **Reverb: CONCERT** | HALL grown: half rate, a 12 ms pre-delay, 4 diffusers, 16 modulated lines (26 to 67 ms), mixed by a 16x16 Hadamard matrix computed as a fast Walsh transform (lossless). ~34 KB of the memory pool. | `fx.c` `rev_concert` |
| **Reverb: PLATE** *(2026-10-17)* | amb-1's plate (Charles Vestal's): Dattorro's ("Effect Design, Part 1", JAES 1997) in float at half rate: a 30 ms pre-delay, a bandwidth low-pass, four input diffusers, a figure-8 tank of two modulated allpasses, delays, damping low-passes and allpasses, seven taps a side. SIZE the decay (0.6 to 0.975), DAMP the damping. Its lines (69 KB) share CONCERT's memory; as loud as HALL. | `fx_amb.c` `rev_plate` |
| **Native-only reverbs** | CONCERT at full rate in floating point, CAVERN (a 70 ms gap, six discrete wall echoes, then a long dark FDN tail) and SHIMMER (a two-head pitch shifter an octave up inside the loop). On the device they load as HALL. | `fx_native.c` |

### 3.3 The performance layer (FX held)

On the master, between its level and its output, with its own 16 KB buffer (4096 stereo frames). Its default key map:
LPF, HPF, FREEZE, OCT UP, OCT DN on the first five white keys.

| Effect | Algorithm |
|---|---|
| **LPF / HPF** | A resonant filter over the mix; the FILTER macro sweeps it. |
| **FREEZE** | What it caught (186 ms) held, read by two overlapping grains under triangle windows at random places. |
| **OCT UP / OCT DN** | A rotating-delay harmonizer: two read taps sweep a 128 to 2176-sample delay half a sweep apart at the speed that gives 2x or 0.5x, cross-faded by a raised cosine. SHIMMER feeds the shifted sound back, so each pass climbs another octave. |
| **Macros** | FILTER, CRUSH (bit reduction), THROW (the dry mix into the delay and reverb), DEPTH (the effect's level, or SHIMMER while an octave plays). |

Black keys 1 to 4 mute tracks 1 to 4 while held. Every effect starts and ends with a 2.9 ms ramp. REPEAT 1/8, 1/16,
1/32, REVERSE and TAPE STOP were archived 2026-10-16; their numbers in the stored key map are kept and hold nothing.

### 3.4 Master and output

| Stage | Algorithm | Where |
|---|---|---|
| **EQ** *(2026-10-17)* | The patch's four bands, in float: a low shelf at 100 Hz, peaks at 400 Hz and 2.5 kHz (Q 0.9), a high shelf at 8 kHz, ±12 dB each (RBJ cookbook biquads, transposed direct form II; coefficients from `fastmath.h`). A gain glides 0.25 dB a block; a band at 0 dB is passed by, bit for bit. FX > EQ on the FM-1. | `fx.c` `eq_block` |
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

Shared by the engines and the effects (`dsp.c`, `shared/`); who uses each in HARE FM1:

| Block | What it is |
|---|---|
| **Phase accumulators** | 32-bit phases; pitch from a 1/16-semitone table plus cents as a factor (`cents_inc`). |
| **Sine** | A 1024-point table, linearly interpolated (THD about -55 dB). |
| **Band-limited saw and pulse** | polyBLEP: a quadratic residual around each wrap (`blep`, `osc_saw`, `osc_pulse`). SID, SH101. |
| **Triangle** | Naive (its harmonics fall fast enough). |
| **Noise** | xorshift32 generators, one per voice or engine; the shared `rng` for the LFO's S&H on tracks 1 to 3. |
| **Ladder filter** | Four trapezoidal one-pole low-passes (`G = g / (1 + g)`, Q15, `LADDER_G`), the feedback around them solved for the sample (zero-delay: `u = (x - k S) / (1 + k G^4)`), the input through the tanh table. SH101. |
| **State-variable filter** | Andrew Simper's trapezoidal SVF, unconditionally stable, low-pass output; coefficients per block in Q13 (`tsvf_coef`, `tsvf_lp`). WAVE, WEST (13 engines before the archive). |
| **Soft clip and soft knee** | The tanh table (`softclip`: WAVE, DIST, the master, the harmonizer's shimmer) and a linear-then-soft knee for peaks only (`soft_knee`). |
| **2x oversampling** | A polyphase IIR half-band (two first-order allpass paths, after hiir's design for a 0.16 transition) up and down around a shaper; what would fold under 15 kHz is 46 dB down. Its users (RAZOR, VENOM, GUTS, SANDSTORM) are archived; it stays for the next drive (the SH-101 engine's). |
| **Physical models** | DaisySP's resonators ported to fixed point (MIT, `shared/phys_dsp.c`): a modal bank of up to 12 band-pass SVF modes with a strike exciter, a circular-membrane mode set, and an extended Karplus-Strong string (dispersion allpass or curved bridge, DC block, one-pole damping, a resampler for low notes). Q20 to Q24 signals, Q28 SVF coefficients, no float. MODAL's resonator. |

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

HARE FM1 has eight engines, in the order the device lists them (`distributions/hare_fm1.txt`); the native image the
same. Each has a stable id and a substitute that loads in its place in a build without it
(`engine_ids.h`).

| Engine | Id | Algorithm | Substitute |
|---|---|---|---|
| **SID** | 24 | The C64 chip's way of sounding: a phase accumulator whose top 12 bits make the waves (triangle, saw, pulse with PWM, and a 23-bit noise shift register clocked by the oscillator), their combinations, ring modulation and hard sync, then a resonant filter. Basses, sync leads, PWM pads, chip drums. | WAVE |
| **SH101** | 61 | After the 1982 monosynth: one oscillator's waves at once, mixed by level: a band-limited SAW and PULSE (PW 50 % to ~5 %, the SHAPE routings its PWM), a SUB locked to the oscillator's own cycles (a square one or two octaves down, or a 25 % pulse two down) and NOISE; into a 24 dB ladder low-pass (four trapezoidal one-poles, the feedback solved for the sample, zero-delay), its input saturating through the tanh table (a louder mix drives it), the passband half restored as RES rises, self-oscillating past ~110 (pinged at note-on past 100: the resonant kick); CUT half follows the keys; a ~3 cent analog drift. Basses, acid lines, PWM leads, brass, a kick. | SID |
| **VOSIM** | 21 | VOice SIMulation (Kaegi and Tempelaars, 1978) with two formants: each period of the note starts a train of sin^2 pulses, each DECAY times the last; the pulses' width sets the formant (130 Hz to 4.2 kHz), which stays put whatever the note unless KTRK makes it follow; SWEEP glides both at each note (wah, brass, "yow"); BRTH adds breath. The cut where a train meets the next period is band-limited (a polyBLEP), a DC blocker takes out the one-signed pulses' offset. Back from the archive 2026-10-16. | WAVE |
| **WAVE** | 14 | Wavetables: 8 banks of 8 single-cycle frames in flash (`tools/gen_wavetables.py`), band-limited per octave; POS crossfades neighbouring frames (the shape routings move it), windowed SYNC up to 8x, a detuned second oscillator, drive, a resonant low-pass (Simper SVF), CRSH (rate and bit reduction). | BLISS |
| **BLISS** | 26 | Four-operator FM as two pairs, after the 80s FM pianos and bells: the body (a sine phase-modulated by a sine at RATIO, its index falling over DECAY, FDBK on the modulator) and the tine (a high, fast-decaying strike); the index is held under ~14 kHz (Carson's rule against the note) so high notes do not alias; VEL brightens, KSCL softens the top. | WAVE |
| **WEST** | 16 | West coast: a complex oscillator (a modulator at a whole, half or inharmonic RATIO, FM depth and feedback), a sine wavefolder (FOLD, SYM), and a low-pass gate whose cutoff follows the envelope through a vactrol-like lag. FM and FOLD ease off on high notes. | MODAL |
| **MODAL** | 34 | DaisySP's modal resonator in fixed point (`shared/phys_dsp.c`, MIT): an exciter (mallet, pluck, bow, click) strikes 12 tuned band-pass SVF modes of a material (bar, marimba, bell, glass, plate, drum, tube, string); STRUCT stretches them, POS sets the strike place, DECAY / DAMP / BRIGHT the ring. Capped at 4 voices. | WEST |
| **STRINGS** | 55 | A string ensemble (after the Waldorf Streichfett): three saws at 16', 8' and 4' and a square per voice, blended by REG through five registrations (contrabass, cello, viola, violin, organ), BODY the 16' on top, DTN detuning the 16' and 4' against the 8'; a gentle low-pass at BRITE that ANIM sweeps, TREM a ~6 Hz tremolo. Then the layer's ensemble (`engine_t.post`): three reads of a 23 ms line moved by a slow and a fast LFO a third of a cycle apart (SPEED), ENSEM the wet share. Back 2026-10-17 in ECHOES' place (ECHOES and CANYON archived). | WAVE |

The other engines (40 of them, ids 15 .. 60) are archived in `archive/engines/` (its README describes each); their ids
are retired, and a sound of one is refused.

## 7. The backbone

The eight engines cover the synthesizer's ground with little overlap: classic subtractive (SH101), a chip voice (SID), wavetables
(WAVE), formant voices (VOSIM), FM keys and bells (BLISS), west coast FM and folding (WEST), physical modelling (MODAL) and a rhythmic,
string ensemble (STRINGS). What the rest of the system leans on:

- **WAVE**: the wavetable data (`bluehare_wavetables.h`, 130 KB of flash) and the end of most substitute chains (SID,
  BLISS and STRINGS fall back to it); the power-on pad (WARM PAD).
- **MODAL**: the only user of DaisySP's physical models (`shared/phys_dsp.c`); the power-on bells (KALIMBA) and the
  host tests' drums (WOOD DRUM); the voice-capped engine the voice-budget tests use.
- **SH101**: the power-on bass (101 BASS); the one user of the ladder filter (`LADDER_G`).
- **SID**: SH101's substitute; the first engine (the fallback of last resort, and engine 0 in the tests).
- **STRINGS**: the one user of the track's engine block for an effect after the voices (its ensemble line, `eng_slab`).
- **BLISS** and **WEST**: self-contained.

The power-on tracks (`engines.c` `TRK_DEF`) are the roles Milestone 1's generative engine will play: a pad (WAVE), a
bass (SH101), keys (BLISS) and bells (MODAL).

## Next

2026-10-17: amb-1's plate and echoes as REVERB TYPE PLATE and DELAY TYPE AMB, the EQ, all in float with fast math
(`tests/baseline/float_fx_test.c` checks fastmath against libm, the EQ's bands, PLATE's tail and level, AMB's echoes and its
switch). Milestone 1 has SH101 (above) and the sound design studio (`web/studio/`: every engine and every effect of section 3
in the browser, the firmware itself in WebAssembly) and GEN (section 8). Next: FX
presets (the FX screen chooses between presets of the chain in section 3). The 2x oversampling stays unused: SH101's
saturation sits inside its filter's feedback loop, where the half-band's delay would detune the resonance.

## 8. GEN, the generative engine

`gen.c`, on the ARP button, the same code on the FM-1, the native image, the emulator and the studio. Its composition
is amb1's (`docs/excerpt_amb1.c`), ported to fixed point and played on the four tracks with their own sounds (amb1's
own voices, float pads, FM bells, drums and plate, are not used). It runs in the audio interrupt's event step once a
block (`seq.c` `events_block`), its decisions once a 1/16, out of the sample loops.

| Part | Algorithm |
|---|---|
| **Clock** | 1/16 steps of the song's tempo (`beat_samples`: BPM, or the MIDI clock's), the transport running or not; seconds as steps at the tempo. |
| **Randomness** | One xorshift32 generator, seeded from the clock at each start (a fixed seed in the tests: the same notes at the same steps). |
| **Harmony** | The scale (SCL's) on ROOT; a chord stacks the scale's thirds from a degree, four tones in a scale of six notes or more, else three. DRIFT: after its time (20 s to 5 min, x 0.7..1.3) the chord moves on the next bar to another degree, by weight (seven-note scales I 4, IV V vi 3, ii 2, iii 1, vii 0.4; others I 3, the rest 1.5). A key turned while it plays: the chord again on its degree, at once. |
| **Arc** | A slow random swell, -1 sparse to +1 full: a new target every 50 to 150 s (pushed away from the middle), reached over ~35 s, times MOTION. The bells' count, the rests and the pulse's strength follow it. |
| **Pad** (track 1) | Four voices that breathe, as Music for Airports' "2/1": each on its own cycle (1 : 1.21 : 1.43 : 1.67 of 4 to 16 s by SWELL, x 0.85..1.15) holds its note through the rise and the hang (55 %), lets go for the fall (the sound's release) and rests; each breath takes the voice's current note. The notes: the chord's tones voice-led from the last ones over the 24 orders (the least movement, no two voices on one note, G#2..F5). A chord change: a voice on a note the new chord lacks lets go and breathes in on its new note a second later. |
| **Bass** (track 2) | The chord's root (C2..B2, BASS OCT), held; halfway to the next chord, FIFTH's chance of the fifth; at a change it may sit the chord out (EVOLVE, more when the arc is sparse). |
| **Bells** (track 3) | Four loops of 13, 17, 23 and 29 quarter notes, each in its register (C5, G4, G5, D4 by ROOT; BELL OCT), phasing against each other. Each holds 0 to 5 notes (DENSITY, swelled and thinned by the arc): scale steps over the chord's degree, a chord tone 72 % (an octave up 30 % of those), else a passing 2nd, 4th or 6th. At the end of a pass: grown or pruned to the count; EVOLVE moves a note or changes it, and rests the next pass now and then. Sometimes a bell's octave with it, softer. |
| **Pulse** (track 4) | Three euclidean patterns (Bjorklund), 16, 12 and 7 steps (HITS of 5, 7, 3), rotated 0, 2, 3: a thump on the chord's root (C2..B2), a tick on its fifth and a longer one on its root, high. Each bar EVOLVE may turn a pattern or move a hit (within one of its home), or rest the pulse 2 to 4 bars. Its velocity follows the arc. |
| **Waves** (track 4, 2026-10-17) | In the pieces without a pulse (DEPARTURES, VOICES, SHORELINE: SH101's SURF): a swell of noise on the chord's root, 2 to 5 s, its filter opening as it rises (the sound's envelope); then a gap of 1 to 6 s; fewer as the arc thins. |
| **Density** (2026-10-17) | A role each (G_GDPAD .. G_GDTEX, the GEN MIX page while it plays, the studio's Density group): 64 the piece as written. Lower: breaths skipped, bass notes resting, fewer bells, hits dropped, longer gaps, until 0 silences the role. Higher (to 127, about twice): shorter breaths, the bass restruck on the bar, fuller loops, ticks between the hits, shorter gaps. |
| **Play along** (2026-10-17) | A key played while GEN runs (the keys, MIDI IN) is snapped to its scale on ROOT (the nearest step, the lower on a tie) and sounds on the bells' track; its release ends it. |
| **The ocean** (2026-10-17, `ui_ocean.c`) | GEN's pages while it plays: the graph strip (240 x 122) as a sea, redrawn every other UI frame as HOME's scope: a gradient, three light rays, starfish, a shoal of eight fish, bubbles; a bell lets bubbles rise from the sand, a bass note sends the crowned king fish across, a breath of the pad brightens the rays, a hit or a swell flicks the tails. In the GREY and MONO palettes, in gray. The main loop only: the audio is untouched. |
| **Pieces** | amb1's 8 (GARDEN, DEPARTURES, VOICES, AFTERNOON, NIGHT BUS, SHORELINE, GLASS, TIDES): each role's level (0: silent), DENSITY, EVOLVE, DRIFT, HITS, SWELL, MOTION, FIFTH, the tempo, ROOT, the scale, the octaves; here also the four tracks' sounds (an engine id and a preset name) and the delay, reverb and chorus. Picking one loads them; the first start since power-on loads the one shown. |
