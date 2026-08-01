#!/bin/sh
# SPDX-License-Identifier: MIT
set -u

printf '%s\n' '== OS and kernel =='
if [ -r /etc/os-release ]; then
    grep -E '^(PRETTY_NAME|ID|VERSION_ID)=' /etc/os-release
fi
uname -a

printf '\n%s\n' '== ALSA cards =='
cat /proc/asound/cards 2>/dev/null || true
aplay -l 2>&1 || true
arecord -l 2>&1 || true

printf '\n%s\n' '== Services =='
systemctl --no-pager --full status \
    wm8960-soundcard.service \
    music-box-jack.service \
    music-box-fluidsynth.service \
    music-box.service 2>&1 || true

printf '\n%s\n' '== JACK ports and connections =='
jack_lsp -c 2>&1 || true

printf '\n%s\n' '== ALSA sequencer connections =='
aconnect -l 2>&1 || true
