# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Mount policy integration tests for the dosbox-automation REST API.

Exercises the mount security wall against a live headless DOSBox binary.
Tests drive swap (image mounting via API), the mount lock latch, and
autoexec-driven mounts. Verifies that symlinks, traversal, system paths,
and out-of-whitelist paths are all rejected.

Each test that needs its own DOSBox instance uses the dosbox_e2e fixture.
Tests that only need API calls against the shared module instance use the
dosbox fixture directly.
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

HAS_MFORMAT = shutil.which("mformat") is not None


# -- Helpers --

def create_fat12_floppy(path: Path) -> Path:
    """Create a valid FAT12 1.44M floppy image using mformat."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(1474560))
    subprocess.run(
        ["mformat", "-i", str(path), "-f", "1440", "::"],
        check=True, capture_output=True,
    )
    return path


def write_floppy_image(path: Path) -> Path:
    """Write a 1.44M file that passes ValidateDiskImageStructure (known floppy size).

    Not a real FAT filesystem - only for tests that verify rejection before
    the mount attempt (symlink, traversal, system path, etc).
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(1474560))
    return path


# -----------------------------------------------------------------------
# Drive swap: positive case - valid image under conf anchor
# -----------------------------------------------------------------------

@pytest.mark.skipif(not HAS_MFORMAT, reason="mformat not available")
def test_drive_swap_valid_image(dosbox_e2e, tmp_path):
    """A valid floppy image under allowed_image_roots should swap in."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()
    disk = create_fat12_floppy(game_dir / "disk.img")

    inst = dosbox_e2e(
        autoexec_lines=[f"mount a {disk} -t floppy"],
        conf_dir=game_dir,
        allowed_image_roots=[game_dir],
    )

    # The autoexec mounts the disk. Now try swapping it via API.
    disk2 = create_fat12_floppy(game_dir / "disk2.img")
    r = inst.client.drive_swap("A", str(disk2))
    assert r.status_code == 200, f"Expected 200, got {r.status_code}: {r.text}"
    assert r.json().get("drive") == "A"


# -----------------------------------------------------------------------
# Drive swap: image outside allowed roots
# -----------------------------------------------------------------------

def test_drive_swap_outside_roots(dosbox_e2e, tmp_path):
    """An image outside the conf anchor and allowed_image_roots is rejected."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    outside_disk = write_floppy_image(elsewhere / "sneaky.img")

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("A", str(outside_disk))
    assert r.status_code == 400, f"Expected 400, got {r.status_code}: {r.text}"


# -----------------------------------------------------------------------
# Drive swap: symlink to valid image
# -----------------------------------------------------------------------

def test_drive_swap_symlink_rejected(dosbox_e2e, tmp_path):
    """A symlink to a valid image must be rejected (symlink component)."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()
    real_disk = write_floppy_image(game_dir / "real.img")
    link = game_dir / "link.img"
    link.symlink_to(real_disk)

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("A", str(link))
    assert r.status_code == 400, f"Expected 400, got {r.status_code}: {r.text}"


# -----------------------------------------------------------------------
# Drive swap: system path
# -----------------------------------------------------------------------

@pytest.mark.skipif(not os.path.exists("/etc/passwd"), reason="/etc/passwd not available")
def test_drive_swap_system_path_rejected(dosbox_e2e, tmp_path):
    """Trying to mount /etc/passwd as a disk image must be rejected."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("A", "/etc/passwd")
    assert r.status_code == 400, f"Expected 400, got {r.status_code}: {r.text}"


# -----------------------------------------------------------------------
# Drive swap: traversal path
# -----------------------------------------------------------------------

