# Implementation history

## Initial standalone workstation

The project began with MPD218 USB MIDI, JACK, FluidSynth, a custom C microphone looper, and systemd startup. The first capture device was a Samson GoMic; the looper recorded mono and duplicated playback to both outputs.

## WM8960 migration

The GoMic was replaced by the Waveshare WM8960 Audio HAT for two MEMS microphones, stereo capture/playback, headphones and direct speaker outputs. Initially ALSA showed no capture device. Investigation established that the Pi was a 3B Rev 1.2, the actual kernel was `4.19.66-v7+`, matching headers existed, DKMS state `added` was incomplete, and the vendor `rpi-4.9.y` branch was needed for the old kernel. After a successful build, ALSA exposed `wm8960soundcard`.

## JACK boot failure

After reboot all units appeared active while `jack_lsp` could not connect. The journal showed `Driver is not running` and client synchronisation errors. Starting the exact JACK command manually succeeded, isolating service environment and ordering. Fixes included `JACK_NO_AUDIO_RESERVATION=1`, correct service user/command, proper ordering and codec readiness. The current project waits for the ALSA card rather than using a blind delay.

## Stereo four-loop prototype

The looper grew to four stereo slots on notes 52–55, stopping a playing loop with its pad, and forwarding all other MIDI to FluidSynth. FluidSynth's ALSA client included a changing PID; prefix/numeric discovery replaced literal-name shell routing.

## Refactor for quantization and AEC

The prototype shared `volatile` state between MIDI and JACK threads. The refactor introduces C11 lock-free queues, exclusive real-time-thread ownership, an integer sample-accurate transport, explicit loop state machines, FluidSynth audio routed through the Music Box mixer, a dependency-free stereo NLMS echo canceller, in-process JACK/ALSA connection repair, modular files and hardware-independent tests.
