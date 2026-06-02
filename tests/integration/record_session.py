#!/usr/bin/env python3
"""Record a fresh input session through dosbox-automation.

Starts DOSBox with the White Wolf CD mounted, begins recording,
then waits for you to interact with DOSBox manually. Press Ctrl+C
in the terminal when done — the recording is saved to a JSON file.
"""

import json
import os
import signal
import socket
import subprocess
import sys
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
    port = find_free_port()
    work_dir = WORKSPACE / f"record-{port}"
    work_dir.mkdir(parents=True, exist_ok=True)

    c_drive = work_dir / "c_drive"
    c_drive.mkdir(exist_ok=True)

    iso_path = DATA_DIR / "white-wolf-11" / "white-wolf-series-11.iso"
    output_file = work_dir / "recording.json"
    stderr_log = work_dir / "dosbox-stderr.log"

    print(f"Starting DOSBox on port {port}...")
    env = {**os.environ, "HOME": str(work_dir)}
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
        stderr=open(stderr_log, "w"),
        cwd=str(work_dir),
    )

    client = DosboxClient(f"http://127.0.0.1:{port}")

    try:
        client.wait_ready(timeout=10)
        client.wait_shell(timeout=10)
        print("DOSBox ready.")
        print()
        print("Starting recording. Do the install in the DOSBox window.")
        print("Press Ctrl+C here when done.")
        print()

        resp = client.recording_start()
        print(f"Recording started: {resp.json()}")

        while True:
            time.sleep(2)
            status = client.recording_status().json()
            count = status.get("event_count", 0)
            dur = status.get("duration_ms", 0) / 1000
            print(f"\r  Recording: {count} events, {dur:.0f}s", end="", flush=True)

    except KeyboardInterrupt:
        print("\n\nStopping recording...")
        resp = client.recording_stop()
        data = resp.json()
        events = data.get("events", [])
        duration = data.get("duration_ms", 0) / 1000

        with open(output_file, "w") as f:
            json.dump({"events": events}, f)

        print(f"Saved {len(events)} events ({duration:.1f}s) to:")
        print(f"  {output_file}")
        print(f"  Stderr log: {stderr_log}")

    finally:
        try:
            client.shutdown()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
            proc.wait(timeout=3)


if __name__ == "__main__":
    main()
