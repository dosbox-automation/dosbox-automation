#!/bin/bash
# This file is part of the dosbox-automation Project.
# License: GPL-3.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

# builds dosbox-automation as a sharun-based AppImage.
#
# usage:
#   ./build-appimage.sh             build the AppImage
#   ./build-appimage.sh cleanup     remove build artifacts, keep output
#
# the script builds from the local source tree (not a clone), fetches sharun
# tooling, compiles a release build, bundles shared libraries, and produces
# the AppImage. everything except the source tree happens inside a build
# directory that can be safely deleted afterwards.
#
# requirements: cmake, ninja, git, wget, strace, xvfb-run, and the
# dosbox-automation build dependencies (SDL2, SDL2_image, FluidSynth,
# mt32emu, zlib, and whatever else CMakeLists.txt pulls in).
#
# xvfb-run is needed because dosbox-automation is a graphical SDL2 app.
# sharun uses strace to find dlopened libraries at runtime, which means
# the binary has to actually start. xvfb-run gives it a virtual display
# so this works in headless and CI environments.
#
# based on the lgogdownloader AppImage build script from ~/Projects/gog/.
#
# SoundFont notes:
#
#   The AppImage bundles GeneralUser GS, a freely distributable General
#   MIDI / GS SoundFont so MIDI works out of the box. The user can
#   override this in the config with their own preferred SoundFont.
#   The bundled font is a fallback, not a default that overrides user
#   choice.
#
#   Without a bundled SoundFont, MIDI is broken on any system that
#   doesn't ship its own GM SoundFont, which is most minimal installs
#   and many desktop distros. ~30 MB is worth working out of the box.
#
#   GeneralUser GS has GS support beyond just GM, which matters for
#   the subset of DOS games that send GS bank-select messages. DOSBox
#   Staging does not bundle any SoundFont, so this is a genuine
#   usability differentiator.
#
#   License: custom permissive (see documentation/LICENSE.txt in the
#   GeneralUser GS distribution). Requires LICENSE.txt and README to
#   accompany the .sf2 file. Both are bundled.
#
# MT-32 ROM notes:
#
#   MT-32 ROMs (Roland copyright, cannot be distributed):
#     dosbox-automation looks for MT-32 ROMs in the config directory at
#     ~/.config/dosbox-automation/mt32-roms/ (CM32L_CONTROL.ROM,
#     CM32L_PCM.ROM). The AppImage does NOT bundle these. If a game
#     requests MT-32 and the ROMs are missing, dosbox-automation prints
#     a message telling the user where to place them.

set -euo pipefail

VERSION="0.2"

SCRIPTDIR="$(cd "$(dirname "$0")" && pwd)"
SRCDIR="$(cd "$SCRIPTDIR/../../.." && pwd)"
BUILDROOT="$SRCDIR/.build-appimage"
TOOLSDIR="$BUILDROOT/tools"

SHARUN_URL="https://github.com/VHSgunzo/sharun/releases/latest/download/sharun-x86_64-aio"
QUICKSHARUN_URL="https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/refs/heads/main/useful-tools/quick-sharun.sh"

# GeneralUser GS SoundFont - bundled for out-of-box MIDI
SOUNDFONT_BASE_URL="https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/main"
SOUNDFONT_NAME="GeneralUser-GS.sf2"
SOUNDFONT_LICENSE_URL="$SOUNDFONT_BASE_URL/documentation/LICENSE.txt"
SOUNDFONT_README_URL="$SOUNDFONT_BASE_URL/documentation/README.md"
SOUNDFONT_SF2_URL="$SOUNDFONT_BASE_URL/$SOUNDFONT_NAME"

ARCH=$(uname -m)
export DWARFS_COMP="${DWARFS_COMP:-zstd:level=9 -S26 -B6}"

BINARY_NAME="dosbox-automation"

mycommand="${1:-build}"

if [ "$mycommand" = "cleanup" ]; then
    if [ ! -d "$BUILDROOT" ]; then
        echo "nothing to clean."
        exit 0
    fi
    echo "## cleaning up build artifacts"
    rm -rf "$BUILDROOT/build" "$BUILDROOT/AppDir"
    exit 0
fi

if [ "$mycommand" != "build" ]; then
    echo "usage: $0 [build|cleanup]"
    exit 1
fi


check_for_binary()
{
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "NOT INSTALLED: $1"
        echo
        echo "please install with your package manager before running this script."
        exit 1
    fi
}

