# Development Notes

## 2026-08-31 — Exact-length, beat-anchored assigned loops

The first sample-to-loop implementation deliberately separates phase anchoring
from loop duration. A track starts on the next shared beat, but then wraps at
the selected sample's exact trimmed length. A 0.8-second sample therefore
repeats every 0.8 seconds; it is neither padded nor stretched to the transport.

Later design topics include beat/bar padding, fixed musical lengths,
time-stretching, transient-aware slicing, wrap crossfades, and whether
reassignment should snapshot audio or retain a live reference. Time stretching
will need a Pi 3 CPU budget and must remain allocation-free in the JACK callback.

CTRL BANK and NR CONFIG are controller-local buttons in common MPD218 presets
and may not produce ALSA events. Their CC mappings are configurable but disabled
by default. Capture actual events with `aseqdump -p MPD218`; if none appear, an
editor preset or alternate physical controls will be required.

---

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
display shows the mode, what each pad holds, and a compact beat indication.

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

(This layout was replaced twice as the pad count grew; see below.)

Rendering was checked by compiling `display.c` into a throwaway harness that
dumps the framebuffer as ASCII. Worth repeating for any layout change — it
catches overlap and clipping without the hardware.

### Sampler recording is never quantized
First correction after the feature was written, before any device test. The
sampler bank had inherited the configured `quantize`, which is wrong for what a
sample is: rounding a spoken word or a hit up to a whole bar defeats the point.
A sampler bank is now always built with `MUSIC_BOX_QUANTIZE_OFF`, so it reuses
the state machine's existing unquantized paths and never reaches
`ARMED_RECORD` or `ARMED_FINISH`. Loop mode still follows `quantize`.

That exposed a gap the quantized version had hidden. A sample has to run from
the press, but a press cannot start a recording, because a short press is how a
sample is triggered. The gesture is only known to be a recording once the pad
has been held for `pad_hold_milliseconds`, and by then the first 400 ms of the
take have gone past. Losing 400 ms of head removes the attack of most things
worth sampling, so it is not an acceptable rounding error.

The fix is a pre-roll: a ring of the last `pad_hold_milliseconds` of cleaned
input, per sampler bank, written on every frame and copied into the slot as the
head of the take when recording starts. Cost is one bounded `memcpy` per
recording inside the audio callback, about 154 kB at 48 kHz for the 400 ms
default. At a 256-frame period that is roughly 3% of one JACK cycle, once,
which is acceptable; a per-slot capture buffer with a pointer swap would avoid
even that, at the price of doubling sampler memory.

The pre-roll length is the configured hold rather than the true press-to-hold
interval, which the MIDI thread knows exactly. The difference is bounded by the
5 ms poll, so the head can be up to 5 ms short. Not worth a wider command
struct.

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

Sixteen pads do need a config edit, though. A device still carrying
`loop_notes=52,53,54,55` starts fine and keeps working, but only its bottom row
is live and the display draws only that row. To light up the whole grid, replace
the line rather than appending to it:

```bash
sudo sed -i 's/^loop_notes=.*/loop_notes=52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67/' \
    /etc/music-box/music-box.conf
sudo systemctl restart music-box.service
```

Check `free -m` afterwards: sixteen slots per bank reserve 246 MB at the shipped
lengths. Add `mode_cc_hysteresis=2` at the same time to get the lighter mode
switch; without it the built-in default already applies.

### Storage model: nothing is written to disk
Worth stating plainly, because "sampler" suggests files and there are none.

**Where samples live.** In RAM only, inside the `music-box` process. There is no
sample directory, no file format, no naming scheme and no `write()` anywhere in
the audio path. A sample is a pair of `float` arrays in a `MusicBoxLoopSlot`,
addressed by slot index alone. The `*.wav` line in `.gitignore` is precautionary
and matches nothing the program produces.

**Lifetime.** Loops and samples are lost on restart, on a JACK shutdown, and on
`systemctl restart music-box.service`. Recording over a slot frees nothing and
allocates nothing; it overwrites the buffer that was allocated at startup.

**How many fit.** Exactly sixteen per bank, always. Thirty-two recordings total,
sixteen loops and sixteen samples. The count is fixed by `MUSIC_BOX_LOOP_COUNT`
and the pad grid, not by available memory, so nothing runs out and nothing is
evicted: a seventeenth recording is not a capacity problem, it is simply not
expressible.

**What a second costs.** Stereo 32-bit float at the JACK sample rate:

    sample_rate x 2 channels x 4 bytes

