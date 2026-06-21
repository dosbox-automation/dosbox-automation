import os
import secrets
import shutil
import socket
import subprocess
import threading
from pathlib import Path

import pytest

from dosbox_client import DosboxClient

DOSBOX_BIN = os.environ.get(
    "DOSBOX_BIN",
    str(Path(__file__).resolve().parents[2] / "build" / "debug-linux" / "dosbox"),
)

WORKSPACE = Path(__file__).resolve().parents[2] / ".workspace" / "test-runs"


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class StderrCapture(threading.Thread):
    """Read stderr line by line, buffer output."""

    def __init__(self, pipe):
        super().__init__(daemon=True)
        self.pipe = pipe
        self.ready = threading.Event()
        self.lines = []

    def run(self):
        for raw_line in self.pipe:
            line = raw_line.decode(errors="replace").rstrip()
            self.lines.append(line)
            if "WEBSERVER:" in line and "API token" in line:
                self.ready.set()

    def get_output(self) -> str:
        return "\n".join(self.lines)


@pytest.fixture(scope="module")
def dosbox(tmp_path_factory):
    """Start a headless DOSBox with webserver enabled, yield API client."""

    port = find_free_port()
    token = secrets.token_hex(32)

    work_dir = WORKSPACE / f"run-{port}"
    work_dir.mkdir(parents=True, exist_ok=True)

    env = {
        **os.environ,
        "SDL_VIDEODRIVER": "offscreen",
        "SDL_AUDIODRIVER": "dummy",
        "HOME": str(work_dir),
        "DOSBOX_API_TOKEN": token,
    }

    cmd = [
        DOSBOX_BIN,
        "--noprimaryconf",
        "--nolocalconf",
        "--set", "webserver_enabled=true",
        "--set", f"webserver_port={port}",
    ]

    proc = subprocess.Popen(
        cmd,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        cwd=str(work_dir),
    )

    capture = StderrCapture(proc.stderr)
    capture.start()

    if not capture.ready.wait(timeout=15):
        proc.kill()
        capture.join(timeout=2)
        pytest.fail(
            f"DOSBox webserver did not start within 15s:\n"
            f"{capture.get_output()[:2000]}"
        )

    client = DosboxClient(
        f"http://127.0.0.1:{port}", token=token
    )

    try:
        client.wait_ready(timeout=10)
        client.wait_shell(timeout=10)
    except TimeoutError:
        proc.kill()
        capture.join(timeout=2)
        pytest.fail(
            f"DOSBox failed to reach shell:\n{capture.get_output()[:2000]}"
        )

    yield client

    try:
        client.shutdown()
        proc.wait(timeout=5)
    except Exception:
        proc.kill()
        proc.wait(timeout=3)

    capture.join(timeout=2)
    shutil.rmtree(work_dir, ignore_errors=True)
