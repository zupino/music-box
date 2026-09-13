# Teensy port implementation status

This file is intentionally strict: a box is checked only when implementation exists; hardware validation is separate.

## Build targets
- [x] Raspberry Pi source/build remains intact (`make` / `make rpi`).
- [x] Top-level Make targets for Teensy 4.0 and 4.1 (`make teensy40`, `make teensy41`).
- [x] No PlatformIO dependency.
- [x] Teensy build uses PJRC's Teensy board package through `arduino-cli`; the package supplies the ARM GCC (`arm-none-eabi-gcc/g++`) toolchain, Teensy core, linker definitions and bundled Audio/USBHost/SD libraries.
- [x] `make teensy-setup` installs the PJRC board package plus Adafruit GFX/SSD1306 libraries.
- [x] Teensy 4.0 uses Audio Adapter/external SPI SD; no PSRAM dependency.
- [x] Teensy 4.1 may use its built-in SD interface once the storage adapter selects it at compile time.

`arduino-cli` is intentionally an orchestration dependency, not an IDE and not the compiler. Maintaining the complete Teensy core build recipe directly in this repository would duplicate PJRC's board-package flags, startup objects, linker scripts and library discovery. Make is the public build interface for both platforms.

## Implemented control/platform foundation
- [x] MPD218 USB-host MIDI callbacks and original 16-pad note map.
- [x] Five modes and K2/K4/K5/K6/K1/K3 CC routing.
- [x] SGTL5000 Audio Adapter bring-up and stereo line input/output graph.
- [x] SSD1306 I2C bring-up and basic mode/sample/slot display.
- [x] SD session directory, sample naming, atomic-ish temp/rename manifest writes.
- [x] FILES mode single/all deletion state handling.
- [x] Loop-to-sample assignment metadata and trim metadata.

## Still required before claiming feature parity
- [ ] Compile verification for both Teensy Make targets.
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
- [ ] Physical Teensy 4.0 + Audio Adapter + MPD218 + SD + OLED validation.
- [ ] Audio underrun, SD contention, AEC and soak testing.

The current firmware is therefore a bring-up/control skeleton, not a feature-complete Teensy port. Do not merge it as parity-complete until the unchecked software items are implemented and the hardware items are tested.
