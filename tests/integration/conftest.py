import os
import secrets
import shutil
import socket
import subprocess
import textwrap
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


class DosboxInstance:
    """A running headless DOSBox instance with its working directory."""

    def __init__(self, client, proc, capture_thread, work_dir,
                 conf_path=None):
        self.client = client
        self.proc = proc
        self.capture_thread = capture_thread
        self.work_dir = work_dir
        self.conf_path = conf_path

    def shutdown(self):
        try:
            self.client.shutdown()
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()
            self.proc.wait(timeout=3)
        self.capture_thread.join(timeout=2)
        # Clean up temp config if it was placed outside work_dir.
        if self.conf_path and self.conf_path.exists():
            try:
                self.conf_path.unlink()
            except OSError:
                pass


def start_dosbox_instance(work_dir, autoexec_lines=None, extra_sets=None,
                          conf_dir=None):
    """Start a headless DOSBox instance with optional autoexec and config.

    conf_dir: directory for the config file. The mount policy's conf
    anchor is the config file's parent, so placing the config next to
    disk images lets MOUNT reach them. Defaults to work_dir.

    Returns a DosboxInstance with client, process, and work_dir.
    """
    port = find_free_port()
    token = secrets.token_hex(32)

    work_dir.mkdir(parents=True, exist_ok=True)

    if conf_dir is None:
        conf_dir = work_dir

    # The bundled Y: drive (resources/drives/y with debug.com etc) is
    # auto-mounted by DOSBox on startup. When the webserver is enabled,
    # the mount policy blocks it because the relative build path falls
    # outside the conf anchor. Write a primary config that whitelists
    # the build resource directory so the internal auto-mount succeeds.
    dosbox_bin = Path(DOSBOX_BIN).resolve()
    resource_dir = dosbox_bin.parent / "resources"
    config_dir = work_dir / ".config" / "dosbox-automation"
    config_dir.mkdir(parents=True, exist_ok=True)
    primary_conf = config_dir / "dosbox-automation.conf"
    primary_conf.write_text(textwrap.dedent(f"""\
        [webserver]
        mount_allowed_bases = {resource_dir}
    """))

    # Write a config file with autoexec if provided.
    conf_path = None
    if autoexec_lines:
        conf_path = conf_dir / f"test-{secrets.token_hex(4)}.conf"
        autoexec_block = "\n".join(autoexec_lines)
        conf_path.write_text(textwrap.dedent(f"""\
            [autoexec]
            {autoexec_block}
        """))

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
        "--set", f"capture_dir={work_dir / 'capture'}",
    ]

    if extra_sets:
        for s in extra_sets:
            cmd.extend(["--set", s])

    if conf_path:
        cmd.extend(["-conf", str(conf_path)])

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
        raise RuntimeError(
            f"DOSBox webserver did not start:\n{capture.get_output()[:2000]}"
        )

    client = DosboxClient(f"http://127.0.0.1:{port}", token=token)
    client.wait_ready(timeout=10)

    return DosboxInstance(client, proc, capture, work_dir,
                          conf_path=conf_path)


@pytest.fixture
def dosbox_e2e(tmp_path):
    """Factory fixture: call with autoexec lines to get a per-test DOSBox.

    Usage:
        instance = dosbox_e2e(["mount c /path/to/hd", "imgmount a disk.img -t floppy"])
        client = instance.client
        # ... run test ...
        # fixture handles shutdown
    """
    instances = []

    def _factory(autoexec_lines=None, extra_sets=None, work_dir=None,
                 conf_dir=None):
        if work_dir is None:
            work_dir = WORKSPACE / f"e2e-{secrets.token_hex(4)}"
        inst = start_dosbox_instance(
            work_dir, autoexec_lines, extra_sets, conf_dir=conf_dir,
        )
        instances.append(inst)
        return inst

    yield _factory

    for inst in instances:
        inst.shutdown()
        # Don't delete work_dir here - provenance collection needs it
