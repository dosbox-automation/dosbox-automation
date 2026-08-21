<!-- This file is part of the dosbox-automation Project. -->
<!-- License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net -->

# Building dosbox-automation

This page covers what is common to all platforms. The platform pages
carry the details:

- [Linux](linux.md)
- [Windows](windows.md)
- [macOS](macos.md)

## The short version

The build is CMake with presets. List what is available for your
platform, then configure and build:

```bash
cmake --list-presets
cmake --preset release-linux
cmake --build build/release-linux -- -j$(nproc)
```

Preset names follow `{debug,release}-{platform}` with an optional
`-vcpkg` suffix on Linux. The presets are the supported way to build:
they pin the compiler flags our binaries are tested with.

## Dependencies

Two strategies, picked by preset:

- **System libraries** (Linux default): your distribution provides the
  dependencies. The platform pages list the required packages.
- **vcpkg** (`-vcpkg` presets, and all Windows builds): dependencies
  are fetched and built from the `vcpkg.json` manifest. Set
  `VCPKG_ROOT` to your vcpkg checkout before configuring.

A C++23 compiler is required on every platform.

## Running the tests

```bash
cd build/<preset name> && ctest
```

The Python integration tests in `tests/integration/` need a built
binary and a Python virtual environment with pytest. They start real
emulator instances, so run them on a machine with a display or let
them fall back to the offscreen backend.
