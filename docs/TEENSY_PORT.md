# Teensy dual-target port

This branch develops Teensy support alongside the existing Raspberry Pi target. The design rule is one project and one musical/DSP core, with platform adapters for Linux/Raspberry Pi and Teensy. Raspberry Pi behavior remains the reference implementation while Teensy reaches feature parity.

## Supported boards

- Raspberry Pi: existing JACK/ALSA/FluidSynth target.
- Teensy 4.0: first-class compatibility target. Because it has no onboard SD slot and substantially less practical storage expansion than 4.1, removable storage must be supplied by the Audio Adapter SD socket or an external SPI SD module. The core must not require PSRAM.
- Teensy 4.1: same firmware architecture, with optional native SDIO and optional PSRAM acceleration. These are optimizations, not requirements.

Use compile-time capability flags rather than separate application forks. No feature may depend on 4.1-only hardware unless there is a 4.0 fallback.

## Target architecture

Shared core should own transport, quantization, loop/sample state, melody/resampling, AEC, noise gate, commands/events and mode behavior. Platform code should own clocks, audio I/O, USB/ALSA MIDI, display bus, codec control, filesystem/persistence and host lifecycle.

The Teensy implementation should use deterministic audio callbacks/update blocks and a non-real-time service loop. USB MIDI, display refresh and SD writes must never perform unbounded work in the audio path.

The current Raspberry Pi sampler stores long stereo float buffers in RAM. Do not reproduce that storage model on Teensy. Introduce a sample-storage abstraction and use SD-backed 16-bit PCM with bounded read-ahead/write-behind buffers. Teensy 4.1 PSRAM may enlarge caches, but Teensy 4.0 must work without it.

## Teensy Audio Adapter notes

The PJRC Teensy Audio Adapter (SGTL5000) is the preferred first bring-up codec because Teensy Audio Library support is mature. Treat its line-level stereo input as the preferred route when two microphone channels are required: microphones need suitable low-noise preamplifiers and biasing before line input. The board's single mic input is useful for a simple mono prototype but does not replace the current dual-microphone Raspberry Pi path.

Use line output as the feed to an external speaker power amplifier. Do not connect a bridge-tied speaker amplifier to the headphone output without verifying grounding/topology. Keep codec analog wiring short, keep it away from USB/SD/display clock lines, and use a clean common ground strategy.

Teensy 4.0 can use the Audio Adapter's microSD socket for sample/session storage. Teensy 4.1 may use its onboard SDIO slot for better storage throughput while retaining the same storage interface in firmware.

## PAM8403 speaker stage

PAM8403 modules are appropriate for the small speaker output stage. Feed their L/R inputs from codec line output through the module's normal input coupling arrangement. PAM8403 outputs are BTL/class-D: neither speaker terminal is ground. Never join L- and R- speaker negatives, and never connect either output terminal to system ground.

A nominal 5 V supply is appropriate for common PAM8403 modules. The advertised 3 W/channel figure is near the high end of its operating conditions and depends on load, supply and distortion; design thermal/acoustic expectations conservatively. Use speakers compatible with the particular module (commonly 4–8 ohm), verify the module specification, and avoid undersized speakers.

Place local bulk plus ceramic supply decoupling close to the amplifier module. Route high-current/class-D speaker wiring away from microphone preamps, codec inputs, I2S and OLED wiring. Twisted speaker pairs and short runs help reduce radiated switching noise. A star-like power/ground distribution from the supply is preferable to routing codec/mic return current through the amplifier return path.

For best noise performance, consider separate filtered analog/codec and power-amplifier supply branches from a good 5 V source. Validate noise both while idle and during SD writes, OLED updates, USB activity and maximum speaker output.

## Product/case audio-quality guidance

Prefer a rigid enclosure with separate acoustic volumes for left and right speakers; do not allow rear speaker pressure to directly reach the microphones. Mechanically isolate microphones from speaker panels and the Teensy/PCB. Put microphones near acoustically useful openings with wind/pop protection and keep their analog preamps close to the capsules.

Keep the PAM8403 and speaker wiring physically distant from the codec and microphone front end. Keep digital/display/USB wiring on the digital side of the enclosure. Provide strain relief for USB and power connectors. If using a metal enclosure, plan grounding intentionally rather than allowing multiple accidental chassis-ground paths.

Speaker choice and enclosure volume will affect perceived quality more than small DSP refinements. Prefer matched full-range drivers with published response/Thiele-Small information when possible, a sealed or deliberately designed vented chamber, a rigid baffle, gasketed driver mounting, and damping material where appropriate. Prototype the acoustic chamber before freezing the electronics PCB.

## Port milestones

1. Platform boundary and build targets: keep Raspberry Pi build working; add Teensy 4.0/4.1 board profiles and capability flags.
2. Hardware bring-up: Teensy Audio Adapter I2S, stereo line I/O, MPD218 USB host, SSD1306 OLED, SD storage, PAM8403 line-output test.
3. Shared transport and loop engine: one loop first, then 16 slots, beat/bar quantization and assignment behavior.
4. Storage abstraction: SD-backed PCM recorder/player with bounded FIFO/cache, 4.0 SPI-SD fallback and 4.1 SDIO optimization.
5. Sampler parity: press/hold recording, pre-roll, source selection, trimming, one-shots, sample assignment and FILES deletion behavior.
6. Melody parity: pitched playback through the storage-reader abstraction.
7. Session persistence: sample files, trim/assignment metadata, restore and crash-tolerant update strategy suitable for SD/FAT.
8. UI/control parity: OLED state/beat rail, K2 mode, K3 monitor, K4 sample, K5/K6 trim and hardware/software volume abstraction.
9. DSP parity: stereo microphone path, noise gate and NLMS AEC; benchmark CPU and tune taps/delay without compromising audio deadlines.
10. Synth parity and integration: replace runtime FluidSynth dependency with an embedded wavetable/sample-instrument backend, with an offline SoundFont conversion workflow; run full feature and soak tests on Teensy hardware.

## Definition of Teensy feature-ready

The branch is considered hardware-testable for parity when a Teensy 4.0 plus Audio Adapter/external SD (and a Teensy 4.1 configuration where available) can build firmware that exposes all current modes, accepts the MPD218 mappings, records and plays all 16 sampler/loop slots from persistent storage, restores sessions, supports melody playback, provides a pad instrument backend, updates the OLED, controls output level, and runs the microphone cleanup/AEC path without audio underruns in a sustained stress test.

Hardware-only validation items must be explicitly marked as such; they must not be reported as verified merely because firmware compiles.
