#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

ACCEPT=0
WAVESHARE_REF=${WAVESHARE_REF:-}
SOURCE_DIR=${SOURCE_DIR:-/usr/local/src/WM8960-Audio-HAT}
REPOSITORY=${WAVESHARE_REPOSITORY:-https://github.com/waveshareteam/WM8960-Audio-HAT.git}

usage() {
    cat <<USAGE
Usage: sudo ./scripts/install-wm8960-driver.sh --accept-third-party [options]

This script downloads and executes Waveshare's external GPL-3.0 driver
installer. No Waveshare source code is included in the Music Box repository.

Options:
  --accept-third-party    Required acknowledgement
  --ref GIT_REF           Optional Waveshare tag/commit/branch
  --source-dir PATH       Clone location (default: $SOURCE_DIR)
  -h, --help              Show this help
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --accept-third-party) ACCEPT=1; shift ;;
        --ref) WAVESHARE_REF=${2:?missing git reference}; shift 2 ;;
        --source-dir) SOURCE_DIR=${2:?missing source directory}; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this script with sudo or as root." >&2
    exit 1
fi
if [ "$ACCEPT" -ne 1 ]; then
    usage >&2
    exit 2
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y git dkms build-essential

if [ ! -e "/lib/modules/$(uname -r)/build" ]; then
    if apt-cache show "linux-headers-$(uname -r)" >/dev/null 2>&1; then
        apt-get install -y "linux-headers-$(uname -r)"
    elif apt-cache show raspberrypi-kernel-headers >/dev/null 2>&1; then
        apt-get install -y raspberrypi-kernel-headers
    else
        echo "No matching kernel-header package was found for $(uname -r)." >&2
        echo "Install the active kernel's headers, then retry." >&2
        exit 1
    fi
fi

if [ ! -e "/lib/modules/$(uname -r)/build" ]; then
    echo "Headers still do not match the running kernel $(uname -r)." >&2
    echo "A kernel/header upgrade may require a reboot before retrying." >&2
    exit 1
fi

kernel_major=$(uname -r | cut -d. -f1)
if [ "$kernel_major" -lt 5 ] && [ -z "$WAVESHARE_REF" ]; then
    # Waveshare documents this branch for pre-5.0 Raspberry Pi kernels.
    WAVESHARE_REF=rpi-4.9.y
fi

install -d "$(dirname "$SOURCE_DIR")"
rm -rf "$SOURCE_DIR"
git clone --depth 1 "$REPOSITORY" "$SOURCE_DIR"
cd "$SOURCE_DIR"

if [ -n "$WAVESHARE_REF" ]; then
    # Accept a branch, tag or exact commit without assuming it is the default.
    git fetch --depth 1 origin "$WAVESHARE_REF"
    git checkout --detach FETCH_HEAD
fi

cat <<NOTICE

About to execute third-party code from:
  $REPOSITORY
Checked-out commit:
  $(git rev-parse HEAD)
License supplied by that repository:
  $SOURCE_DIR/LICENSE

Music Box does not redistribute or relicense this driver.
NOTICE

chmod +x install.sh
./install.sh

printf '\nDKMS status:\n'
dkms status || true
cat <<DONE

The vendor installer completed. Reboot before testing the WM8960.
After reboot, verify with:
  aplay -l
  arecord -l
  dkms status
DONE
