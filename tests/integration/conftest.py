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

SETTING_SECTIONS = {
    "cpu_cycles": "cpu",
    "cpu_cycles_protected": "cpu",
    "cpu_throttle": "cpu",
    "cpu_type": "cpu",
    "keyboard_layout": "dos",
    "machine": "dosbox",
    "memsize": "dosbox",
    "output": "sdl",
    "joysticktype": "joystick",
}

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
    """Read stderr line by line, buffer output and optionally write to file."""

    def __init__(self, pipe, log_path=None):
        super().__init__(daemon=True)
        self.pipe = pipe
        self.ready = threading.Event()
        self.lines = []
        self._log_fh = open(log_path, "w") if log_path else None

    def run(self):
        for raw_line in self.pipe:
            line = raw_line.decode(errors="replace").rstrip()
            self.lines.append(line)
            if self._log_fh:
                self._log_fh.write(line + "\n")
                self._log_fh.flush()
            if "WEBSERVER:" in line and ("API token" in line or "Token written" in line):
                self.ready.set()
        if self._log_fh:
            self._log_fh.close()

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
        # HOME redirects the config dir on POSIX only; XDG_CONFIG_HOME
        # does it on Windows too (token file and logs must stay in the
        # per-test work dir on every platform)
        "XDG_CONFIG_HOME": str(work_dir / ".config"),
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
                          conf_dir=None, allowed_image_roots=None,
                          settings=None):
    """Start a headless DOSBox instance with optional autoexec and config.

    conf_dir: directory for the config file. The mount policy's conf
    anchor is the config file's parent, so placing the config next to
    disk images lets MOUNT reach them. Defaults to work_dir.

    allowed_image_roots: list of paths to whitelist for API image mounting
    (drive swap). Written into the primary config as mount_allowed_image_roots.

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

    primary_lines = [f"mount_allowed_bases = {resource_dir}"]
    if allowed_image_roots:
        roots_str = ";".join(str(p) for p in allowed_image_roots)
        primary_lines.append(f"mount_allowed_image_roots = {roots_str}")

    primary_conf.write_text(
        "[webserver]\n" + "\n".join(primary_lines) + "\n"
    )

    # When running visible, force texture output (OSD needs SDL_Renderer)
    # and disable joystick (Stadia controller bug).
    visible = os.environ.get("DOSBOX_VISIBLE")
    if visible:
        if settings is None:
            settings = {}
        settings.setdefault("output", "texture")
        settings.setdefault("joysticktype", "none")

    # Write a config file with settings and autoexec.
    conf_path = None
    if autoexec_lines or settings:
        conf_path = conf_dir / f"test-{secrets.token_hex(4)}.conf"
        conf_parts = []
        if settings:
            sections = {}
            for key, value in settings.items():
                section = SETTING_SECTIONS.get(key, "dosbox")
                sections.setdefault(section, []).append(f"{key} = {value}")
            for section, lines in sections.items():
                conf_parts.append(f"[{section}]")
                conf_parts.extend(lines)
                conf_parts.append("")
        if autoexec_lines:
            conf_parts.append("[autoexec]")
            conf_parts.extend(autoexec_lines)
        conf_path.write_text("\n".join(conf_parts) + "\n")

    env = {
        **os.environ,
        "HOME": str(work_dir),
        "XDG_CONFIG_HOME": str(work_dir / ".config"),
        "DOSBOX_API_TOKEN": token,
    }

    if not visible:
        env["SDL_VIDEODRIVER"] = "offscreen"
        env["SDL_AUDIODRIVER"] = "dummy"

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

    stderr_log = work_dir / "dosbox-stderr.log"
    capture = StderrCapture(proc.stderr, log_path=stderr_log)
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
                 conf_dir=None, allowed_image_roots=None, settings=None):
        if work_dir is None:
            work_dir = WORKSPACE / f"e2e-{secrets.token_hex(4)}"
        inst = start_dosbox_instance(
            work_dir, autoexec_lines, extra_sets, conf_dir=conf_dir,
            allowed_image_roots=allowed_image_roots,
            settings=settings,
        )
        instances.append(inst)
        return inst

    yield _factory

    for inst in instances:
        inst.shutdown()
        # Don't delete work_dir here - provenance collection needs it
