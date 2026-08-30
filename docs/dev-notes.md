# Development Notes

Chronological log of debugging sessions, key findings, and hardware quirks.

---

## 2026-08-10 — Display branch created

Branch `codex/add-visual-indication-for-current-beat` (commit `94c87e4`) added
a 128×32 SSD1306 I2C OLED driver (`src/display.c`, `src/display.h`). The
display shows tempo in BPM, current beat position (four circles, filled on the
active beat), and REC/ARMED state for loop slots.

Key design decisions in that branch:
- Display init failure is non-fatal — audio engine continues without display.
- All I2C writes happen in the main loop thread, never inside the JACK RT
  callback, so I2C latency cannot cause audio glitches.
- The 5×7 bitmap font only contains glyphs actually used on screen.

That branch was never merged to main.

---

## 2026-08-29 — Display integration session

### Goal
Merge display feature from `codex/add-visual-indication-for-current-beat` into
`main` so a fresh Raspberry Pi OS Lite install works out of the box.

### What was ported to main (commit `679247f`)
- `src/display.h`, `src/display.c` — new SSD1306 driver
- `src/rt_queue.h` — added `MUSIC_BOX_EVENT_BEAT` event type
- `src/audio_engine.c` — `emit_beat_event()` fired when `transport.beat_boundary` is true
- `src/config.h` / `src/config.c` — `display_enabled`, `display_i2c_device`,
  `display_i2c_address` config fields; defaults `true` / `/dev/i2c-1` / `0x3c`
- `config/music-box.conf` — added `display_*` section
- `scripts/install.sh` — adds service user to `i2c` group when group exists
- `README.md` — display feature listed; "Beat display" configuration section

