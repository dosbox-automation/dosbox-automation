# Building on Linux

Two build methods are available for Linux — using the [system libraries](#building-using-system-libraries)
provided by your Linux distribution or using the [vcpkg tool](#building-using-vcpkg)
to fetch and compile dependencies.

Both library options are available using CMake presets, which we highly
recommend because they're CI-tested and produce a binary using consistent
compiler flags. Run `cmake --list-presets` to list the presets.

The vcpkg presets are used by the team to provide our official binaries, which
are intended to be run on different distros — they only depend on glibc.

## Packaging

To create a distro package, use the [system libraries](#building-using-system-libraries)
build method. Pass the `-DOPT_TESTS=OFF` option to CMake when configuring the
project to skip building unit tests and get rid of the GTest dependency.

To collect the built files outside the tree (binary, resources, desktop
entry, icons, man page, licenses), use the install step with a prefix or
a staging directory:

```bash
cmake --install build/release-linux --prefix /usr/local
# or, packager style:
DESTDIR=/tmp/stage cmake --install build/release-linux --prefix /usr
```

For a relocatable tarball to unpack and run anywhere, there is a script
that stages the install and bundles any libraries that did not come from
your distribution (self-compiled SDL3, FluidSynth, and so on) next to
the binary:

```bash
./scripts/packaging/linux/make-linux-tarball.sh build/release-linux
```

On a build against pure distro libraries it bundles nothing and the
result relies on your system packages.

dosbox-automation ships with some binary files as a part of its assets:

- `resources/drives/y` — important DOS commands which are not yet
  implemented internally.
  These are pre-built DOS executables, taken from the FreeDOS or other projects.
  To rebuild them, one might need legacy build tools, which are considered
  exotic today (like the OpenWatcom compiler), that's why they are not being
  compiled at build time.
  If shipping such binaries is against your distro policy, feel free to strip
  them away. dosbox-automation will continue to work normally - the only real
  disadvantage for the end users is that they will have to provide them by
  themselves to run some game/software installers.

- `resources/freedos-cpi` — DOS screen fonts (CodePage Information
  files, CPI).
  These are bitmap fonts in a native MS-DOS format, taken from the FreeDOS
  project, painted by hand using a specialized font editor (so no source code
  exists for them).
  Probably all the other DOSBox forks use such files in some form; it's just
  most store them as an array of binary data, like [original DOSBox does](https://sourceforge.net/p/dosbox/code-0/HEAD/tree/dosbox/tags/RELEASE_0_74_3/src/dos/dos_codepages.h).
  Running dosbox-automation without these files is completely unsupported; even if
  it seems to work for you, the internationalization features will malfunction.

- `resources/freedos-keyboard` — DOS keyboard layout definitions.
  Despite their extensions suggesting a DOS device driver, these are data files,
  not executables. They are, too, taken from the FreeDOS project; the binaries
  were created from the source `*.KEY` text files, using specialized tools,
  written in Pascal — they are, too, part of the FreeDOS project; search for
  `KEYB200S.ZIP`, `KEYB200X.ZIP`, `KC200S.ZIP`, and `KC200X.ZIP` files.
  Probably all the other DOSBox forks use such files in some form; it's just
  most store them as an array of binary data, like [original DOSBox does](https://sourceforge.net/p/dosbox/code-0/HEAD/tree/dosbox/tags/RELEASE_0_74_3/src/dos/dos_keyboard_layout_data.h).
  Running dosbox-automation without these files is completely unsupported; even if
  it seems to work for you, the internationalization features will malfunction.

## Building using system libraries

These are generic, distro-independent building instructions.

### Install the necessary build tools

- GCC or Clang compiler (the compiler has to support C++23)
- Git
- CMake
- pkg-config
- Python 3 with the `venv` module (only needed for the Python integration
  test harness in `tests/integration/`; install `python3-venv` on
  Debian/Ubuntu)

### Install the dependencies (development packages are needed, too)

- ALSA
- FluidSynth 2.5 or newer
- GTest
- IIR (iir1)
- libpng
- MT32Emu
- OpenGL headers
- OpusFile
- SDL 3.4.0 or newer
- SDL3_image
- asio
- SpeexDSP
- zlib

Lua 5.5 is vendored in `vendor/lua` and built as part of dosbox-automation.
No system Lua package is needed or used, in either build method.

Mind the SDL and FluidSynth versions: the code uses SDL functions
introduced in 3.4.0 and FluidSynth functions introduced in 2.5, so older
packages will not do. Configuration fails with a clear version message
if yours are too old.

### Distro package names

Verified on Debian 13:

```bash
sudo apt install git build-essential cmake ninja-build pkg-config \
    libasound2-dev libasio-dev libfluidsynth-dev libgtest-dev \
    libgl1-mesa-dev libpng-dev libopusfile-dev libsdl3-dev \
    libsdl3-image-dev libspeexdsp-dev zlib1g-dev
```

Caveats on Debian 13: `iir1` and `mt32emu` are not packaged at all (see
below), and the shipped SDL3 (3.2) and FluidSynth (2.4) are too old.
Until newer versions land in backports, either use the vcpkg build
method or build the too-old libraries from source as described below.

On other distributions the names differ; these lists are a starting point,
not gospel:

- Fedora: `gcc-c++ cmake ninja-build pkgconf-pkg-config alsa-lib-devel
  asio-devel fluidsynth-devel gtest-devel libpng-devel mesa-libGL-devel
  opusfile-devel SDL3-devel SDL3_image-devel speexdsp-devel zlib-ng-devel`
- Arch: `base-devel cmake ninja pkgconf alsa-lib asio fluidsynth gtest
  libpng opusfile sdl3 sdl3_image speexdsp zlib` (iir1 and mt32emu are on
  the AUR)
- Gentoo: `dev-build/cmake dev-build/ninja media-libs/alsa-lib
  dev-cpp/asio media-sound/fluidsynth dev-cpp/gtest media-libs/libpng
  media-libs/opusfile media-libs/libsdl3 media-libs/sdl3-image
  media-libs/speexdsp sys-libs/zlib`

### Libraries your distro does not package

`iir1` and `mt32emu` are missing from several distributions. Both build
from source in under a minute and install cleanly into a local prefix:

```bash
PREFIX=$HOME/.local/dosbox-automation-deps

git clone --depth 1 --branch 1.10.0 https://github.com/berndporr/iir1.git
cmake -S iir1 -B iir1/build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$PREFIX -DIIR1_BUILD_TESTING=OFF \
    -DIIR1_BUILD_DEMO=OFF
cmake --build iir1/build -j$(nproc)
cmake --install iir1/build

git clone --depth 1 --branch libmt32emu_2_7_3 https://github.com/munt/munt.git
cmake -S munt/mt32emu -B munt/build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$PREFIX
cmake --build munt/build -j$(nproc)
cmake --install munt/build
```

Then point CMake at the prefix when configuring dosbox-automation:

```bash
cmake --preset=release-linux -DCMAKE_PREFIX_PATH=$PREFIX
```

The same pattern works for distributions whose SDL3 or FluidSynth are
older than required: SDL3 (`release-3.4.8` tag of
https://github.com/libsdl-org/SDL), SDL3_image (`release-3.4.4` of
https://github.com/libsdl-org/SDL_image), and FluidSynth (`v2.5.4` of
https://github.com/FluidSynth/fluidsynth). Build and install SDL3 into
the prefix before SDL3_image so the image library picks it up.

One caveat about running the result: libraries that came from your
distribution are found at runtime as usual, but anything you built into
a local prefix is a shared library the binary still has to find outside
the build tree. Which ones those are depends on your system. Either
install them somewhere your loader searches (`/usr/local` plus
`ldconfig`), or start dosbox-automation with
`LD_LIBRARY_PATH=$PREFIX/lib`. The vendored Lua is not affected; it is
linked statically into the binary.

### Clone dosbox-automation

```bash
git clone https://github.com/dosbox-automation/dosbox-automation.git
```

### Configure and build

To create the debug build:

```bash
cd dosbox-automation
cmake --preset=debug-linux
cmake --build --preset=debug-linux
```

To create the optimised release build:

```bash
cd dosbox-automation
cmake --preset=release-linux
cmake --build --preset=release-linux
```

### Start dosbox-automation

Once built, you can launch dosbox-automation with the following commands.

Debug build:

``` bash
./build/debug-linux/dosbox
```

Release build:

``` bash
./build/release-linux/dosbox
```

## Building using vcpkg

### Install the necessary build tools

- for Ubuntu:

```bash
sudo apt-get install git build-essential pkg-config cmake curl ninja-build \
             autoconf autoconf-archive automake bison libtool libgl1-mesa-dev \
             libsdl3-dev python3-venv
```

### Install the vcpkg tool

- clone the vcpkg tool into your home directory:

```bash
cd ~
git clone https://github.com/microsoft/vcpkg.git
```

- bootstrap the vcpkg tool:

```bash
cd vcpkg
./bootstrap-vcpkg.sh -disableMetrics
```

- set the vcpkg path in your compile shell (it is recommended to add the command
  to your shell startup script, usually `~/.bashrc`):

```bash
export VCPKG_ROOT=$HOME/vcpkg
```

### Clone dosbox-automation

```bash
cd ~
git clone https://github.com/dosbox-automation/dosbox-automation.git
```

### Configure and build

To create the debug build:

```bash
cd dosbox-automation
cmake --preset=debug-linux-vcpkg
cmake --build --preset=debug-linux-vcpkg
```

To create the optimised release build:

```bash
cd dosbox-automation
cmake --preset=release-linux-vcpkg
cmake --build --preset=release-linux-vcpkg
```

### Start dosbox-automation

Once built, you can launch dosbox-automation with the following commands.

Debug build:

``` bash
./build/debug-linux-vcpkg/dosbox
```

Release build:

``` bash
./build/release-linux-vcpkg/dosbox
```

## Bisecting and building old versions

Prior to release 0.83.0, the Meson build system was used. The following commands
can be used to configure and build the project:

```bash
meson setup -Dbuildtype=release build
meson compile -C build
```

Prior to release 0.77.0, the Autotools build system was used. A build script
available in these old versions can be used (choose one for your compiler):

```bash
./scripts/build.sh -c clang -t release` or `./scripts/build.sh -c gcc -t release
```

## Unit tests

Unit tests are built by default. To disable building unit tests, pass the
`-DOPT_TESTS=OFF` option when configuring the project, for example:

```bash
cmake --preset=release-linux -DOPT_TESTS=OFF
```

To run the entire test suite, execute the following (use the same CMake preset
you used for building):

```bash
ctest -j 8 --preset debug-linux
```

The `-j 8` option runs the tests in parallel on 8 CPU cores. You can adjust
this to suit your system.

To run all test cases in a single test suite, pass in the name of the suite
with the `-R` option:

```bash
ctest -j 8 --preset debug-linux -R DOS_FilesTest
```

You can narrow this down to run a single test case only:

```bash
ctest -j 8 --preset debug-linux -R DOS_FilesTest.DOS_MakeName_Basic_Failures
```

To run a group of tests, you can use wildcards and regexes. E.g. to run all
test cases in the `DOS_FilesTest` suite with names starting with
`DOS_MakeName_`:

```bash
ctest -j 8 --preset debug-linux -R "DOS_FilesTest.DOS_MakeName_*"
```

Pass in the `-V` option to see the dosbox-automation log output:

```bash
ctest -j 8 --preset debug-linux -R DOS_FilesTest.DOS_MakeName_Basic_Failures -V
```

You might want to run the test executable directly to get coloured output, and
the option to start an interactive `gdb` session if a test crashes. For
example:

```
build/debug-linux/tests/dosbox_tests --gtest_filter=DOS_FilesTest.DOS_MakeName_Basic_Failures
```

See the [ctest documentation](https://cmake.org/cmake/help/v3.31/manual/ctest.1.html)
for the full list of available options.


## Sanitizer build

There are two (mutually exclusive) sanitizer settings available:
- `OPT_SANITIZER` — detects memory errors and undefined behaviors
- `OPT_THREAD_SANITIZER` — data race detector

To use any of these, pass the appropriate option when configuring the project,
for example:

```bash
cmake -DOPT_SANITIZER=ON --preset=release-linux
cmake --build --preset=release-linux
```

For more information about sanitizers, check the `GCC` or `clang` documentation
on the `-fsanitize` option.

As sanitizer availability and performance are are highly platform-dependent,
you might need to manually adapt the `SANITIZER_FLAGS` variable in
`CMakeLists.txt` file to suit your needs.
