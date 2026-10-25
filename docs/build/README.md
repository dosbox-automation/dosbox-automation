<!-- This file is part of the dosbox-automation Project. -->
<!-- License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net -->

# Building dosbox-automation

This page covers what is common to all platforms. The platform pages
carry the details:

- [Linux](linux.md)
- [Windows](windows.md)
- [macOS](macos.md)
- [FreeBSD](freebsd.md)

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

The vcpkg builds take libarchive from an overlay port in
`vcpkg/ports/libarchive`: version 3.8.10 with one local change, which
keeps Joliet file names longer than 103 characters whole. Builds against
a system libarchive (the Linux system build, Homebrew, FreeBSD) cut such
names at 103 characters, as every libarchive release does. A member with
a longer name on a disc then shows up under its cut name, and an archive
among them cannot be entered. Compilation discs authored on Windows
sometimes carry names that long.

A C++23 compiler is required on every platform.

## CPUs and the dynamic core

The emulator has a dynamic CPU core for x86-64 and for arm64, and an
interpreter that runs everywhere the code compiles. Which one you get
is decided at configure time by `OPT_DYNAREC`:

- `KNOWN_CPU` (the default): the dynamic core is enabled when the
  target CPU is in `DYNAREC_X86_CPUS` or `DYNAREC_ARM_CPUS` in the
  top-level CMakeLists, the spellings each backend is known to build
  for. On any other CPU you get the interpreter and a warning.
- `NO`: interpreter only, on every CPU. Useful for debugging the core
  or for a packager who wants one binary flavour everywhere.
- `YES`: the dynamic core regardless of CPU. On a CPU without a backend
  the build fails in `src/cpu/core_dynrec.cpp`, and the warning at
  configure time says so first.

Unlisted CPUs (ppc64le, riscv64, s390x and the like) are community
supported: the build is offered as is, we cannot test it, and issues
filed against it are closed with a pointer to the community. A new
spelling of x86-64 or arm64 that your platform reports is a one-line
pull request against those lists; a new architecture needs a backend
in `src/cpu/core_dynrec/` before it can join. Build fixes that keep
the interpreter building on an unlisted CPU are welcome on the same
terms. Windows on ARM is not on the list: nobody has built the arm64
backend with MSVC yet, so a native ARM64 host gets the interpreter.
Cross-configuring with the Visual Studio generator's `-A` platform
is not detected; our Windows presets do not use it.

On macOS the target is `CMAKE_OSX_ARCHITECTURES`, one architecture
per configure; universal builds are refused.

`scripts/tools/check-dynarec-option.sh` configures the tree in every
state of the option, on the host and on a simulated ppc64le, and checks
the flags that reach `dosbox_config.h`; `--full` also builds and runs
the test suite for the default, `NO` and ppc64le states, and checks
that the forced recompiler fails on ppc64le where the warning says.

## Running the tests

```bash
cd build/<preset name> && ctest
```

The Python integration tests in `tests/integration/` need a built
binary and a Python virtual environment with pytest. They start real
emulator instances, so run them on a machine with a display or let
them fall back to the offscreen backend.
