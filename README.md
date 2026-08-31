# Music Box

Music Box turns a Raspberry Pi, Waveshare WM8960 Audio HAT and Akai MPD218 into a headless stereo synthesizer, sixteen-slot live looper, sixteen-pad sampler and SoundFont pad controller.

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
- sixteen independent loop slots, one per MPD218 pad, on notes 52–67;
- sampler-only recording from either cleaned microphones or the speaker monitor;
- sample assignment to loop tracks, with beat-anchored starts;
- three performance modes on the same pads: loop, sampler and pad;
- sampler mode gives each pad a one-shot recording of its own;
- pad mode plays the FluidSynth SoundFont straight from the sixteen pads;
- knob 2 switches mode without silencing anything already playing;
- knobs 3-6 select tracks and samples and trim sample heads and tails;
- non-loop MIDI forwarded to FluidSynth;
- sample-accurate shared beat/bar transport;
- beat- or bar-quantized record start, finish and stop;
- dependency-free stereo NLMS acoustic echo reduction;
- automatic JACK and ALSA MIDI connection repair;
- ordered systemd startup;
- 128x32 I2C OLED showing the mode, a 4x4 map of what each pad holds, and the beat;
- MPD218 knob control of the WM8960 hardware output volume, shown briefly on the display;
- current Raspberry Pi OS and legacy Raspbian/kernel notes.

The AEC is intentionally lightweight for Raspberry Pi 3. It is acoustic echo reduction rather than telephony-grade all-room cancellation; delay normally needs tuning for the enclosure.

## Supported targets

Primary: current Raspberry Pi OS Lite (Trixie or Bookworm), 32- or 64-bit, Raspberry Pi 3B or newer, WM8960 HAT and MPD218.

Also intended: Ubuntu Server LTS when the WM8960 driver is functioning, and the original Pi 3B Raspbian installation using kernel `4.19.66-v7+`.

## Signal flow

```text
MPD218 -> Music Box MIDI router
            |-- notes 52-67 -> loop or sampler pad commands
            |                  (forwarded to FluidSynth in pad mode)
            |-- knob 2      -> performance mode
            `-- other MIDI -> FluidSynth

FluidSynth JACK L/R -----------+
Loop and sample playback L/R --+--> Music Box mixer --> WM8960 speakers
                               |          |
                               |          `--> exact AEC reference
WM8960 microphones --> AEC ----+--> unquantized sampler recorder
```

FluidSynth is routed through Music Box rather than directly to the physical playback ports so AEC sees the exact loudspeaker signal.

## Controls

All sixteen pads, notes 52 to 67, drive three performance modes. Knob 2 (K2,
bank A, CC 9 by default) chooses between them, a third of its travel each:
loop, then sampler, then pad.

Pads are numbered the way the MPD218 labels them, top-left first. In the
factory bank-B preset pad 1 is note 64 and pad 16 is note 55 because MIDI notes
ascend from the controller's opposite row. The display follows printed labels.

### Loop mode

Loop mode manages tracks; it never records or trims samples. Knob 4 selects
among recorded samples only. Holding a pad assigns the selected sample to that
pad's loop track: holding pad 2 assigns it to loop 2. A short press starts or
stops the same track. K3 is no longer needed and is not shown in loop mode.
Starts wait for the next beat, then the trimmed sample repeats at its exact
length. Stops continue to use the configured `quantize` boundary.

1. Turn knob 4 to select a recorded sample.
2. Hold a loop pad to assign that sample to the corresponding track.
3. Tap the pad: playback starts at the next beat.
4. Press a playing pad: stop is armed for the configured boundary. The loop keeps sounding
   out the current cycle, and the display hatches its cell to show it is on the
   way out. Pressing again before the boundary cancels the stop.

The application starts in pad mode, so startup never turns a parked mode knob
into an unexpected recording or loop-control gesture.

### Sampler mode

Every pad is a one-shot recorder. Nothing repeats on its own, and nothing is
quantized: a sample lasts exactly as long as its pad was held.

