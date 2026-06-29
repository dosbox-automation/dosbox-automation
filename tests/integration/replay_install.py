#!/usr/bin/env python3
# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Replay a recorded installation with a visible DOSBox window.

Usage:
    python3 tests/integration/replay_install.py doom-shareware
"""

import json
import os
import secrets
import shutil
import sys
import textwrap
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dosbox_client import DosboxClient
from e2e_helpers import GameManifest, resolve_cycle_settings

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
        print("Usage: replay_install.py <game-slug>")
        sys.exit(1)

    slug = sys.argv[1]
    game_dir = DISKS_DIR / slug
    manifest = GameManifest.from_file(game_dir / "manifest.toml")

    recording_path = game_dir / "recording.json"
    if not recording_path.exists():
        print(f"No recording.json in {game_dir}")
        sys.exit(1)

    with open(recording_path) as f:
        recording = json.load(f)

    events = recording.get("events", [])
    if not events:
        print("Recording has no events.")
        sys.exit(1)

    run_id = secrets.token_hex(4)
    hd_dir = game_dir / f"run-{run_id}"
    hd_dir.mkdir()
    work_dir = WORKSPACE / f"replay-{run_id}"
    work_dir.mkdir(parents=True, exist_ok=True)

    port = find_free_port()
    token = secrets.token_hex(32)

    # Build autoexec (same as record_install.py).
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
    else:
        autoexec.append(
            f'mount {manifest.source_drive} '
            f'"{game_dir / manifest.disc_images[0]}" -t floppy'
        )

    # Replay at the rates the recording was made at, not DOSBox's defaults.
    # Pin both or the game jumps to 60000 in protected mode.
    cycles = resolve_cycle_settings(recording, manifest.settings)
    conf_path = game_dir / f"replay-{run_id}.conf"
    conf_path.write_text(
        f"[cpu]\ncpu_cycles = {cycles['cpu_cycles']}\n"
        f"cpu_cycles_protected = {cycles['cpu_cycles_protected']}\n\n"
        "[autoexec]\n" + "\n".join(autoexec) + "\n"
    )

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
        "--nolocalconf",
        "--set", "webserver_enabled=true",
        "--set", f"webserver_port={port}",
        "--set", f"capture_dir={work_dir / 'capture'}",
        "-conf", str(conf_path),
    ]

    print(f"Starting DOSBox for {manifest.name} (replay)...")
    print(f"  Port: {port}")
    proc = subprocess.Popen(cmd, env=env, cwd=str(work_dir))

    client = DosboxClient(f"http://127.0.0.1:{port}", token=token)
    try:
        client.wait_ready(timeout=15)
        client.wait_shell(timeout=15)
    except TimeoutError:
        print("DOSBox did not reach shell.")
        proc.kill()
        sys.exit(1)

    time.sleep(1.0)

    # Split events at drive_swap markers, preserving the time gap
    # between each swap and the next chunk's first event.
    chunks = []
    current = []
    for event in events:
        if event.get("type") == "drive_swap":
            chunks.append((current, event))
            current = []
        else:
            current.append(event)
    chunks.append((current, None))

    print(f"Replaying {len(events)} events in {len(chunks)} chunks...")

    for i, (input_events, swap) in enumerate(chunks):
        if input_events:
            last_t = max(e.get("t", 0) for e in input_events)
            first_t = min(e.get("t", 0) for e in input_events)
            duration_s = (last_t - first_t) / 1000.0
            print(f"  Chunk {i+1}: {len(input_events)} events, ~{duration_s:.1f}s")
            r = client.input_sequence(input_events)
            if r.status_code != 200:
                print(f"  Input sequence failed: {r.status_code} {r.text[:200]}")
                break

            wait_s = duration_s + 3.0
            print(f"  Waiting {wait_s:.1f}s for replay to finish...")
            time.sleep(wait_s)

        if swap:
            image_path = str(game_dir / swap["image"])
            swap_t = swap.get("t", 0)

            # Preserve the original gap between the swap and the next
            # chunk's first event (installer extracting files, etc).
            next_chunk_events = chunks[i + 1][0] if i + 1 < len(chunks) else []
            if next_chunk_events:
                next_first_t = min(e.get("t", 0) for e in next_chunk_events)
                gap_s = (next_first_t - swap_t) / 1000.0
            else:
                gap_s = 1.0

            print(f"  Swapping to {swap['image']}...")
            r = client.drive_swap(swap["drive"], image_path)
            if r.status_code == 200:
                print(f"  Swapped {swap['drive']}: to {swap['image']}")
            else:
                print(f"  Swap failed: {r.status_code} {r.text[:200]}")
                break

            print(f"  Waiting {gap_s:.1f}s (original gap before next input)...")
            time.sleep(gap_s)

    print()
    print("Replay sent. Watching for completion...")
    print("Press ENTER here to stop and verify, or wait for the shell.")

    import select
    deadline = time.monotonic() + 300
    try:
        while proc.poll() is None and time.monotonic() < deadline:
            ready, _, _ = select.select([sys.stdin], [], [], 1.0)
            if ready:
                sys.stdin.readline()
                break
            try:
                ps = client.program_state()
                if ps.status_code == 200 and ps.json().get("is_shell"):
                    print("Back at DOS prompt.")
                    time.sleep(2)
                    break
            except Exception:
                pass
    except KeyboardInterrupt:
        pass

    # Verify installed files.
    print()
    print("Verifying installed files:")
    all_ok = True
    for expected_file in manifest.verify_files:
        rel = expected_file.split(":\\", 1)[1].replace("\\", "/")
        host_path = hd_dir / rel
        if host_path.exists():
            print(f"  OK: {expected_file} ({host_path.stat().st_size} bytes)")
        else:
            print(f"  MISSING: {expected_file}")
            all_ok = False

    if all_ok:
        print("\nAll verification files present.")
    else:
        print("\nSome files missing.")

    # Shut down.
    try:
        client.shutdown()
        proc.wait(timeout=5)
    except Exception:
        proc.kill()
        proc.wait(timeout=3)

    # Clean up.
    conf_path.unlink(missing_ok=True)
    resolved_hd = hd_dir.resolve()
    resolved_game = game_dir.resolve()
    if (resolved_hd.parent == resolved_game
            and resolved_hd.name.startswith("run-")
            and resolved_hd.is_dir()):
        shutil.rmtree(resolved_hd, ignore_errors=True)

    print("Done." if all_ok else "Done (with failures).")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
