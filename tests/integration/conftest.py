import os
import re
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

TOKEN_RE = re.compile(r"WEBSERVER: API token: ([0-9a-f]{64})")


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class StderrCapture(threading.Thread):
    """Read stderr line by line, extract API token, buffer output."""

    def __init__(self, pipe):
        super().__init__(daemon=True)
        self.pipe = pipe
        self.token = None
        self.token_found = threading.Event()
        self.lines = []

    def run(self):
        for raw_line in self.pipe:
            line = raw_line.decode(errors="replace").rstrip()
            self.lines.append(line)
            m = TOKEN_RE.search(line)
            if m:
                self.token = m.group(1)
                self.token_found.set()

    def get_output(self) -> str:
        return "\n".join(self.lines)


@pytest.fixture(scope="module")
def dosbox(tmp_path_factory):
    """Start a headless DOSBox with webserver enabled, yield API client."""

    port = find_free_port()

    work_dir = WORKSPACE / f"run-{port}"
    work_dir.mkdir(parents=True, exist_ok=True)

    env = {
        **os.environ,
        "SDL_VIDEODRIVER": "dummy",
        "SDL_AUDIODRIVER": "dummy",
        "HOME": str(work_dir),
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

    if not capture.token_found.wait(timeout=10):
        proc.kill()
        capture.join(timeout=2)
        pytest.fail(
            f"DOSBox did not emit API token within 10s:\n"
            f"{capture.get_output()[:2000]}"
        )

    client = DosboxClient(
        f"http://127.0.0.1:{port}", token=capture.token
    )

    try:
        client.wait_ready(timeout=10)
        client.wait_shell(timeout=10)
    except TimeoutError:
        proc.kill()
        capture.join(timeout=2)
        pytest.fail(
            f"DOSBox failed to start:\n{capture.get_output()[:2000]}"
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