def test_drive_swap_traversal_rejected(dosbox_e2e, tmp_path):
    """A path with ../ attempting to escape the conf anchor is rejected."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()
    # Put a valid image outside the conf anchor
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    write_floppy_image(elsewhere / "escape.img")

    inst = dosbox_e2e(conf_dir=game_dir)
    traversal = str(game_dir / ".." / "elsewhere" / "escape.img")
    r = inst.client.drive_swap("A", traversal)
    assert r.status_code == 400, f"Expected 400, got {r.status_code}: {r.text}"


# -----------------------------------------------------------------------
# Drive swap: nonexistent file
# -----------------------------------------------------------------------

def test_drive_swap_nonexistent(dosbox_e2e, tmp_path):
    """A path that does not exist is rejected."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("A", str(game_dir / "nope.img"))
    assert r.status_code == 400, f"Expected 400, got {r.status_code}: {r.text}"


# -----------------------------------------------------------------------
# Drive swap: not a disk image (structural validation)
# -----------------------------------------------------------------------

def test_drive_swap_junk_file_rejected(dosbox_e2e, tmp_path):
    """A regular file that is not a disk image must be rejected."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()
    junk = game_dir / "readme.txt"
    junk.write_text("This is not a disk image.\n")

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("A", str(junk))
    assert r.status_code == 400, f"Expected 400, got {r.status_code}: {r.text}"


# -----------------------------------------------------------------------
# Drive swap: directory instead of file
# -----------------------------------------------------------------------

def test_drive_swap_directory_rejected(dosbox_e2e, tmp_path):
    """Passing a directory path to drive swap must be rejected."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()
    subdir = game_dir / "subdir"
    subdir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("A", str(subdir))
    assert r.status_code == 400, f"Expected 400, got {r.status_code}: {r.text}"


# -----------------------------------------------------------------------
# Drive swap: no path echo in error response
# -----------------------------------------------------------------------

def test_drive_swap_no_path_echo(dosbox_e2e, tmp_path):
    """Error responses must not echo the requested path back to the caller."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)
    hostile = "/etc/shadow"
    r = inst.client.drive_swap("A", hostile)
    assert r.status_code in (400, 403)
    assert hostile not in r.text, f"Error response echoed hostile path: {r.text}"


# -----------------------------------------------------------------------
# Drive swap: empty or invalid drive letter
# -----------------------------------------------------------------------

def test_drive_swap_invalid_drive_letter(dosbox_e2e, tmp_path):
    """Drive swap with a non-alpha drive letter is rejected at parse time."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()
    disk = write_floppy_image(game_dir / "disk.img")

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("1", str(disk))
    assert r.status_code == 400


def test_drive_swap_empty_drive(dosbox_e2e, tmp_path):
    """Drive swap with empty drive field is rejected."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()
    disk = write_floppy_image(game_dir / "disk.img")

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("", str(disk))
    assert r.status_code == 400


# -----------------------------------------------------------------------
# Mount lock: initial state is unlocked
# -----------------------------------------------------------------------

def test_mount_lock_initially_unlocked(dosbox_e2e, tmp_path):
    """A fresh DOSBox instance starts with mounts unlocked."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.mount_lock_status()
    assert r.status_code == 200
    assert r.json()["locked"] is False


# -----------------------------------------------------------------------
# Mount lock: lock and verify
# -----------------------------------------------------------------------

def test_mount_lock_engages(dosbox_e2e, tmp_path):
    """Posting to mount/lock sets the one-way latch."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)

    r = inst.client.mount_lock()
    assert r.status_code == 200
    assert r.json()["status"] == "locked"

    r = inst.client.mount_lock_status()
    assert r.status_code == 200
    assert r.json()["locked"] is True


# -----------------------------------------------------------------------
# Mount lock: drive swap rejected after lock
# -----------------------------------------------------------------------

def test_drive_swap_rejected_after_lock(dosbox_e2e, tmp_path):
    """Once the mount lock is engaged, drive swap returns 403."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()
    disk = write_floppy_image(game_dir / "disk.img")

    inst = dosbox_e2e(conf_dir=game_dir)

    # Lock first
    inst.client.mount_lock()

    # Now try to swap
    r = inst.client.drive_swap("A", str(disk))
    assert r.status_code == 403, f"Expected 403 after lock, got {r.status_code}: {r.text}"
    assert "locked" in r.json().get("error", "").lower()


