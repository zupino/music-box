# Architecture

## Design goals

The implementation prioritises understandable C code, Raspberry Pi 3B compatibility, sample-accurate timing, no allocation/logging/locks in the JACK callback, and no vendored DSP or driver dependencies.

The diagrams in this document use PlantUML. GitHub displays the source blocks directly; they can be rendered with a PlantUML-capable editor, documentation generator, or the PlantUML CLI.

## System context

Music Box combines four external sources:

- Akai MPD218 MIDI events;
- WM8960 left microphone capture;
- WM8960 right microphone capture;
- FluidSynth stereo audio.

It produces three principal sinks:

- WM8960 left speaker output;
- WM8960 right speaker output;
- four stereo loop buffers in memory.

The final speaker-bound signal is also reused internally as the reference for acoustic echo reduction.

## Software architecture

```plantuml
@startuml
' SPDX-License-Identifier: MIT
title Music Box software architecture

skinparam componentStyle rectangle
skinparam packageStyle rectangle
skinparam shadowing false
skinparam linetype ortho

package "Physical hardware" {
  component "Akai MPD218\nMIDI controller" as MPD
  component "Waveshare WM8960\nAudio HAT" as HAT
  component "Microphones L/R" as MICS
  component "Speakers L/R" as SPEAKERS

  MICS -[hidden]-> SPEAKERS
  MICS --> HAT : analogue capture
  HAT --> SPEAKERS : analogue playback
}

package "Linux audio and services" {
  component "ALSA Sequencer" as ALSAMIDI
  component "JACK server" as JACK
  component "FluidSynth service" as FLUID
  component "systemd units" as SYSTEMD
}

package "music-box process" {
  component "main.c\nstartup, shutdown, route repair" as MAIN
  component "config.c\nkey=value configuration" as CONFIG
  component "midi.c\nclassification and forwarding" as MIDI
  component "rt_queue.c\nSPSC command/status queues" as QUEUE
  component "audio_engine.c\nJACK callback and mixer" as ENGINE
  component "transport.c\nsample-accurate beat/bar clock" as TRANSPORT
  component "loop_bank.c\n4 stereo loop state machines" as LOOPS
  component "echo_cancel.c\nstereo MIMO NLMS AEC" as AEC
}

SYSTEMD --> JACK : starts first
SYSTEMD --> FLUID : starts after JACK
SYSTEMD --> MAIN : starts music-box

MPD --> ALSAMIDI : USB MIDI
ALSAMIDI --> MIDI : MIDI events
MIDI --> QUEUE : loop commands\nnotes 52-55
MIDI --> FLUID : non-loop MIDI

FLUID --> JACK : stereo synth audio
HAT --> JACK : stereo capture device
JACK --> ENGINE : real-time callback buffers
ENGINE --> JACK : final stereo playback
JACK --> HAT : stereo playback device

MAIN --> CONFIG
MAIN --> MIDI
MAIN --> ENGINE
MAIN --> JACK : repair JACK routes
MAIN --> ALSAMIDI : repair ALSA routes

QUEUE --> ENGINE : commands
ENGINE --> QUEUE : status events
ENGINE --> TRANSPORT
ENGINE --> LOOPS
ENGINE --> AEC

note right of ENGINE
Only the JACK real-time thread mutates:
- transport state
- loop states
- loop audio buffers
- AEC adaptive coefficients
end note
@enduml
```

## Module responsibilities

| Module | Responsibility |
|---|---|
| `main.c` | Startup, shutdown, event logging and route repair |
| `config.c` | Small `key=value` configuration parser |
| `midi.c` | ALSA sequencer ports, MPD discovery and MIDI forwarding |
| `audio_engine.c` | JACK callback, signal routing and final mixer |
| `transport.c` | Drift-free sample/beat/bar clock |
| `loop_bank.c` | Four stereo buffers and loop state machines |
| `echo_cancel.c` | Built-in two-reference/two-microphone NLMS AEC |
| `rt_queue.c` | Lock-free SPSC command and status queues |
| `volume.c` | ALSA simple-mixer hardware output volume driven by a MIDI knob |
| `display.c` | SSD1306 beat, recording-status and volume screens |

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

## Audio sources, internal processors and sinks

