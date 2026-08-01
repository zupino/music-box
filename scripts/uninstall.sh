#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

PURGE=0
if [ "${1:-}" = "--purge" ]; then
    PURGE=1
elif [ "$#" -ne 0 ]; then
    echo "Usage: sudo ./scripts/uninstall.sh [--purge]" >&2
    exit 2
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this script with sudo or as root." >&2
    exit 1
fi

systemctl disable --now \
    music-box.service \
    music-box-fluidsynth.service \
    music-box-jack.service 2>/dev/null || true

rm -f \
    /etc/systemd/system/music-box.service \
    /etc/systemd/system/music-box-fluidsynth.service \
    /etc/systemd/system/music-box-jack.service \
    /usr/local/bin/music-box \
    /usr/local/bin/music-box-check \
    /usr/local/libexec/music-box/start-fluidsynth \
    /usr/local/libexec/music-box/wait-for-wm8960 \
    /etc/security/limits.d/95-music-box.conf
rmdir /usr/local/libexec/music-box 2>/dev/null || true

if [ "$PURGE" -eq 1 ]; then
    rm -rf /etc/music-box
else
    echo "Keeping /etc/music-box; use --purge to remove configuration."
fi

systemctl daemon-reload
