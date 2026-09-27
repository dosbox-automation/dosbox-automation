# Building on macOS

dosbox-automation builds on macOS with Apple Clang and either Homebrew
libraries or vcpkg. Both paths use the Ninja generator and the Command
Line Tools; full Xcode.app is not required.

Verified on macOS 26.6.2 (Tahoe), Apple Clang 21.0, CMake 4.4.3, Mac
mini M1 (arm64). The full unit test suite passes on the Homebrew build.


## Prerequisites

Install the Command Line Tools and Homebrew:

```shell
xcode-select --install
```

Accept the license. Then install Homebrew (<https://brew.sh>) and the
build tools:

```shell
brew install cmake ninja pkg-config python3
```

Add `brew` to your shell if you have not already:

```shell
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> "$HOME"/.zprofile
eval "$(/opt/homebrew/bin/brew shellenv)"
```


## Path A: Homebrew libraries

This path uses Homebrew packages for all dependencies. Leaving out
the vcpkg toolchain file is what selects Homebrew over vcpkg.

### Install dependencies

```shell
brew install sdl3 sdl3_image fluid-synth opusfile speexdsp mt32emu \
  iir1 googletest asio freetype libpng jpeg-turbo
```

### Configure and build

From the repository root:

```shell
cmake --preset debug-macos-homebrew
cmake --build --preset debug-macos-homebrew -- -j$(sysctl -n hw.ncpu)
```

For a release build use `release-macos-homebrew`. The presets set the
Ninja generator, the build type, a deployment target of macOS 12.0 and
`USE_SYSTEM_LIBS=ON`, which makes the build find opusfile through
pkg-config instead of a CMake package file, since Homebrew ships none;
the build directory is `build/<preset name>`.

The linker will warn about Homebrew bottles being built for a newer
macOS than the deployment target. This is expected when building against
Homebrew on the current OS - the deployment target matters for
portability of release builds, not for development.


## Path B: vcpkg

This path builds all library dependencies from source through vcpkg.
The first configure takes a while; later runs reuse the binary cache.

### Install and bootstrap vcpkg

Clone vcpkg into your home directory:

```shell
cd ~
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg && ./bootstrap-vcpkg.sh && cd ..
```

Add the export to your shell profile:

```shell
echo "export VCPKG_ROOT=\"$HOME/vcpkg\"" >> "$HOME"/.zshenv
source "$HOME"/.zshenv
```

### Configure and build

From the repository root:

```shell
cmake --preset debug-macos-vcpkg
cmake --build --preset debug-macos-vcpkg -- -j$(sysctl -n hw.ncpu)
```

For a release build use `release-macos-vcpkg`. The preset reads
`VCPKG_ROOT` for the toolchain file.


## Running tests

```shell
cd build/debug-macos-homebrew && ctest --output-on-failure
```


## Running the emulator

A build tree is not a self-contained install. The emulator looks for
`resources/` relative to the working directory (or at
`<binary>/../Resources`), so run it from the repository root:

```shell
./build/debug-macos-homebrew/dosbox-automation
```

If GLSL shaders are not found, copy `resources/shaders` and
`resources/shader-presets` into
`~/Library/Preferences/dosbox-automation/`.

When running for the first time, macOS will ask for folder access
permissions. Allow them.


## Installing clang-format

```shell
brew install clang-format
```


## Sanitizer builds

Two mutually exclusive sanitizer options are available:
- `OPT_SANITIZER` for memory errors and undefined behaviour
- `OPT_THREAD_SANITIZER` for data race detection

Pass the option at configure time, using a separate build directory:

```shell
cmake -G Ninja -B build/debug-macos-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DOPT_SANITIZER=ON
```


## Notes

- macOS is not covered by CI and no macOS binaries are published.
  Reports and fixes are welcome.
- `CMakePresets.json` also carries `debug-macos` and `release-macos`
  presets for the Xcode generator, which need Xcode.app. The
  `*-macos-homebrew` and `*-macos-vcpkg` presets used above need only
  the Command Line Tools.