fetch_tool()
{
    local url="$1"
    local dest="$2"

    if [ -f "$dest" ]; then
        return 0
    fi

    echo "## fetching $(basename "$dest") ..."
    wget -q -O "$dest" "$url"
    if [ $? -gt 0 ]; then
        echo "## fatal: failed to download $url"
        rm -f "$dest"
        exit 1
    fi
    chmod +x "$dest"
}

fetch_file()
{
    local url="$1"
    local dest="$2"
    local label="$3"

    if [ -f "$dest" ]; then
        echo "## $label already cached"
        return 0
    fi

    echo "## downloading $label ..."
    wget -q -O "$dest" "$url"
    if [ $? -gt 0 ]; then
        echo "## fatal: failed to download $label from $url"
        rm -f "$dest"
        return 1
    fi
}


### sanity checks
check_for_binary git
check_for_binary cmake
check_for_binary ninja
check_for_binary wget
check_for_binary strace
check_for_binary xvfb-run

### verify we are in the source tree
if [ ! -f "$SRCDIR/CMakeLists.txt" ]; then
    echo "## fatal: cannot find CMakeLists.txt in $SRCDIR"
    echo "   this script must live in scripts/packaging/linux/ of the source tree."
    exit 1
fi

### setup directories
mkdir -p "$BUILDROOT" "$TOOLSDIR"

### fetch sharun tooling (cached, downloaded once)
fetch_tool "$QUICKSHARUN_URL" "$TOOLSDIR/quick-sharun.sh"
fetch_tool "$SHARUN_URL" "$TOOLSDIR/sharun-$ARCH-aio"

export PATH="$TOOLSDIR:$PATH"

if [ ! -f "$TOOLSDIR/sharun" ]; then
    cp "$TOOLSDIR/sharun-$ARCH-aio" "$TOOLSDIR/sharun"
fi

### figure out version from CMakeLists.txt and git
cmake_version=$(grep -oP 'set\(DOSBOX_VERSION\s+\K[^)]+' "$SRCDIR/CMakeLists.txt" || echo "0.84-da1")
git_short=$(cd "$SRCDIR" && git rev-parse --short HEAD 2>/dev/null || echo "unknown")
version="${cmake_version}+${git_short}"
echo "## building version $version"

### cmake release build using the tested preset
CMAKEBUILD="$SRCDIR/build/release-linux"
APPDIR="$BUILDROOT/AppDir"
OUTPATH="$BUILDROOT"

echo "## running cmake (release-linux preset)"
cmake --preset release-linux -S "$SRCDIR"
ret=$?
if [ $ret -gt 0 ]; then
    echo "## fatal: cmake configure failed."
    exit $ret
fi

echo "## compiling dosbox-automation"
cmake --build --preset release-linux -j"$(nproc)"
ret=$?
if [ $ret -gt 0 ]; then
    echo "## fatal: build failed."
    exit $ret
fi

### verify the binary
if [ ! -x "$CMAKEBUILD/$BINARY_NAME" ]; then
    echo "## fatal: build did not produce $BINARY_NAME binary."
    exit 1
fi

echo "## binary built: $CMAKEBUILD/$BINARY_NAME"

echo "## starting AppImage construction #########################"

### package as AppImage with sharun
rm -rf "$APPDIR"
mkdir -p "$OUTPATH"

export ARCH
export VERSION="$version"
export MAIN_BIN="$BINARY_NAME"
export STRIP=1
export APPDIR
export OUTPATH

# dosbox-automation is a graphical SDL2 application
export DEPLOY_OPENGL=1
export DEPLOY_VULKAN=0

# icon - use 256px from extras/icons/
ICON_SRC="$SRCDIR/extras/icons/png/icon_256.png"
if [ ! -f "$ICON_SRC" ]; then
    echo "## fatal: icon not found at $ICON_SRC"
    exit 1
fi

# create desktop file and icon in AppDir before sharun deploy
mkdir -p "$APPDIR"

cp "$ICON_SRC" "$APPDIR/$BINARY_NAME.png"
cp "$ICON_SRC" "$APPDIR/.DirIcon"

cat > "$APPDIR/$BINARY_NAME.desktop" << DESKTOP_EOF
[Desktop Entry]
Type=Application
Name=dosbox-automation
Exec=dosbox-automation
Icon=dosbox-automation
Categories=Game;Emulator;
Comment=DOSBox fork for scripted DOS automation and install testing
X-AppImage-Name=dosbox-automation
X-AppImage-Version=$version
DESKTOP_EOF

# point quick-sharun at our real desktop file
export DESKTOP="$APPDIR/$BINARY_NAME.desktop"
export ICON="$APPDIR/$BINARY_NAME.png"

