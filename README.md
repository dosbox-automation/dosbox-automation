# dosbox-automation

![GPL-2.0-or-later][gpl-badge]

A DOSBox variant for Linux and Windows with a local HTTP REST API for automated game installation, input recording with deterministic replay, and programmatic emulator control.

Based on DOSBox Staging 0.84. Your existing DOSBox configurations will continue to work.

## What it does

- Automated game installs, including multi-disk installs driven through the API
- Recording of keyboard and mouse input with deterministic frame-accurate replay
- Frame capture for screenshots and visual verification
- Lua scripting inside the emulator for install automation and testing
- Disk image swapping via API for multi-disk games
- Memory and CPU register inspection for debugging and modding
- Lifecycle control for integration with launchers and CI systems

## Security

If you open a web server, you open an attack surface. dosbox-automation ships with bearer token authentication, host header validation, mount path restrictions, and localhost-only binding. See the [security documentation](https://www.dosbox-automation.org/0.84-da1/automation/security/) for details.

## Project website

https://www.dosbox-automation.org/

## Downloads

Release builds are available on [GitHub](https://github.com/dosbox-automation/dosbox-automation/releases).

## Build from source

See the platform-specific build instructions:

- [Linux](docs/build-linux.md)
- [Windows](docs/build-windows.md)
- [macOS](docs/build-macos.md) (instructions taken verbatim from upstream
  DOSBox Staging, untested here - reports welcome)

## License

dosbox-automation is licensed under GNU GPL v2+, based on DOSBox Staging.

[gpl-badge]: https://img.shields.io/badge/license-GPL--2.0--or--later-blue

---
Built with AI-assisted development, using industry-standard software engineering practices. See [CONTRIBUTING.md](docs/CONTRIBUTING.md) for exactly what that means.
