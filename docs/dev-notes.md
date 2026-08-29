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