| Type | Name | Role |
|---|---|---|
| Source | `system:capture_1` | WM8960 left microphone |
| Source | `system:capture_2` | WM8960 right microphone |
| Source | `fluidsynth:l_00` | FluidSynth left channel |
| Source | `fluidsynth:r_00` | FluidSynth right channel |
| Source | Loop slots 1-4 | Previously recorded stereo playback |
| Processor | Echo canceller | Removes an estimate of speaker bleed from microphone capture |
| Processor | Quantized recorder | Writes cleaned microphone samples into an armed loop slot |
| Processor | Loop bank | Plays active loop slots phase-locked to their own stored length |
| Processor | Stereo mixer | Sums synth and loop playback |
| Processor | Output gain/clip | Produces the exact samples sent to the codec |
| Internal sink/source | AEC reference delay | Stores speaker-bound samples for later echo estimation |
| Sink | Loop buffers | Four stereo recordings held in memory |
| Sink | `system:playback_1` | WM8960 left playback channel |
| Sink | `system:playback_2` | WM8960 right playback channel |

## Sound path with sources and sinks

```plantuml
@startuml
' SPDX-License-Identifier: MIT
title Music Box sound path: sources, processors and sinks

skinparam componentStyle rectangle
skinparam shadowing false
skinparam linetype ortho

rectangle "AUDIO SOURCES" as Sources {
  component "WM8960 microphone L\nsystem:capture_1" as MICL <<source>>
  component "WM8960 microphone R\nsystem:capture_2" as MICR <<source>>
  component "FluidSynth L/R" as SYNTH <<source>>
  component "Loop playback\nslots 1-4" as LOOPPLAY <<source>>
}

rectangle "MUSIC BOX REAL-TIME PIPELINE" as Pipeline {
  component "Stereo echo reduction\n2 speaker refs x 2 microphones" as AEC
  component "Quantized stereo recorder" as REC
  component "Stereo loop buffers\nslots 1-4" as LOOPBUF
  component "Stereo mixer" as MIX
  component "Output gain and clipping" as LIMIT
  queue "AEC reference delay\nexact speaker-bound L/R" as REF
  component "Shared transport\nbeat/bar boundaries" as CLOCK
}

rectangle "AUDIO SINKS" as Sinks {
  component "WM8960 speaker L\nsystem:playback_1" as SPKL <<sink>>
  component "WM8960 speaker R\nsystem:playback_2" as SPKR <<sink>>
}

MICL --> AEC : captured L
MICR --> AEC : captured R
REF --> AEC : delayed playback L/R
AEC --> REC : cleaned microphone L/R
CLOCK --> REC : start / finish boundary
REC --> LOOPBUF : write stereo samples
LOOPBUF --> LOOPPLAY : stored audio
LOOPPLAY --> MIX : loop L/R
SYNTH --> MIX : synth L/R
MIX --> LIMIT : summed L/R
LIMIT --> REF : exact output reference
LIMIT --> SPKL : final L
LIMIT --> SPKR : final R

note bottom of REF
The AEC reference is taken after mixing,
output gain and clipping so it matches
the digital signal delivered to JACK.
end note
@enduml
```

## JACK audio graph

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

## MIDI and control path

```plantuml
@startuml
' SPDX-License-Identifier: MIT
title Music Box MIDI and control path

skinparam componentStyle rectangle
skinparam shadowing false
skinparam linetype ortho

component "MPD218" as MPD <<source>>
component "ALSA Sequencer" as ALSA
component "MIDI classifier" as CLASSIFY
component "Loop command queue" as COMMANDS
component "Audio-thread loop state machines" as STATES
component "FluidSynth MIDI input" as FLUID <<sink>>
component "ALSA simple mixer\nSpeaker / Headphone" as MIXER <<sink>>

MPD --> ALSA : notes, note-off, CC,
ALSA --> CLASSIFY
CLASSIFY --> COMMANDS : notes 52-55\nrecord / finish / stop
COMMANDS --> STATES : consume in JACK callback
CLASSIFY --> MIXER : volume CC\nhardware playback volume
CLASSIFY --> FLUID : all other MIDI events

note right of CLASSIFY
Loop-control notes and the volume
controller are consumed and are not
forwarded to FluidSynth. The mixer is
written from the MIDI thread, never
from the JACK callback.
end note
@enduml
```

## Transport

Tempo is stored in milli-BPM. Every audio frame adds `tempo_millibpm` to an integer accumulator. A beat occurs when it crosses `sample_rate * 60 * 1000`. At 48 kHz and 120 BPM this gives exactly 24,000 frames per beat and 96,000 per 4/4 bar. Fractional BPM values use rational accumulation rather than repeated floating-point rounding.