### What was intentionally NOT ported
The display branch's `src/midi.c`, docs, and scripts diffs reflected diverging
from main before commit `fd37028` ("Fixed missing header alloca.h and
snd_seq_ev_source argument") and would have regressed already-fixed code.

### Hardware discovery: two I2C buses
On the test Raspberry Pi 3 B+:
- `/dev/i2c-1` — WM8960 audio HAT (shows `UU` in `i2cdetect`, owned by kernel
  driver; do not use for userspace I2C)
- `/dev/i2c-2` — SSD1306 display at address `0x3c` (confirmed by
  `sudo i2cdetect -y 2`)

**The default config (`/dev/i2c-1`) is wrong for this board.** The config
installed to `/etc/music-box/music-box.conf` must use `display_i2c_device=/dev/i2c-2`.

### install.sh preserves existing config
`install.sh` deliberately keeps `/etc/music-box/music-box.conf` if it already
exists ("Keeping existing /etc/music-box/music-box.conf"). New config keys
introduced by a `git pull` + reinstall are therefore NOT written automatically.

**Workaround (manual, on device):**
```bash
sudo tee -a /etc/music-box/music-box.conf <<'EOF'

# Adafruit 128x32 SSD1306 beat/status display.
display_enabled=true
display_i2c_device=/dev/i2c-2
display_i2c_address=0x3c
EOF
sudo systemctl restart music-box.service
```

**TODO:** improve `install.sh` to inject missing config keys rather than
skipping the file entirely.

### RT scheduling error (commit `2eb3601`)
```
music-box[921]: JackClient::AcquireSelfRealTime error
```
`music-box.service` was missing `LimitRTPRIO` and `LimitMEMLOCK`. systemd
services do not inherit PAM `limits.d` settings. Fixed by adding to
`systemd/music-box.service.in`:
```ini
LimitRTPRIO=95
LimitMEMLOCK=infinity
```
`music-box-jack.service` already had these; now both units are consistent.

Apply on device without reinstalling:
```bash
sudo sed -i '/^RestartSec=/a LimitRTPRIO=95\nLimitMEMLOCK=infinity' \
    /etc/systemd/system/music-box.service
sudo systemctl daemon-reload
sudo systemctl restart music-box.service
```

### Display still blank after reboot (under investigation)
Service is active and running, config keys are present, I2C device is at
`0x3c` on bus 2, user is in `i2c` group. Display remains blank.

Next diagnostic steps:
```bash
# Full service journal
journalctl -u music-box -b --no-pager

# Device permissions
ls -la /dev/i2c*

# Direct hardware write bypassing the service
i2ctransfer -y 2 w2@0x3c 0x00 0xaf   # Display ON
```

---

## 2026-08-29 — MIDI volume knob

### Goal
Control the Raspberry Pi output volume from the MPD218 top-left knob and show
the value on the OLED when it changes.

### What was added
- `src/volume.h`, `src/volume.c` — ALSA simple-mixer wrapper. Resolves a
  comma-separated control list, scales a CC value onto each control's raw
  range, unmutes non-zero settings, and publishes the result through two
  atomics (`percent` plus a `generation` counter).
- `src/midi.c` — `SND_SEQ_EVENT_CONTROLLER` matching `volume_cc` is consumed
  instead of being forwarded to FluidSynth.
- `src/display.c` — `VOLUME` screen with a percentage and a bar, shown for
  `display_volume_seconds` and then replaced by the beat screen. Font gained
  `L`, `O`, `U`, `V` and `%`.
- `src/config.c` — `volume_control_enabled`, `volume_cc`, `volume_mixer_card`,
  `volume_mixer_control`, `display_volume_seconds`.
- `tests/test_volume.c` — CC/raw scaling and control-list parsing; no hardware.
- `scripts/check-system.sh` — reports onboard `snd_bcm2835`, the boot config
  audio lines, and the WM8960 `Speaker`/`Headphone`/`PCM` mixer state.

### Threading
The mixer handle is owned by the MIDI thread; ALSA control ioctls never run in
the JACK callback. The main loop polls the generation counter at its existing
100 ms tick, which coalesces a knob sweep into at most ten redraws — important
because a full 128x32 frame is 512 bytes over I2C.

### Why the knob writes the mixer on every event
An earlier draft skipped the ioctl when the new percentage equalled the cached
one. That is wrong: `alsactl`, `amixer` or the `wm8960-soundcard.service`
state restore can move the hardware behind our back, after which a knob
position that maps to the cached value silently does nothing. The cache now
only gates the display refresh.

### MPD218 controller numbers are preset-dependent
`volume_cc=3` matches the common factory mapping for K1 in bank A but is not
guaranteed. Verify with `aseqdump -p MPD218`. If a knob turns out to be an
endless encoder sending relative increments (small value up, near-127 value
down) rather than an absolute position, this absolute mapping will behave
badly — jumping to 100% when turned down — and a relative mode would be needed.

### Headphone jack and speakers are independent outputs
The WM8960 drives the 3.5 mm jack and the speaker terminals from separate
mixer controls, and the Waveshare driver does not auto-mute the speakers when
a plug is inserted. `volume_mixer_control=Speaker,Headphone` moves both from
one knob. A silent jack is normally a muted `Headphone` control or an output
mixer switch in the restored ALSA state, not a routing bug:

```bash
amixer -c wm8960soundcard sget 'Headphone'
amixer -c wm8960soundcard sset 'Headphone' 100% unmute
amixer -c wm8960soundcard sset 'Left Output Mixer PCM' on
amixer -c wm8960soundcard sset 'Right Output Mixer PCM' on
systemctl cat wm8960-soundcard.service   # find the state file it restores
sudo alsactl -f <state file> store wm8960soundcard
```

### Config keys must be appended on an existing device
`install.sh` keeps an existing `/etc/music-box/music-box.conf`, so the new
`volume_*` and `display_volume_seconds` keys are not written by a reinstall:

```bash
sudo tee -a /etc/music-box/music-box.conf <<'EOF'

display_volume_seconds=2
volume_control_enabled=true
volume_cc=3
volume_mixer_card=hw:wm8960soundcard
volume_mixer_control=Speaker,Headphone
EOF
sudo systemctl restart music-box.service
```

The parser rejects unknown keys, so a config written by a newer build is not
readable by an older binary; update the binary before the config.

### Not verified on hardware
Written without access to the test Pi. The ALSA-dependent translation unit was
type-checked against a stub header only; mixer control names, the knob's
actual CC number and the display layout all need a device check.

### There is no "louder than 100%" in ALSA
100% is the top of a control's raw register range, and
`music_box_volume_raw_for_percent(min, max, 100)` returns `max` exactly, so the
knob does reach the hardware maximum. Memories of pushing past 100% usually
come from PulseAudio, which applies software gain above unity and is not
present here (JACK owns the card exclusively). On the WM8960 the only
above-unity stage is the class-D boost, `Speaker DC` / `Speaker AC`, six steps
of roughly 1.0x to 1.7x, which no Music Box code touches.

### The knob stops working after alsamixer touches the same control
Reported on the device: raising the volume by hand in `alsamixer` left the
knob apparently dead. Two stale caches were involved, in two layers.

1. Ours: `handle_cc` skipped the ioctl when the new percentage matched the
   last one we had written. Removed; the cache now only gates the display.
2. alsa-lib's: the simple mixer keeps per-element values on the `snd_mixer_t`
   handle and returns success *without writing* when the requested value
   matches that cache. It is refreshed only by `snd_mixer_handle_events()`.
   Music Box holds one handle for the process lifetime, so an external
   `alsamixer` or `alsactl restore` desynchronised it permanently, and every
   knob position already written once became a silent no-op.

`music_box_volume_set_percent` now calls `snd_mixer_handle_events()` before
writing. This is why `amixer` never shows the problem: it opens a fresh handle
per invocation, so its cache is always freshly loaded.

### The knob curve: raw register vs decibels
`amixer -c wm8960soundcard sget 'Speaker'` on the test board reports
`Limits: Playback 0 - 127`, with raw 95 at -26.00 dB and raw 126 at +5.00 dB:
one decibel per step, 0 dB at raw 121, and everything below raw 48 muted.

Mapping the knob proportionally onto 0-127 therefore produced a knob whose
bottom 37% was silence and whose entire audible range lived in the last tenth
of the travel. That, not a code fault, is why the maximum felt low and the
knob felt unresponsive.

| Knob | Raw mapping | dB curve |
|---|---|---|
| 20% | muted | -31 dB |
| 50% | -57 dB | -11 dB |
| 80% | -19 dB | +0.5 dB |

`volume_db_curve=true` (default) now maps through
`snd_mixer_selem_set_playback_dB_all` using the normalized curve from
alsa-utils: proportional for ranges narrower than 24 dB, otherwise
`6000 * log10(normalized) + dB_max`. A driver reporting a mute sentinel for its
floor is clamped to a 100 dB window so the curve cannot degenerate.

### Other gain stages on this card
`scontrols` also lists `Playback` (the DAC digital volume), the
`PCM Playback -6dB` switch, and `Left/Right Output Mixer PCM`. All sit ahead of
`Speaker`/`Headphone` and none are touched by the knob. Check them before
concluding the knob is at fault.

### Mixer inventory of the test board (2026-08-29)
`cat /proc/asound/cards` before disabling the onboard devices:

```text
 0 [Headphones     ]: bcm2835_headpho - bcm2835 Headphones
 1 [wm8960soundcard]: simple-card - wm8960-soundcard
 2 [MPD218         ]: USB-Audio - MPD218
 3 [vc4hdmi        ]: vc4-hdmi - vc4-hdmi
```

The MPD218 registers as a USB audio card as well as a MIDI device. Nothing in
Music Box uses card indices, but four cards make `aplay`/`amixer` output harder
to read, which is reason enough to disable the onboard pair.

Playback controls with a volume on this driver revision: `Headphone`,
`Speaker`, `Speaker AC`, `Speaker DC`, `Playback` (DAC), `Capture`, `ADC PCM`.
Relevant switches: `PCM Playback -6dB`, `Left/Right Output Mixer PCM`,
`Headphone/Speaker Playback ZC`. So `volume_mixer_control=Speaker,Headphone`
resolves on this board, and the startup banner should read
`'Speaker+Headphone'`.

### The two outputs can drift far apart
As found, `Headphone` sat at raw 126 (+5 dB) while `Speaker` sat at raw 95
(-26 dB): a 31 dB gap, because only `Speaker` had been under knob control and
`Headphone` kept whatever the stored ALSA state held. Driving both from one
knob synchronises them on the first move, so the speakers jump loudly if the
knob is parked high. Start the knob low after this change.

### Sequence of volume defects found on hardware, in order
1. Only `Speaker` was driven, so the 3.5 mm jack ignored the knob entirely.
2. Our own percentage cache skipped writes, so a knob position already used
   once became a no-op.
3. alsa-lib's per-element cache did the same thing one layer lower, which is
   what made the knob look permanently dead after `alsamixer` touched the
   control; fixed with `snd_mixer_handle_events()`.
4. The proportional raw mapping put 37% of the travel in the mute zone and 0 dB
   at 95% of travel, which is what made the maximum feel low.

Only the fourth was a design error rather than a bug; together they explain
every symptom reported from the device.

---

## 2026-08-30 — Loop and sampler modes

### Goal
Add a second performance mode. Loop mode keeps today's behaviour. Sampler mode
turns every pad into a one-shot recorder: a long press records, a tap plays the
result once, a further long press replaces it. Knob 2 selects the mode, and the
display shows the mode, what each pad holds, and a compact beat footer.

### What was added
- `src/rt_queue.h` — `MusicBoxMode`, the `PAD_HOLD` and `SET_MODE` commands, the
  `SAMPLE_STARTED` / `SAMPLE_FINISHED` / `MODE_CHANGED` events, and a `bank`
  field on every event so a consumer can tell the two banks apart.
- `src/loop_bank.c` — the bank carries a mode. A sampler bank arms on the hold
  instead of the press, closes a recording into `STOPPED` instead of `PLAYING`,
  and plays through the new `ONE_SHOT` state, which stops at the stored length
  rather than wrapping.
- `src/audio_engine.c` — a second `MusicBoxLoopBank`. Both banks apply
  boundaries, mix and record on every frame; only pad routing depends on the
  mode.
- `src/midi.c` — pad hold timing and the mode controller.
- `src/display.c` — new layout, described below.
- `src/config.c` — `mode_control_enabled`, `mode_cc`, `max_sample_seconds`,
  `pad_hold_milliseconds`.
- `tests/test_loop_bank.c` — split into a loop case and a sampler case.

### Why two banks instead of one
The requirement that loops keep playing after a switch to sampler mode rules out
reinterpreting one set of buffers. With a single bank, a pad that is both a
running loop and a sampler pad has no unambiguous meaning for a short press. Two
banks of four slots cost four more stereo buffers and remove the ambiguity
entirely: `max_sample_seconds` defaults to 10 rather than the loop's 60 to keep
that cost near 15 MB at 48 kHz.

### Why the hold is timed on the MIDI thread
Sampler mode needs the recording to be armed *while the pad is still down*, as
in loop mode, so it cannot wait for the Note Off. The MIDI thread already wakes
every 5 ms, so it times the hold and emits one extra `PAD_HOLD` command. This
keeps wall-clock logic out of the JACK callback; the recording it arms still
starts on an exact transport boundary, so the 5 ms poll does not affect timing.

### Mode changes travel through the command queue
The mode is applied in the audio callback, not read from an atomic by it. Both
the mode and the pad gestures then pass through one queue and cannot be
reordered against each other. The engine also remembers which bank each pad was
pressed on and routes that pad's hold and release to the same bank, so turning
the knob mid-gesture cannot leave a slot stuck in `RECORDING`.

### The mode knob is absolute
Like the volume knob, and with the same consequence: the mode follows the knob
only once it is moved, so the service always starts in loop mode no matter where
knob 2 is parked. Thresholds are 72 and up for sampler, below 56 for loop, with
the gap between them holding the current mode so a knob resting near the middle
cannot flap. `mode_cc=9` matches the common factory mapping for K2 in bank A;
verify with `aseqdump -p MPD218`, and keep it distinct from `volume_cc`.

### Display layout, 128x32
```text
rows 0-6    mode name          tempo, or REC / ARMED
rows 9-23   [ 1 ] [ 2 ] [ 3 ] [ 4 ]     one box per pad
rows 27-31  [===] [   ] [   ] [   ]     beat footer, every screen
```
Columns are 32 pixels apart, so each pad box sits directly above its beat bar.
A pad box is bare when empty, outlined when it holds audio, double-outlined
while recording, and inverted while sounding. The inversion is an XOR pass over
the box after the digit is drawn, which is why `invert_rectangle` exists: the
1-bit framebuffer only ORs pixels otherwise. The four beat circles and the note
glyph are gone; the volume bar lost four rows to make room for the footer.

Rendering was checked by compiling `display.c` into a throwaway harness that
dumps the framebuffer as ASCII. Worth repeating for any layout change — it
catches overlap and clipping without the hardware.

### Config on an existing device
Unlike the volume session, nothing has to be appended for the feature to work:
the four new keys all have defaults, and a config written before this change is
still accepted. `install.sh` keeps the existing
`/etc/music-box/music-box.conf`, so append these only to tune them:

```bash
sudo tee -a /etc/music-box/music-box.conf <<'EOF'

mode_control_enabled=true
mode_cc=9
max_sample_seconds=10
pad_hold_milliseconds=400
EOF
sudo systemctl restart music-box.service
```

The usual caveat still applies in the other direction: the parser rejects
unknown keys, so once these are written the binary has to be updated before the
config.

### Not verified on hardware
Written without access to the test Pi. Knob 2's actual controller number, the
readability of the boxes at 128x32, and whether 400 ms is a comfortable hold
threshold all need a device check.
