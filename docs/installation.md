# Installation

## Hardware

Required: Raspberry Pi 3B or newer, Waveshare WM8960 Audio HAT, Akai MPD218, suitable power supply, and headphones or passive speakers suitable for the HAT. The speaker terminals are bridge-tied outputs (`LP/LN` and `RP/RN`); do not join either negative terminal to Raspberry Pi ground.

## Operating system

The preferred image is current Raspberry Pi OS Lite. Bookworm and Trixie are supported targets, in 32- or 64-bit form; Raspberry Pi OS Lite 64-bit works without a desktop environment. Music Box is headless: it produces audio and consumes MIDI but does not render an HDMI, console or attached-display UI. A blank display is therefore not evidence that the service failed.

Ubuntu Server LTS can run the application and services; the external WM8960 kernel driver is the less portable part because Waveshare's Raspberry Pi guide targets Raspberry Pi OS.

### Prepare a freshly flashed board

In Raspberry Pi Imager, choose Raspberry Pi OS Lite, configure a user, Wi-Fi or Ethernet, hostname and SSH in **OS customisation**, then flash the card. Connect the HAT and MPD218 while the Pi is powered off. On first boot, log in locally or over SSH and bring the image, kernel and headers into sync:

```bash
sudo apt update
sudo apt full-upgrade
sudo reboot
```

Reconnect after the reboot, clone this repository, and run all remaining commands from its root directory:

```bash
sudo apt install -y git
git clone https://github.com/zupino/music-box.git
cd music-box
```

## Verify/install WM8960

```bash
aplay -l
arecord -l
```

Both should show `wm8960soundcard`. If already present, do not install another driver copy. When absent, run the helper from the repository root:

```bash
sudo ./scripts/install-wm8960-driver.sh --accept-third-party
sudo reboot
```

The helper installs build/DKMS prerequisites, verifies matching headers, clones the external GPL Waveshare repository, selects `rpi-4.9.y` for kernels older than 5.0, runs the vendor installer, and prints the exact external revision and license path. It may also print a path to a system SoundFont; that is informational and does not configure Music Box. No driver source is contained in this repository.

Current Raspberry Pi OS normally uses `/boot/firmware/config.txt`; older Raspbian uses `/boot/config.txt`. After reboot check:

```bash
dkms status
aplay -l
arecord -l
```

DKMS must say `installed`, not only `added`.

## Install Music Box

```bash
sudo ./scripts/install.sh --migrate-legacy
```

With a custom SoundFont:

```bash
sudo ./scripts/install.sh --migrate-legacy --soundfont /path/to/file.sf2
```

`--migrate-legacy` disables `jack-audio`, `fluidsynth-mpd`, `mpd-looper` and `mpd-connect`. Two JACK servers cannot own one device, and the old direct routes would bypass the AEC mixer.

The installer installs compiler/JACK/ALSA/FluidSynth packages and the distribution's General MIDI SoundFont when available, builds the C application, installs systemd units, adds the selected user to `audio`, preserves an existing configuration, and starts the service chain when the HAT is visible.

Do not run an older standalone `start-fluidsynth.sh` or manually create `/etc/music-box/soundfont.sf2`. The current installer detects `/usr/share/sounds/sf2/FluidR3_GM.sf2`, `/usr/share/sounds/sf2/FluidR3_GS.sf2`, or `/usr/share/sounds/sf3/default-GM.sf3` and writes the selected path to `/etc/music-box/music-box.env`. To use some other readable `.sf2` or `.sf3` file, pass `--soundfont` as shown above. If FluidSynth reports `SoundFont is not readable`, inspect the configured value and reinstall with the correct path:

```bash
sudo cat /etc/music-box/music-box.env
find /usr/share/sounds -type f \( -name '*.sf2' -o -name '*.sf3' \) -print
sudo ./scripts/install.sh --migrate-legacy --soundfont /absolute/path/to/file.sf2
```

## Reboot and verify

```bash
sudo reboot
systemctl status music-box-jack music-box-fluidsynth music-box --no-pager
music-box-check
jack_lsp -c
aconnect -l
```

All three Music Box units should be `active (running)`. On Lite, use these commands and the journal rather than a screen or desktop UI to determine status.

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

## Uninstall and return the Pi to general use

The safe default stops and disables all three services and removes Music Box executables, service units, helper scripts and real-time limits. It deliberately keeps `/etc/music-box`, Debian packages, the user's pre-existing `audio` group membership, and the third-party WM8960 driver because they may be shared with other audio applications:

```bash
sudo ./scripts/uninstall.sh
```

For a more complete, explicitly requested cleanup, replace `pi` with the account used during installation:

```bash
sudo ./scripts/uninstall.sh --purge --remove-packages \
    --remove-driver-source --remove-audio-group pi
```

`--remove-packages` removes every Debian package that `install.sh` requests, even if some were installed before Music Box; inspect the list printed by APT before using this option on a multipurpose Pi. It does not run `apt autoremove`. `--remove-driver-source` deletes the downloaded Waveshare source tree but intentionally does **not** uninstall its active kernel driver. Keep the driver if you want to use the WM8960 HAT with other software; consult the exact checked-out Waveshare revision for its kernel-driver removal procedure if you need to remove it too.

The installer cannot reliably know whether the Debian packages, `audio` membership, or driver existed before Music Box, so destructive cleanup is never automatic. It also cannot restore legacy services disabled by `--migrate-legacy`; enable only the old units you intentionally want to use again.

After uninstalling, the following should report that the Music Box units and executable do not exist:

```bash
systemctl status music-box-jack music-box-fluidsynth music-box --no-pager
command -v music-box
```
