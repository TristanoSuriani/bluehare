# Blue Hare — manual

Blue Hare is an alternative firmware for the **M-VAVE FM-1**. It turns the FM-1 into a **three-part synthesizer for
live playing**: a synth, a drum kit and a voice, each on its own track, played from the FM-1's keys or from an
external sequencer over USB or TRS MIDI. It has no sequencer of its own. Instead it has an arpeggiator per track, and
REC records what your sequencer plays into a loop per track.

This manual covers version 0.40.0. Blue Hare is a fork of [Felucca](https://github.com/hugelton/Felucca) by Leo
Kuroshita (Hügelton Instruments); see the [README](../README.md) for credits and licences.

## Contents

1. [Installing and getting back to the stock firmware](#1-installing-and-getting-back-to-the-stock-firmware)
2. [The panel](#2-the-panel)
3. [The three tracks](#3-the-three-tracks)
4. [Sounds: presets, favourites, your own](#4-sounds-presets-favourites-your-own)
5. [Shaping a sound](#5-shaping-a-sound)
6. [Effects](#6-effects)
7. [The quick layers](#7-the-quick-layers)
8. [The arpeggiator](#8-the-arpeggiator)
9. [Playing from a sequencer: MIDI](#9-playing-from-a-sequencer-midi)
10. [REC: recording loops](#10-rec-recording-loops)
11. [Songs and the autosave](#11-songs-and-the-autosave)
12. [The menu](#12-the-menu)
13. [The engines](#13-the-engines)
14. [The games](#14-the-games)
15. [Troubleshooting](#15-troubleshooting)

---

## 1. Installing and getting back to the stock firmware

Blue Hare installs over USB, like any FM-1 update. From a computer with Python 3:

```
pip install mido python-rtmidi
python3 tools/fm1_install.py bluehare-0.40.0.fwsc
```

The installer finds the FM-1, checks the package and writes it. The FM-1 restarts by itself, and the installer then
checks that the FM-1 is running the new firmware. Keep the cable connected and the computer awake until it says `done`.

**The first time you install Blue Hare 0.40.0** the package also writes its boot protection. That takes about a
second at the start of the install, and for that second only, an interruption cannot be repaired from the FM-1
itself. Use a direct cable and a charged FM-1.

**From then on every install is protected.** If an install is cut off (the cable, the power), the FM-1 starts in
*update mode* (the key lights flash three times) and you simply install again. You can also enter update mode on
purpose: **switch the FM-1 off, hold OCT− alone, switch it on.** (OCT− together with OCT+ at power-on is still the
key calibration.)

**Back to the stock firmware:** install M-VAVE's official package the same way:
`python3 tools/fm1_install.py FM-1.fwsc`. Installing another firmware removes Blue Hare's boot protection.

## 2. The panel

| Control | What it does |
|---|---|
| **Keys** | Play the selected track (27 keys, F3 to G5; OCT− / OCT+ shift them) |
| **SELECT** knob | The tempo (unless MENU > BPM LOCK is on) |
| **ALGORITHM** knob | Selects the track (1, 2, 3); in the menu, the tab |
| **PRESETS** knob | Browses the selected track's sounds and loads them as you turn; in the menu, the row |
| **KNOB 1–4** | The four values shown on the page's cards |
| **HOME** | Tap: the home page, then the MIXER, then CLOCK. Hold: the menu |
| **ENV, LFO, FX, SCL, EDIT, GLO, SAVE, ARP, SEQ** | Tap: their pages (tap again for the next page). FX, SCL, EDIT and GLO held: their quick layer (section 7) |
| **PLAY** | Starts and stops the transport (with an external clock: the sequencer does) |
| **REC** | Arms a take, or ends one (section 10) |
| **OCT− / OCT+** | The keys' octave; on action pages and in dialogs, OCT+ does it and OCT− goes back |

Every page shows four cards, one per knob, and a picture of what they do below them. The header shows the track,
its sound, the tempo and a small metronome while playing.

Page buttons act when you let go. Holding **SAVE** undoes the last sound load. Holding **LFO** while turning a knob
changes the value only while LFO is held (MOMENTARY); press OCT+ before letting go to keep it.

### The pages

| Button | Its pages, in turn |
|---|---|
| HOME | home, MIXER (each track's level, pan, reverb send and mute), CLOCK (BPM, SWING, CLOCK) |
| ENV | ENV (attack, decay, sustain, release), ENV DEST (the envelope to filter, pitch, shape) |
| LFO | LFO (rate, wave, phase, fade), LFO 2 (sync, trigger, polarity), LFO DEST (to pitch, filter, shape, amplitude), MOD (the modulation matrix: four slots of source, destination, amount) |
| FX | REVERB, DELAY, CHORUS, EQ; with MENU > EXPERT on also FX (distortion, chorus send), CHORUS EDIT, EQ BANDS |
| SCL | SCL (root, scale, quantize, transpose), CHORD (the chord a key plays, its voicing) |
| EDIT | SOUND (the engine's main controls); with EXPERT on: EDIT 1 and EDIT 2 (all eight); then VOICE (mono, poly, legato, unison; glide; priority), VOICE 2 (allocation, unison detune, pan, mute), VOICE 3 (spread, octave) |
| SAVE | PRESETS (the sound list), USER (your sounds), PROJECT (load and save the song), TOOLS (INIT SOUND) |
| ARP | starts or stops the arpeggiator; ARP and ARP 2 (section 8) |
| GLO | GLOBAL (BPM, SWING, CLOCK, TUNE), MIDI IN (the selected track's MIDI input and channel) |
| SEQ | LOOP (section 10). Held for a second: HARE CROSSING (section 14) |

## 3. The three tracks

Each track has a job and its own engines.

| Track | Job | Engines |
|---|---|---|
| 1 | the synth | VA, SANDSTORM, HOOVER, SID, OBIE |
| 2 | the drums | KIT, RHYTHM, VAKIT, LINNKIT, WAR |
| 3 | the voices | SPEECH, ONYX, VOCODER, VOSIM, DUBSIREN, RISERS, NOISE |

Turn **ALGORITHM** to select a track. The keys, the pages and the knobs then act on that track; the PRESETS knob
lists only its engines' sounds. A sound saved on one track and loaded onto another becomes a sound of that track's
own engines.

The drum engines play the **General MIDI drum notes on every octave** (C kick, C# rim, D snare, D# clap, F# closed
hat, A# open hat, F / G / A toms, and the kit's own extras on E, G#, B). A drum hit always plays to its end, however
short the note that started it.

## 4. Sounds: presets, favourites, your own

- **Load:** turn **PRESETS**. Each sound loads as soon as it is shown. Hold SAVE to go back to the one before.
- **SAVE > PRESETS:** KNOB 1 steps through the sounds, KNOB 2 changes the engine, KNOB 3 marks a favourite, KNOB 4
  picks the list: ALL, FAV or a category (BASS, LEAD, PAD, PLUCK, KEYS, DRUM, FX, OTHER).
- **Your own sounds:** stop the transport, tap **SAVE** until USER, pick a slot with KNOB 1, press **OCT+**, type a
  name with the keys (the screen shows which key types what), and **OCT+** again. There are 80 slots.
- **Start over:** SAVE > TOOLS > INIT SOUND resets the track to its engine's defaults.

## 5. Shaping a sound

- **The engine's controls:** EDIT > SOUND shows its main four; turn on MENU > CONTROL > EXPERT for all eight on
  EDIT 1 and EDIT 2. What they do for each engine: section 13.
- **Envelope:** ENV shapes the level over a note. ENV DEST sends it to the filter, the pitch or the shape.
- **LFO:** a low-frequency oscillator per track: its rate (or sync to the tempo on LFO 2), wave and destinations.
- **MOD:** four free routings per track: a source (LFO, envelope, velocity, key, random, mod wheel, aftertouch,
  expression, sample-and-hold, slew) to a destination (pitch, cutoff, shape, amplitude, pan, sends, the engine's
  controls) by an amount.
- **VOICE:** POLY, MONO, LEGATO or UNISON (the synth engines stack 3 or 6 voices, detuned by DETUNE), and GLIDE.
- **SCL:** quantize the keys to a scale, transpose, or let one key play a chord (CHORD).

## 6. Effects

- **REVERB** is shared by all tracks: its type (DRY, SPRING, GATED, PLATE), size and mix. Each track sends to it.
- **DELAY** is each track's own: its send, feedback and time (in note values at the tempo).
- **CHORUS** and **EQ** come as presets; MENU > EXPERT opens their full controls.
- **FX** (with MENU > EXPERT) adds distortion per track.
- **The FX layer** (hold FX) has performance effects on the white keys and four macros on the knobs: section 7.

## 7. The quick layers

Hold a button for its layer: a map appears over the page, and the keys and knobs become its shortcuts. Let go to
close it. Double-tap the button to keep the layer open without holding; tap once to close it.

- **FX (held):** the white keys switch performance effects on while held (with MENU > FX LATCH: until pressed
  again); the black keys mute tracks; KNOB 1–4 are four macros. A white key held while turning PRESETS chooses which
  effect that key plays; EDIT puts its default back.
- **GLO (set):** the black keys mute and unmute tracks 1–3; C4 unmutes all; F4 taps the tempo; KNOB 1–3 the tracks'
  levels. GLO + PLAY restarts from the top without stopping.
- **SCL (set):** a key sets the root; KNOB 1–4: root, scale, chord, voicing.
- **EDIT (set):** the white keys pick the selected track's engine (one key each), the key after them INIT; KNOB 1
  engine, 2 sound number, 3 favourite.

In the set layers, **OCT−** puts back what you changed since opening the layer.

## 8. The arpeggiator

Press **ARP** to start or stop it. While it runs, keys held on a track whose MODE is not OFF are played one after
another; each track has its own settings, so a latched bass arpeggio on track 1 can run while you play track 3.

- **ARP:** MODE (UP, DN, UPDN, RND, ORD as played, REPEAT the last key, DNUP, UP+8, CONV converging, DIVG diverging,
  PINKY, THUMB, WALK, CHORD all together, or OFF for this track), RATE (note values at the tempo), OCT (octaves),
  GATE (note length)
- **ARP 2:** SWING, PROB (the chance each step plays), HOLD (the chord stays after you let go; a new chord replaces
  it), ORD (NOTE: sorted low to high; PLAY: as played)

It follows the tempo, or the external MIDI clock. While it plays, the screen shows the visuals.

## 9. Playing from a sequencer: MIDI

Blue Hare is made to be played from a sequencer (a DAW, an Elektron, any MIDI sequencer).

- **Inputs and channels:** GLO > MIDI IN sets, for the selected track, its **input** (USB, TRS, BOTH or OFF) and its
  **channel** (TRK, its own number: track 1 on channel 1, and so on; or 1–16; or ALL). A track on ALL takes the
  channels no other track listens to.
- **Clock:** GLO > GLOBAL > CLK: INT (Blue Hare's own tempo, SELECT), USB or TRS (the sequencer's MIDI clock, its
  Start and Stop).
- **Controllers:** the mod wheel (CC1), expression (CC11) and aftertouch reach the modulation matrix; each engine
  answers CC74 and CC71 with its two most useful controls (section 13); pitch bend works on the tonal engines.
- **USB audio:** the FM-1 appears to the computer as an audio input with Blue Hare's output (MENU > USB LEVEL sets
  whether MASTER changes it).

## 10. REC: recording loops

REC records what the sequencer (or the keys) plays on each track into that track's loop, then plays it back.

1. Set **SEQ > LOOP > BARS** (1, 2, 4, 8 or 16) and **QNT** (OFF: as played; 1/16; 1/32).
2. Press **REC** (it breathes: armed). The take starts with PLAY (or the sequencer's Start), or on the next bar's
   downbeat if already playing (REC lit).
3. Play. A track's **first note in the take clears its old loop**; tracks that get no notes keep theirs and play on.
4. After BARS bars the take ends and the loops play. Press REC during a take to end it early.

**SEQ > LOOP > CLEAR** clears the selected track's loop, **ALL** every loop (turn the knob, then OCT+). The loops
follow the transport and the MIDI clock. They are kept in memory only: they are gone when the FM-1 is switched off.

## 11. Songs and the autosave

**SAVE > PROJECT** saves the whole setup (the three tracks' sounds, the mix, the effects, the tempo and the MIDI
map) as the song, and loads it back. With MENU > RESTORE LAST on (the default), the FM-1 also starts with what you
left: it saves an autosave when you have stopped and not touched anything for 10 seconds.

Songs and sounds made with earlier versions of Blue Hare (or with Felucca) do not load.

## 12. The menu

Hold **HOME**. ALGORITHM moves between the tabs, PRESETS through the rows; any knob or OCT+ / OCT− changes the value;
HOME closes it. The settings are kept across power-offs.

- **DISPLAY:** COLOR (the palette), STYLE (FLAT, LINE, LED), LARGE (big labels and values), ANIM (motion, and an
  idle light show), LEDS (how the buttons glow), SCREEN OFF (after a time without use), SCOPE (what HOME's scope
  shows), HOME (scope or the tracks)
- **CONTROL:** HOLD (how long a layer button is held), KNOB ACCEL, FX LATCH, BPM LOCK, SCALE LEDS, EXPERT (every
  engine control and the effects' full pages)
- **AUDIO:** SPEAKER EQ (FLAT for headphones and recording), USB LEVEL, CLICK, CLICK LEVEL, TUNE
- **SYSTEM:** USB SERIAL (OFF makes the FM-1 a plain audio + MIDI device, needed on some macOS versions for USB
  audio), RESTORE LAST, **CORES**, CALIBRATION (the keys and buttons), **INFO** (the version, the USB link, the CPU
  load, the cores, and what went wrong since power-on: AUDIO and MIDI IN), ABOUT

### CORES: the second processor core

The FM-1's chip has two cores. Blue Hare uses one by default. Set **MENU > SYSTEM > CORES** to 2 and switch the
FM-1 off and on to let the second core mix the busiest track, which leaves room for more voices. The change applies
only at the next start; until then the row says AT RESTART.

With two cores, Blue Hare raises the chip's core supply to the maker's levels for its clock speed (1.35 V and
1.50 V), as the ChoralRoot and Melodee firmwares do; it never goes higher. Two cores are still being tested: on some
units the second core misbehaves. If it does, Blue Hare stops using it for the session on its own, and after a crash
keeps it off at the next start. **INFO** shows CORES: 2, or 1 and the reason.

INFO also counts what went wrong since power-on: **AUDIO** LATE (moments the sound was computed too late: clicks,
notes out of time) and CUT (voices faded out because the load was too high), and **MIDI IN** (LOST: incoming MIDI
came faster than it could be read, so every sounding note was released).

## 13. The engines

KNOB order is EDIT 1 then EDIT 2 (with EXPERT on). The synth engines play 6 voices (UNISON: 3 or 6 stacked).

### Track 1: synths

| Engine | What it is | Controls |
|---|---|---|
| **VA** | The virtual analogue: two oscillators (saw, pulse, sine, triangle, or white, pink, brown noise) and a sub-oscillator into a 24 dB ladder filter | W1, W2 (waves), DTN (detune), SUB (level), SUBW (its wave), CUT, RES, UNI (unison of 3 or 6) |
| **SANDSTORM** | The late-90s trance lead: five saws, a plucked filter, a gate that repeats at the tempo, a pitch dive on release | SPRD, CUT, RES, ENV, DEC, GATE, DIVE, DRIVE |
| **HOOVER** | The rave lead: two pulse-width-modulated saws, the sub, a bend into each note | PW, PWM, RATE, DTN, SUB, BEND, BTIM, CUT |
| **SID** | A Commodore 64 chip voice: pulse, saw, noise, ring and sync | WAVE, PW, PWM, MOD, MDTN, FILT, CUT, RES |
| **OBIE** | After the Oberheim OB-Xa: two oscillators, 12 or 24 dB filter, analog drift | WAVE, PW, DTN, INTV, CUT, RES, FILT, SLOP |

### Track 2: drums

| Engine | What it is | Controls |
|---|---|---|
| **KIT** | An FM drum kit | KIT, TUNE, DECAY, PUNCH, FM, SNAP, TONE, VARY |
| **RHYTHM** | The 808 and the 909, synthesized the way their circuits made their drums | MODEL (808 / 909), TUNE, DECAY, KICK, SNAP, TONE, DRIVE, ACC |
| **VAKIT** | Synth drums: kick, snare, clap, toms, zap, blip, hats and cymbal from the VA's oscillators and filters | TUNE, DECAY, PENV (pitch drop), CUT, RES, NOISE, DRIVE, WAVE |
| **LINNKIT** | The 80s drum machine after the LinnDrum, with its gated sound and sampler grit | TUNE, DECAY, TONE, SNAP, BODY, GATE, CHAR, VEL |
| **WAR** | An industrial kit: FM drums through a folder into clipping | TUNE, DECAY, TONE, METAL, NOISE, PUNCH, DIST, CRUSH |

### Track 3: voices

| Engine | What it is | Controls |
|---|---|---|
| **SPEECH** | Words, as the 80s speech chips spoke them (no recordings) | WORD, BANK, MODE, RATE, INTN, BUZZ, FORM, END |
| **ONYX** | Phrases from the electronic-music vocabulary in four voices, and vowels to sing | WORD, BANK, PLAY, RATE, INTN, BUZZ, FORM, VOICE |
| **VOCODER** | The voices' words and vowels shaping a saw, a square or a supersaw | WORD, BANK, END, RATE, CARR, SPRD, FORM, VOICE |
| **VOSIM** | Two formants of pulse trains: voices, reeds, brass | F1, F2, BAL, PULS, DECAY, KTRK, SWEEP, BRTH |
| **DUBSIREN** | The dub siren and its echo | WAVE, RATE, DEPTH, SHAPE, SWEEP, TIME, FDBK, MIX |
| **RISERS** | Build-ups and landings, timed to the song: noise, saws, sine, an endless Shepard tone, down-lifters, impacts | TYPE, LEN (beats), RANGE, CURVE, SPRD, RES, BUILD, NOISE |
| **NOISE** | Noise as an instrument: three colours, four filters, the filter following the keys | COLOR, FILT, CUT, RES, TRACK, ENV, DEC, TONE |

## 14. The games

### HARE CROSSING

Hold **SEQ** for a second: **HARE CROSSING**. Get Niglio, Blue Hare's mascot, across the road and the river into
the three burrows. The music goes on while you play (your sequencer, the loops); the keys are silent.

- **PLAY:** start; during a game, pause and go on
- **SELECT:** hop forward (right) and back (left); **KNOB 4:** hop right and left
- **HOME** or **SEQ:** back to the instrument

Cars run Niglio over; in the river only the logs carry him. Each life has a time limit (the bar at the top); three
lives. Fill all three burrows for the next level: faster lanes, less time, and from level 4 sinking logs and a fast
lane.

### PONG

Hold **PLAY** for a second: **PONG**, as in the 1972 arcade game. The transport stays as it was before you pressed
PLAY (if it was playing, it starts again from the top). The music goes on; the keys are silent.

- **SELECT** on the start screen: 1 PLAYER (against the FM-1) or 2 PLAYERS
- **PLAY:** serve; during a game, pause and go on; after a game, back to the start screen
- **SELECT:** player 1's paddle (left); **KNOB 4:** player 2's paddle (right)
- **HOME:** back to the instrument

The first to 11 wins. Where the ball meets the paddle sets its angle (the ends send it off steepest), and it speeds
up after the 4th and the 12th hit of a rally. After a point the ball is served toward the player who lost it. The
colours follow MENU > COLOR.

## 15. Troubleshooting

- **No sound from the keys:** check the track is not muted (HOME > MIXER, or the GLO layer), and that the games and
  NAME are not open.
- **Notes from the sequencer go to the wrong track:** GLO > MIDI IN, for each track: its input and channel.
- **The FM-1 does not start (dark):** switch it off, hold OCT− alone and switch it on. Three short flashes: update
  mode; install Blue Hare again.
- **Clicks or missing notes under heavy load:** look at MENU > SYSTEM > INFO (the CPU load, CORES, AUDIO and MIDI IN). Fewer voices
  (MONO or POLY instead of UNISON), or lighter engines on the other tracks, help.
- **The computer calls the FM-1 "Felucca":** your computer remembers the name it first saw for this USB device. On a
  Mac, remove the device in Audio MIDI Setup (with the FM-1 unplugged) and plug it in again.
