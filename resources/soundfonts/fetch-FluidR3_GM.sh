#!/bin/bash
# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

# Fetch the FluidR3_GM SoundFont (bundled default for MIDI playback).
#
# The .sf2 is 142 MB and lives outside git; release packages bundle it,
# and source builds fetch it once with this script. The download comes
# from the Debian package it was originally taken from and is verified
# against a pinned sha256 before it is put in place. An existing file
# with the correct hash is left alone, so repeated runs are free.

set -euo pipefail

SF2_NAME="FluidR3_GM.sf2"
SF2_SHA256="74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0"
DEB_URL="https://deb.debian.org/debian/pool/main/f/fluid-soundfont/fluid-soundfont-gm_3.1-5.3_all.deb"
DEB_PATH_IN_PKG="./usr/share/sounds/sf2/FluidR3_GM.sf2"

DEST_DIR="$(cd "$(dirname "$0")" && pwd)"
DEST="$DEST_DIR/$SF2_NAME"

checksum_ok() {
    echo "$SF2_SHA256  $1" | sha256sum --check --quiet - 2>/dev/null
}

if [ -f "$DEST" ] && checksum_ok "$DEST"; then
    echo "$SF2_NAME already present and verified."
    exit 0
fi

command -v curl >/dev/null || { echo "Error: curl is required" >&2; exit 1; }
command -v ar   >/dev/null || { echo "Error: ar (binutils) is required" >&2; exit 1; }

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

echo "Downloading fluid-soundfont-gm from deb.debian.org (114 MB) ..."
curl -fL --retry 3 -o "$WORK_DIR/pkg.deb" "$DEB_URL"

# A .deb is an ar archive holding data.tar.xz; tar keeps the mtime the
# soundfont was packaged with.
( cd "$WORK_DIR" && ar x pkg.deb data.tar.xz && tar -xJf data.tar.xz "$DEB_PATH_IN_PKG" )

EXTRACTED="$WORK_DIR/$DEB_PATH_IN_PKG"
checksum_ok "$EXTRACTED" || { echo "Error: checksum mismatch on downloaded $SF2_NAME" >&2; exit 1; }

# Move into place via a temp name in the destination directory so a
# crash cannot leave a truncated soundfont behind.
cp --archive "$EXTRACTED" "$DEST.tmp"
mv "$DEST.tmp" "$DEST"
echo "$SF2_NAME installed and verified."
