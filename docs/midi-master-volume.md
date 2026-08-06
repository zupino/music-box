# MIDI master volume

Music Box can consume one MIDI Control Change message from the MPD218 and use it as the software master volume for both FluidSynth and loop playback.

The mapping is disabled by default because MPD218 presets may assign different CC numbers to the knobs.

## Find the knob CC

List ALSA MIDI clients:

```bash
aconnect -l
```

Monitor the MPD218 port, replacing `20:0` with the actual client and port:

```bash
aseqdump -p 20:0
```

Turn the desired knob. A line similar to this identifies the controller number and channel:

```text
Control change  0, controller 21, value 57
```

This example means channel `0`, CC `21`.

## Configure Music Box

Edit `/etc/music-box/music-box.conf`:

```ini
master_volume_cc=21
master_volume_channel=0
master_volume_smoothing_ms=30
```

Use `master_volume_channel=any` to accept the configured CC on every MIDI channel. Use `master_volume_cc=off` to disable the mapping.

Restart Music Box:

```bash
sudo systemctl restart music-box.service
```

The service log reports the active mapping and volume changes:

```bash
journalctl -u music-box -f
```

## Gain behaviour

The configured `master_gain` is both the startup gain and the maximum gain available at the knob's fully clockwise position. The MIDI range is mapped with a squared curve, giving finer control at quiet levels:

```text
CC 0   -> mute
CC 64  -> about 25% of master_gain
CC 127 -> master_gain
```

Changes are ramped in the JACK callback over `master_volume_smoothing_ms` to avoid clicks and zipper noise. The gain is applied after synth and loop mixing but before clipping and the AEC reference, so acoustic echo reduction continues to receive the exact speaker-bound signal.

The selected CC is consumed by Music Box and is not forwarded to FluidSynth. All other non-loop MIDI events continue to be forwarded normally.
