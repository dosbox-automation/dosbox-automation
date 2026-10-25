<!-- This file is part of the dosbox-automation Project. -->
<!-- License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net -->

<p align="center">
  <a href="https://www.dosbox-automation.org/">
    <img src="https://www.dosbox-automation.org/imgs/logo-splash.png" alt="dosbox-automation" width="420">
  </a>
</p>

<h2 align="center">
dosbox-automation
</h2>

<h3 align="center">
  A DOS emulator you can play with <em>and</em> fully remotely control.
</h3>

<p align="center">
  A safer DOSBox staging fork with an HTTP REST API, sandbox Lua scripting, frame-accurate input recording, and games that run straight from their zip files - for gamers, game recordings, game launchers, AI agents, and CI pipelines alike.
</p>

<p align="center">
  Releases for <strong>Linux, Windows</strong>, builds from source on <strong>macOS, FreeBSD</strong>&ensp;|&ensp;
  <a href="https://github.com/dosbox-automation/dosbox-automation/releases">Downloads</a>&ensp;|&ensp;
  <a href="https://www.dosbox-automation.org/">Documentation</a>&ensp;|&ensp;
  <a href="https://www.dosbox-automation.org/0.85.1/automation/security/">Security</a>
</p>

---

## Getting started

Download from the [releases page](https://github.com/dosbox-automation/dosbox-automation/releases), unpack, run. Linux ships as a portable tarball and AppImage, Windows as a zip and installer.

With a build from the current sources you can also hand it a game archive directly: `dosbox-automation "Commander Keen 4.zip"` mounts the zip as drive C: and starts the game. See [Games straight from their archives](#games-straight-from-their-archives) below.

To use the API, add `webserver_enabled = true` to the `[webserver]` section in your config. The Swagger UI is then at `http://localhost:8386` with every endpoint documented and testable.

The [project manual](https://www.dosbox-automation.org/) has the rest.

## Platform support

The table applies to 0.85.0 and to builds from the current sources.

| Distribution | Environment | Icon | Decoration |
|---|---|---|---|
| Debian 13 | X11 / KDE | yes | yes |
| Ubuntu 22.04 LTS | Wayland / GNOME | yes (1) | yes (2) |
| Ubuntu 26.04 LTS | Wayland / GNOME | yes (1) | yes (2) |
| Fedora 44 GNOME | Wayland / GNOME | yes (1) | yes (2) |
| Pop!_OS 22.04 | X11 / GNOME | yes (1) | yes |
| Linux Mint 21 | X11 / XFCE | yes | yes |
| Arch (vanilla) | XLibre / LXQt | yes | yes |
| Windows 10 | Windows DWM | yes | yes |

(1) User will be asked for permission to install the application icon for the dock.
(2) CSD decoration, light/dark aware, with XWayland native decoration without fallback.

The compat AppImage (glibc 2.35, for FUSE-based systems) will not have window decorations on GNOME on newer distributions due to a glibc version mismatch with the system's pangocairo. Use the standard AppImage there.

Running a combination not listed here? Open an issue with a screenshot showing the dock icon and title bar, plus the log lines where dosbox-automation prints the detected session type and display server. We will add verified setups to the table.

**macOS and FreeBSD:** dosbox-automation builds from source on both, and the full unit test suite passes on each: macOS on an Apple Silicon Mac mini, with Homebrew or vcpkg libraries, and FreeBSD 15 against its own packages. There are no release packages for either yet. The build pages for [macOS](docs/build/macos.md) and [FreeBSD](docs/build/freebsd.md) have the steps.

### Runtime dependencies (Linux tarball)

The AppImage is self-contained. The portable tarball links statically against SDL3 and its dependencies, but the display and audio backends are loaded at runtime from the system. Any desktop installation with a graphical session has these already.

On a minimal or headless system, install the following:

**Debian / Ubuntu:**
```
sudo apt install libgl1 libdecor-0-0 libxkbcommon0 libwayland-client0 \
  libx11-6 libxcursor1 libxrandr2 libxi6 \
  libasound2t64 libpulse0 libpipewire-0.3-0t64
```

**Fedora:**
```
sudo dnf install mesa-libGL libdecor libxkbcommon libwayland-client \
  libX11 libXcursor libXrandr libXi \
  alsa-lib pulseaudio-libs pipewire-libs
```

---

<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/vga/budokan-vga.png" width="48%" alt="Budokan (VGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/ega/commander-keen-4-ega.png" width="48%" alt="Commander Keen 4 (EGA)">
</p>
<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/cga/popcorn-cga.png" width="48%" alt="Popcorn (CGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/svga/simcity-2000-svga.png" width="48%" alt="SimCity 2000 (SVGA)">
</p>

<p align="center">
  <em>All screenshots captured from dosbox-automation's own rendering, with adaptive CRT shaders and correct aspect ratios - zero configuration.</em>
</p>

---

## Playing games

dosbox-automation runs DOS games with good defaults for graphics and sound. The automation features are additional, not a replacement for normal use.

<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/svga/warcraft-2-svga.png" width="48%" alt="Warcraft 2 (SVGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/tandy/grand-prix-circuit-tandy.png" width="48%" alt="Grand Prix Circuit (Tandy)">
</p>

**Better sound:** dosbox-automation includes the FluidR3 soundfont. General MIDI games sound right on the first run. Sound Blaster 16, AdLib, Gravis UltraSound and MT-32 emulation are included, as well as the traditional PC beeper for old games.

**CRT shaders:** A combination of shaders simulate the look of old CRT monitors of yesteryear. The right shader settings for each graphics mode are picked automatically across CGA, EGA, VGA, SVGA, Tandy, and Hercules, with correct aspect ratios and integer scaling.

**Sharp text with TrueType:** For long sessions in text mode, `ttf_output = true` draws the DOS screen with a scalable TrueType font instead of scaling up the 8x16 bitmap font. The bundled font is VileR's Flexi IBM VGA, which keeps the look of the VGA text mode and covers around 75 DOS code pages; any monospace font with the needed glyphs works too. Programs that load their own screen font keep it, because the output steps aside while the font is changed. FeralChild64 wrote the feature and tells the whole story, with pictures, in [TrueType text output for dosbox-automation](https://feralchild64.github.io/Articles/2026.10.21%20-%20TrueType%20text%20output%20for%20dosbox-automation.html). It is in the current sources and comes with the next release.

**A DOS that behaves like DOS:** APPEND works as it did on MS-DOS, `/X` and `/PATH` included, so the games and tools of the time that look for their data through it find it. SUBST and JOIN work too. The Y: drive carries a set of tools on the path: EDIT (Mateusz Viste's MSEDIT), our own VIEW for reading a file of any size as text or hex, down to an 8086 with DOS 2.0, plus FIND, SORT, XCOPY, DELTREE, DEBUG, ZIP and UNZIP.

**Video and image capture:** You can record your gameplay with CTRL+F7, dosboxctl (included helper script in source distribution) or programmatic API calls, either in the original screen resolution or as they are rendered on screen.  

**Safer to run:** DOS games you download can be set up to only access folders where your dosbox configuration lives and it is harder for malicious setups to do harm to your computer.

<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/vga/legend-of-kyrandia-2-hand-of-fate-vga.png" width="32%" alt="Legend of Kyrandia 2: Hand of Fate (VGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/hercules/blockout-hercules.png" width="32%" alt="Blockout (Hercules)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/ega/railroad-tycoon-ega.png" width="32%" alt="Railroad Tycoon (EGA)">
</p>
<p align="center">
  <em>Left to right: Legend of Kyrandia 2: Hand of Fate on VGA, Blockout on Hercules, Railroad Tycoon on EGA.</em>
</p>

For the full feature set see the [documentation](https://www.dosbox-automation.org/).

---

## Games straight from their archives

Most DOS games reach you today as a zip file, and the usual routine is to unpack it into a folder, mount the folder, and keep track of which folder belonged to which game. With a build from the current sources you can skip that. dosbox-automation mounts the archive itself and the game runs from it. The archive stays exactly as it is, byte for byte.

```bash
dosbox-automation "Commander Keen 4.zip"
dosbox-automation "Keen Collection.zip" --run 'KEEN4\KEEN4E.EXE'
```

Given an archive on the command line, dosbox-automation mounts it as C:, starts the one program at its root and quits when that program ends. If there is more than one program, `--run` picks it. Without `--run`, a small menu comes up in DOS and lets you choose; ESC leaves you at the prompt. Whoever packs an archive can name the entries by hand: a `CHOOSER.TML` at its root gives the menu its title, labels and the order, and can mark the one entry that starts on its own.

Inside DOS the same works with MOUNT:

```
mount c d:\games\doom.zip
mount c doom.zip -run doom.exe -exit
mount c collection.zip/doom2.zip
```

ZIP, RAR, 7z and tar archives mount, and an archive inside an archive is reached by naming the path to it, as in the last line. Disk and CD images inside an archive mount like images on the host: a floppy inside a zip boots, a floppy set inside a 7z swaps with Ctrl+F4, and a CUE sheet finds the files it names next to it in the archive. Compressed single images (`.imz`, `.isz`, `disk1.img.xz`, `game.iso.gz`) mount as the image they contain.

The game can still write. Saved games, high scores and configuration files go to a save folder of the game's own, outside the archive, and the game sees them on its drive next to its original files. Delete that folder and the game is back to the state it came in. `-scratch` keeps the writes for that mount only, which suits an installer CD, and `-nosidecar` makes the drive read-only.

A few things are good to know. 7z, RAR 5 and tar archives cannot hand out one file without unpacking everything before it, so they are refused until you set `archive_sequential = cache`; then they are unpacked once into a cache and served from there. Solid RAR 4 archives are refused either way. The cache has a size cap and keeps unused entries for a week, and `mount -cache` shows what is in it. Archives that ask for a password, and archives split into volumes, are refused with a message that says so.

---

## Automate your DOS

dosbox-automation has an HTTP REST API and a Lua scripting engine built in. You can install games, type commands, swap disks, grab screenshots, and shut down the emulator without ever touching the keyboard.

To get started, enable the web server in your config and set up a token. Every API call needs one.

```ini
[webserver]
webserver_enabled = true
webserver_token_file = true
```

Start dosbox-automation, wait a couple of seconds, and the token is written to `~/.config/dosbox-automation/webserver/api_token`. You can also set the `DOSBOX_API_TOKEN` environment variable before starting if you prefer to generate your own. The Swagger UI is at `http://localhost:8386` once the server is running.

```bash
# grab a screenshot
curl -s http://localhost:8386/api/v1/video/frame \
     -H "Authorization: Bearer $TOKEN" \
     --output screenshot.png

# read what is on screen (text modes)
curl -s http://localhost:8386/api/v1/video/text \
     -H "Authorization: Bearer $TOKEN"

# type a command
curl -X POST http://localhost:8386/api/v1/input/type \
     -H "Authorization: Bearer $TOKEN" \
     -d '{"text": "dir c:\n"}'
```

Lua scripts run on the emulation thread with frame-accurate timing. They can wait for specific text on screen, navigate setup menus, swap floppy disks, and report results back through the API.

Games can also be installed from TOML recipe files. The Python reference harness in `tests/integration/` reads the recipe, fetches the media, runs the installer unattended, answers its questions and swaps the floppies when it asks. This is the DOOM shareware recipe, cut down to the parts that matter; the 7z with both floppy images is mounted as it is:

```toml
[game]
name = "DOOM Shareware"
media = "floppy"

[discs]
archive = "Doom Shareware Floppy (v1.666) [2 Disk].7z"
images = [
    "Doom Shareware Floppy (v1.666) [Disk 1 of 2].ima",
    "Doom Shareware Floppy (v1.666) [Disk 2 of 2].ima",
]

[installer]
path = "SETUP.EXE"
source_drive = "A"
target_drive = "C"

[prompts]
sequence = [
    { wait = "Available Drives", type = "C" },
    { wait = "insert disk DOOM 2", action = "swap:2", key = "enter" },
]
```

The [recipes page](https://www.dosbox-automation.org/0.85.1/automation/recipes/) of the manual has the full format.

Keyboard and mouse input can be recorded as JSON and replayed frame-accurately on another machine. The recording includes the CPU speed and keyboard layout so the replay is deterministic.

The API also gives you read and write access to guest memory, CPU registers and I/O ports, with memory search and value freezes on top. Useful for debugging DOS programs, building trainers, or writing test assertions that check what the game actually did.

**AI agents:** [dosbox-mcp](https://github.com/dosbox-automation/dosbox-mcp) connects any agent that speaks the Model Context Protocol, such as Claude Code or Claude Desktop, to a running emulator. The agent types into DOS programs, reads the screen, takes frames and video, runs Lua scripts and inspects memory, all through the same API and the same token.

The test suite has over 2300 C++ unit tests and about 300 Python integration tests. Several of those are full game installations that run end to end through the API.

---

## The Cheat Workbench

The Cheat Workbench is a web page that dosbox-automation serves itself whenever the API is on, and a browser on the same machine connects to it without a token. Every download carries a small launcher that starts the emulator with the API on and a fresh token, and prints the page address, also for a browser on another machine. In the current sources you can open it from inside, too: type `WORKBENCH` at the DOS prompt or press Ctrl+Alt+W.

<p align="center">
  <img src="https://www.dosbox-automation.org/docs-images/manual/workbench-opened.jpg" width="80%" alt="The Cheat Workbench, connected, with Commander Keen 4 running">
</p>

The page puts everything the API can do with memory into one place. The scanner finds a value, say your ammo, and narrows it down scan by scan as the value changes. The cheat table keeps what you found, writes it, and freezes it so the health stays put. Beside them sit a hex viewer with bookmarks and write watches, the CPU registers, the DOS memory blocks and the program's PSP. The page can also capture video, screenshots and audio, set the mixer, type into the game, record your input and trim the recording on a timeline, and export it as JSON or as a Lua script you can replay.

The [Cheat Workbench page](https://www.dosbox-automation.org/0.85.1/automation/cheat-workbench/) of the manual walks through it panel by panel, and its [two-minute video](https://youtu.be/FeRsGde-AH4) finds, writes and freezes the ammo counter in Commander Keen 4.

---

## Video showcase

Recorded and scripted with the included reference harness, driving the emulator through the REST API.

| | |
|:---:|:---:|
| [**Quest for Glory IV**](https://www.dosbox-automation.org/#video-showcase) | [**System Shock**](https://www.dosbox-automation.org/#video-showcase) |
| Floppy install with automated disk swaps | CD-ROM install, fully unattended |
| [**DOOM shareware**](https://www.dosbox-automation.org/#video-showcase) | [**Epic Pinball**](https://www.dosbox-automation.org/#video-showcase) |
| Install and launch | Installation replay |
| [**The Incredible Machine**](https://www.dosbox-automation.org/#video-showcase) | [**Turbo Pascal 5.5**](https://www.dosbox-automation.org/#video-showcase) |
| Demo install by recipe | Compiler install, scripted end to end |

[Watch the videos on dosbox-automation.org](https://www.dosbox-automation.org/#video-showcase)

---

## Security

An HTTP server inside an emulator is an attack surface and we treat it as one: bearer-token auth on every endpoint, host header validation, and mount restrictions that limit what the guest can reach on the host. The [security policy](https://www.dosbox-automation.org/0.85.1/automation/security/) has the details.

Security reports go to the contact address in the policy. Everything else goes on the [issue tracker](https://github.com/dosbox-automation/dosbox-automation/issues).

---

## Build from source

- [General instructions](docs/build/README.md)
- [Linux](docs/build/linux.md)
- [Windows](docs/build/windows.md)
- [macOS](docs/build/macos.md)
- [FreeBSD](docs/build/freebsd.md)

---

## License

dosbox-automation is licensed under the GNU General Public License v2.0 or later, like the DOSBox Staging code it builds on. Bundled third-party libraries and tools keep their own licenses; [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt) lists them. UniMenu, the program menu on the bundled Y: drive, ships as a binary under its own freeware licence (resources/drives/y/dos/unimenu.txt).

---

<p align="center">
  <sub>dosbox-automation has been written with the help of tooled assistance, but has been designed, reviewed and thoroughly tested and signed off by its developer.</sub>
</p>
