# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""Archive mounts through MOUNT: a zip served read-only to DOS.

Builds a small zip in the test, mounts it from the autoexec, and reads
DIR and TYPE back through the screen text route. The archive's bytes
are unchanged afterwards.
"""

import hashlib
import io
import shutil
import tarfile
import time
import zipfile
from pathlib import Path

import pytest

STAMP = (1990, 1, 1, 0, 0, 0)


def build_zip(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in [
            ("game/doom.exe", b"x" * 3000),
            ("game/data/level.wad", b"w" * 5000),
            ("game/readme.txt", b"hello from the archive\r\n"),
        ]:
            info = zipfile.ZipInfo(name, date_time=STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    return path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def screen(client) -> str:
    r = client.screen_text()
    assert r.status_code == 200, r.text
    return "\n".join(line.rstrip() for line in r.json()["text"].split("\n"))


def wait_text(client, needle, timeout=15.0):
    """Waits for needle on the screen, also where a long path pushed it
    across the 80-column wrap (the raw rows joined end to end)."""
    deadline = time.monotonic() + timeout
    text = ""
    while time.monotonic() < deadline:
        r = client.screen_text()
        assert r.status_code == 200, r.text
        rows = r.json()["text"].split("\n")
        text = "\n".join(row.rstrip() for row in rows)
        if needle in text or needle in "".join(rows):
            return text
        time.sleep(0.2)
    raise AssertionError(f"{needle!r} not found:\n{text}")


def settings_for(tmp_path):
    return {
        "archive_cache_path": str(tmp_path / "cache"),
        "archive_sidecar_path": str(tmp_path / "saves"),
    }


def test_zip_mounts_and_dir_lists_its_members(dosbox_e2e, tmp_path):
    zip_path = build_zip(tmp_path / "game" / "doom.zip")
    before = sha256(zip_path)
    inst = dosbox_e2e(
        autoexec_lines=[f"mount c {zip_path}", "c:", "dir /b", "type readme.txt"],
        conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    text = wait_text(inst.client, "hello from the archive")
    # D10: the single top directory became the root, so DIR lists its
    # children and no GAME entry (the console line names GAME as the root).
    lines = text.split("\n")
    start = next(i for i, line in enumerate(lines) if line.endswith(">dir /b"))
    listing = []
    for line in lines[start + 1:]:
        if ">" in line and line.split(">", 1)[0].endswith(":\\"):
            break
        if line.strip():
            listing.append(line.strip())
    assert listing == ["DOOM.EXE", "DATA", "README.TXT"], text
    assert "Using GAME as the root" in text, text
    assert sha256(zip_path) == before


def test_writes_are_refused_on_an_archive_drive(dosbox_e2e, tmp_path):
    # Without a sidecar (D1) the archive drive is read-only.
    zip_path = build_zip(tmp_path / "game" / "doom.zip")
    inst = dosbox_e2e(
        autoexec_lines=[
            f"mount c {zip_path} -nosidecar",
            "c:",
            "echo test > new.txt",
            "del readme.txt",
            "dir /b",
        ],
        conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    text = wait_text(inst.client, "README.TXT")
    assert "NEW.TXT" not in text, text
    assert "Access denied" in text or "denied" in text.lower(), text


def test_sequential_archive_is_refused_unless_cached(dosbox_e2e, tmp_path):
    import tarfile

    tgz = tmp_path / "game" / "doom.tar.gz"
    tgz.parent.mkdir(parents=True)
    with tarfile.open(tgz, "w:gz") as t:
        for name, data in [("game/doom.exe", b"x" * 100), ("game/readme.txt", b"hi\r\n")]:
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mtime = 631152000
            import io

            t.addfile(info, io.BytesIO(data))
    inst = dosbox_e2e(
        autoexec_lines=[f"mount c {tgz}"],
        conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    text = wait_text(inst.client, "sequential archive")
    assert "archive_sequential" in text, text

    inst2 = dosbox_e2e(
        autoexec_lines=[f"mount c {tgz}", "c:", "type readme.txt"],
        conf_dir=tmp_path / "game",
        settings={**settings_for(tmp_path), "archive_sequential": "cache"},
    )
    wait_text(inst2.client, "hi")


def test_a_refusal_names_the_archive_as_typed_on_a_line_of_its_own(
        dosbox_e2e, tmp_path):
    """ada-pbr6: with the working directory above the archive, MOUNT used
    to print the shorter cwd-relative name, on the reason's line."""
    tgz = tmp_path / "game" / "doom.tar.gz"
    tgz.parent.mkdir(parents=True)
    with tarfile.open(tgz, "w:gz") as t:
        info = tarfile.TarInfo("doom.exe")
        info.size = 100
        info.mtime = 631152000
        t.addfile(info, io.BytesIO(b"x" * 100))
    inst = dosbox_e2e(
        autoexec_lines=[f"mount c {tgz}"],
        work_dir=tmp_path,
        conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "sequential archive")
    r = inst.client.screen_text()
    assert r.status_code == 200, r.text
    rows = [row.rstrip() for row in r.json()["text"].split("\n")]
    # A name longer than a row wraps; its rows join without a gap.
    assert f"{tgz}is a sequential archive" in "".join(rows), "\n".join(rows)