Frame zero is a beat/bar boundary and all slots share this transport.

## Quantized recording sequence

The MIDI thread only requests changes. The audio callback applies them at exact frame boundaries supplied by the shared transport.

```plantuml
@startuml
' SPDX-License-Identifier: MIT
title Quantized loop recording sequence

autonumber
actor Performer
participant "MPD218" as MPD
participant "ALSA MIDI thread" as MIDI
queue "Command queue" as Q
participant "JACK audio callback" as AUDIO
participant "Shared transport" as CLOCK
participant "Loop slot" as LOOP
participant "Stereo loop buffers" as BUFFER

Performer -> MPD : press and hold loop pad
MPD -> MIDI : NOTE_ON (52..55)
MIDI -> Q : ARM_RECORD(slot)
Q -> AUDIO : command consumed
AUDIO -> LOOP : state = ARMED_RECORD

loop each JACK frame/block
  AUDIO -> CLOCK : advance frame counter
end

CLOCK --> AUDIO : next quantization boundary
AUDIO -> LOOP : state = RECORDING
AUDIO -> BUFFER : clear old length and start writes

loop while recording
  AUDIO -> BUFFER : append cleaned mic L/R
end

Performer -> MPD : release pad
MPD -> MIDI : NOTE_OFF
MIDI -> Q : ARM_FINISH(slot)
Q -> AUDIO : command consumed
AUDIO -> LOOP : state = ARMED_FINISH

CLOCK --> AUDIO : next quantization boundary
AUDIO -> LOOP : close exact loop length
AUDIO -> LOOP : state = PLAYING
AUDIO -> BUFFER : playback starts from frame 0
@enduml
```

A release received while still `ARMED_RECORD` is remembered. Recording begins at the next boundary and ends at the following boundary, producing one complete beat or bar. This also tends to keep the physical pad-tap transient before the recorded interval.

## Loop-slot state machine

```plantuml
@startuml
' SPDX-License-Identifier: MIT
title Per-slot quantized looper state machine

skinparam shadowing false

[*] --> EMPTY

EMPTY --> ARMED_RECORD : pad press\nquantization enabled
EMPTY --> RECORDING : pad press\nquantization off
STOPPED --> ARMED_RECORD : pad press\nquantization enabled
STOPPED --> RECORDING : pad press\nquantization off

ARMED_RECORD --> RECORDING : next beat/bar boundary
ARMED_RECORD --> ARMED_FINISH : pad released before start\nremember minimum interval

RECORDING --> ARMED_FINISH : pad release\nquantization enabled
RECORDING --> PLAYING : pad release\nquantization off and frames > 0
RECORDING --> PLAYING : maximum loop length reached

ARMED_FINISH --> PLAYING : next beat/bar boundary\nclose recording
ARMED_FINISH --> RECORDING : start boundary when release\narrived before recording began

PLAYING --> ARMED_STOP : pad press\nquantization enabled
PLAYING --> STOPPED : pad press\nquantization off
ARMED_STOP --> STOPPED : next beat/bar boundary

note right of PLAYING
Recorded samples remain in memory when stopped.
A later recording replaces the slot contents.
end note
@enduml
```

## Built-in acoustic echo reduction

The implementation is a stereo MIMO normalized least-mean-square filter with four adaptive paths:

- left speaker reference to left microphone;
- right speaker reference to left microphone;
- left speaker reference to right microphone;
- right speaker reference to right microphone.

Coefficients adapt when there is enough reference energy and a simple double-talk guard does not detect disproportionate microphone energy.

It remains deliberately small and dependency-free. Limitations include manual delay tuning, linear-only modelling, simple double-talk handling and limited reverberation coverage. It is intended to reduce close-speaker bleed in the specific Music Box enclosure rather than provide telephony-grade room echo cancellation.

## Route maintenance

The application periodically repairs WM8960 capture/playback, FluidSynth JACK and MPD218/FluidSynth ALSA MIDI connections. This replaces the original shell polling service while preserving recovery after client restarts.

## Platform boundaries

The process itself depends only on libc, pthreads, JACK and ALSA. FluidSynth and the WM8960 kernel driver remain external components and keep their original licenses.

The same application architecture is used on current Raspberry Pi OS and on the original Raspberry Pi 3B environment. Differences involving old kernels, `/boot/config.txt`, legacy DKMS branches, JACK shared-memory cleanup and older FluidSynth behaviour are documented in `legacy-raspbian.md` rather than embedded in the real-time code.
