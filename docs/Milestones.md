# Milestones

## Goal
Make a user-friendly multi-layer synthesizer software for multiple architectures, with the first hardware target being the M-VAVE FM-1. 
Sources of inspirations include the Waldorf Blofeld and Waldorf Protein, with the latter being particularly relevant for the M-VAVE FM-1 due to its similar amount of voices. 

## Focus
Excellent UI and sound quality. When a feature undermines the UI or sound quality, it will be postponed or dropped, or disallowed on a particular target.

## New UI
The UI will be iteratively redesigned according to the design principles document.

## Architecture and technical debt
Reducing technical debt according to the high level architecture is a priority and will be done iteratively. Easy changes with high impact should be prioritised rather than postponed.

## Milestone 1: Basic synthesizer engines and FX chain
In order to be able to test the UI and sound quality, the first milestone is to implement a basic set of synthesizer engines and an FX chain.
The FX chain will have presets and all algorithms used. A sound design studio webapp must be created in order to provide a way to design sounds and test the engines and FX chain. The webapp will also be used to create presets for the engines and FX chain. A generative ambient engine will be implemented in order to provide a way to test the sound quality of the engines and FX chain. The generative ambient engine will be used to create a set of presets for the engines and FX chain. On the FM-1 it will use the ARP button to start and to stop the generative engine and the ui will provide a way to switch between scales and presets. The FX screen will only allow to choose between presets.

Base engines to start with:
- SH101 (new) after the Roland SH-101.
- FMKeys
- Modal
- Wave
- West
- Sid
- Echoes
This list can be discussed and negotiated based to the desired outcomes.

FX:
All the FX that are listed as global or per track which are not groovebox oriented have to be enabled. The ones that are not relevant will be archived.

## Milestone 2: Multi-layer synthesizer
The second milestone is to implement a multi-layer synthesizer. The multi-layer synthesizer will have a set of layers, each with its own engine and FX chain. The user will be able to create and edit layers, and the UI will provide a way to switch between layers. The user will be able to save and load multi-layer presets, and the webapp will provide a way to create and edit multi-layer presets. 
This Milestone is completed when the user can create a multi-layer preset with at most 4 layers and can save and load the patches on the FM-1. 
Open questions: 
- 8 or 12 voices maximum on the FM-1? A layer will use at least 1 voice, so more layers means less polyphony and more CPU usage. On the FM-1 at most 4 layers will be allowed.
- How do we provide an intuitive way to add layers to sound well together so that the user can create a multi-layer preset without having to understand the details of each engine and FX chain, but without limiting exploration for the user?
- How do we stack layers together? They could play at unison, or morphing or alternating between each others, for example.


## Milestone 3: 32 steps sequencer
The 32 steps sequencer will be implemented in order to provide a way to create and edit sequences. The user should be allowed to switch between sequences and the sequences should be able to support being synchronised with MIDI clock as well as using its own internal clock.
