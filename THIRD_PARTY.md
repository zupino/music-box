# Third-party software and data

Music Box itself is MIT licensed. This repository does **not** contain source
or binary copies of the components below.

## Waveshare WM8960 Audio HAT driver

- Upstream: `waveshareteam/WM8960-Audio-HAT` on GitHub
- Upstream license: GPL-3.0
- Relationship: external kernel/DKMS driver installed only when required

`scripts/install-wm8960-driver.sh` clones the upstream repository and executes
its installer after the user passes `--accept-third-party`. The script prints
the checked-out commit and the upstream license path. The fetched driver
remains governed by its upstream GPL license and is not relicensed as MIT.

The helper chooses Waveshare's documented `rpi-4.9.y` branch for kernels older
than 5.0 and the current branch for newer kernels. A specific revision can be
selected with `--ref` or `WAVESHARE_REF`.

## FluidSynth

FluidSynth is installed from the operating system package repository and runs
as a separate process. It is not linked into the Music Box binary. FluidSynth
retains its upstream LGPL license.

## JACK and ALSA

The Music Box binary dynamically links to the system JACK and ALSA client
libraries. They are installed from Debian/Raspberry Pi OS/Ubuntu packages and
retain their respective upstream licenses.

## SoundFonts

No SoundFont is committed to this repository. The installer either finds a
system package's SoundFont or accepts a user-supplied `.sf2`/`.sf3` path.
SoundFonts have independent licenses; users and redistributors must verify the
terms of the particular file they choose.

## Generated or copied material

Do not copy the external WM8960 driver or a SoundFont into this repository
without preserving its license and clearly separating it from the MIT-licensed
project files.
