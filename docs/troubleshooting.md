# Troubleshooting

Run `music-box-check` for OS/kernel information, ALSA cards, service status, JACK connections and ALSA sequencer routes.

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
