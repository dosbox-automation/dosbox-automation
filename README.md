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
  A safer DOSBox fork with an HTTP REST API, sandbox Lua scripting, and frame-accurate input recording - for gamers, game recordings, game launchers, and CI pipelines alike.
</p>

<p align="center">
  Available for <strong>Linux, Windows</strong>&ensp;|&ensp;
  <a href="https://github.com/dosbox-automation/dosbox-automation/releases">Downloads</a>&ensp;|&ensp;
  <a href="https://www.dosbox-automation.org/">Documentation</a>&ensp;|&ensp;
  <a href="https://www.dosbox-automation.org/0.84-da4/automation/security/">Security</a>
</p>

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

**Better sound:** dosbox-automation includes the FluidR3 soundfont. General MIDI games sound right on the first run. Sound Blaster 16, AdLib, Gravis UltraSound, MT-32 emulation is included, as well the traditional PC beeper for old games.

**CRT shaders:** A combination of shaders simulate the look of old CRT monitors of yesteryear. The right shader settings for each graphics mode are picked automatically across CGA, EGA, VGA, SVGA, Tandy, and Hercules, with correct aspect ratios and integer scaling.

**Video and image capture:** You can record your gameplay with CTRL+F7, dosboxctl (included helper script in source distribution) or programmatic API calls, either in the original screen resolution or as they are rendered on screen.  

**Safer to run:** DOS games you download can be setup to only access folders where your dosbox configuration lives and it is harder for malicious setups to do harm to your computer.

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

Games can also be installed from TOML recipe files. A Python reference harness reads the recipe, downloads the media, runs the installer unattended, and handles multi-disk floppy swaps:

```toml
[game]
name = "DOOM Shareware"
publisher = "id Software"
year = 1993

[install]
media = [{ type = "cdrom", path = "doom-shareware.iso" }]
target = "C:\\DOOM"
```

Keyboard and mouse input can be recorded as JSON and replayed frame-accurately on another machine. The recording includes the CPU speed and keyboard layout so the replay is deterministic.

The API also gives you read and write access to guest memory and CPU registers. Useful for debugging DOS programs, building trainers, or writing test assertions that check what the game actually did.

The test suite has over 1100 C++ unit tests and 160 Python integration tests. Several of those are full game installations that run end to end through the API.

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

## Getting started

Download from the [releases page](https://github.com/dosbox-automation/dosbox-automation/releases), unpack, run. Linux ships as a portable tarball and AppImage, Windows as a zip and installer.

To use the API, add `webserver_enabled = true` to the `[webserver]` section in your config. The Swagger UI is then at `http://localhost:8386` with every endpoint documented and testable.

The [project manual](https://www.dosbox-automation.org/) has the rest.

---

## Security

An HTTP server inside an emulator is an attack surface and we treat it as one: bearer-token auth on every endpoint, host header validation, and mount restrictions that limit what the guest can reach on the host. The [security policy](https://www.dosbox-automation.org/0.84-da4/automation/security/) has the details.

Security reports go to the contact address in the policy. Everything else goes on the [issue tracker](https://github.com/dosbox-automation/dosbox-automation/issues).

---

## Build from source

- [Linux](docs/build-linux.md)
- [Windows](docs/build-windows.md)

---

## License

dosbox-automation's upstream code, its modifications and contributors is licensed under the GNU General Public License v2.0 or later.

---

<details>
<summary><strong>Screenshot gallery</strong> - one reel per graphics era, from Hercules to SVGA</summary>
<br>

### Hercules

<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/hercules/blockout-hercules.png" width="48%" alt="Blockout (Hercules)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/hercules/msflightsim4-hercules.png" width="48%" alt="Flight Simulator 4 (Hercules)">
</p>
<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/hercules/simant-hercules.png" width="48%" alt="SimAnt (Hercules)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/hercules/wonderland-hercules.png" width="48%" alt="Wonderland (Hercules)">
</p>

### CGA

<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/cga/popcorn-cga.png" width="48%" alt="Popcorn (CGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/cga/pharaohs-tomb-cga.png" width="48%" alt="Pharaoh's Tomb (CGA)">
</p>
<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/cga/simcity-cga.png" width="48%" alt="SimCity (CGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/cga/tetris-cga.png" width="48%" alt="Tetris (CGA)">
</p>

### Tandy

<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/tandy/zeliard-tandy.png" width="48%" alt="Zeliard (Tandy)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/tandy/grand-prix-circuit-tandy.png" width="48%" alt="Grand Prix Circuit (Tandy)">
</p>
<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/tandy/maniac-mansion-tandy.png" width="48%" alt="Maniac Mansion (Tandy)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/tandy/silpheed-tandy.png" width="48%" alt="Silpheed (Tandy)">
</p>

### EGA

<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/ega/outrun-ega.png" width="48%" alt="OutRun (EGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/ega/death-knights-of-krynn-ega.png" width="48%" alt="Death Knights of Krynn (EGA)">
</p>
<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/ega/commander-keen-4-ega.png" width="48%" alt="Commander Keen 4 (EGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/ega/space-quest-3-ega.png" width="48%" alt="Space Quest III (EGA)">
</p>

### VGA

<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/vga/legend-of-kyrandia-2-hand-of-fate-vga.png" width="48%" alt="Legend of Kyrandia: Hand of Fate (VGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/vga/budokan-vga.png" width="48%" alt="Budokan (VGA)">
</p>
<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/vga/epic-pinball-super-android-1-vga.png" width="48%" alt="Epic Pinball (VGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/vga/the-incredible-machine-vga.png" width="48%" alt="The Incredible Machine (VGA)">
</p>

### SVGA

<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/svga/simcity-2000-svga.png" width="48%" alt="SimCity 2000 (SVGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/svga/warcraft-2-svga.png" width="48%" alt="Warcraft II (SVGA)">
</p>
<p align="center">
  <img src="https://www.dosbox-automation.org/imgs/gallery/svga/panzer-general-svga.png" width="48%" alt="Panzer General (SVGA)">
  <img src="https://www.dosbox-automation.org/imgs/gallery/svga/master-of-orion-2-svga.png" width="48%" alt="Master of Orion II (SVGA)">
</p>

</details>

---

<p align="center">
  <sub>This project is developed with tooled assistance, but tested, reviewed and signed off by a human developer.</sub>
</p>
