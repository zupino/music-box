# Installation

## Hardware

Required: Raspberry Pi 3B or newer, Waveshare WM8960 Audio HAT, Akai MPD218, suitable power supply, and headphones or passive speakers suitable for the HAT. The speaker terminals are bridge-tied outputs (`LP/LN` and `RP/RN`); do not join either negative terminal to Raspberry Pi ground.

## Operating system

The preferred image is current Raspberry Pi OS Lite. Bookworm and Trixie are practical targets, in 32- or 64-bit form. Ubuntu Server LTS can run the application and services; the external WM8960 kernel driver is the less portable part because Waveshare's Raspberry Pi guide targets Raspberry Pi OS.

```bash
sudo apt update
sudo apt full-upgrade
sudo reboot
```

## Verify/install WM8960

```bash
aplay -l
arecord -l
```

Both should show `wm8960soundcard`. If already present, do not install another driver copy. When absent:

```bash
sudo ./scripts/install-wm8960-driver.sh --accept-third-party
sudo reboot
```

The helper installs build/DKMS prerequisites, verifies matching headers, clones the external GPL Waveshare repository, selects `rpi-4.9.y` for kernels older than 5.0, runs the vendor installer, and prints the exact external revision and license path. No driver source is contained in this repository.

Current Raspberry Pi OS normally uses `/boot/firmware/config.txt`; older Raspbian uses `/boot/config.txt`. After reboot check:

```bash
dkms status
aplay -l
arecord -l
```

DKMS must say `installed`, not only `added`.

## Install Music Box

```bash
git clone https://github.com/zupino/music-box.git
cd music-box
sudo ./scripts/install.sh --migrate-legacy
```

With a custom SoundFont:

```bash
sudo ./scripts/install.sh --migrate-legacy --soundfont /path/to/file.sf2
```

`--migrate-legacy` disables `jack-audio`, `fluidsynth-mpd`, `mpd-looper` and `mpd-connect`. Two JACK servers cannot own one device, and the old direct routes would bypass the AEC mixer.

The installer installs compiler/JACK/ALSA/FluidSynth packages, builds the C application, installs systemd units, adds the selected user to `audio`, preserves an existing configuration, and starts the service chain when the HAT is visible.

## Reboot and verify

```bash
sudo reboot
systemctl status music-box-jack music-box-fluidsynth music-box --no-pager
jack_lsp -c
aconnect -l
```

Expected audio shape:

```text
system captures -> music_box mic inputs
FluidSynth outputs -> music_box synth inputs
music_box outputs -> system playback
```

Expected MIDI shape:

```text
MPD218 -> Music Box:Looper Control
Music Box:Synth Out -> FLUID Synth
```

## Configuration

```bash
sudo nano /etc/music-box/music-box.conf
sudo systemctl restart music-box
```

Default is 120 BPM, 4/4, bar quantization. Use `quantize=beat` for shorter waits or `quantize=off` for the original immediate behaviour.

## Direct ALSA testing

`arecord`/`aplay` may report `Device or resource busy` while JACK owns the codec. Stop the stack first:

```bash
sudo systemctl stop music-box music-box-fluidsynth music-box-jack
arecord -D hw:CARD=wm8960soundcard,DEV=0 -f S32_LE -r 48000 -c 2 -d 5 /tmp/test.wav
aplay -D hw:CARD=wm8960soundcard,DEV=0 /tmp/test.wav
sudo systemctl start music-box-jack music-box-fluidsynth music-box
```
