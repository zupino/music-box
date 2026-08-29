#!/bin/sh
# SPDX-License-Identifier: MIT
set -u

printf '%s\n' '== OS and kernel =='
if [ -r /etc/os-release ]; then
    grep -E '^(PRETTY_NAME|ID|VERSION_ID)=' /etc/os-release
fi
uname -a

printf '\n%s\n' '== SoundFont configuration =='
if [ -r /etc/music-box/music-box.env ]; then
    cat /etc/music-box/music-box.env
else
    printf '%s\n' '/etc/music-box/music-box.env is missing or unreadable'
fi

printf '\n%s\n' '== ALSA cards =='
cat /proc/asound/cards 2>/dev/null || true
aplay -l 2>&1 || true
arecord -l 2>&1 || true

printf '\n%s\n' '== Onboard Raspberry Pi audio (expected absent) =='
if lsmod 2>/dev/null | grep -q '^snd_bcm2835'; then
    printf '%s\n' 'snd_bcm2835 is loaded; onboard audio is still enabled'
else
    printf '%s\n' 'snd_bcm2835 is not loaded'
fi
for boot_config in /boot/firmware/config.txt /boot/config.txt; do
    if [ -r "$boot_config" ]; then
        printf '%s:\n' "$boot_config"
        grep -nE '^[[:space:]]*(dtparam=audio|dtoverlay=vc4)' "$boot_config" ||
            printf '%s\n' '  no dtparam=audio or vc4 overlay line'
    fi
done

printf '\n%s\n' '== WM8960 playback mixer =='
for control in Speaker Headphone PCM; do
    amixer -c wm8960soundcard sget "$control" 2>&1 || true
done

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
