# Layers: how Blue Hare builds a multilayer patch

Blue Hare is a multilayer synthesizer for live performance, not a groovebox. A **patch** is up to four **layers**, each a
whole sound on its own engine, playing together through one shared effects chain. This page records the design and
what is built (2026-10-16). It follows the Waldorf Blofeld's Multi mode and the Waldorf Protein's layers
([Milestones.md](Milestones.md), Milestone 2).

## The model

| Part | What it is | Where |
| --- | --- | --- |
| Layer | A whole sound: an engine, its parameters, envelope, LFO, mod matrix, voice mode. The firmware's four tracks are the four layers | `core.h` `trk[]` |
| In the patch | A layer ON plays the notes; with no layer ON, the keys play the selected layer alone | `core.h` `lay.on` |
| Ranges | Each layer's key range and velocity range: a key split or a velocity split is ranges set apart | `lay.klo` `khi` `vlo` `vhi` |
| Transpose | Each layer plays the note plus its own TRANS (-24 .. 24): intervals, octaves, a sub layer | `lay.tr` |
| Play mode | STACK: every ON layer whose ranges hold the note. ROUND ROBIN: one of them a note, in turn | `lay.mode` |
| Crossfade | XFADE by VELOCITY, KEY or WHEEL: the layers given a place (XPOS) fade into each other, equal power, two neighbours at a time; a layer without a place stays at full level | `lay.xsrc`, `lay.xpos` |
| Layer insert | DRIVE, a 12 dB filter (OFF LP BP HP, cutoff, resonance), LOW CUT, PAN MODE, before the patch's effects | `fx.c` `track_dist`, `track_filter`, `track_lowcut` |
| Patch effects | One chain for the whole patch: chorus, delay, reverb, master; each layer reaches it by its sends | `fx.c` buses |
| Voices | One pool shared by the layers (8, or 12 while the load allows), capped per engine | `voice.c` |

Layer sounds are **embedded** in the patch: a patch file holds each layer whole, so editing a saved sound never changes a
patch. A sound is pulled into a layer with **Load layer**, as the Protein's Load Layer.

A MIDI channel per layer comes later. Today every channel that MIDI IN listens to plays the patch.

## How a note is routed

Every note played, from the keys or MIDI IN, goes through `seq.c` `input_on` / `input_off`. With a layer ON, `layer_targets`
picks the layers (STACK: all whose ranges hold the note and its velocity; ROUND ROBIN: the next of them after the
last that played). Each plays the note plus its TRANS. `lay_tn[note][layer]` remembers the note each layer started,
so the release ends exactly that note, whatever changed in between. Chords, the sustain pedal and MIDI note ownership
work as before, one level up. Pitch bend, the mod wheel, expression, aftertouch and the mapped CCs (cutoff,
resonance, attack, release, the sends ...) reach every ON layer: the patch is played as one instrument.

**Crossfade** *(2026-10-17)*. VELOCITY and KEY give each note its own level on each layer as it starts (`voice_t.xg`, by
`lay_xgain`); a layer faded out for a note does not play it (no voice spent). WHEEL (MIDI CC1, the studio's MORPH)
sets the layers' levels now, gliding (~5 ms), every voice at once. Example: a pad at place 0 and strings at 127 on
WHEEL, the bass with no place under both: the wheel morphs the pad into the strings, the bass stays.

**MONO layers in a chord** *(2026-10-17)*. A MONO or LEGATO layer stacked under a POLY one gets every note of a
chord. Notes within 30 ms of the one before on that layer are a chord's: they move its pitch (by PRIO: LAST, LOW,
HIGH) without a new attack, so a chord no longer stutters through the mono voice; notes further apart are a phrase and
retrigger as before. For a bass layer: MONO, PRIO LOW, a key range below the split.

## How Blofeld and Protein do it

| | Blofeld (Multi) | Protein | Blue Hare |
| --- | --- | --- | --- |
| A layer is | A stored Sound, referenced by the part | A layer of the preset | A whole sound, embedded |
| Layers | 16 parts | Up to 4 | Up to 4 (8 in an 8-track build) |
| Voices | 25, shared | 8, shared | 8 or 12, shared |
| Per layer | MIDI channel, key and velocity range, volume, pan, transpose, detune | Its own 8-slot mod matrix | Ranges, transpose, level, pan, insert (drive, filter, low cut) |
| Combining | Ranges, MIDI channels | Stack, round-robin, MIDI split | Ranges, STACK or ROUND ROBIN |
| Effects | One global slot (delay, reverb) for all parts; parts 1-4 keep one more | 2 slots | One chain per patch, sends per layer |

Sources: [Sound On Sound](https://www.soundonsound.com/reviews/waldorf-blofeld-keyboard),
[Elektronauts](https://www.elektronauts.com/t/can-any-blofeld-users-check-this/131509),
[Kraft Music](https://kraftmusic.com/products/waldorf-protein-8-voice-desktop-wavetable-synthesizer).

## On the FM-1

- **ALGORITHM** steps through the layers: the knobs edit the selected one.
- **GLO held + F3 .. B3** jumps to layer 1 .. 4 (was SOLO). GLO's black keys still mute layers, its knobs set their levels.
- Still to design: a page that shows the patch's layers and sets ON, ranges and TRANS on the device (the studio has it).

## In the studio

A **LAYER** tab (in the patch, key and velocity ranges, transpose; the layer insert; the crossfade and MORPH), the play mode beside the layers
in the header, **Export patch** / **Import patch** (all layers embedded, the play mode, the effects), **Export layer** /
**Load layer** for one sound. The EFFECTS tab is the patch's chain.

## Next

- The patch's effects in float on the FPU: amb-1's delay and plate as types beside Blue Hare's, fast math where it fits.
- Patch storage on the FM-1 (patches beside sounds), and the device's layer page.
- Layer roles beyond ranges, transpose and the crossfade (to discuss): a drone that holds (later), layers that follow
  another layer's envelope, a time offset (strum).
- A knob for WHEEL on the FM-1 itself (today: MIDI CC1, the studio's MORPH).
