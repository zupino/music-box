# Teensy dual-target port

This branch develops Teensy support alongside the existing Raspberry Pi target. The design rule is one project with target-specific platform code and a shared musical/DSP behavior model. Raspberry Pi behavior remains the reference while Teensy reaches feature parity.

## Supported boards

- Raspberry Pi: existing JACK/ALSA/FluidSynth target.
- Teensy 4.0: first-class compatibility target. Removable storage comes from the Audio Adapter SD socket or an external SPI SD module. The firmware must not require PSRAM.
- Teensy 4.1: same firmware architecture, with optional native SDIO and optional PSRAM acceleration. These are optimizations, not requirements.

No feature may depend on 4.1-only hardware unless there is a 4.0 fallback.

## Build model

`make` is the repository's top-level build interface. PlatformIO is not required.

Raspberry Pi uses the native host C compiler plus JACK/ALSA. Teensy uses Arduino CLI only as a package/build orchestrator. PJRC's Teensy board package supplies the actual ARM toolchain (`arm-none-eabi-gcc/g++`), Teensy core, linker/startup files, Audio library, USBHost library, SD support, discovery and upload tooling. This avoids copying PJRC's board-specific compiler/linker recipes into this repository.

### macOS development environment

Install Homebrew if it is not already present, then install Arduino CLI:

```sh
brew update
brew install arduino-cli
```

Verify the host tool:

```sh
arduino-cli version
make teensy-check
```

Install the PJRC package and the two external display libraries used by the firmware:

```sh
make teensy-setup
```

`make teensy-setup` runs the PJRC package index install using:

```text
https://www.pjrc.com/teensy/package_teensy_index.json
```

The downloaded PJRC package contains the Teensy ARM GCC toolchain and board tools; a separate ARM compiler installation is not required.

Build either board from macOS:

```sh
make teensy40
make teensy41
```

Connect the Teensy by USB and inspect its detected port:

```sh
arduino-cli board list
```

Then upload, for example:

```sh
make teensy40-upload PORT=/dev/cu.usbmodemXXXX
```

or:

```sh
make teensy41-upload PORT=/dev/cu.usbmodemXXXX
```

The exact macOS `/dev/cu.*` name is discovered at runtime and should not be hard-coded. A Teensy that cannot be software-rebooted into its bootloader may require pressing the physical Program button once before retrying upload.

The repository also contains a macOS GitHub Actions build that invokes the same `make teensy-setup` and `make teensy40` / `make teensy41` paths. Hardware flashing remains a local operation.

## Target architecture

Shared behavior covers transport/quantization semantics, loop/sample state, melody behavior, AEC/noise-gate parameters, commands/events and mode behavior. Platform code owns clocks, audio I/O, USB/ALSA MIDI, display bus, codec control, filesystem/persistence and host lifecycle.

The Teensy implementation uses Teensy Audio update blocks and a non-real-time service loop. USB MIDI, display refresh and SD operations are kept out of `AudioStream::update()`.

The Raspberry Pi sampler stores long stereo float buffers in RAM. Teensy instead stores interleaved signed 16-bit stereo PCM on SD and uses bounded read-ahead/write-behind buffering. Teensy 4.1 PSRAM may enlarge caches later, but Teensy 4.0 works without it.

## Teensy Audio Adapter notes

The PJRC Teensy Audio Adapter (SGTL5000) is the first bring-up codec. Treat its stereo line input as the preferred route when two microphone channels are required: microphones need suitable low-noise preamplifiers and biasing before line input. The board's single microphone input is only suitable for a mono prototype.

Use line output as the feed to an external speaker power amplifier. Do not connect a bridge-tied speaker amplifier to the headphone output without verifying grounding/topology. Keep codec analog wiring short and away from USB/SD/display clock lines.

Teensy 4.0 uses the Audio Adapter/external SPI SD path. Teensy 4.1 uses its onboard SDIO slot in the current implementation.

## PAM8403 speaker stage

Feed PAM8403 L/R inputs from codec line output through the module's normal input coupling arrangement. PAM8403 outputs are BTL/class-D: neither speaker terminal is ground. Never join L- and R- speaker negatives, and never connect either output terminal to system ground.

Use clean 5 V power, local bulk plus ceramic decoupling, short twisted speaker pairs, and route amplifier/speaker current away from microphone preamps, codec inputs, I2S and OLED wiring.

## Port milestones

1. Platform boundary and build targets: keep Raspberry Pi build working; provide Teensy 4.0/4.1 Make targets and capability handling.
2. Hardware bring-up: Teensy Audio Adapter I2S, stereo line I/O, MPD218 USB host, SSD1306 OLED, SD storage, PAM8403 line-output test.
3. Shared transport and loop engine: 16 slots, beat/bar quantization and assignment behavior.
4. Storage abstraction: SD-backed PCM recorder/player with bounded FIFO/cache, 4.0 SPI-SD fallback and 4.1 SDIO optimization.
5. Sampler parity: press/hold recording, pre-roll, source selection, trimming, one-shots, sample assignment and FILES deletion behavior.
6. Melody parity: pitched playback through the storage-reader abstraction.
7. Session persistence: sample files, trim/assignment metadata, restore and crash-tolerant update strategy suitable for SD/FAT.
8. UI/control parity: OLED state/beat rail, K2 mode, K3 monitor, K4 sample, K5/K6 trim and volume abstraction.
9. DSP parity: stereo microphone path, noise gate and NLMS AEC using the exact final speaker mix as reference; benchmark CPU and tune taps/delay without compromising audio deadlines.
10. Synth parity and integration: embedded pad instrument replacing runtime FluidSynth, plus complete build verification and hardware/soak testing.

## Definition of Teensy feature-ready

The branch is hardware-testable for parity when a Teensy 4.0 plus Audio Adapter/external SD (and a Teensy 4.1 configuration where available) can build firmware that exposes all current modes, accepts MPD218 mappings, records and plays all 16 sampler/loop slots from persistent storage, restores sessions, supports melody playback, provides a pad instrument backend, updates the OLED, controls output level, and runs microphone cleanup/AEC with underrun instrumentation.

Hardware-only validation items must remain explicitly marked unverified until exercised on physical hardware.
