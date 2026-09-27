<!-- This file is part of the dosbox-automation Project.
     License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net -->

# Porting dosbox-automation to another operating system

This is the list of places a port has to touch, written so that you do
not have to read the whole tree to find them. Linux, Windows and macOS
are the platforms the project builds and tests itself. FreeBSD came in
as a community port (PR 9, 2026-09) and is kept building on a FreeBSD
VM, but it is community-supported: no house hardware runs it beyond the
build and the unit tests, and reports and fixes are welcome.

## What a port must add

Each item names the file to change. Do them in this order; the build
stops at the first one you skip.

1. **CMake platform detection** in `CMakeLists.txt`. The block that
   sets `DOSBOX_PLATFORM_WINDOWS`, `DOSBOX_PLATFORM_MACOS`,
   `DOSBOX_PLATFORM_LINUX` and `DOSBOX_PLATFORM_BSD` ends in
   `message(FATAL_ERROR "Unknown system ...")`. Add a branch for your
   `CMAKE_SYSTEM_NAME`, and the platform-specific link libraries the
   same file adds further down (the BSD branch is the model: threads,
   `libexecinfo` for the backtrace symbols, ALSA as optional).
2. **Presets** in `CMakePresets.json`. Add a debug and a release
   configure preset with a `condition` on `${hostSystemName}`, and the
   matching build and test presets. `debug-bsd` and `release-bsd` are
   the pattern.
3. **Dynamic recompiler CPU lists** in `CMakeLists.txt`,
   `DYNAREC_X86_CPUS` and `DYNAREC_ARM_CPUS`. Add the spelling your
   system's CMake reports for the CPU (`uname -m` style). A CPU not on
   either list builds the interpreter core only, which is correct but
   slow.
4. **Host locale** in `src/misc/host_locale_*.cpp`, selected in
   `src/misc/CMakeLists.txt`. The Linux file covers the BSDs through
   `#if` blocks; a system with a different locale API needs its own
   file and a line in the CMake selection.
5. **The system-path table** in
   `src/dos/programs/mount_policy_system_paths.cpp`. The mount policy
   refuses to mount system directories, and the list is per host OS.
   Add your OS to the `HostOs` enum and the detection chain in the
   header (an unknown OS is a build error there on purpose), then a row
   in the table: whether it takes the common Unix list, the directories
   your `hier(7)` adds, and the carve-outs. Pin the row's entries in
   `tests/mount_policy_system_paths_tests.cpp`; those tests run on
   every host, so a Linux build checks your row.
6. **Platform includes and quirks.** `src/misc/std_filesystem.h` and
   `src/network/ethernet_slirp.cpp` carry the include differences the
   BSD port needed, `src/hardware/CMakeLists.txt` the threads dependency; `src/gui/render/opengl_renderer.cpp` holds the
   renderer workarounds keyed by platform (the llvmpipe channel swap on
   BSD lives there). Look at those three before adding a new `#if`
   elsewhere.
7. **Tests that assume a host layout.** `tests/host_browser_tests.cpp`
   looks for `true` in `/usr/bin` and `/bin`; `tests/mount_policy_tests.cpp`
   picks a system directory that is not a symlink on the host
   (`kPlainSystemDir`). `tests/CMakeLists.txt` has the BSD+GCC path for
   GoogleTest. A new host that fails one of these usually needs the
   test taught about its layout, not the test disabled.
8. **Packaging** in `scripts/packaging/`. The Linux tarball script, the
   desktop and metainfo files, the macOS `Info.plist` template and the
   Windows installer script name the binary `dosbox-automation`; a new
   package format goes next to them.
9. **A build document** in `docs/build/`, one per platform, with the
   exact commands you verified. Say what you tested on and what you
   did not.

## What a port may leave

- Hardware-specific backends (MIDI devices, joystick APIs) can stay
  off; the CMake options report what is missing at configure time.
- The dynamic recompiler, if the CPU is not on the lists.
- Packaging, until someone wants a package.

## Running the suite

Configure with your preset, build, then from the build directory run
`ctest`. The unit suite is the acceptance test for a port: every test
either passes or skips with a reason that names your host. The
integration suite under `tests/integration/` needs Python and drives a
built binary over the REST API; run it once on a port with a window
system, headless runs use `SDL_VIDEODRIVER=offscreen`.

## Sending it

Open a pull request with the project's template, tested on the port
and on at least one of the three house platforms, with the build
document from step 9. Ports are reviewed like any other contribution:
the code is read, built and run here before it merges.