# -----------------------------------------------------------------------
# Mount lock: idempotent (double lock does not error)
# -----------------------------------------------------------------------

def test_mount_lock_idempotent(dosbox_e2e, tmp_path):
    """Locking twice does not crash or return an error."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)

    r1 = inst.client.mount_lock()
    assert r1.status_code == 200
    r2 = inst.client.mount_lock()
    assert r2.status_code == 200

    r = inst.client.mount_lock_status()
    assert r.json()["locked"] is True


# -----------------------------------------------------------------------
# Drive swap: symlink in intermediate directory component
# -----------------------------------------------------------------------

def test_drive_swap_symlink_in_parent_rejected(dosbox_e2e, tmp_path):
    """A symlink used as an intermediate directory component is rejected."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()
    real_subdir = game_dir / "real_sub"
    real_subdir.mkdir()
    write_floppy_image(real_subdir / "disk.img")

    link_subdir = game_dir / "link_sub"
    link_subdir.symlink_to(real_subdir)

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("A", str(link_subdir / "disk.img"))
    assert r.status_code == 400, f"Expected 400, got {r.status_code}: {r.text}"


# -----------------------------------------------------------------------
# Drive swap: null byte injection
# -----------------------------------------------------------------------

def test_drive_swap_null_byte_rejected(dosbox_e2e, tmp_path):
    """A path with embedded null bytes must not reach the filesystem."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("A", "/tmp/safe.img\x00/etc/passwd")
    assert r.status_code == 400


# -----------------------------------------------------------------------
# Drive swap: very long path
# -----------------------------------------------------------------------

def test_drive_swap_very_long_path(dosbox_e2e, tmp_path):
    """An excessively long path should be rejected, not cause a crash."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)
    long_path = "/tmp/" + "A" * 8192 + ".img"
    r = inst.client.drive_swap("A", long_path)
    assert r.status_code in (400, 403, 413, 414)


# -----------------------------------------------------------------------
# Drive swap: device paths (Linux)
# -----------------------------------------------------------------------

@pytest.mark.skipif(not os.path.exists("/dev/null"), reason="not Linux")
def test_drive_swap_dev_null_rejected(dosbox_e2e, tmp_path):
    """/dev/null is not a regular file and must be rejected."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("A", "/dev/null")
    assert r.status_code == 400


@pytest.mark.skipif(not os.path.exists("/dev/sda"), reason="/dev/sda not available")
def test_drive_swap_block_device_rejected(dosbox_e2e, tmp_path):
    """Block devices must not be mountable via the API."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("A", "/dev/sda")
    assert r.status_code == 400


# -----------------------------------------------------------------------
# Drive swap: proc/sys filesystem (Linux)
# -----------------------------------------------------------------------

@pytest.mark.skipif(not os.path.exists("/proc/self/environ"),
                    reason="/proc not available")
def test_drive_swap_proc_rejected(dosbox_e2e, tmp_path):
    """/proc paths are system paths and must be rejected."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap("A", "/proc/self/environ")
    assert r.status_code == 400


# -----------------------------------------------------------------------
# Drive swap: JSON format attacks
# -----------------------------------------------------------------------

def test_drive_swap_missing_fields(dosbox_e2e, tmp_path):
    """Missing required JSON fields returns 400."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)

    r = inst.client.drive_swap_raw('{"drive": "A"}')
    assert r.status_code == 400

    r = inst.client.drive_swap_raw('{"image": "/tmp/x.img"}')
    assert r.status_code == 400


def test_drive_swap_malformed_json(dosbox_e2e, tmp_path):
    """Malformed JSON body must not crash the server."""
    game_dir = tmp_path / "game"
    game_dir.mkdir()

    inst = dosbox_e2e(conf_dir=game_dir)
    r = inst.client.drive_swap_raw("{not valid json")
    assert r.status_code in (400, 500)

    # Server should still be responsive
    r2 = inst.client.status()
    assert r2.status_code == 200
