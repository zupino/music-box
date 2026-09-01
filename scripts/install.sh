#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MUSIC_BOX_USER=${SUDO_USER:-}
SOUNDFONT_PATH=""
INSTALL_PACKAGES=1
MIGRATE_LEGACY=0
START_SERVICES=1

usage() {
    cat <<USAGE
Usage: sudo ./scripts/install.sh [options]

Options:
  --user USER             Service account; defaults to the sudo caller
  --soundfont PATH        Existing .sf2/.sf3 file
  --skip-packages         Do not install Debian packages
  --migrate-legacy        Disable jack-audio, fluidsynth-mpd, mpd-looper and
                          mpd-connect before enabling the new services
  --no-start              Install and enable services without starting them
  -h, --help              Show this help
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --user) MUSIC_BOX_USER=${2:?missing user name}; shift 2 ;;
        --soundfont) SOUNDFONT_PATH=${2:?missing SoundFont path}; shift 2 ;;
        --skip-packages) INSTALL_PACKAGES=0; shift ;;
        --migrate-legacy) MIGRATE_LEGACY=1; shift ;;
        --no-start) START_SERVICES=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this installer with sudo or as root." >&2
    exit 1
fi

if [ -z "$MUSIC_BOX_USER" ]; then
    if id pi >/dev/null 2>&1; then
        MUSIC_BOX_USER=pi
    else
        MUSIC_BOX_USER=$(awk -F: '$3 >= 1000 && $3 < 65534 {print $1; exit}' /etc/passwd)
    fi
fi

if [ -z "$MUSIC_BOX_USER" ] || ! id "$MUSIC_BOX_USER" >/dev/null 2>&1; then
    echo "Cannot determine service user; pass --user USER." >&2
    exit 1
fi

MUSIC_BOX_HOME=$(getent passwd "$MUSIC_BOX_USER" | cut -d: -f6)
if [ -z "$MUSIC_BOX_HOME" ]; then
    echo "Cannot determine home directory for $MUSIC_BOX_USER" >&2
    exit 1
fi

if [ "$INSTALL_PACKAGES" -eq 1 ]; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y build-essential libjack-jackd2-dev libasound2-dev jackd2 fluidsynth alsa-utils git
    # The SoundFont is external data and is never copied into this repository.
    if apt-cache show fluid-soundfont-gm >/dev/null 2>&1; then
        apt-get install -y fluid-soundfont-gm
    fi
fi

make -C "$ROOT_DIR" clean all
install -m 0755 "$ROOT_DIR/music-box" /usr/local/bin/music-box
install -d -m 0755 /usr/local/libexec/music-box
install -m 0755 "$ROOT_DIR/scripts/wait-for-wm8960" /usr/local/libexec/music-box/wait-for-wm8960
install -m 0755 "$ROOT_DIR/scripts/start-fluidsynth" /usr/local/libexec/music-box/start-fluidsynth
install -m 0755 "$ROOT_DIR/scripts/check-system.sh" /usr/local/bin/music-box-check

install -d -m 0755 /etc/music-box
install -d -o "$MUSIC_BOX_USER" -g audio -m 0755 /var/lib/music-box/session
if [ ! -e /etc/music-box/music-box.conf ]; then
    install -m 0644 "$ROOT_DIR/config/music-box.conf" /etc/music-box/music-box.conf
else
    echo "Keeping existing /etc/music-box/music-box.conf"
    # Correct the original shipped MPD218 bank-B ordering. Preserve every
    # custom mapping; only the exact old default is migrated in place.
    sed -i \
        's/^loop_notes=52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67$/loop_notes=64,65,66,67,60,61,62,63,56,57,58,59,52,53,54,55/' \
        /etc/music-box/music-box.conf
fi

if [ -n "$SOUNDFONT_PATH" ]; then
    if [ ! -r "$SOUNDFONT_PATH" ]; then
        echo "SoundFont is not readable: $SOUNDFONT_PATH" >&2
        exit 1
    fi
else
    for candidate in /usr/share/sounds/sf2/FluidR3_GM.sf2 /usr/share/sounds/sf2/FluidR3_GS.sf2 /usr/share/sounds/sf3/default-GM.sf3; do
        if [ -r "$candidate" ]; then
            SOUNDFONT_PATH=$candidate
            break
        fi
    done
fi

if [ -z "$SOUNDFONT_PATH" ]; then
    echo "No SoundFont found. Re-run with --soundfont /path/to/file.sf2" >&2
    exit 1
fi

case "$SOUNDFONT_PATH" in
    *$'\n'*|*$'\r'*) echo "SoundFont path must not contain a newline" >&2; exit 1 ;;
esac
escaped_soundfont=${SOUNDFONT_PATH//\\/\\\\}
escaped_soundfont=${escaped_soundfont//\"/\\\"}
printf 'MUSIC_BOX_SOUNDFONT="%s"\nFLUIDSYNTH_GAIN=0.70\n' "$escaped_soundfont" > /etc/music-box/music-box.env
chmod 0644 /etc/music-box/music-box.env

install -d -m 0755 /etc/security/limits.d
cat > /etc/security/limits.d/95-music-box.conf <<'LIMITS'
# Music Box real-time audio permissions
@audio - rtprio 95
@audio - memlock unlimited
LIMITS

usermod -a -G audio "$MUSIC_BOX_USER"
if getent group i2c > /dev/null 2>&1; then
    usermod -a -G i2c "$MUSIC_BOX_USER"
fi

install_service() {
    local source=$1
    local destination=$2
    sed -e "s|@MUSIC_BOX_USER@|$MUSIC_BOX_USER|g" -e "s|@MUSIC_BOX_HOME@|$MUSIC_BOX_HOME|g" "$source" > "$destination"
    chmod 0644 "$destination"
}

install_service "$ROOT_DIR/systemd/music-box-jack.service.in" /etc/systemd/system/music-box-jack.service
install_service "$ROOT_DIR/systemd/music-box-fluidsynth.service.in" /etc/systemd/system/music-box-fluidsynth.service
install_service "$ROOT_DIR/systemd/music-box.service.in" /etc/systemd/system/music-box.service

if [ "$MIGRATE_LEGACY" -eq 1 ]; then
    for unit in jack-audio fluidsynth-mpd mpd-looper mpd-connect; do
        systemctl disable --now "$unit.service" 2>/dev/null || true
    done
fi

systemctl daemon-reload
systemctl enable music-box-jack.service music-box-fluidsynth.service music-box.service

if [ "$START_SERVICES" -eq 1 ]; then
    if grep -q 'wm8960soundcard' /proc/asound/cards 2>/dev/null; then
        systemctl restart music-box-jack.service
        systemctl restart music-box-fluidsynth.service
        systemctl restart music-box.service
    else
        echo "WM8960 is not visible; services were enabled but not started."
        echo "Install/repair the driver, reboot, then start music-box.service."
    fi
fi

cat <<SUMMARY

Music Box installed for user: $MUSIC_BOX_USER
Configuration: /etc/music-box/music-box.conf
SoundFont: $SOUNDFONT_PATH

The application now owns JACK/MIDI routing; do not run the old mpd-connect
service at the same time. Re-login or reboot after the first installation so
manual real-time audio commands inherit the audio-group membership.
SUMMARY
