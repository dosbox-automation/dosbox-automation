#!/usr/bin/env python3
"""Replay a recording JSON file through the dosbox-automation API.

Usage: python3 replay_recording.py [recording.json]
Defaults to the Riptide install recording.
"""

import json
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


def main():
    recording_path = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        DATA_DIR / "white-wolf-11" / "riptide-install-recording.json"
    )
    if not recording_path.exists():
        print(f"Recording not found: {recording_path}")
        sys.exit(1)

    with open(recording_path) as f:
        recording = json.load(f)

    events = recording if isinstance(recording, list) else recording.get("events", [])
    duration_s = events[-1]["t"] / 1000 if events else 0
    print(f"Recording: {recording_path.name}")
    print(f"  {len(events)} events, {duration_s:.1f}s duration")

    port = find_free_port()
    work_dir = WORKSPACE / f"replay-{port}"
    work_dir.mkdir(parents=True, exist_ok=True)
    shots_dir = work_dir / "screenshots"
    shots_dir.mkdir(exist_ok=True)

    c_drive = work_dir / "c_drive"
    c_drive.mkdir(exist_ok=True)

    iso_path = DATA_DIR / "white-wolf-11" / "white-wolf-series-11.iso"

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

    print(f"Starting DOSBox on port {port}...")
    env = {**os.environ, "HOME": str(work_dir)}
    proc = subprocess.Popen(
        [
            DOSBOX_BIN,
            "--noprimaryconf",
            "--nolocalconf",
            "--set", "webserver_enabled=true",
            "--set", f"webserver_port={port}",
            "--set", f"cycles={os.environ.get('DOSBOX_CYCLES', 'auto')}",
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
        print("DOSBox ready. Sending recording as bulk replay...")

        client.capture_frame(shots_dir / "00-before-replay.jpg")

        resp = client.input_sequence(events)
        print(f"  API response: {resp.status_code} — {resp.json()}")

        print(f"  Waiting {duration_s:.0f}s for replay to complete...")
        interval = 5
        elapsed = 0
        shot_num = 1
        while elapsed < duration_s + 10:
            time.sleep(interval)
            elapsed += interval
            path = shots_dir / f"{shot_num:02d}-replay-{elapsed}s.jpg"
            client.capture_frame(path)
            state = client.program_state().json()
            prog = state.get("program_name", "?")
            print(f"  [{elapsed:3.0f}s] program={prog}")
            shot_num += 1

        client.capture_frame(shots_dir / "99-final.jpg")
        print(f"\nDone. Screenshots: {shots_dir}")

    finally:
        try:
            client.shutdown()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
            proc.wait(timeout=3)


if __name__ == "__main__":
    main()