The pad itself always chooses the recording/playback slot. Knob 4 only chooses
the existing sample edited by the trim knobs: K5 moves its start and K6 moves
its end. Sampler mode shows the current K4 target as `TRIM Sx`; while either
trim knob moves, the OLED shows that sample name, the whole sample as an
outlined horizontal bar, and its retained range as a filled section.

1. Hold a pad past `pad_hold_milliseconds` (400 ms by default): recording runs
   from the moment of the press, with no wait for a beat or bar.
2. Release: the recording closes immediately and stays with that pad, silent.
3. Tap the pad: the sample plays once, from its start, and stops at its end.
   Tapping again while it plays restarts it.
4. Hold the pad again: a new recording replaces the sample.

Step 1 captures the head of the sample even though the press only becomes a
recording gesture 400 ms later: the sampler keeps a rolling pre-roll of the last
`pad_hold_milliseconds` of input and lays it down as the start of the take. A
sample therefore spans press to release, which is what you want for a word, a
hit or a short phrase, rather than the whole bar `quantize` would round it up
to. Loop mode is unaffected and still follows `quantize`.

The pre-roll only keeps input captured while the sampler bank was silent. Since
tapping a loaded pad previews it, re-recording over a pad that is sounding
starts the take at the hold instead, losing that head. That is deliberate: those
400 ms contain the outgoing sample coming back through the microphones, which
would otherwise be pasted onto the front of its own replacement.

### Pad mode

The sixteen pads play the FluidSynth SoundFont. Nothing is recorded and nothing
is consumed: each pad's note is forwarded to the synthesizer with its velocity
intact, so the pads behave like any other keyboard into FluidSynth, and the
display lights a cell while its pad is held.

Loops and samples carry on playing underneath, which is the point — pad mode is
how you play over what you have already recorded. A Note Off always follows its
own Note On, so turning the knob while a pad is down cannot leave a note hanging
on the synthesizer.

Sampler audio and assigned loop-track state are separate, so assignment does
not overwrite a sample. Assigned loops keep playing while sampler pads are used, and
samples keep playing after a switch back. That also means a sampler recording
captures any loop still running through the microphones unless the loops are
stopped first.

The mode knob is absolute, like the volume knob: the mode follows it only once
it is moved, so a freshly started service is always in pad mode regardless of
where the knob is parked. Its travel splits into three zones with a switch at
each boundary:

| Knob | Mode |
|---|---|
| 0–42 | loop |
| 43–84 | sampler |
| 85–127 | pad |

Each switch has a dead band of `mode_cc_hysteresis` counts either side that
holds the current mode. That dead band is the only rotation a mode change costs
once the knob is near a switch — 2 counts out of 127 by default, roughly 8
degrees on the MPD218 pot — so lower it for a lighter switch and raise it if a
resting knob starts flapping on its own jitter. It is capped at 16, because a
dead band wider than half a zone would make sampler mode unreachable.

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

### Performance modes

```ini
mode_control_enabled=true
mode_cc=9
max_sample_seconds=10
pad_hold_milliseconds=400
```

```ini
mode_cc_hysteresis=2
```

`max_sample_seconds` allocates sixteen stereo buffers of that length at startup.
Loop tracks reference assigned sample storage, so they do not duplicate it.
The total is `16 x max_sample_seconds x 384 kB` at 48 kHz — about 61 MB with
the shipped value. The startup banner prints the figure.
`pad_hold_milliseconds` is the line between
triggering a sample and recording a new one, and it also sizes the pre-roll, so
lowering it makes the gesture snappier while raising it costs a little more
memory. The hold is timed on the MIDI thread at 5 ms resolution. Setting
`mode_control_enabled=false` pins the box to pad mode.

`quantize` applies to loop mode only. Sampler and monitor recording are always
unquantized. In pad mode, move K3 in either direction to start capturing the
final speaker mix into the first empty sampler slot; pause, then move K3 again
in either direction to stop. A continuous sweep is one gesture, not many
toggles. The completed take is selected for K4/K5/K6 trimming and can be
assigned to a loop like any microphone recording. Music Box refuses to start
when all sample slots are occupied rather than overwriting a take.

