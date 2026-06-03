# dosbox-automation

![GPL-2.0-or-later][gpl-badge]

A DOSBOX variant for Linux and Windows, mainly for automated installation of games, supporting DOS developers, better launcher support, export of pristine game screenshots and replaying recorded keyboard and mouse events.

It is a (mostly) drop-in replacement for older DOSBox versions—your existing configurations will continue to work, and you will have access to many advanced features.

**dosbox-automation controls DOSBox through a local HTTP REST API.**

What does that mean?
- automated game installs, including multi disk installs
- recording of keyboard and mouse with deterministic replay (pre recorded game demos)
- tighter launcher integration by controlling starting, installing and stopping games and programmatic shutdown of dosbox-automation
- controllable disk image support, with swapping disks via API
- support for unit and regression testing for DOS developers (via automation features)

**Security considerations**
If you open a web server with your system, you automatically open up an attack surface. dosbox-automation factors this in and we strive to provide proper security with our webserver. This is often overlooked and still important to us to protect our users from malicious software trying to exploit our product.

## Project website

https://www.dosbox-automation.org/

## Stable release builds

Regular users should use our stable release builds available on our project website:

- [Linux](https://github.com/dosbox-automation/dosbox-automation/releases)
- [Windows](https://github.com/dosbox-automation/dosbox-automation/releases)

## Development builds

Development builds are automatically created on every commit merged to the `dosbox-automation` branch. You need to be logged in to GitHub to download the development builds.

## Dependencies

dosbox-automation has the following library dependencies:

| Dependency                                                | Provides feature                                | vcpkg package name | vcpkg version     | Optional?           |
| --------------------------------------------------------- | ----------------------------------------------- | ------------------ | ----------------- | ------------------- |
| [FluidSynth](https://www.fluidsynth.org/)                 | General MIDI playback                           | fluidsynth         | 2.5.1             | **no** :red_circle: |
| [GoogleTest](https://github.com/google/googletest)        | Unit testing (development)                      | gtest              | 1.70.0#2          | yes :green_circle:  |
| [IIR](https://github.com/berndporr/iir1)                  | Audio filtering                                 | iir1               | 1.10.0            | **no** :red_circle: |
| [libmt32emu](https://github.com/munt/munt)                | Roland MT-32 and CM-32L emulation               | libmt32emu         | 2.7.1             | yes :green_circle:  |
| [libpng](http://www.libpng.org/pub/png/libpng.html)       | PNG encoding of screen captures                 | libpng             | 1.6.53            | **no** :red_circle: |
| [libslirp](https://gitlab.freedesktop.org/slirp/libslirp) | General purpose TCP-IP emulator                 | libslirp           | 4.9.0             | yes :green_circle:  |
| [Opus File](https://opus-codec.org/)                      | CD Audio playback for Opus-encoded audio tracks | opusfile           | 0.12+20221121#1   | **no** :red_circle: |
| [SDL 2](https://github.com/libsdl-org/SDL)                | OS-agnostic API for video, audio, and eventing  | sdl2               | 2.32.10 (overlay) | **no** :red_circle: |
| [SDL_image 2](https://github.com/libsdl-org/SDL_image)    | Image decoding for many popular formats         | sdl2-image         | 2.8.8#2           | **no** :red_circle: |
| [Asio](https://think-async.com/Asio/)                     | Network API for emulated serial and IPX         | asio               | 1.30.2            | **no** :red_circle: |
| [SpeexDSP](https://github.com/xiph/speexdsp)              | Audio resampling                                | speexdsp           | 1.2.1#1           | **no** :red_circle: |
| [zlib-ng](https://github.com/zlib-ng/zlib-ng)             | ZMBV video capture                              | zlib-ng            | 2.3.2             | yes¹ :green_circle: |

_¹ You can use plain old zlib instead._


## Get the sources

Clone the repository (one-time step):

``` shell
git clone https://github.com/dosbox-automation/dosbox-automation.git
```

## Build instructions

Please refer to the platform specific build instructions:

- [Linux](docs/build-linux.md)
- [Windows](docs/build-windows.md)
- [macOS](docs/build-macos.md)   (note: this is untested, use them with caution)

### Linux, macOS

Install build dependencies appropriate for your OS:

``` shell
# Fedora
sudo dnf install ccache gcc-c++ meson alsa-lib-devel libatomic libpng-devel \
                 SDL2-devel asio-devel opusfile-devel \
                 fluidsynth-devel iir1-devel mt32emu-devel libslirp-devel \
                 speexdsp-devel libXi-devel zlib-ng-devel
```

``` shell
# Debian, Ubuntu
sudo apt install ccache build-essential libasound2-dev libatomic1 libpng-dev \
                 libsdl2-dev libasio-dev libopusfile-dev \
                 libfluidsynth-dev libslirp-dev libspeexdsp-dev libxi-dev

# Install Meson on Debian-11 "Bullseye" or Ubuntu-21.04 and newer
sudo apt install meson
```

``` shell
# Arch, Manjaro
sudo pacman -S ccache gcc meson alsa-lib libpng sdl2 sdl2_net \
               asio opusfile fluidsynth libslirp speexdsp libxi pkgconf
```

``` shell
# openSUSE
sudo zypper install ccache gcc gcc-c++ meson alsa-devel libatomic1 libpng-devel \
                    libSDL2-devel asio-devel \
                    opusfile-devel fluidsynth-devel libmt32emu-devel libslirp-devel \
                    speexdsp libXi-devel
```

``` shell
# macOS
xcode-select --install
brew install cmake ccache meson libpng sdl2 asio opusfile \
     fluid-synth libslirp pkg-config python3 speexdsp
```

### Build and stay up-to-date with the latest sources

1. Check out the main branch:

    ``` shell
    # commit or stash any personal code changes
    git checkout main -f
    ```

2. Pull the latest updates. This is necessary every time you want a new build:

    ``` shell
    git pull
    ```

3. Set up the build. This is a one-time step either after cloning the repo or
    cleaning your working directories:

    ``` shell
    meson setup build
    ```

    The above enables all of DOSBox Staging's functional features. If you're
    interested in seeing all of Meson's setup options, run `meson configure`.

4. Compile the sources. This is necessary every time you want a new build:

    ``` shell
    meson compile -C build
    ```

    Your binary is: `build/dosbox`

    The binary depends on local resources relative to it, so we suggest
    symlinking to the binary from your `PATH`, such as into `~/.local/bin/`.


### Windows – Visual Studio (2022 or newer)

First, you need to setup [vcpkg] to install build dependencies. Once vcpkg
is bootstrapped, open PowerShell and run:

``` powershell
PS:\> .\vcpkg integrate install
```

This step will ensure that MSVC can use vcpkg to build, find and links all
dependencies.

Start Visual Studio and open the file `vs\dosbox.sln`. Make sure you have
`x64` selected as the solution platform.  Use **Ctrl+Shift+B** to build all
projects.

Note, the first time you build a configuration, dependencies will be built
automatically and stored in the `vcpkg_installed` directory. This can take
a significant length of time.

[vcpkg]: https://github.com/microsoft/vcpkg

## Imported branches, community patches, old forks

Upstream commits are imported to this repo in a timely manner,
see branch [`svn/trunk`].

- [`svn/*`] - branches from SVN
- [`forks/*`] - code for various abandoned DOSBox forks
- [`vogons/*`] - community patches posted on the Vogons forum

Git tags matching pattern `svn/*` are pointing to the commits referenced by SVN
"tag" paths at the time of creation.

Additionally, we attach some optional metadata to the commits in the form of
[Git notes][git-notes]. To fetch them, run:

``` shell
git fetch origin "refs/notes/*:refs/notes/*"
```

## Website & documentation

Please refer to the [documentation guide](DOCUMENTATION.md) before making
changes to the website or the documentation.


[`svn/*`]:     https://github.com/dosbox-automation/dosbox-automation/branches/all?utf8=%E2%9C%93&query=svn%2F
[`svn/trunk`]: https://github.com/dosbox-automation/dosbox-automation/tree/svn/trunk
[`vogons/*`]:  https://github.com/dosbox-automation/dosbox-automation/branches/all?utf8=%E2%9C%93&query=vogons%2F
[`forks/*`]:   https://github.com/dosbox-automation/dosbox-automation/branches/all?utf8=%E2%9C%93&query=forks%2F
[git-notes]:   https://git-scm.com/docs/git-notes

[gpl-badge]:     https://img.shields.io/badge/license-GPL--2.0--or--later-blue

*Note: this is a preliminarily document version. Information may be outdated and will reworked*

---
Built with AI-assisted development, using industry-standard software engineering practices. See [CONTRIBUTING.md](docs/CONTRIBUTING.md) for exactly what that means.