### trace shared libraries
# xvfb-run provides a virtual display so the SDL2 binary can start
# under strace without needing a real X11/Wayland session.
echo "## tracing shared libraries (via xvfb-run + strace)"
export XVFB_RUN="xvfb-run -a"
bash "$TOOLSDIR/quick-sharun.sh" "$CMAKEBUILD/$BINARY_NAME"
if [ $? -gt 0 ]; then
    echo "## fatal: quick-sharun.sh trace failed."
    exit 1
fi

### trim the AppDir
#
# dosbox-automation uses SDL2 + OpenGL. it does not use Qt, Wayland-specific
# plugins, or most of the multimedia stack that strace might pull in.
# trim aggressively but keep what SDL2 and FluidSynth actually need.
echo "## trimming AppDir"
SHARED="$APPDIR/shared"

# DRI drivers: rely on host system's graphics drivers
echo "# -> removing bundled graphics drivers (DRI, VDPAU, VA-API)"
find "$SHARED" -name "*_dri.so" -delete 2>/dev/null
find "$SHARED" -name "*_drv_video.so" -delete 2>/dev/null
find "$SHARED" -name "libvdpau_*.so*" -delete 2>/dev/null
find "$SHARED" -name "libva-*.so*" -delete 2>/dev/null
find "$SHARED" -name "libgallium-*.so" -delete 2>/dev/null

# PulseAudio/PipeWire modules: use host audio stack
echo "# -> removing audio server modules (use host)"
find "$SHARED" -path "*/pulse/modules/*" -delete 2>/dev/null
find "$SHARED" -path "*/pipewire-*/*" -type f -delete 2>/dev/null

# anything Qt-related that strace might have pulled in transitively
echo "# -> removing any Qt libraries (not used)"
find "$SHARED" -name "libQt*.so*" -delete 2>/dev/null
find "$SHARED" -path "*/qt5/*" -type f -delete 2>/dev/null
find "$SHARED" -path "*/qt6/*" -type f -delete 2>/dev/null

trimmed=$(du -sh "$APPDIR" | awk '{print $1}')
echo "## AppDir after trim: $trimmed"

### bundle SoundFont for FluidSynth MIDI
#
# FluidSynth searches $XDG_DATA_DIRS/soundfonts/ at runtime.
# sharun adds the AppImage-internal share/ to XDG_DATA_DIRS,
# so placing the sf2 at share/soundfonts/ makes it discoverable.
echo "## bundling SoundFont: $SOUNDFONT_NAME"
SF_DIR="$APPDIR/share/soundfonts"
SF_LICENSE_DIR="$SF_DIR/GeneralUser-GS-license"
mkdir -p "$SF_DIR" "$SF_LICENSE_DIR"

fetch_file "$SOUNDFONT_SF2_URL" "$SF_DIR/$SOUNDFONT_NAME" "GeneralUser GS SoundFont"
sf2_ok=$?

fetch_file "$SOUNDFONT_LICENSE_URL" "$SF_LICENSE_DIR/LICENSE.txt" "GeneralUser GS license"
fetch_file "$SOUNDFONT_README_URL" "$SF_LICENSE_DIR/README.md" "GeneralUser GS readme"

if [ $sf2_ok -gt 0 ]; then
    echo "## warning: SoundFont download failed. MIDI may not work without host SoundFont."
fi

sf_size=$(du -sh "$SF_DIR" | awk '{print $1}')
echo "## SoundFont bundle: $sf_size"

### build the AppImage
echo "## building the AppImage"
bash "$TOOLSDIR/quick-sharun.sh" --make-appimage
if [ $? -gt 0 ]; then
    echo "## fatal: AppImage build failed."
    exit 1
fi

APPIMAGE=$(ls -t1 "$OUTPATH"/dosbox-automation-*.AppImage 2>/dev/null | head -1)
if [ -z "$APPIMAGE" ]; then
    echo "## build failed: no AppImage produced."
    exit 1
fi

echo
echo "## verifying AppImage"
chmod +x "$APPIMAGE"
"$APPIMAGE" --version 2>/dev/null || echo "(version check not available)"
echo
echo "done: $(basename "$APPIMAGE")"
ls -lh "$APPIMAGE"
echo
echo "notes:"
echo "  - MT-32: place CM32L_CONTROL.ROM and CM32L_PCM.ROM in"
echo "    ~/.config/dosbox-automation/mt32-roms/"
echo "  - SoundFont: bundled ($SOUNDFONT_NAME) or override in config"
echo "  - SoundFont license: bundled at share/soundfonts/GeneralUser-GS-license/"
