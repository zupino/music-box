# Roadmap

## Implemented in this refactor

- shared sample-accurate transport;
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

### Loop editing

Possible later additions: overdub/undo, mute/resume without replacement, fixed 1/2/4/8-bar lengths, persistent loop save/load and per-loop gain.
