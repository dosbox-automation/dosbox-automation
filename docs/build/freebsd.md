<!-- This file is part of the dosbox-automation Project. -->
<!-- License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net -->

# Building on FreeBSD

dosbox-automation builds on FreeBSD against the system packages, with
the base system's Clang. There is no vcpkg path here: the BSD presets
take every library from pkg, and libarchive comes from the base system.

Our builder runs FreeBSD 15.1 on amd64, and the package names below are
the ones in that release's repository. The full unit test suite runs
there under Xvfb.


## Install dependencies

```shell
sudo pkg install git cmake ninja pkgconf \
  alsa-lib asio googletest iir1 libjpeg-turbo libmt32emu png opusfile \
  fluidsynth nlohmann-json sdl3 sdl3_image speexdsp freetype2
```

googletest is only needed for the unit tests. FreeType is needed for the
TrueType text output.


## Configure and build

From the repository root:

```shell
cmake --preset release-bsd
cmake --build build/release-bsd -- -j$(nproc)
```

For a debug build use `debug-bsd`. Both presets set the Ninja generator
and `USE_SYSTEM_LIBS=ON`; the build directory is `build/<preset name>`.


## Running tests

```shell
cd build/release-bsd && ctest --output-on-failure
```

On a machine without a running X session, start a virtual one first
(`pkg install xorg-vfbserver`, or `xlibre-vfb` if your desktop runs
XLibre, since the two conflict):

```shell
Xvfb :99 &
cd build/release-bsd && DISPLAY=:99 ctest --output-on-failure
```

DOS file attributes on directory mounts are kept in extended attributes
(extattr(2)). On a file system without them the tests that need them
skip and say so in their output.


## Running the emulator

A build tree is not a self-contained install. The emulator looks for
`resources/` relative to the working directory, so run it from the
repository root:

```shell
./build/release-bsd/dosbox-automation
```

The build writes `dosbox-automation-archive-helper` next to the
emulator, and the emulator looks for it there, so keep the two together
when you copy a build directory.
