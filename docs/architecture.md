# Architecture

## Design goals

The implementation prioritises understandable C code, Raspberry Pi 3B compatibility, sample-accurate timing, no allocation/logging/locks in the JACK callback, and no vendored DSP or driver dependencies.

## Modules

| Module | Responsibility |
|---|---|
| `main.c` | startup, shutdown, event logging and route repair |
| `config.c` | small `key=value` configuration parser |
| `midi.c` | ALSA sequencer ports, MPD discovery and MIDI forwarding |
| `audio_engine.c` | JACK callback, signal routing and final mixer |
| `transport.c` | drift-free sample/beat/bar clock |
| `loop_bank.c` | four stereo buffers and loop state machines |
| `echo_cancel.c` | built-in two-reference/two-microphone NLMS AEC |
| `rt_queue.c` | lock-free SPSC command and status queues |

## Thread ownership

```text
ALSA MIDI thread                    JACK real-time thread
----------------                    ---------------------
read MIDI event                     drain command queue
classify loop note       queue      update transport
forward other MIDI     -------->    apply loop transitions
                                    mix synth and loops
                                    run stereo AEC
                         <--------   publish status event
print/log thread         queue      record clean microphone
```

The JACK callback is the only code allowed to mutate transport, loop buffers or loop states. Both queues are fixed-size single-producer/single-consumer rings using C11 atomics.

## Transport

Tempo is stored in milli-BPM. Every audio frame adds `tempo_millibpm` to an integer accumulator. A beat occurs when it crosses `sample_rate * 60 * 1000`. At 48 kHz and 120 BPM this gives exactly 24,000 frames per beat and 96,000 per 4/4 bar. Fractional BPM values use rational accumulation rather than repeated floating-point rounding.

Frame zero is a beat/bar boundary and all slots share this transport.

## Loop state machine

```text
EMPTY / STOPPED -> ARMED_RECORD -> RECORDING -> ARMED_FINISH -> PLAYING
PLAYING -> ARMED_STOP -> STOPPED
```

With quantization disabled, transitions happen immediately. A release received while still `ARMED_RECORD` is remembered: recording starts at the next boundary and ends at the following boundary, producing one complete beat or bar.

## Audio graph and AEC reference

FluidSynth is routed through Music Box rather than directly to playback:

```text
system:capture_1  -> music_box:mic_l
system:capture_2  -> music_box:mic_r
fluidsynth:l_00   -> music_box:synth_l
fluidsynth:r_00   -> music_box:synth_r
music_box:out_l   -> system:playback_1
music_box:out_r   -> system:playback_2
```

The final gain-adjusted/clipped speaker samples are the AEC reference. The AEC uses a configurable delay line because the matching acoustic echo appears in a later capture frame.

## Built-in AEC

The implementation is a stereo MIMO normalized least-mean-square filter with four adaptive paths: each speaker to each microphone. Coefficients adapt when there is enough reference energy and a simple double-talk guard does not detect disproportionate microphone energy.

It remains deliberately small and dependency-free. Limitations include manual delay tuning, linear-only modelling, simple double-talk handling and limited reverberation coverage.

## Route maintenance

The application periodically repairs WM8960 capture/playback, FluidSynth JACK and MPD218/FluidSynth ALSA MIDI connections. This replaces the original shell polling service while preserving recovery after client restarts.
