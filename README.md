# Music Box

Music Box turns a Raspberry Pi, Waveshare WM8960 Audio HAT and Akai MPD218 into a headless stereo synthesizer and four-slot live looper.

The project deliberately stays small: one C11 process for audio/loop state/MIDI routing, JACK for low-latency audio, ALSA sequencer MIDI, FluidSynth as a separate service, and no bundled third-party source or SoundFonts.

## Current features

- stereo recording from both WM8960 microphones;
- four independent loop slots on MPD218 notes 52–55;
- hold to record, release to finish, press a playing loop to stop it;
- non-loop MIDI forwarded to FluidSynth;
- sample-accurate shared beat/bar transport;
- beat- or bar-quantized record start, finish and stop;
- dependency-free stereo NLMS acoustic echo reduction;
- automatic JACK and ALSA MIDI connection repair;
- ordered systemd startup;
- 128x32 I2C OLED beat and recording-status display;
- current Raspberry Pi OS and legacy Raspbian/kernel notes.

The AEC is intentionally lightweight for Raspberry Pi 3. It is acoustic echo reduction rather than telephony-grade all-room cancellation; delay normally needs tuning for the enclosure.

## Supported targets

Primary: current Raspberry Pi OS Lite (Trixie or Bookworm), 32- or 64-bit, Raspberry Pi 3B or newer, WM8960 HAT and MPD218.

Also intended: Ubuntu Server LTS when the WM8960 driver is functioning, and the original Pi 3B Raspbian installation using kernel `4.19.66-v7+`.

## Signal flow

```text
MPD218 -> Music Box MIDI router
            |-- notes 52-55 -> loop commands
            `-- other MIDI -> FluidSynth

FluidSynth JACK L/R -----------+
Existing loop playback L/R ----+--> Music Box mixer --> WM8960 speakers
                               |          |
                               |          `--> exact AEC reference
WM8960 microphones --> AEC ----+--> quantized loop recorder
```

FluidSynth is routed through Music Box rather than directly to the physical playback ports so AEC sees the exact loudspeaker signal.

## Install

First make sure `aplay -l` and `arecord -l` show `wm8960soundcard`. Then:

```bash
git clone https://github.com/zupino/music-box.git
cd music-box
sudo bash ./scripts/install.sh --migrate-legacy
sudo reboot
```

Custom SoundFont:

```bash
sudo bash ./scripts/install.sh --migrate-legacy --soundfont /path/to/file.sf2
```

If the HAT is absent, the optional helper downloads and runs Waveshare's external GPL driver installer:

```bash
sudo bash ./scripts/install-wm8960-driver.sh --accept-third-party
sudo reboot
```

No Waveshare source is contained here. Read `THIRD_PARTY.md` first.

## Controls

Default loop pads are notes 52, 53, 54 and 55. With `quantize=bar`:

1. Hold a stopped pad: recording is armed.
2. Recording starts at the next common bar boundary.
3. Release: finish is armed.
4. Recording closes at the next bar and begins looping.
5. Press a playing pad: stop is armed for the next bar.

A quick press/release before recording starts creates one complete quantization interval. This often leaves the physical pad tap before the recorded material; dedicated tap-noise filtering remains deferred.

## Configuration

Installed at `/etc/music-box/music-box.conf`:

```ini
bpm=120.0
beats_per_bar=4
quantize=bar

aec_enabled=true
aec_taps=128
aec_delay_frames=512
```

Restart after edits:

```bash
sudo systemctl restart music-box.service
```

See `docs/aec-tuning.md` for echo calibration.

### Beat display

An SSD1306-compatible 128x32 I2C OLED at address `0x3c` shows the tempo,
recording state, and the current beat in a four-beat bar. The default device is
`/dev/i2c-1`; the display is optional, and an initialization failure does not
stop the audio engine. Configure it with:

```ini
display_enabled=true
display_i2c_device=/dev/i2c-1
display_i2c_address=0x3c
```

The display shares the I2C bus with the WM8960 but uses a different address.
Display writes happen outside the JACK callback so I2C traffic cannot block the
real-time audio thread.

## Services and diagnostics

```text
music-box-jack.service
        -> music-box-fluidsynth.service
        -> music-box.service
```

The application replaces the old `mpd-connect` polling service and repairs connections itself.

```bash
music-box-check
jack_lsp -c
aconnect -l
journalctl -u music-box -b --no-pager
```

`arecord: Device or resource busy` while JACK is healthy is expected because JACK owns the WM8960 exclusively.

## Build and tests

```bash
make
make test
```

The unit suite covers transport boundaries, lock-free queues, quantized loop states and synthetic AEC convergence.

## Documentation

- `docs/installation.md` — Raspberry Pi OS/Ubuntu installation and migration
- `docs/architecture.md` — real-time ownership, signal graph and DSP structure
- `docs/implementation-history.md` — complete intervention summary
- `docs/legacy-raspbian.md` — Stretch/4.19/DKMS/JACK special cases
- `docs/aec-tuning.md` — echo cancellation tuning
- `docs/troubleshooting.md` — boot, codec, MIDI and JACK diagnosis
- `docs/roadmap.md` — deferred pad-tap handling and future features

## License

Project-owned code, scripts and documentation are MIT licensed; see `LICENSE`. External drivers, FluidSynth, JACK, ALSA and SoundFonts retain their own licenses and are not redistributed here. See `THIRD_PARTY.md` and `LICENSES/README.md`.
