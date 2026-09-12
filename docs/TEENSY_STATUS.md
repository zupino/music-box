# Teensy port implementation status

This file is intentionally strict: a box is checked only when implementation exists; hardware validation is separate.

## Build targets
- [x] Raspberry Pi source/build remains intact.
- [x] PlatformIO profiles for Teensy 4.0 and 4.1.
- [x] Teensy 4.0 uses Audio Adapter/external SPI SD; no PSRAM dependency.
- [x] Teensy 4.1 profile may use BUILTIN_SDCARD.

## Implemented control/platform foundation
- [x] MPD218 USB-host MIDI callbacks and original 16-pad note map.
- [x] Five modes and K2/K4/K5/K6/K1/K3 CC routing.
- [x] SGTL5000 Audio Adapter bring-up and stereo line input/output graph.
- [x] SSD1306 I2C bring-up and basic mode/sample/slot display.
- [x] SD session directory, sample naming, atomic-ish temp/rename manifest writes.
- [x] FILES mode single/all deletion state handling.
- [x] Loop-to-sample assignment metadata and trim metadata.

## Still required before claiming feature parity
- [ ] Real SD-backed stereo PCM record writer using AudioRecordQueue/write-behind FIFO.
- [ ] Polyphonic SD sample voice pool with bounded read-ahead and trim support.
- [ ] Sample-accurate loop/bar quantization wired to audio blocks.
- [ ] Sampler pre-roll equal to pad hold duration.
- [ ] Monitor-mix recording path.
- [ ] Melody variable-rate/resampling playback using selected SD sample.
- [ ] Embedded pad instrument / offline SoundFont conversion and runtime wavetable backend.
- [ ] Teensy noise gate integration.
- [ ] Teensy NLMS AEC integration using exact speaker mix as reference.
- [ ] Full OLED parity (beat rail, transient states, volume overlay).
- [ ] Session recovery parity including interrupted-write cases.
- [ ] Compile verification under PlatformIO for both board profiles.
- [ ] Physical Teensy 4.0 + Audio Adapter + MPD218 + SD + OLED validation.
- [ ] Audio underrun, SD contention, AEC and soak testing.

The current firmware is therefore a bring-up/control skeleton, not a feature-complete Teensy port. Do not merge it as parity-complete until the unchecked software items are implemented and the hardware items are tested.