| Rate | Per second | Per minute |
|---|---|---|
| 44.1 kHz | 352.8 kB | 21.2 MB |
| 48 kHz | 384 kB | 23.0 MB |

**What is actually allocated.** All of it at startup, up front, whether or not
anything is ever recorded, because the audio callback cannot allocate. Each
bank reserves its maximum length on all four slots:

    loops    16 x max_loop_seconds          x 384 kB/s
    samples  16 x max_sample_seconds        x 384 kB/s
    pre-roll  1 x pad_hold_milliseconds/1000 x 384 kB/s

With the shipped config at 48 kHz (`max_loop_seconds=30`,
`max_sample_seconds=10`, `pad_hold_milliseconds=400`):

| Bank | Reserved |
|---|---|
| Loops | 184.3 MB |
| Samples | 61.4 MB |
| Pre-roll | 0.15 MB |
| Total | 245.9 MB |

The startup banner prints this total, because with sixteen slots per bank it is
no longer a number anyone will work out by eye. `config.c` used to default
`max_loop_seconds` to 60 while the shipped config set 30; at four slots that was
a 46 MB discrepancy, at sixteen it is 184 MB, so the built-in default was
lowered to 30 to match. `max_loop_seconds=600` with `max_sample_seconds=600`,
both of which the parser still accepts, would ask for 7.4 GB and fail at
startup.

**Disk usage of the installation itself** is unrelated and static: the binary,
three unit files, the config and the helper scripts. The only large file on a
Music Box system is the SoundFont, which belongs to FluidSynth, is chosen at
install time and is never written by this program.

Persisting samples across a restart is deferred; see `roadmap.md`. It is the
one place a real filesystem model would have to be designed, and the open
questions are the format, where they live relative to `/etc/music-box`, whether
a slot maps to a fixed filename, and what happens when a stored sample is longer
than the current `max_sample_seconds`.

### Second round of device findings

Four items reported after the first build was tried on the box.

**All sixteen pads, not four.** `MUSIC_BOX_LOOP_COUNT` is now 16 and
`loop_notes` defaults to the full bank B, notes 52-67, listed in printed pad order
so slot order matches the physical grid. `parse_loop_notes` now accepts one to
sixteen notes instead of demanding exactly the slot count, and fills the rest
with `MUSIC_BOX_NOTE_UNMAPPED`. That matters more than it looks: without it,
every device carrying the old `loop_notes=52,53,54,55` would have failed to
parse its configuration and the service would not have started after the
update.

The cost is memory, and it is not small. Sixteen slots in two banks at the
shipped lengths reserve 246 MB, up from 62 MB, all of it at startup. See the
storage model above; the startup banner now prints the figure.

**Pending stop needed its own indication.** Pressing a playing loop arms a stop
that only takes effect at the next boundary, and the loop rightly keeps sounding
until then, but the screen gave no sign the press had registered. Arming now
hatches the cell: still sounding, on the way out. This also exposed a missing
event — cancelling a pending stop with a second press changed the slot state
silently, so the display would have shown a loop as leaving forever.
`MUSIC_BOX_EVENT_LOOP_STOP_CANCELLED` closes that.

**The mode knob asked for too much rotation.** The dead band was hard-coded at
56 to 72, sixteen counts out of 127, about 34 degrees of an MPD218 pot before
anything happened. It is now `mode_cc_hysteresis` either side of the midpoint,
default 2, so the switch costs about 8 degrees. Nothing can make an absolute pot
flip from any position — the switch is inherently at mid-travel — but the wiggle
around that point is now small.

**The sampler recorded its own preview.** Reported as noise at the head of a
re-recorded sample. The cause is the pre-roll meeting the trigger gesture: a tap
on a loaded pad previews it, the hold that turns the gesture into a recording
only arrives 400 ms later, and the pre-roll ring spent exactly those 400 ms
capturing the outgoing sample coming back through the microphones. The echo
canceller reduces that bleed but does not remove it, so the residue was being
laid down as the head of the replacement.

The ring now only retains frames captured while the bank was silent;
`mix_sample` sets a `sounding` flag and `push_preroll` discards its history
whenever that flag is set. Frames from before the press are discarded too, since
they predate the gesture. The consequence is that re-recording over a sounding
pad starts at the hold and loses that head, while a first recording on a silent
pad still spans the full press. That asymmetry is the right way round: during
the preview the performer is listening, not playing.

The same fix covers a case not reported yet — one sampler pad playing a long
sample while another is held to record would have poisoned that pad's head too.

