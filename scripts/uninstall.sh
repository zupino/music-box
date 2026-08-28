#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

PURGE=0
REMOVE_PACKAGES=0
REMOVE_DRIVER_SOURCE=0
REMOVE_AUDIO_USER=""

usage() {
    cat <<'USAGE'
Usage: sudo ./scripts/uninstall.sh [options]

By default, remove Music Box programs, services and real-time limits while
preserving configuration, shared Debian packages and the external WM8960
driver.

Options:
  --purge                       Also remove /etc/music-box
  --remove-packages             Remove Debian packages requested by install.sh
  --remove-driver-source        Remove /usr/local/src/WM8960-Audio-HAT only
  --remove-audio-group USER     Remove USER from the audio supplementary group
  -h, --help                    Show this help

Package and audio-group removal are opt-in because those resources may have
existed before Music Box or may be used by other audio software. Removing the
driver source does not uninstall the active third-party kernel driver.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --purge) PURGE=1; shift ;;
        --remove-packages) REMOVE_PACKAGES=1; shift ;;
        --remove-driver-source) REMOVE_DRIVER_SOURCE=1; shift ;;
        --remove-audio-group)
            REMOVE_AUDIO_USER=${2:?missing user name}
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

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
rmdir /usr/local/libexec 2>/dev/null || true

if [ "$PURGE" -eq 1 ]; then
    rm -rf /etc/music-box
else
    echo "Keeping /etc/music-box; use --purge to remove configuration."
fi

if [ "$REMOVE_DRIVER_SOURCE" -eq 1 ]; then
    rm -rf /usr/local/src/WM8960-Audio-HAT
fi

if [ -n "$REMOVE_AUDIO_USER" ]; then
    if ! id "$REMOVE_AUDIO_USER" >/dev/null 2>&1; then
        echo "User does not exist: $REMOVE_AUDIO_USER" >&2
        exit 1
    fi
    gpasswd -d "$REMOVE_AUDIO_USER" audio >/dev/null 2>&1 || true
fi

if [ "$REMOVE_PACKAGES" -eq 1 ]; then
    export DEBIAN_FRONTEND=noninteractive
    installed_packages=()
    for package in build-essential libjack-jackd2-dev libasound2-dev jackd2 \
        fluidsynth alsa-utils git fluid-soundfont-gm; do
        if dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null | \
                grep -qx 'installed'; then
            installed_packages+=("$package")
        fi
    done
    if [ "${#installed_packages[@]}" -gt 0 ]; then
        apt-get remove -y "${installed_packages[@]}"
    else
        echo "None of the packages requested by install.sh are installed."
    fi
    echo "Package cleanup complete. Review 'sudo apt autoremove' separately before running it."
fi

systemctl daemon-reload
systemctl reset-failed music-box.service music-box-fluidsynth.service \
    music-box-jack.service 2>/dev/null || true

cat <<SUMMARY

Music Box application files and services removed.
Configuration: $([ "$PURGE" -eq 1 ] && echo removed || echo preserved)
Debian packages: $([ "$REMOVE_PACKAGES" -eq 1 ] && echo removed || echo preserved)
WM8960 driver: preserved (third-party; required for continued HAT use)
SUMMARY
