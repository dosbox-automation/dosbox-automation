#!/usr/bin/env python3
# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Run an E2E installer test on screen with ZMBV recording.

Usage:
    python3 tests/integration/run_e2e_visual.py epic-pinball
"""

import os
import secrets
import shutil
import subprocess
import sys
import textwrap
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dosbox_client import DosboxClient
from e2e_helpers import (
    GameManifest,
    collect_provenance,
    download_disk_images,
    generate_install_script,
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
        print("Usage: run_e2e_visual.py <game-slug>")
        print("Available games:")
        for d in sorted(DISKS_DIR.iterdir()):
            if (d / "manifest.toml").exists():
                print(f"  {d.name}")
        sys.exit(1)

    slug = sys.argv[1]
    game_dir = DISKS_DIR / slug
    manifest = GameManifest.from_file(game_dir / "manifest.toml")

    if not all((game_dir / img).exists() for img in manifest.disc_images):
        print(f"Downloading disk images for {manifest.name}...")
        if not download_disk_images(manifest, game_dir):
            print("Download failed.")
            sys.exit(1)

    run_id = secrets.token_hex(4)
    hd_dir = game_dir / f"run-{run_id}"
    hd_dir.mkdir()
    work_dir = WORKSPACE / f"visual-{run_id}"
    work_dir.mkdir(parents=True, exist_ok=True)

    port = find_free_port()
    token = secrets.token_hex(32)

    # Build autoexec.
    if manifest.media == "booter":
        autoexec = [f'mount a "{game_dir / manifest.disc_images[0]}" -t floppy']
    elif manifest.media in ("cdrom-iso", "cdrom-cue"):
        autoexec = [
            f'mount {manifest.target_drive} "{hd_dir}"',
            f'mount {manifest.source_drive} "{game_dir / manifest.disc_images[0]}" -t cdrom',
        ]
    elif manifest.media == "directory":
        autoexec = [
            f'mount {manifest.target_drive} "{hd_dir}"',
            f'mount {manifest.source_drive} "{game_dir / manifest.disc_images[0]}"',
        ]
    else:
        autoexec = [
            f'mount {manifest.target_drive} "{hd_dir}"',
            f'mount {manifest.source_drive} "{game_dir / manifest.disc_images[0]}" -t floppy',
        ]

    # Write config in game_dir so conf anchor covers disk images and HD.
    conf_path = game_dir / f"test-visual-{run_id}.conf"
    conf_path.write_text(textwrap.dedent("""\
        [autoexec]
        {}
    """.format("\n        ".join(autoexec))))

    # Write primary config for mount policy.
    dosbox_bin = Path(DOSBOX_BIN).resolve()
    resource_dir = dosbox_bin.parent / "resources"
    config_dir = work_dir / ".config" / "dosbox-automation"
    config_dir.mkdir(parents=True, exist_ok=True)
    (config_dir / "dosbox-automation.conf").write_text(textwrap.dedent(f"""\
        [webserver]
        mount_allowed_bases = {resource_dir}
    """))

    env = {
        **os.environ,
        "SDL_AUDIODRIVER": "dummy",
        "HOME": str(work_dir),
        "DOSBOX_API_TOKEN": token,
    }

    cmd = [
        str(dosbox_bin),
        "--nolocalconf",
        "--set", "webserver_enabled=true",
        "--set", f"webserver_port={port}",
        "--set", f"capture_dir={work_dir / 'capture'}",
        "-conf", str(conf_path),
    ]

    print(f"Starting DOSBox on port {port}...")
    proc = subprocess.Popen(cmd, env=env, cwd=str(work_dir))

    client = DosboxClient(f"http://127.0.0.1:{port}", token=token)
    try:
        client.wait_ready(timeout=15)
        client.wait_shell(timeout=15)
    except TimeoutError:
        print("DOSBox did not reach shell in time.")
        proc.kill()
        sys.exit(1)

    print("Shell ready. Loading automation script...")
    time.sleep(1)

    script = generate_install_script(manifest)
    print("--- Script ---")
    print(script)
    print("--- End ---")
    print()

    r = client.script_load(script, name=f"install-{slug}", debug=True)
    print(f"Load: {r.status_code} {r.json()}")
    if r.status_code != 200:
        proc.kill()
        sys.exit(1)

    r = client.script_start()
    print(f"Start: {r.status_code}")

    # Poll for completion.
    current_disc = 1
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        r = client.script_status()
        if r.status_code != 200:
            print(f"Status error: {r.status_code} {r.text[:200]}")
            break
        data = r.json()
        state = data["state"]
        out = data.get("output", {})

        for key, val in out.items():
            if key.startswith("swap_") and isinstance(val, int):
                if val != current_disc:
                    disc_path = str(game_dir / manifest.disc_images[val - 1])
                    client.drive_swap(manifest.source_drive, disc_path)
                    current_disc = val
                    print(f"  Swapped to disc {val}")

        prompt_summary = {k: v for k, v in out.items() if k.startswith("prompt_")}
        if prompt_summary:
            print(f"  [{state}] {prompt_summary}")

        if state in ("completed", "error"):
            print(f"Final state: {state}")
            if "error" in data:
                print(f"Error: {data['error']}")
            print(f"Output: {out}")
            break
        time.sleep(0.5)

    # Collect provenance.
    print()
    collect_provenance(work_dir, game_dir, slug)
    print(f"Provenance written to {game_dir / 'provenance'}")

    # Verify files.
    if manifest.media != "booter":
        for f in manifest.verify_files:
            rel = f.split(":\\", 1)[1].replace("\\", "/")
            host_path = hd_dir / rel
            status = "OK" if host_path.exists() else "MISSING"
            print(f"  [{status}] {f}")

    # Wait for DOSBox to close or kill.
    print()
    print("Press Ctrl+C or close DOSBox window to finish.")
    try:
        proc.wait()
    except KeyboardInterrupt:
        proc.kill()
        proc.wait()

    # Cleanup.
    conf_path.unlink(missing_ok=True)
    resolved_hd = hd_dir.resolve()
    resolved_game = game_dir.resolve()
    if (resolved_hd.parent == resolved_game
            and resolved_hd.name.startswith("run-")
            and resolved_hd.is_dir()):
        shutil.rmtree(resolved_hd, ignore_errors=True)


if __name__ == "__main__":
    main()
