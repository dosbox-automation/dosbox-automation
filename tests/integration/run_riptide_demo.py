#!/usr/bin/env python3
"""Live demo: install Riptide from White Wolf CD via dosbox-automation API.

Run this directly to watch the installation demo.
"""

import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dosbox_client import DosboxClient

DOSBOX_BIN = os.environ.get(
    "DOSBOX_BIN",
    str(Path(__file__).resolve().parents[2] / "build" / "debug-linux" / "dosbox"),
)
DATA_DIR = Path(__file__).resolve().parent / "data"
WORKSPACE = Path(__file__).resolve().parents[2] / ".workspace" / "test-runs"


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def capture(client, shots_dir, name, pane=True):
    path = shots_dir / f"{name}.jpg"
    client.capture_frame(path)
    if pane:
        try:
            import requests
            requests.post(
                "http://localhost:13338/tool/pane_show",
                json={
                    "content_type": "image",
                    "path": str(path),
                    "title": "dosbox-automation",
                    "subtitle": name.replace("-", " "),
                },
                timeout=2,
            )
        except Exception:
            pass
    return path


def main():
    port = find_free_port()
    work_dir = WORKSPACE / f"riptide-demo-{port}"
    work_dir.mkdir(parents=True, exist_ok=True)
    shots_dir = work_dir / "screenshots"
    shots_dir.mkdir(exist_ok=True)

    c_drive = work_dir / "c_drive"
    c_drive.mkdir(exist_ok=True)

    iso_path = DATA_DIR / "white-wolf-11" / "white-wolf-series-11.iso"

    print(f"Starting DOSBox on port {port}...")
    headless = os.environ.get("DOSBOX_HEADLESS", "0") == "1"
    env = {**os.environ, "HOME": str(work_dir)}
    if headless:
        env["SDL_VIDEODRIVER"] = "dummy"
        env["SDL_AUDIODRIVER"] = "dummy"

    token_re = re.compile(r"WEBSERVER: API token: ([0-9a-f]{64})")
    token_holder = {"token": None}
    token_found = threading.Event()
    stderr_log = work_dir / "dosbox-stderr.log"

    def read_stderr(pipe, log_path):
        with open(log_path, "w") as log:
            for raw in pipe:
                line = raw.decode(errors="replace").rstrip()
                log.write(line + "\n")
                log.flush()
                m = token_re.search(line)
                if m:
                    token_holder["token"] = m.group(1)
                    token_found.set()

    proc = subprocess.Popen(
        [
            DOSBOX_BIN,
            "--noprimaryconf",
            "--nolocalconf",
            "--set", "webserver_enabled=true",
            "--set", f"webserver_port={port}",
            "-c", f"MOUNT D {iso_path} -t iso",
            str(c_drive),
        ],
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        cwd=str(work_dir),
    )

    reader = threading.Thread(
        target=read_stderr, args=(proc.stderr, stderr_log), daemon=True
    )
    reader.start()

    if not token_found.wait(timeout=10):
        proc.kill()
        sys.exit("ERROR: DOSBox did not emit API token within 10s")

    client = DosboxClient(f"http://127.0.0.1:{port}", token=token_holder["token"])

    try:
        client.wait_ready(timeout=10)
        client.wait_shell(timeout=10)
        print("DOSBox ready.")

        # Step 1: Initial shell
        capture(client, shots_dir, "01-shell")
        print("  [1/8] Shell prompt")
        time.sleep(0.5)

        # Step 2: Switch to D: drive and list Riptide
        client.type_string("D:\n")
        time.sleep(1)
        capture(client, shots_dir, "02-d-drive")
        print("  [2/8] Switched to D:")

        client.type_string("dir riptide\n")
        time.sleep(1.5)
        capture(client, shots_dir, "03-dir-riptide")
        print("  [3/8] DIR RIPTIDE")

        # Step 3: Run the installer
        client.type_string("cd riptide\n")
        time.sleep(0.5)
        client.type_string("install\n")
        time.sleep(3)
        capture(client, shots_dir, "04-installer-start")
        print("  [4/8] Installer started")

        # Step 4: Navigate installer — press Enter through prompts
        client.press_key("KBD_enter")
        time.sleep(2)
        capture(client, shots_dir, "05-installer-prompt1")
        print("  [5/8] Installer prompt 1")

        client.press_key("KBD_enter")
        time.sleep(2)
        capture(client, shots_dir, "06-installer-prompt2")
        print("  [6/8] Installer prompt 2")

        client.press_key("KBD_enter")
        time.sleep(3)
        capture(client, shots_dir, "07-installer-done")
        print("  [7/8] Installer progress")

        # Keep pressing Enter until we're back at the shell
        for i in range(5):
            client.press_key("KBD_enter")
            time.sleep(1.5)
            state = client.program_state().json()
            capture(client, shots_dir, f"08-post-install-{i}")
            if state.get("is_shell"):
                print(f"  [8/8] Back at shell after {i+1} Enter presses")
                break

        # Step 5: Check what got installed
        client.type_string("C:\n")
        time.sleep(0.5)
        client.type_string("dir\n")
        time.sleep(1.5)
        capture(client, shots_dir, "09-c-drive-contents")
        print("  Installed files on C:")

        # Step 6: Try to run the game
        installed = list(c_drive.iterdir())
        print(f"  C: drive contents: {[p.name for p in installed]}")

        if installed:
            game_dir = [d for d in installed if d.is_dir()]
            if game_dir:
                client.type_string(f"cd {game_dir[0].name}\n")
                time.sleep(0.5)
                client.type_string("dir\n")
                time.sleep(1.5)
                capture(client, shots_dir, "10-game-dir")

                exe_files = list(game_dir[0].glob("*.EXE")) + list(game_dir[0].glob("*.exe"))
                if exe_files:
                    game_exe = exe_files[0].name
                    print(f"  Launching {game_exe}...")
                    client.type_string(f"{game_exe}\n")
                    time.sleep(5)
                    capture(client, shots_dir, "11-game-running")

                    state = client.program_state().json()
                    print(f"  Program state: {state}")

                    # Send a few inputs
                    client.press_key("KBD_enter")
                    time.sleep(2)
                    capture(client, shots_dir, "12-game-input")

        print("\nDone. Shutting down...")
        capture(client, shots_dir, "99-final")

    finally:
        try:
            client.shutdown()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
            proc.wait(timeout=3)

    print(f"\nScreenshots saved to: {shots_dir}")
    print(f"Total: {len(list(shots_dir.glob('*.jpg')))} frames")


if __name__ == "__main__":
    main()
