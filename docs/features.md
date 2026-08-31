# Feature List

This page summarises the capabilities currently implemented on the main development line and the features deliberately left for later work.

## Implemented

### Hardware integration

- Raspberry Pi 3B compatibility retained;
- current Raspberry Pi OS support;
- Ubuntu/Debian compatibility when the WM8960 ALSA device is available;
- Waveshare WM8960 stereo capture and playback;
- Akai MPD218 USB MIDI input;
- optional external WM8960 driver installer without vendoring third-party source.

### Stereo looper

- sixteen independent stereo loop tracks, one per MPD218 pad;
- MPD218 notes 52-67 mapped to pads 1-16, bottom-left first;
- a shorter note list leaves the remaining pads inert, so an older
  four-slot configuration still starts;
- knob 4 selects an existing sampler recording;
- holding a pad assigns that sample to the corresponding track;
- tapping an assigned track starts it on the next beat;
- pressing a playing loop arms its stop;
- the track repeats at the sample's exact trimmed length.

### Performance modes

- three modes, loop, sampler and pad, sharing the same sixteen pads;
- an MPD218 knob selects the mode, a third of its travel each, with a
  configurable dead band that sets how much rotation a change costs;
- only sampler mode records; loop mode manages sample assignments;
- both banks are always mixed, so a running loop survives any mode change;
- the bank that received a pad press also receives its release, so changing
  mode mid-gesture cannot strand a recording;
- disabling the knob leaves the startup mode pinned to pad.

### Pad mode

- all sixteen pads play the FluidSynth SoundFont;
- pad notes are forwarded with their velocity rather than consumed;
- loops and samples keep playing underneath;
- a Note Off follows its own Note On, so a mode change mid-press cannot hang a
  note on the synthesizer;
- the display lights a cell while its pad is held;
- nothing is recorded and no buffer is reserved for the mode.

### Sampler

- every pad records a one-shot, pad-associated sample;
- a hold past a configurable threshold starts a recording;
- recording is never quantized: a sample spans exactly press to release;
- a rolling pre-roll recovers the head of the take, which elapses before the
  hold is recognised;
- the pre-roll drops anything captured while the sampler bank was sounding, so
  a pad's own preview cannot bleed into its replacement;
- a finished sample stays silent until its pad is tapped;
- a tap plays the sample once from its start and stops at its end;
- a tap during playback restarts the sample;
- a further hold replaces the sample;
- recording can use cleaned microphones or the exact speaker-monitor signal;
- knobs 5 and 6 non-destructively trim the selected sample's head and tail;
- maximum sample length configured separately from the loop length.

### Shared transport and quantization

- one sample-accurate transport shared by all loop slots;
- configurable BPM;
- configurable beats per bar;
- assigned-loop starts are anchored to the next beat;
- quantization modes off, beat, or bar apply to loop stops;
- quantized loop stop, with the loop sounding out its current cycle and shown
  as leaving until the boundary arrives;
- a second press before the boundary cancels a pending stop;
- sampler recording remains unquantized;
- integer/rational timing avoids long-term floating-point drift.

### Acoustic echo reduction

- dependency-free stereo MIMO NLMS implementation;
- four adaptive acoustic paths: each speaker reference to each microphone;
- configurable filter length;
- configurable acoustic delay;
- configurable adaptation rate;
- simple double-talk guard;
- exact post-mix, post-gain speaker signal used as the AEC reference;
- AEC can be disabled from configuration without changing JACK routing.

### MIDI routing

- loop-control notes are consumed by Music Box, except in pad mode where they
  reach the synthesizer;
- the mode and volume controllers are consumed by Music Box;
- pad holds are timed on the MIDI thread so the audio callback stays free of
  wall-clock logic;
- all other MIDI events are forwarded to FluidSynth;
- no direct MPD218-to-FluidSynth connection is required;
- ALSA client discovery does not depend on FluidSynth process IDs;
- route repair recovers after client restarts.

### JACK audio routing

- WM8960 microphone channels connected to Music Box capture inputs;
- FluidSynth stereo output routed into Music Box;
- loop playback and synth audio mixed inside Music Box;
- final stereo mix connected to WM8960 playback channels;
- automatic route repair replaces the earlier `mpd-connect` polling service.

### Real-time design

- the JACK callback owns transport, loop and AEC state;
- fixed-size C11 single-producer/single-consumer queues connect MIDI and audio threads;
- no dynamic allocation in the audio callback;
- no logging in the audio callback;
- no sleeping or blocking mutexes in the audio callback;
- audio buffers allocated during startup.

### Installation and operation

- installer for current Raspberry Pi OS and compatible Debian-family systems;
- migration option for the original four-service prototype;
- ordered systemd units for JACK, FluidSynth and Music Box;
- WM8960 readiness check before JACK starts;
- configuration stored in `/etc/music-box/music-box.conf`;
- diagnostic helper for ALSA, JACK, MIDI and service state;
- uninstall script;
- legacy Raspbian Stretch/kernel 4.19 notes.

### Documentation and licensing

- PlantUML software architecture diagram;
- PlantUML source-to-sink sound-path diagram;
- PlantUML MIDI/control-path diagram;
- PlantUML quantized-recording sequence diagram;
- PlantUML per-loop state-machine diagram;
- implementation history;
- AEC tuning guide;
- troubleshooting guide;
- legacy Raspberry Pi OS notes;
- MIT SPDX headers on project-owned source;
- separate third-party licensing documentation;
- no third-party source, kernel modules, SoundFonts or DSP libraries vendored into the repository.

### Tests and CI

- transport boundary tests;
- loop-state transition tests;
- sampler trigger, one-shot completion and re-record tests;
- pre-roll rejection of the bank's own playback;
- stop-arm and stop-cancel notification tests;
- configuration parsing, including short and rejected pad-note lists;
- mode-knob zone resolution across the whole 0-127 range;
- lock-free queue tests;
- synthetic AEC convergence tests;
- strict compiler warnings;
- GitHub Actions build and test workflow.

## Deferred

### Dedicated pad-tap rejection

Quantized recording should usually place the physical pad tap before the recording boundary. A dedicated transient detector, pre-roll trimmer or gate is not implemented yet.

### Advanced looping

- overdub and undo;
- mute/resume without replacing a loop;
- fixed 1/2/4/8-bar recording lengths;
- per-loop and per-sample gain and pan;
- persistent loop and sample save/load;
- reverse playback;
- loop clear gesture.

### External synchronization

- MIDI Clock input/output;
- Ableton Link or network transport;
- first-loop tempo derivation;
- tap tempo;
- MPD218 knob-based tempo control.

### Advanced echo control

- automatic acoustic-delay estimation;
- nonlinear loudspeaker modelling;
- stronger double-talk detection;
- room-tail/reverberation modelling;
- optional backend using an external AEC library.

## Architecture diagrams

See [`architecture.md`](architecture.md) for the full PlantUML diagrams and detailed source/sink descriptions.
