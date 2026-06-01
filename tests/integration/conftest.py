import os
import shutil
import socket
import subprocess
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

    client = DosboxClient(f"http://127.0.0.1:{port}")

    try:
        client.wait_ready(timeout=10)
        client.wait_shell(timeout=10)
    except TimeoutError:
        proc.kill()
        stderr = proc.stderr.read().decode(errors="replace")
        pytest.fail(f"DOSBox failed to start:\n{stderr[:2000]}")

    yield client

    try:
        client.shutdown()
        proc.wait(timeout=5)
    except Exception:
        proc.kill()
        proc.wait(timeout=3)

    shutil.rmtree(work_dir, ignore_errors=True)
