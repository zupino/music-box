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
