# Legacy Raspbian and old-kernel notes

These notes preserve the special cases encountered on the original Raspberry Pi 3B Rev 1.2.

## Original system and headers

The active kernel was ultimately confirmed as `4.19.66-v7+`, with matching headers at `/lib/modules/4.19.66-v7+/build`. Always trust `uname -r` and the active modules tree rather than an older log or remembered kernel version.

## WM8960 branch and boot layout

Waveshare documents `rpi-4.9.y` for kernels below 5.0 and the current/default branch for newer kernels. The old installation used:

```bash
git clone -b rpi-4.9.y https://github.com/waveshareteam/WM8960-Audio-HAT.git
```

Its boot file was `/boot/config.txt`; current Raspberry Pi OS generally uses `/boot/firmware/config.txt`.

## DKMS diagnosis

`wm8960-soundcard, 1.0: added` was incomplete: source was registered but not installed for the running kernel. Success ended in `installed`, and `arecord -l` exposed `wm8960soundcard` / `bcm2835-i2s-wm8960-hifi`.

## JACK special cases

The old service required:

```ini
Environment=JACK_NO_AUDIO_RESERVATION=1
```

The known-good command was:

```bash
export JACK_NO_AUDIO_RESERVATION=1
jackd -R -d alsa -d hw:wm8960soundcard -r 48000 -p 256 -n 2 -i 2 -o 2
```

When healthy, `jack_lsp` showed two capture and two playback ports. A unit looking active did not prove the backend was healthy: `Driver is not running`, `Cannot create new client`, or `jack_client_open() failed` meant JACK had failed. An early cleanup command used `/usr/bin/rm`; Stretch had `rm` at `/bin/rm`. The new service does not need that cleanup command.

## FluidSynth compatibility

The Stretch-era FluidSynth process could exit at stdin EOF. The working service fed `/dev/zero`; the new launcher retains this harmless workaround and also uses `-i`.

## Startup and device ownership

Stable order was WM8960 driver/service, JACK, FluidSynth, looper/routing. Once JACK owns the HAT, direct `arecord` returning `Device or resource busy` is expected.

## MIDI discovery

The physical loop pads were notes 52–55. The direct MPD218-to-FluidSynth route caused those notes to play instruments, so they are now consumed by Music Box while other events are forwarded. FluidSynth's ALSA client name included a changing PID, so the new code matches a stable prefix rather than a complete literal name.
