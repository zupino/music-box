# Troubleshooting

## Pad instrument does not change

The running service reads `/etc/music-box/music-box.conf`, not the copy under
the Git checkout. Check the exact spelling of the installed path and confirm
the zero-based program that the process loaded:

```sh
grep '^soundfont_program=' /etc/music-box/music-box.conf
sudo systemctl restart music-box.service
journalctl -u music-box.service -n 30 --no-pager | grep 'Pad SoundFont program'
```

An MPD218 may send on MIDI channel 10 (reported by FluidSynth as zero-based
`channel 9`). FluidSynth pins that channel to percussion bank 128 and may log
messages such as `bank=128 prog=127` even after receiving melodic bank
selection. Some packaged FluidSynth versions do not provide a setting to
disable that behavior. Music Box therefore routes mapped pad-mode notes to MIDI
channel 1, then selects melodic SoundFont bank 0 and applies
`soundfont_program`. Re-run the installer after upgrading because both the
installed binary and launcher need to be refreshed, then restart both services:

```sh
sudo ./scripts/install.sh --skip-packages
sudo systemctl restart music-box-fluidsynth.service
sudo systemctl restart music-box.service
```

Run `music-box-check` for OS/kernel information, ALSA cards, service status, JACK connections and ALSA sequencer routes.

## No picture or display UI

Music Box is a headless audio service and does not currently render a user interface. Raspberry Pi OS Lite has no desktop by design. Diagnose the application with `music-box-check`, `systemctl status music-box-jack music-box-fluidsynth music-box`, and the service journals. A language-learning display mode is only a roadmap proposal.

## Active services but no audio

```bash
jack_lsp
journalctl -u music-box-jack -b --no-pager
```

If JACK cannot open, stop the stack and test manually:

```bash
sudo systemctl stop music-box music-box-fluidsynth music-box-jack
export JACK_NO_AUDIO_RESERVATION=1
jackd -R -d alsa -d hw:wm8960soundcard -r 48000 -p 256 -n 2 -i 2 -o 2
```

A successful `jack_lsp` from another terminal isolates service environment/order rather than hardware.

## `arecord`: Device or resource busy

Expected while JACK owns the codec. Stop `music-box-jack` before direct ALSA tests.

## Loops work but synth pads are silent

Check `aconnect -l` and `jack_lsp -c`. Expected MIDI is MPD218 to Music Box and Music Box Synth Out to FluidSynth. Expected audio is FluidSynth outputs to `music_box:synth_l/r`. FluidSynth must not bypass Music Box directly to system playback because the AEC would not see it.

If the FluidSynth journal says that `/etc/music-box/soundfont.sf2` is unreadable, that path came from an older installation. The current installer records an existing system SoundFont in `/etc/music-box/music-box.env`; rerun `sudo ./scripts/install.sh --migrate-legacy` or explicitly add `--soundfont /absolute/path/to/file.sf2`.

## WM8960 missing after reboot

```bash
uname -r
ls -l /lib/modules/$(uname -r)/build
dkms status
aplay -l
arecord -l
journalctl -u wm8960-soundcard -b --no-pager
```

A kernel update may require a driver rebuild. Headers must match `uname -r`.

## Correct JACK graph but silence

Use `sudo alsamixer -c wm8960soundcard`, unmute playback/DAC/speaker controls, then `sudo alsactl store`.

## AEC problems

Metallic/hollow voice: lower step size or double-talk ratio, or disable AEC. Weak cancellation: tune delay in 32/64-frame steps around 512 and allow several seconds for adaptation before recording.

## XRUNs

Increase JACK buffering to `-p 256 -n 3` or `-p 512 -n 2`. Changing latency changes the likely AEC delay, so retune it afterward.