### Display: stop stacking, start aligning
Four numbered boxes became a 4x4 grid, which leaves no room for a digit. That is
fine, and arguably better: the grid has the same shape as the controller, so
position identifies the pad. Slot 0 is now drawn top-left, matching the printed
PAD 1 position; the factory preset's MIDI note rows run in the opposite order.

The first attempt stacked the grid under a full-width header, and it was too
tight: header, four cell rows and a footer do not fit in 32 rows with any room
to breathe, and cells ended up 4 pixels tall with the header touching them.

The fix was to stop fighting the aspect ratio. A 128x32 panel is wide and short,
so nothing is stacked any more: the grid, the status text and the beat run as
three columns. Dropping the bottom strip is what pays for the cells, because the
grid then owns the panel's full height rather than sharing it with a header and
a footer.

The beat became a rail of four dots down the right edge, small when idle and a
filled square on the current beat. It costs 5 pixels of width, which this panel
has to spare, instead of 5 rows of height, which it does not. Cells went from
28x4 through 16x6 to 14x7: narrower each time, and taller each time.

Then everything went onto one rhythm, which is what made it look deliberate
rather than merely roomier:

- the beat dots took the grid's row pitch and sit centred on its rows;
- the three text lines were spread over exactly the grid's height, flush with
  its top and bottom row;
- every gutter and every outer margin is the same 3 pixels;
- the volume overlay's bar picked up the same margins and stops short of the
  rail.

Every one of those constants is derived from the cell size and gap rather than
written out, so the rhythm cannot drift when a dimension changes.

Six cell states fit into seven pixel rows as blank, baseline, outline, solid,
hatched and blinking. Blinking is the screen's only self-driven cadence; it runs
from `music_box_display_tick` and only while a pad is armed or recording, so an
idle Music Box still redraws only on events. `invert_rectangle` went away with
the numbered boxes.

The ASCII framebuffer harness earned its keep again here — the grid, the legacy
four-note layout and both blink phases were all checked that way before any of
it reached the device.

### Pad mode

The sixteen-slot change above was asked for as a sampler-only widening, and
turning it into a third mode is the better answer: the looper keeps its sixteen
slots, and playing the SoundFont from the pads becomes its own mode rather than
a special case inside the other two.

Pad mode owns no bank, which is what makes it cheap. The MIDI thread stops
consuming pad notes and forwards them to FluidSynth like any other note, with
velocity intact. Nothing is recorded and no buffer is reserved, so the mode adds
no memory at all.

**Why the pad command is still queued.** The display wants to light a cell while
its pad is down, and the MIDI thread cannot write the event queue: it is a
single-producer ring owned by the audio thread. So the pad command still goes
through the command queue, the audio thread recognises the pad-mode target and
emits nothing but a `SLOT_STARTED` or `SLOT_FINISHED` event for the screen. The
note itself never touches the audio thread. That keeps the SPSC invariant that
the rest of the design rests on.

`SAMPLE_STARTED` and `SAMPLE_FINISHED` were renamed to `SLOT_STARTED` and
`SLOT_FINISHED` at the same time; they now mean "a slot began or stopped
sounding", which covers a sampler one-shot and a held pad-mode note equally.

**Forwarding has the same mid-gesture hazard as the banks.** If the knob moves
from pad mode to loop mode while a pad is down, the Note Off would not be
forwarded and FluidSynth would hold the note forever. `pad_forwarded` records
which pads had their Note On sent to the synthesizer, and the Note Off follows
it, exactly as `pad_bank` does for the recording banks.

**Three zones on an absolute pot.** The knob splits into thirds: 0-42 loop,
43-84 sampler, 85-127 pad, with `mode_cc_hysteresis` either side of each switch.
A switch point itself belongs to the outer zone, so with the dead band off the
zones are 43, 42 and 43 counts wide. The parser now caps the dead band at 16,
because anything wider than half a zone would close the middle one and make
sampler mode unreachable — the sort of configuration that would look like a
hardware fault.

`mode_for_knob` moved out of `midi.c` into `config.c` as
`music_box_mode_for_knob` to get it under test: it is mapping policy, not MIDI
plumbing, and `config.c` has no ALSA dependency so the test binary needs no
hardware. The test sweeps all 128 positions.

### Not verified on hardware
Written without access to the test Pi. Knob 2's actual controller number, the
readability of the boxes at 128x32, whether 400 ms is a comfortable hold
threshold, and whether the pre-roll head sounds right in a live take all need a
device check.