def test_mount_listing_names_the_archive(dosbox_e2e, tmp_path):
    zip_path = build_zip(tmp_path / "game" / "doom.zip")
    inst = dosbox_e2e(
        autoexec_lines=[f"mount c {zip_path}", "mount"],
        conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    text = wait_text(inst.client, "Archive")
    assert "doom.zip" in text, text


def test_cache_cleanup_runs_at_engine_start_without_a_mount(dosbox_e2e, tmp_path):
    zip_path = build_zip(tmp_path / "game" / "doom.zip")
    inst = dosbox_e2e(
        autoexec_lines=[f"mount c {zip_path}", "c:", "type readme.txt"],
        conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "hello from the archive")
    inst.shutdown()
    # A directory under a spread letter with files but no toml is a damaged
    # entry (design D14); the cleanup removes it.
    damaged = tmp_path / "cache" / "z" / "zombie-00000000"
    (damaged / "files").mkdir(parents=True)
    (damaged / "files" / "0").write_bytes(b"zzz")
    inst2 = dosbox_e2e(
        autoexec_lines=["echo READY-MARK"],
        conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    wait_text(inst2.client, "READY-MARK")
    assert not damaged.exists()


# DOOM 1.9 shareware as id published it (doom19s.zip, 1995-02-03): INSTALL.BAT,
# DEICE.EXE, DOOMS_19.DAT and the two archive halves. Fetched once into the
# ignored disks directory, like the e2e installs' media.
DOOM19S_URL = "https://ftp.fu-berlin.de/pc/games/idgames/idstuff/doom/doom19s.zip"
DOOM19S_SHA256 = "cacf0142b31ca1af00796b4a0339e07992ac5f21bc3f81e7532fe1b5e1b486e6"
DOOM19S_DIR = Path(__file__).resolve().parents[1] / "files" / "disks" / "doom-shareware-zip"


def fetch_doom19s() -> Path:
    """The shareware zip from the idgames mirror, or a skip naming the issue."""
    import email.utils
    import os
    import urllib.error
    import urllib.request

    target = DOOM19S_DIR / "doom19s.zip"
    if target.exists() and sha256(target) == DOOM19S_SHA256:
        return target
    DOOM19S_DIR.mkdir(parents=True, exist_ok=True)
    part = target.with_suffix(".zip.part")
    try:
        with urllib.request.urlopen(DOOM19S_URL, timeout=60) as r:  # nosec B310 - fixed https URL
            part.write_bytes(r.read())
            stamp = r.headers.get("Last-Modified")
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        pytest.skip(f"download issue: {DOOM19S_URL}: {e}")
    if sha256(part) != DOOM19S_SHA256:
        part.unlink()
        pytest.skip(f"download issue: {DOOM19S_URL}: checksum differs from the pinned file")
    if stamp:
        when = email.utils.parsedate_to_datetime(stamp).timestamp()
        os.utime(part, (when, when))
    part.replace(target)
    return target


def test_doom_shareware_zip_mounts_and_lists_the_installer(dosbox_e2e, tmp_path):
    # The policy wants the archive under the conf's directory (D5).
    (tmp_path / "game").mkdir()
    zip_path = tmp_path / "game" / "doom19s.zip"
    shutil.copy2(fetch_doom19s(), zip_path)
    before = sha256(zip_path)
    inst = dosbox_e2e(
        autoexec_lines=[f"mount c {zip_path}", "c:", "dir /b", "type install.bat"],
        conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    text = wait_text(inst.client, "Error installing DOOMS_19.EXE!")
    for name in ("INSTALL.BAT", "DEICE.EXE", "DOOMS_19.DAT", "DOOMS_19.1", "DOOMS_19.2"):
        assert name in text, text
    assert sha256(zip_path) == before


def press(client, key):
    for pressed in (True, False):
        r = client.press_key(key, pressed)
        assert r.status_code == 200, r.text


def test_doom_shareware_installs_into_the_sidecar(dosbox_e2e, tmp_path):
    """INSTALL.BAT from the zip: DEICE joins the halves into DOOMS_19.EXE, PKSFX
    unpacks into \\DOOMS, the batch deletes the EXE and starts SETUP. Every
    write lands in the sidecar; the zip keeps its bytes."""
    (tmp_path / "game").mkdir()
    zip_path = tmp_path / "game" / "doom19s.zip"
    shutil.copy2(fetch_doom19s(), zip_path)
    before = sha256(zip_path)
    settings = settings_for(tmp_path)
    inst = dosbox_e2e(
        autoexec_lines=[f"mount c {zip_path}", "c:", "install.bat"],
        conf_dir=tmp_path / "game",
        settings=settings,
    )
    wait_text(inst.client, "Which drive to install to", timeout=60)
    inst.client.type_string("C")
    wait_text(inst.client, "Enter directory name to copy into", timeout=30)
    time.sleep(1)
    press(inst.client, "KBD_enter")
    wait_text(inst.client, "Press 'Y' to create", timeout=30)
    time.sleep(1)
    inst.client.type_string("Y")
    # SETUP's main menu is the batch's last line.
    wait_text(inst.client, "F1=Warp", timeout=240)
    assert sha256(zip_path) == before
    saves = [d for d in (tmp_path / "saves").iterdir() if d.is_dir()]
    assert len(saves) == 1, saves
    dooms = saves[0] / "DOOMS"
    assert (dooms / "DOOM1.WAD").stat().st_size == 4196020
    assert (dooms / "DOOM.EXE").stat().st_size == 709905
    assert (dooms / "SETUP.EXE").exists()
    assert not list((tmp_path / "saves").rglob("DOOMS_19.EXE"))


def floppy_bytes(label: str) -> bytes:
    """A 320K DOS 1.x floppy: the media byte decides the BPB (two one-sector
    FATs, the root at sector 3) and the root holds only a volume label."""
    img = bytearray(327680)
    img[512] = 0xFF
    root = 3 * 512
    img[root:root + 11] = label.ljust(11).encode("ascii")
    img[root + 11] = 0x08
    return bytes(img)


HOUSE_FILES = [
    ("info.toml", b'[game]\r\ntitle = "House Test"\r\n'),
    ("disks/disk1.img", floppy_bytes("HOUSEONE")),
    ("disks/disk2.img", floppy_bytes("HOUSETWO")),
    ("game/house.exe", b"MZ" + b"\0" * 62),
]


def build_house_zip(path: Path) -> Path:
    """The house archive layout: info.toml at the root, the disks in disks/,
    the installed game in game/ (plan 5 item 5.5)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in HOUSE_FILES:
            z.writestr(zipfile.ZipInfo(name, date_time=STAMP), data,
                       compress_type=zipfile.ZIP_DEFLATED)
    return path


def listing_after(text: str, command: str) -> list[str]:
    lines = text.split("\n")
    start = next(i for i, line in enumerate(lines) if line.endswith(command))
    names = []
    for line in lines[start + 1:]:
        if ">" in line and line.split(">", 1)[0].endswith(":\\"):
            break
        if line.strip():
            names.append(line.strip())
    return names


def test_a_house_archive_mounts_with_its_info_and_its_disks(dosbox_e2e, tmp_path):
    zip_path = build_house_zip(tmp_path / "game" / "house.zip")
    folder = tmp_path / "game" / "house"
    for name, data in HOUSE_FILES:
        (folder / name).parent.mkdir(parents=True, exist_ok=True)
        (folder / name).write_bytes(data)
    before = sha256(zip_path)
    inst = dosbox_e2e(
        autoexec_lines=[
            f"mount c {zip_path}",
            f"mount d {folder}",
            f"mount a {zip_path}/disks/disk1.img",
            f"mount b {zip_path}/disks/disk2.img",
            "d:",
            "dir /b",
            "c:",
            "dir /b",
            "type info~1.tom",
            "vol a:",
            "vol b:",
        ],
        conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    text = wait_text(inst.client, "HOUSETWO")
    # Three entries at the top, so no single root directory is entered.
    assert "as the root" not in text, text
    # info.toml is not 8.3: the archive drive names it as a directory
    # mount of the same files does.
    from_folder = listing_after(text, "D:\\>dir /b")
    from_archive = listing_after(text, "C:\\>dir /b")
    assert sorted(from_archive) == sorted(from_folder), text
    assert sorted(from_archive) == ["DISKS", "GAME", "INFO~1.TOM"], text
    for needle in ('title = "House Test"', "HOUSEONE"):
        assert needle in text, text
    assert sha256(zip_path) == before


# ---------------------------------------------------------------------------
# Plan 5: the archive on the command line, MOUNT -run, the status field
# ---------------------------------------------------------------------------

DONE_BAT = b"@ECHO OFF\r\nECHO done> DONE.TXT\r\n"


def build_program_zip(path: Path, programs: dict[str, bytes]) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in {"README.TXT": b"hello\r\n", **programs}.items():
            z.writestr(zipfile.ZipInfo(name, date_time=STAMP), data,
                       compress_type=zipfile.ZIP_DEFLATED)
    return path


def run_to_exit(tmp_path: Path, args: list[str]) -> int:
    """The engine without the webserver, which it would leave at once."""
    import os
    import subprocess

    from conftest import DOSBOX_BIN

    env = {**os.environ, "HOME": str(tmp_path), "SDL_VIDEODRIVER": "offscreen",
           "SDL_AUDIODRIVER": "dummy"}
    cmd = [DOSBOX_BIN, "--noprimaryconf", "--nolocalconf",
           "--set", "webserver_enabled=false",
           "--set", f"archive_cache_path={tmp_path / 'cache'}",
           "--set", f"archive_sidecar_path={tmp_path / 'saves'}",
           *args]
    done = subprocess.run(cmd, env=env, cwd=tmp_path, timeout=60,  # nosec B603 - our engine
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return done.returncode


def sidecar_files(tmp_path: Path) -> list[str]:
    saves = tmp_path / "saves"
    if not saves.is_dir():
        return []
    return sorted(p.name for p in saves.rglob("*") if p.is_file()
                  and not p.name.startswith("."))


def test_an_archive_on_the_command_line_runs_its_program_and_exits(tmp_path):
    zip_path = build_program_zip(tmp_path / "game.zip", {"GAME.BAT": DONE_BAT})
    before = sha256(zip_path)
    assert run_to_exit(tmp_path, [str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["DONE.TXT"]
    assert sha256(zip_path) == before


def test_run_names_the_program_among_several(tmp_path):
    zip_path = build_program_zip(tmp_path / "game.zip", {
        "FIRST.BAT": b"@ECHO OFF\r\nECHO first> FIRST.TXT\r\n",
        "SECOND.BAT": b"@ECHO OFF\r\nECHO second> SECOND.TXT\r\n",
    })
    assert run_to_exit(tmp_path, ["--run", "SECOND.BAT", str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["SECOND.TXT"]


def test_a_percent_sign_in_the_archive_path_and_the_run_name(tmp_path):
    """AUTOEXEC.BAT goes through the batch expansion, where a lone % would
    end the MOUNT line."""
    zip_path = build_program_zip(tmp_path / "100% game.zip", {
        "50%.BAT": b"@ECHO OFF\r\nECHO half> HALF.TXT\r\n",
        "OTHER.BAT": DONE_BAT,
    })
    assert run_to_exit(tmp_path, ["--run", "50%.BAT", str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["HALF.TXT"]


def test_a_program_named_like_a_shell_command_runs_as_itself(tmp_path):
    """The shell takes the stem of TYPE.BAT for its TYPE command unless the
    line names the file by its path."""
    zip_path = build_program_zip(tmp_path / "game.zip", {"TYPE.BAT": DONE_BAT})
    assert run_to_exit(tmp_path, [str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["DONE.TXT"]


def tap(client, key):
    events = [{"type": "key", "key": key, "pressed": True},
              {"type": "key", "key": key, "pressed": False}]
    deadline = time.monotonic() + 5.0
    while True:
        r = client.input_sequence(events)
        if r.status_code != 409 or time.monotonic() > deadline:
            break
        time.sleep(0.05)
    assert r.status_code == 200, r.text


def test_several_programs_open_the_menu_and_escape_keeps_the_engine(dosbox_e2e, tmp_path):
    zip_path = build_program_zip(tmp_path / "game" / "game.zip", {
        "FIRST.BAT": DONE_BAT, "SECOND.BAT": DONE_BAT})
    inst = dosbox_e2e(conf_dir=tmp_path / "game",
                      settings=settings_for(tmp_path),
                      extra_args=[str(zip_path)])
    text = wait_text(inst.client, "Select a program")
    assert "(FIRST.BAT)" in text and "(SECOND.BAT)" in text, text
    tap(inst.client, "KBD_esc")
    # Nothing chosen runs nothing, and the engine stays.
    wait_text(inst.client, "C:\\>")
    time.sleep(1)
    assert inst.proc.poll() is None
    assert inst.client.program_state().json().get("is_shell") is True
    assert "DONE.TXT" not in sidecar_files(tmp_path)


def test_a_choice_from_the_command_line_menu_runs_and_quits(dosbox_e2e, tmp_path):
    zip_path = build_program_zip(tmp_path / "game" / "game.zip", {
        "FIRST.BAT": DONE_BAT, "SECOND.BAT": DONE_BAT})
    inst = dosbox_e2e(conf_dir=tmp_path / "game",
                      settings=settings_for(tmp_path),
                      extra_args=[str(zip_path)])
    wait_text(inst.client, "Select a program")
    tap(inst.client, "KBD_enter")
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and inst.proc.poll() is None:
        time.sleep(0.2)
    assert inst.proc.poll() == 0, "the engine did not quit after the program"
    assert sidecar_files(tmp_path) == ["DONE.TXT"]


def test_mount_run_typed_at_the_prompt(dosbox_e2e, tmp_path):
    host = tmp_path / "game" / "host"
    build_program_zip(host / "GAME.ZIP", {"GAME.BAT": DONE_BAT, "SETUP.EXE": b"MZ"})
    inst = dosbox_e2e(autoexec_lines=[f"mount c {host}", "c:"],
                      conf_dir=tmp_path / "game",
                      settings=settings_for(tmp_path))
    inst.client.wait_shell(timeout=15)
    # ada-hg7c: only characters the client has keys for.
    r = inst.client.type_string("mount d c:\\game.zip -run game.bat\n")
    assert r.status_code == 200, r.text
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and "DONE.TXT" not in sidecar_files(tmp_path):
        time.sleep(0.2)
    assert sidecar_files(tmp_path) == ["DONE.TXT"], screen(inst.client)
    wait_text(inst.client, "D:\\>")


def test_status_shows_the_archive_while_mount_prepares_it(dosbox_e2e, tmp_path):
    """A gzip tar is identified by walking its whole stream and, with
    archive_sequential = cache, unpacked into the cache at mount time; the
    status route answers from the webserver thread meanwhile. The unit
    tests (ArchiveActivityTest, ArchiveHelperClientTest) carry the rules."""
    import io
    import os
    import tarfile

    tgz = tmp_path / "game" / "big.tar.gz"
    tgz.parent.mkdir(parents=True)
    payload = os.urandom(96 * 1024 * 1024)
    with tarfile.open(tgz, "w:gz", compresslevel=1) as t:
        for name, data in [("big/data.bin", payload), ("big/readme.txt", b"hi\r\n")]:
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mtime = 631152000
            t.addfile(info, io.BytesIO(data))
    inst = dosbox_e2e(
        autoexec_lines=["choice /c:y /t:y,3 >nul", f"mount c {tgz}", "echo MOUNTED"],
        conf_dir=tmp_path / "game",
        settings={**settings_for(tmp_path), "archive_sequential": "cache"},
    )
    # The screen route waits on the emulation thread, which MOUNT holds;
    # only the status route is polled until the field has come and gone.
    seen = []
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        archive = inst.client.status().json().get("archive")
        if archive:
            seen.append(archive)
        elif seen:
            break
        time.sleep(0.02)
    assert seen, "the status never showed the archive"
    assert {s["activity"] for s in seen} <= {"identifying", "materializing"}
    assert all(s["archive"] == "big.tar" for s in seen), seen[:3]
    assert all(0 <= s["bytes_done"] <= s["bytes_total"] for s in seen), seen[:3]
    wait_text(inst.client, "MOUNTED", timeout=60)
    assert "archive" not in inst.client.status().json()


def build_installer_zip(path: Path) -> Path:
    """A zip whose INSTALL.BAT writes to its own drive, as the Apogee
    installers do (showroom probe 0.4)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in [
            ("install.bat",
             b"@echo installed> INSTALLD.TXT\r\n@md DATA\r\n@echo x> DATA\\X.DAT\r\n"),
            ("readme.txt", b"installer\r\n"),
        ]:
            info = zipfile.ZipInfo(name, date_time=STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    return path


def installer_scratches(tmp_path):
    # Every archive mount also owns a helper scratch; the union's one holds
    # what the installer wrote.
    scratch = tmp_path / "cache" / "scratch"
    if not scratch.is_dir():
        return []
    return [d for d in scratch.iterdir() if (d / "INSTALLD.TXT").is_file()]


def test_a_scratch_mount_discards_the_installers_writes(dosbox_e2e, tmp_path):
    """Phase 6.4: installer media mount -scratch; the installer writes to its
    drive, nothing lands in a sidecar, the unmount removes the scratch."""
    zip_path = build_installer_zip(tmp_path / "game" / "setup.zip")
    inst = dosbox_e2e(
        autoexec_lines=[f"mount c {zip_path} -scratch -run install.bat"],
        conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and not installer_scratches(tmp_path):
        time.sleep(0.2)
    found = installer_scratches(tmp_path)
    assert len(found) == 1, screen(inst.client)
    written = found[0]
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and not (written / "DATA" / "X.DAT").is_file():
        time.sleep(0.2)
    assert (written / "DATA" / "X.DAT").is_file(), screen(inst.client)
    assert not (tmp_path / "saves").exists()
    wait_text(inst.client, "C:\\>")
    r = inst.client.type_string("z:\nmount -u c\n")
    assert r.status_code == 200, r.text
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and written.exists():
        time.sleep(0.2)
    assert not written.exists(), screen(inst.client)
    assert not (tmp_path / "saves").exists()


# ---------------------------------------------------------------------------
# Nested archives Phase 4: the wrapped house archive, the spelled chain
# ---------------------------------------------------------------------------


def build_wrapped_house_zip(path: Path, programs: dict[str, bytes]) -> Path:
    """augra-pack's outer zip: info.toml beside one stored inner zip named
    after the outer with ".store" (augra-pack.md, "Create outer zip")."""
    path.parent.mkdir(parents=True, exist_ok=True)
    inner = io.BytesIO()
    with zipfile.ZipFile(inner, "w", compression=zipfile.ZIP_STORED) as z:
        for name, data in {"README.TXT": b"hello\r\n", **programs}.items():
            z.writestr(zipfile.ZipInfo(name, date_time=STAMP), data,
                       compress_type=zipfile.ZIP_STORED)
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(zipfile.ZipInfo("info.toml", date_time=STAMP),
                   b'[game]\r\ntitle = "Wrapped"\r\n',
                   compress_type=zipfile.ZIP_DEFLATED)
        z.writestr(zipfile.ZipInfo(f"{path.stem}.store.zip", date_time=STAMP),
                   inner.getvalue(), compress_type=zipfile.ZIP_STORED)
    return path


def build_collection_zip(path: Path, inner_name: str, programs: dict[str, bytes]) -> Path:
    """A wild collection: a readme beside a stored inner zip, so only a
    spelled chain reaches the inner archive (the house rule wants nothing
    but info.toml beside it)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    inner = io.BytesIO()
    with zipfile.ZipFile(inner, "w", compression=zipfile.ZIP_STORED) as z:
        for name, data in programs.items():
            z.writestr(zipfile.ZipInfo(name, date_time=STAMP), data,
                       compress_type=zipfile.ZIP_STORED)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as z:
        z.writestr(zipfile.ZipInfo("readme.txt", date_time=STAMP), b"a collection\r\n")
        z.writestr(zipfile.ZipInfo(inner_name, date_time=STAMP), inner.getvalue())
    return path


def test_a_wrapped_house_archive_on_the_command_line_runs_its_game(tmp_path):
    """Plan 5's --run path through N3: the engine enters the inner zip,
    finds the one program there, runs it, and the writes land in a
    sidecar named after the outer file (N5)."""
    zip_path = build_wrapped_house_zip(tmp_path / "wrapped.zip", {"GAME.BAT": DONE_BAT})
    before = sha256(zip_path)
    assert run_to_exit(tmp_path, [str(zip_path)]) == 0
    assert sidecar_files(tmp_path) == ["DONE.TXT"]
    saves = [p.name for p in (tmp_path / "saves").iterdir()]
    assert len(saves) == 1 and saves[0].startswith("wrapped-"), saves
    assert sha256(zip_path) == before


def test_a_spelled_chain_mounts_the_inner_archive_as_the_drive(dosbox_e2e, tmp_path):
    zip_path = build_collection_zip(tmp_path / "game" / "coll.zip", "game.zip",
                                    {"GAME.BAT": DONE_BAT, "DATA.TXT": b"d\r\n"})
    before = sha256(zip_path)
    inst = dosbox_e2e(
        autoexec_lines=[
            f"mount c {zip_path}/game.zip",
            "c:",
            "dir /b",
            "mount",
        ],
        conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    text = wait_text(inst.client, "Archive")
    assert "coll.zip/game.zip" in text, text
    assert sorted(listing_after(text, "C:\\>dir /b")) == ["DATA.TXT", "GAME.BAT"], text
    assert "README" not in text, text
    assert "Entering" not in text, text  # spelled, not the house rule
    assert sha256(zip_path) == before
    inst.shutdown()
