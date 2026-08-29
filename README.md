# Music Box

Music Box turns a Raspberry Pi, Waveshare WM8960 Audio HAT and Akai MPD218 into a headless stereo synthesizer and four-slot live looper.

The project deliberately stays small: one C11 process for audio/loop state/MIDI routing, JACK for low-latency audio, ALSA sequencer MIDI, FluidSynth as a separate service, and no bundled third-party source or SoundFonts.

## Quick install (fresh Raspberry Pi OS Lite)

Raspberry Pi OS Lite 64-bit (Bookworm or Trixie) is a supported, headless target; a desktop environment is neither installed nor required. After flashing the image, boot it, connect networking and log in over SSH, then:

```bash
sudo apt update && sudo apt full-upgrade -y
sudo reboot
git clone https://github.com/zupino/music-box.git
cd music-box
sudo ./scripts/install-wm8960-driver.sh --accept-third-party # only if the HAT is absent from aplay -l
sudo reboot                                                  # required after driver installation
cd music-box
sudo ./scripts/install.sh --migrate-legacy
sudo reboot
music-box-check
```

Run `aplay -l` before the driver step: skip that step when `wm8960soundcard` is already listed. This project is intentionally headless and does not draw a UI on HDMI or an attached display. See [`docs/installation.md`](docs/installation.md) for image preparation, verification, SoundFont selection and safe speaker wiring.

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
- MPD218 knob control of the WM8960 hardware output volume, shown briefly on the display;
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

### Volume knob

The top-left MPD218 knob (K1, bank A) sets the WM8960 hardware playback volume
through the ALSA simple mixer, and the display shows the new percentage for a
moment before returning to the beat screen.

```ini
volume_control_enabled=true
volume_cc=3
volume_mixer_card=hw:wm8960soundcard
volume_mixer_control=Speaker,Headphone
display_volume_seconds=2
```

`volume_mixer_control` is a comma-separated list, so one knob moves the speaker
terminals and the 3.5 mm jack together; unknown names are skipped with a
warning and a missing list falls back to the first playback control on the
card. Setting a non-zero volume also unmutes each listed output, because a
stored ALSA state can otherwise leave the jack silent. A percentage is a
position in the control's raw register range, the same scale `amixer sset
'Speaker' 70%` uses, so full clockwise is the control's true maximum.
`display_volume_seconds=0` keeps the beat screen visible.

Factory MPD218 presets vary. Confirm the controller number the knob sends
before trusting the default:

```bash
aseqdump -p MPD218
```

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

## Development notes

`docs/dev-notes.md` holds the running session log. Points that repeatedly cost
time:

### Updating a device that already runs Music Box

`scripts/install.sh` is idempotent and is the supported update path; there is
no need to uninstall first.

```bash
cd ~/music-box
git pull
sudo ./scripts/install.sh --skip-packages
```

It rebuilds, reinstalls the binary and helpers, regenerates the three unit
files and restarts the service chain. Two caveats: it never overwrites an
existing `/etc/music-box/music-box.conf`, so keys added by a pull must be
appended by hand; and it always rewrites `/etc/music-box/music-box.env`, so
re-pass `--soundfont` if the original install used a custom one.

### Disabling the onboard Raspberry Pi audio

The built-in `bcm2835` headphone and HDMI audio devices compete with the HAT
for card numbering and can capture applications that open the default device.
Music Box itself addresses the card by name (`hw:wm8960soundcard`), so it is
unaffected, but disabling the onboard devices removes a class of confusion:

```bash
sudo sed -i 's/^dtparam=audio=on/dtparam=audio=off/' /boot/firmware/config.txt
sudo sed -i 's/^dtoverlay=vc4-kms-v3d$/dtoverlay=vc4-kms-v3d,noaudio/' /boot/firmware/config.txt
printf 'blacklist snd_bcm2835\n' | sudo tee /etc/modprobe.d/music-box-no-onboard-audio.conf
sudo reboot
```

Older Raspbian uses `/boot/config.txt`. After the reboot `aplay -l` should list
only `wm8960soundcard`, and `music-box-check` reports whether `snd_bcm2835` is
still loaded.

### Output volume is lower than the hardware maximum

Three attenuation stages sit in front of the speakers, and the knob only moves
the last one:

| Stage | Where | Default |
|---|---|---|
| FluidSynth gain | `FLUIDSYNTH_GAIN` in `/etc/music-box/music-box.env` | 0.70 |
| Mixer gains | `synth_gain` x `master_gain` in `music-box.conf` | 0.70 x 0.80 |
| Codec playback | ALSA `Speaker`/`Headphone`, driven by the knob | knob position |

The digital stages multiply to roughly 0.39 for synth material, about 8 dB
below full scale, before the codec is reached. Raise `master_gain` and
`synth_gain` toward 1.0 for more level, keeping in mind that
`audio_engine.c` hard-clips the sum of four loops plus the synth at +/-1.0, so
headroom exists on purpose. The codec also has its own stages ahead of the
speaker amplifier: check `PCM` and the output mixer switches with `amixer -c
wm8960soundcard scontrols` before concluding that the knob is the limit.
Because the codec register is dB-scaled, the lower half of the knob travel is
nearly inaudible; this is the hardware curve, not the mapping.

There is no volume above 100%: for an ALSA control, 100% *is* the top of its
raw register range, and the knob reaches it exactly. The one place the WM8960
offers above-unity gain is the class-D amplifier boost, exposed as the separate
`Speaker DC` and `Speaker AC` controls (six steps, roughly 1.0x to 1.7x). They
are not part of the ordinary volume control and are left at whatever the stored
ALSA state holds:

```bash
amixer -c wm8960soundcard sget 'Speaker DC'
amixer -c wm8960soundcard sset 'Speaker DC' 5
amixer -c wm8960soundcard sset 'Speaker AC' 5
sudo alsactl -f /etc/wm8960-soundcard/wm8960_asound.state store wm8960soundcard
```

Boost drives the amplifier harder and increases distortion and speaker load, so
raise the digital gains first and treat this as the last resort.

## Documentation

- `docs/installation.md` — Raspberry Pi OS/Ubuntu installation, migration and complete cleanup
- `docs/architecture.md` — real-time ownership, signal graph and DSP structure
- `docs/implementation-history.md` — complete intervention summary
- `docs/legacy-raspbian.md` — Stretch/4.19/DKMS/JACK special cases
- `docs/aec-tuning.md` — echo cancellation tuning
- `docs/troubleshooting.md` — boot, codec, MIDI and JACK diagnosis
- `docs/dev-notes.md` — session log, hardware findings and device update recipes
- `docs/roadmap.md` — deferred pad-tap handling and future features

## License

Project-owned code, scripts and documentation are MIT licensed; see `LICENSE`. External drivers, FluidSynth, JACK, ALSA and SoundFonts retain their own licenses and are not redistributed here. See `THIRD_PARTY.md` and `LICENSES/README.md`.