Factory MPD218 presets vary here too. Confirm which controller knob 2 sends
with `aseqdump -p MPD218`, and keep `mode_cc` and `volume_cc` distinct.

### Mode and pad display

An SSD1306-compatible 128x32 I2C OLED at address `0x3c`. The panel is wide and
short, so the screen is laid out as three columns rather than stacked rows:

```text
+-----------------------------+-+-----------+---+
| [    ][    ][    ][    ]    | | LOOP      | . |
| [    ][    ][    ][    ]    | |           | # |
| [    ][####][    ][    ]    | | 120 BPM   | . |
| [####][::::][~~~~][    ]    | | REC 6     | . |
+-----------------------------+-+-----------+---+
```

Nothing is stacked. The 4x4 map takes the left, a rule separates it from the
status text, and the beat runs as a rail of four dots down the right edge with
the current one filled. Each column then gets the panel's full height: the pad
cells are as tall as the display allows, and mode, tempo and record state each
get their own line instead of sharing one row.

Every gutter and outer margin is 3 pixels, the dots share the map's row pitch,
and the text lines sit flush with the map's top and bottom, so the three columns
run on one rhythm. The beat rail is present on every screen, including the
volume overlay.

The map has the same shape as the controller, pad 1 at the bottom left, so a
cell's position identifies its pad without needing a number:

| Cell | Meaning |
|---|---|
| blank | the pad has no note assigned |
| baseline | mapped, but holds nothing |
| outline | holds audio, silent |
| solid | sounding |
| hatched | sounding out this cycle, stopping at the next boundary |
| blinking | armed or recording |

So in loop mode the solid cells are the loops playing back and a hatched cell is
one you have asked to stop, still finishing its current pass. In sampler mode
the outlined cells are the pads with a sample ready to trigger. In pad mode
nothing is stored, so a cell is solid only while its pad is held.

The default device is `/dev/i2c-1`; the display is optional, and an
initialization failure does not stop the audio engine. Configure it with:

```ini
display_enabled=true
display_i2c_device=/dev/i2c-1
display_i2c_address=0x3c
display_rotate_180=false
```

The display shares the I2C bus with the WM8960 but uses a different address.
Display writes happen outside the JACK callback so I2C traffic cannot block the
real-time audio thread.

Set `display_rotate_180=true` when the OLED is mounted upside-down. The SSD1306
controller rotates the complete image, including the pad grid and trim screen.

### Volume knob

The top-left MPD218 knob (K1, bank A) sets the WM8960 hardware playback volume
through the ALSA simple mixer, and the display shows the new percentage for a
moment before returning to the pad screen.

```ini
volume_control_enabled=true
volume_cc=3
volume_mixer_card=hw:wm8960soundcard
volume_mixer_control=Speaker,Headphone
display_volume_seconds=2
```

`volume_db_curve` spreads the knob over the control's decibel range rather
than its raw register range. On the WM8960 this matters a great deal: raw
values below 48 are muted, so a proportional mapping wastes the first third of
the travel in silence and crams everything from -73 dB to +6 dB into the top
few percent. The curve is the normalized mapping `alsamixer` and `pavucontrol`
present, and it falls back to proportional automatically for controls that
report no decibel range.

| Knob | Raw mapping | dB curve |
|---|---|---|
| 20% | muted | -31 dB |
| 50% | -57 dB | -11 dB |
| 80% | -19 dB | +0.5 dB |
| 100% | +6 dB | +6 dB |

`volume_mixer_control` is a comma-separated list, so one knob moves the speaker
terminals and the 3.5 mm jack together; unknown names are skipped with a
warning and a missing list falls back to the first playback control on the
card. Setting a non-zero volume also unmutes each listed output, because a
stored ALSA state can otherwise leave the jack silent. A percentage is a
position in the control's raw register range, the same scale `amixer sset
'Speaker' 70%` uses, so full clockwise is the control's true maximum.
`display_volume_seconds=0` keeps the pad screen visible.

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

The unit suite covers transport boundaries, lock-free queues, assigned-loop and
sampler states, microphone gating, and synthetic AEC convergence.

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
