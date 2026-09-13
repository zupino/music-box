# Teensy port implementation status

This file is intentionally strict: a box is checked only when implementation exists; hardware validation is separate.

## Build targets
- [x] Raspberry Pi source/build remains intact (`make` / `make rpi`).
- [x] Top-level Make targets for Teensy 4.0 and 4.1 (`make teensy40`, `make teensy41`).
- [x] No PlatformIO dependency.
- [x] Teensy build uses PJRC's Teensy board package through `arduino-cli`; the package supplies ARM GCC (`arm-none-eabi-gcc/g++`), Teensy core, linker definitions and bundled Audio/USBHost/SD libraries.
- [x] `make teensy-setup` installs the PJRC board package plus Adafruit GFX/SSD1306 libraries.
- [x] macOS setup/flash workflow documented; `make teensy-check` validates the local CLI/toolchain environment.
- [x] macOS GitHub Actions compile workflow added for Teensy 4.0 and 4.1.
- [x] Teensy 4.0 uses Audio Adapter/external SPI SD; no PSRAM dependency.
- [x] Teensy 4.1 selects built-in SDIO storage at compile time.
- [x] Teensy 4.0 compiles successfully on macOS with PJRC Teensy core 1.62.0 via `make teensy40`.
- [x] Teensy 4.1 compiles successfully on macOS with PJRC Teensy core 1.62.0 via `make teensy41`.

`arduino-cli` is intentionally an orchestration dependency, not an IDE and not the compiler. Make is the public build interface for both platforms.

## Implemented control/platform/audio foundation
- [x] MPD218 USB-host MIDI callbacks and original 16-pad note map.
- [x] Five modes and K2/K4/K5/K6/K1/K3 CC routing.
- [x] SGTL5000 Audio Adapter bring-up and stereo line I/O graph.
- [x] SSD1306 I2C bring-up and Pi-style 4x4 state grid, pending/record states, beat rail and volume overlay.
- [x] SD session directory and interleaved 16-bit stereo PCM sample files.
- [x] Bounded 16-voice SD sample streamer; no SD reads inside `AudioStream::update()`.
- [x] Trim-aware sample playback.
- [x] Variable-rate sample interpolation used by Melody mode.
- [x] Stereo `AudioRecordQueue` recorder with write-behind service loop.
- [x] ~400 ms stereo microphone pre-roll.
- [x] Monitor-mix recording path using final speaker mix queues.
- [x] 10-second recording limit matching current project default.
- [x] FILES mode single/all deletion state handling.
- [x] Loop-to-sample assignment metadata and trim metadata.
- [x] Audio-frame scheduled bar-quantized loop start/stop; SD data is pre-buffered and the actual transition happens inside the audio update timeline rather than `millis()`/`loop()` timing.
- [x] Crash-tolerant temp/rename manifest handling and stale sample-temp cleanup.
- [x] Manifest validation against actual sample-file length on restore.
- [x] Fixed-memory stereo NLMS AEC using exact final speaker mix as reference.
- [x] Linked stereo noise gate using current Raspberry Pi default parameters.
- [x] Embedded four-voice bass instrument backend for PAD mode; no runtime FluidSynth dependency.
- [x] Playback underrun and recorder dropped-block counters are exposed on serial every two seconds and on the OLED when nonzero; Teensy Audio memory current/max usage is also reported on serial.
- [x] PAD synth parity decision: the embedded bass voice is accepted for functional parity (polyphonic note-on/off and velocity response). Exact Raspberry Pi SoundFont timbre is a post-hardware audio-quality enhancement, not a blocker for first Teensy validation.
- [x] PJRC toolchain compile issues found during CI were fixed; successful builds produced no firmware compiler warnings in the build logs.

## Software handoff status

All planned software milestones required before first hardware validation are complete. The branch is ready for local macOS build, flash and hardware testing.

Reference CI memory usage at the successful macOS build:
- Teensy 4.0: FLASH code 139104 bytes, RAM1 variables 187712 bytes, RAM1 local-variable headroom 172736 bytes, RAM2 variables 60256 bytes.
- Teensy 4.1: FLASH code 140192 bytes, RAM1 variables 187808 bytes, RAM1 local-variable headroom 172640 bytes, RAM2 variables 60256 bytes.

## Hardware-only validation after software handoff
- [ ] Physical Teensy 4.0 + Audio Adapter + MPD218 + SD + OLED validation.
- [ ] Physical Teensy 4.1 validation where available.
- [ ] Stereo microphone preamp/line-input validation.
- [ ] PAM8403 line-output validation.
- [ ] Audio underrun and SD contention stress test.
- [ ] AEC convergence/tuning test against the final enclosure/speaker/microphone geometry.
- [ ] Long-duration soak and power-cycle/session-recovery test.

Do not interpret successful compilation as hardware validation. The remaining unchecked items require real hardware.
