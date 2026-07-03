#!/usr/bin/env python3
# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Record a manual DOS game installation for E2E test replay.

Starts DOSBox on screen with drives mounted, begins input recording
and ZMBV video capture, then waits for you to complete the install
manually. Press Ctrl+C in this terminal when done.

The recorded input events are saved as JSON in the game's disk
directory for replay by the E2E test suite.

Usage:
    python3 tests/integration/record_install.py epic-pinball
"""

import json
import os
import secrets
import shutil
import signal
import sys
import textwrap
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dosbox_client import DosboxClient
from e2e_helpers import (
    GameManifest,
    download_disk_images,
    resolve_cycle_settings,
    resolve_keyboard_layout,
)

DOSBOX_BIN = os.environ.get(
    "DOSBOX_BIN",
    str(Path(__file__).resolve().parents[2] / "build" / "debug-linux" / "dosbox"),
)
DISKS_DIR = Path(__file__).resolve().parents[1] / "files" / "disks"
WORKSPACE = Path(__file__).resolve().parents[2] / ".workspace" / "test-runs"


def find_free_port():
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    if len(sys.argv) < 2:
        print("Usage: record_install.py <game-slug>")
        print()
        print("Available games:")
        for d in sorted(DISKS_DIR.iterdir()):
            if (d / "manifest.toml").exists():
                print(f"  {d.name}")
        sys.exit(1)

    slug = sys.argv[1]
    game_dir = DISKS_DIR / slug
    manifest = GameManifest.from_file(game_dir / "manifest.toml")

    # Download if needed and permitted.
    if not all((game_dir / img).exists() for img in manifest.disc_images):
        print(f"Downloading disk images for {manifest.name}...")
        if not download_disk_images(manifest, game_dir):
            print("Download failed or not permitted (commercial license).")
            sys.exit(1)

    run_id = secrets.token_hex(4)
    hd_dir = game_dir / f"run-{run_id}"
    hd_dir.mkdir()
    work_dir = WORKSPACE / f"record-{run_id}"
    work_dir.mkdir(parents=True, exist_ok=True)

    port = find_free_port()
    token = secrets.token_hex(32)

    # Build autoexec.
    autoexec = [f'mount {manifest.target_drive} "{hd_dir}"']
    if manifest.media in ("cdrom-iso", "cdrom-cue"):
        autoexec.append(
            f'mount {manifest.source_drive} '
            f'"{game_dir / manifest.disc_images[0]}" -t cdrom'
        )
    elif manifest.media == "booter":
        autoexec = [
            f'mount a "{game_dir / manifest.disc_images[0]}" -t floppy'
        ]
    elif manifest.media == "directory":
        autoexec.append(
            f'mount {manifest.source_drive} '
            f'"{game_dir / manifest.disc_images[0]}"'
        )
    elif manifest.media == "zip":
        autoexec.append(
            f'mount {manifest.source_drive} "{game_dir}"'
        )
    else:
        autoexec.append(
            f'mount {manifest.source_drive} '
            f'"{game_dir / manifest.disc_images[0]}" -t floppy'
        )

    # Config in game_dir for conf anchor. The recording is replayed later at
    # this same rate (persisted into recording.json below), so both the real
    # and protected mode rates must be fixed, not auto. Pin both or the game
    # jumps to the 60000 protected default once it leaves real mode.
    # The keyboard layout is pinned for the same reason: recordings store
    # scancodes, and the layout decides what characters they become.
    cycles = resolve_cycle_settings(None, manifest.settings)
    layout = resolve_keyboard_layout(None, manifest.settings)
    conf_path = game_dir / f"record-{run_id}.conf"
    conf_path.write_text(
        f"[cpu]\ncpu_cycles = {cycles['cpu_cycles']}\n"
        f"cpu_cycles_protected = {cycles['cpu_cycles_protected']}\n\n"
        f"[dos]\nkeyboard_layout = {layout['keyboard_layout']}\n\n"
        "[autoexec]\n" + "\n".join(autoexec) + "\n"
    )

    # Primary config for Y: drive whitelist.
    dosbox_bin = Path(DOSBOX_BIN).resolve()
    resource_dir = dosbox_bin.parent / "resources"
    config_dir = work_dir / ".config" / "dosbox-automation"
    config_dir.mkdir(parents=True, exist_ok=True)
    (config_dir / "dosbox-automation.conf").write_text(
        f"[webserver]\nmount_allowed_bases = {resource_dir}\n"
        f"mount_allowed_image_roots = {game_dir}\n"
    )

    env = {
        **os.environ,
        "HOME": str(work_dir),
        "DOSBOX_API_TOKEN": token,
    }

    import subprocess
    cmd = [
        str(dosbox_bin),
        "--noprimaryconf",
        "--nolocalconf",
        "--set", "webserver_enabled=true",
        "--set", f"webserver_port={port}",
        "--set", f"capture_dir={work_dir / 'capture'}",
        "--set", "output=texture",
        "--set", "joysticktype=none",
        "-conf", str(conf_path),
    ]

    print(f"Starting DOSBox for {manifest.name}...")
    print(f"  Port:  {port}")
    print(f"  Token: {token}")
    print(f"  HD:    {hd_dir}")
    print()
    proc = subprocess.Popen(cmd, env=env, cwd=str(work_dir))

    client = DosboxClient(f"http://127.0.0.1:{port}", token=token)
    try:
        client.wait_ready(timeout=15)
        client.wait_shell(timeout=15)
    except TimeoutError:
        print("DOSBox did not reach shell.")
        proc.kill()
        sys.exit(1)

    # Start recording and capture.
    time.sleep(0.5)
    client.recording_start()
    client.capture_start()

    print("Recording started. Install the game manually.")
    print(f"  Type: {manifest.source_drive}:\\{manifest.installer_path}")
    print()
    print("When the install is done and you're back at the DOS prompt,")
    print("press ENTER here to stop recording and save.")
    print("(Do NOT close DOSBox first.)")
    print()

    # Wait for Enter from the terminal, or DOSBox exit.
    import select
    try:
        while proc.poll() is None:
            ready, _, _ = select.select([sys.stdin], [], [], 0.5)
            if ready:
                sys.stdin.readline()
                break
    except KeyboardInterrupt:
        pass

    if proc.poll() is not None:
        print("DOSBox exited before recording could be saved.")
        print("Run again and press ENTER here when done (before closing DOSBox).")
        conf_path.unlink(missing_ok=True)
        resolved_hd = hd_dir.resolve()
        resolved_game = game_dir.resolve()
        if (resolved_hd.parent == resolved_game
                and resolved_hd.name.startswith("run-")
                and resolved_hd.is_dir()):
            shutil.rmtree(resolved_hd, ignore_errors=True)
        sys.exit(1)

    print()
    print("Stopping recording...")

    # Stop capture.
    try:
        client.capture_stop()
    except Exception:
        pass

    # Stop recording and get events.
    try:
        r = client.recording_stop()
        if r.status_code == 200:
            data = r.json()
            event_count = data.get("event_count", 0)
            duration = data.get("duration_ms", 0)

            # Pin the rates and layout this session ran at so replay
            # matches them.
            data["cpu_cycles"] = cycles["cpu_cycles"]
            data["cpu_cycles_protected"] = cycles["cpu_cycles_protected"]
            data["keyboard_layout"] = layout["keyboard_layout"]

            # Save the recording.
            recording_path = game_dir / "recording.json"
            with open(recording_path, "w") as f:
                json.dump(data, f, indent=2)

            print(f"Saved {event_count} events ({duration:.0f}ms) to:")
            print(f"  {recording_path}")
        else:
            print(f"Recording stop failed: {r.status_code} {r.text[:200]}")
    except Exception as e:
        print(f"Could not retrieve recording: {e}")

    # Shut down DOSBox.
    try:
        client.shutdown()
        proc.wait(timeout=5)
    except Exception:
        proc.kill()
        proc.wait(timeout=3)

    # Collect provenance (ZMBV captures).
    from e2e_helpers import collect_provenance
    collect_provenance(work_dir, game_dir, slug)
    print(f"Provenance written to {game_dir / 'provenance'}")

    # Clean up.
    conf_path.unlink(missing_ok=True)
    resolved_hd = hd_dir.resolve()
    resolved_game = game_dir.resolve()
    if (resolved_hd.parent == resolved_game
            and resolved_hd.name.startswith("run-")
            and resolved_hd.is_dir()):
        shutil.rmtree(resolved_hd, ignore_errors=True)

    print("Done.")


if __name__ == "__main__":
    main()
