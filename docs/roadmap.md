# Roadmap

## Implemented in this refactor

- shared sample-accurate transport;
- loop and sampler performance modes on one set of pads;
- beat/bar quantized recording, finish and stop;
- dependency-free stereo adaptive echo cancellation;
- real-time-safe state ownership and command queues;
- automatic MIDI/JACK routing;
- current and legacy Raspberry Pi OS installation paths.

## Deliberately deferred

### Dedicated pad-tap rejection

The physical tap on an MPD218 pad can reach the close microphones. Quantized record start already helps because the tap generally happens before the next beat/bar boundary. A quick tap also creates one complete quantization interval.

No transient detector, pre-roll trimming or noise gate is included yet. Evaluate real-world quantized recordings first.

### Tempo control

Tempo currently comes from configuration. Future options include an MPD218 knob, tap tempo, MIDI Clock, or first-loop tempo derivation.

### MIDI knob controls and sound selection

Future controller mappings may use one MPD218 knob for master output volume and another for selecting a larger bank of instruments or SoundFont presets. The mapping, value scaling, pickup behaviour and persistence still need to be designed.

### Loop editing

Possible later additions: overdub/undo, mute/resume without replacement, fixed 1/2/4/8-bar lengths, persistent loop save/load and per-loop gain.

### One-shot recording persistence

Sampler mode now provides short pad-associated recordings that play once per tap instead of repeating. What remains deferred is keeping them across a restart: samples live only in memory, and a save/load format, naming and storage location still need to be designed.

### Repeat/stutter effect

Investigate a knob-controlled effect that repeatedly plays the most recent slice of audio, with the knob selecting the slice duration in milliseconds. Define the source signal, useful duration range, activation gesture, crossfades and real-time memory requirements before implementation.

### Language-learning mode

Explore an optional mode in which pads play pronunciations in several languages and a display shows a simple matching image. This is not part of the current headless application and would require display hardware/UI, content and language-pack formats, licensing rules, and a mode-selection design.
