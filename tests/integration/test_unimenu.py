# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""UniMenu end to end over an archive with a root game, a folder game that
reads from the current directory, a sound setup and a DOS extender: the
menu on Y: and the choice coming back through the ports."""

import io
import os
import secrets
import subprocess
import time
import zipfile
from pathlib import Path

from conftest import DOSBOX_BIN, WORKSPACE
from PIL import Image

STAMP = (1994, 5, 1, 12, 0, 0)
MENU_TITLE = "Select a program"

PROBE = {
    "PLAY.BAT": b"@echo PLAY-ROOT\r\n",
    "SETSOUND.BAT": b"@echo SETSOUND-RAN > C:\\SOUND.CFG\r\n",
    "KEEN/RUN.BAT": b"@type data.txt\r\n",
    "KEEN/DATA.TXT": b"DATA-FOUND\r\n",
    "KEEN/DOS4GW.EXE": b"MZ" + bytes(62),
}


def build_zip(path: Path, members: dict[str, bytes]) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w") as z:
        for name, data in members.items():
            z.writestr(zipfile.ZipInfo(name, date_time=STAMP), data)
    return path


def settings_for(tmp_path: Path) -> dict[str, str]:
    return {
        "archive_cache_path": str(tmp_path / "cache"),
        "archive_sidecar_path": str(tmp_path / "saves"),
    }


def screen(client) -> str:
    r = client.screen_text()
    assert r.status_code == 200, r.text
    return "\n".join(line.rstrip() for line in r.json()["text"].split("\n"))


def wait_text(client, needle: str, timeout: float = 20.0) -> str:
    deadline = time.monotonic() + timeout
    text = ""
    while time.monotonic() < deadline:
        text = screen(client)
        if needle in text:
            return text
        time.sleep(0.2)
    raise AssertionError(f"{needle!r} not on screen:\n{text}")


def segment(client) -> str:
    r = client.program_state()
    assert r.status_code == 200, r.text
    return r.json()["segment_name"]


def key(client, name: str) -> None:
    events = [{"type": "key", "key": name, "pressed": True},
              {"type": "key", "key": name, "pressed": False}]
    deadline = time.monotonic() + 5.0
    while True:
        r = client.input_sequence(events)
        if r.status_code != 409 or time.monotonic() > deadline:
            break
        time.sleep(0.05)
    assert r.status_code == 200, f"key {name}: {r.status_code} {r.text}"


def start_menu(dosbox_e2e, tmp_path: Path, members: dict[str, bytes],
               before: list[str] | None = None,
               extra_settings: dict[str, str] | None = None):
    zip_path = build_zip(tmp_path / "game" / "probe.zip", members)
    return dosbox_e2e(
        autoexec_lines=[*(before or []), f"mount c {zip_path} -run"],
        conf_dir=tmp_path / "game",
        settings={**settings_for(tmp_path), **(extra_settings or {})},
    )


def pick(client, downs: int) -> None:
    wait_text(client, MENU_TITLE)
    for _ in range(downs):
        key(client, "KBD_down")
        time.sleep(0.2)
    key(client, "KBD_enter")


def test_the_menu_lists_the_programs_and_hides_the_extender(dosbox_e2e, tmp_path):
    inst = start_menu(dosbox_e2e, tmp_path, PROBE)
    text = wait_text(inst.client, MENU_TITLE)
    assert "(PLAY.BAT)" in text, text
    assert "(KEEN\\RUN.BAT)" in text, text
    assert "(SETSOUND.BAT)" in text, text
    assert "DOS4GW" not in text, text
    lines = text.split("\n")
    order = [next(i for i, line in enumerate(lines) if f"({name})" in line)
             for name in ("PLAY.BAT", "KEEN\\RUN.BAT", "SETSOUND.BAT")]
    assert order == sorted(order), f"games first, then setup:\n{text}"


def test_a_program_in_a_folder_runs_in_its_folder(dosbox_e2e, tmp_path):
    inst = start_menu(dosbox_e2e, tmp_path, PROBE)
    pick(inst.client, 1)
    text = wait_text(inst.client, "DATA-FOUND")
    assert "File not found" not in text, text
    assert MENU_TITLE not in text, "the menu is cleared before the program runs"


def test_a_setup_entry_returns_to_the_menu_and_keeps_its_writes(dosbox_e2e, tmp_path):
    inst = start_menu(dosbox_e2e, tmp_path, PROBE)
    pick(inst.client, 2)
    deadline = time.monotonic() + 20.0
    written = []
    while time.monotonic() < deadline and not written:
        written = [p for p in (tmp_path / "saves").rglob("*")
                   if p.name.upper() == "SOUND.CFG"]
        time.sleep(0.2)
    assert written, "the setup's write did not reach the save folder"
    assert "SETSOUND-RAN" in written[0].read_text(encoding="ascii")
    wait_text(inst.client, MENU_TITLE)
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline and segment(inst.client) != "UNIMENU":
        time.sleep(0.2)
    assert segment(inst.client) == "UNIMENU"


def test_escape_leaves_the_prompt(dosbox_e2e, tmp_path):
    inst = start_menu(dosbox_e2e, tmp_path, PROBE)
    wait_text(inst.client, MENU_TITLE)
    key(inst.client, "KBD_esc")
    text = wait_text(inst.client, "C:\\>")
    assert MENU_TITLE not in text, text


def cursor_start_byte(client) -> int:
    """CH of the cursor shape, kept in the BDA at 0040:0061."""
    r = client.memory_read(0x461, 1)
    assert r.status_code == 200, r.text
    return r.content[0]


def test_the_menu_hides_the_cursor_and_gives_it_back(dosbox_e2e, tmp_path):
    inst = start_menu(dosbox_e2e, tmp_path, PROBE)
    wait_text(inst.client, MENU_TITLE)
    # Bits 6 and 5 = 01 hide the cursor (RBIL INT 10/AH=01h, table 00013).
    assert cursor_start_byte(inst.client) & 0x60 == 0x20
    key(inst.client, "KBD_esc")
    wait_text(inst.client, "C:\\>")
    assert cursor_start_byte(inst.client) & 0x60 == 0


def test_unimenu_typed_by_hand_says_who_starts_it(dosbox_e2e, tmp_path):
    inst = dosbox_e2e(autoexec_lines=["Y:\\DOS\\UNIMENU.EXE"])
    wait_text(inst.client, "UniMenu is started by dosbox-automation.")


def test_one_visible_program_runs_without_a_menu(dosbox_e2e, tmp_path):
    members = {"GAME.BAT": b"@echo GAME-RAN\r\n", "DOS4GW.EXE": b"MZ" + bytes(62)}
    inst = start_menu(dosbox_e2e, tmp_path, members)
    text = wait_text(inst.client, "GAME-RAN")
    assert MENU_TITLE not in text, text


CHOOSER = (
    'title = "Curated Keen"\n'
    '[[entry]]\nlabel = "Play Keen"\nexe = "KEEN\\\\RUN.BAT"\ntype = "game"\n'
    '[[entry]]\nlabel = "Sound Setup"\nexe = "SETSOUND.BAT"\ntype = "setup"\n'
).encode("ascii")


def test_a_chooser_file_replaces_the_scan(dosbox_e2e, tmp_path):
    inst = start_menu(dosbox_e2e, tmp_path, {**PROBE, "CHOOSER.TML": CHOOSER})
    text = wait_text(inst.client, "Curated Keen")
    assert "Play Keen (KEEN\\RUN.BAT)" in text, text
    assert "Sound Setup (SETSOUND.BAT)" in text, text
    assert "PLAY.BAT" not in text, f"the file overrides the scan:\n{text}"
    key(inst.client, "KBD_enter")
    wait_text(inst.client, "DATA-FOUND")


def test_an_autostart_entry_runs_without_the_menu(dosbox_e2e, tmp_path):
    chooser = (b'[[entry]]\nlabel = "Play"\nexe = "PLAY.BAT"\nautostart = true\n'
               b'[[entry]]\nlabel = "Sound"\nexe = "SETSOUND.BAT"\n')
    inst = start_menu(dosbox_e2e, tmp_path, {**PROBE, "CHOOSER.TML": chooser})
    text = wait_text(inst.client, "PLAY-ROOT")
    assert MENU_TITLE not in text, text


def test_without_the_web_api_the_menu_waits_for_a_choice(tmp_path):
    """No API, no screen to read: the old build ended the process within a
    second because UniMenu found no port and the queued EXIT ran."""
    game = tmp_path / "game"
    zip_path = build_zip(game / "probe.zip", PROBE)
    conf = game / "run.conf"
    sections = settings_for(tmp_path)
    conf.write_text(
        "[joystick]\njoysticktype = none\n"
        "[dos]\n" + "".join(f"{k} = {v}\n" for k, v in sections.items())
        + "[webserver]\nwebserver_enabled = false\n",
        encoding="utf-8",
    )
    env = {**os.environ, "HOME": str(tmp_path), "XDG_CONFIG_HOME": str(tmp_path / ".config"),
           "SDL_VIDEODRIVER": "offscreen", "SDL_AUDIODRIVER": "dummy"}
    proc = subprocess.Popen(
        [DOSBOX_BIN, "--noprimaryconf", "--nolocalconf", "-conf", str(conf), str(zip_path)],
        env=env, cwd=str(tmp_path), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        time.sleep(8)
        assert proc.poll() is None, f"the engine ended with {proc.returncode}, no menu waited"
    finally:
        proc.kill()
        proc.wait(timeout=10)


def test_ctrl_c_leaves_the_menu_up(dosbox_e2e, tmp_path):
    inst = start_menu(dosbox_e2e, tmp_path, PROBE)
    wait_text(inst.client, MENU_TITLE)
    chord = [{"type": "key", "key": "KBD_leftctrl", "pressed": True},
             {"type": "key", "key": "KBD_c", "pressed": True},
             {"type": "key", "key": "KBD_c", "pressed": False},
             {"type": "key", "key": "KBD_leftctrl", "pressed": False}]
    assert inst.client.input_sequence(chord).status_code == 200
    time.sleep(1.5)
    text = screen(inst.client)
    assert MENU_TITLE in text, f"Ctrl-C ended the menu:\n{text}"
    assert segment(inst.client) == "UNIMENU"


def test_the_footer_stays_inside_the_box_in_40_columns(dosbox_e2e, tmp_path):
    inst = start_menu(dosbox_e2e, tmp_path, PROBE, before=["@Y:\\DOS\\SETMODE 1"])
    lines = wait_text(inst.client, MENU_TITLE).split("\n")
    entry = next(line for line in lines if "(PLAY.BAT)" in line)
    footer = next(line for line in lines if "ENTER" in line)
    right = len(entry) - 1
    assert len(footer) - 1 == right, f"footer past the border:\n{entry}\n{footer}"
    assert footer[right] == entry[right], f"border overwritten:\n{entry}\n{footer}"


def test_the_text_mode_message_is_visible_in_a_graphics_mode(dosbox_e2e, tmp_path):
    inst = dosbox_e2e(autoexec_lines=["@Y:\\DOS\\SETMODE 13", "@Y:\\DOS\\UNIMENU.EXE"])
    time.sleep(4)
    r = inst.client.frame(fmt="png")
    assert r.status_code == 200, r.text
    img = Image.open(io.BytesIO(r.content)).convert("L")
    # UniMenu's message is the first text row after the mode set cleared the screen.
    band = img.crop((0, 0, img.width, 8))
    lit = sum(1 for v in band.getdata() if v > 0)
    assert lit > 0, "the message was drawn in colour 0"


def test_a_stray_row_count_on_hercules_keeps_the_menu_on_screen(dosbox_e2e, tmp_path):
    # 40:84 belongs to EGA and later BIOSes; on older adapters it can hold anything.
    inst = start_menu(dosbox_e2e, tmp_path, PROBE, before=["@pause"],
                      extra_settings={"machine": "hercules"})
    wait_text(inst.client, "Press any key")
    assert inst.client.memory_write(0x484, bytes([48])).status_code == 200
    key(inst.client, "KBD_space")
    lines = wait_text(inst.client, MENU_TITLE).split("\n")
    assert "PN:" in lines[24], "the version line left the 25-row screen"


def test_a_menu_whose_entries_are_all_hidden_says_so(dosbox_e2e, tmp_path):
    chooser = (b'[[entry]]\nlabel = "Extender"\nexe = "DOS4GW.EXE"\n'
               b'[[entry]]\nlabel = "Host"\nexe = "CWSDPMI.EXE"\n')
    members = {"DOS4GW.EXE": b"MZ" + bytes(62), "CWSDPMI.EXE": b"MZ" + bytes(62),
               "CHOOSER.TML": chooser}
    inst = start_menu(dosbox_e2e, tmp_path, members)
    text = wait_text(inst.client, "nothing to start")
    assert "is started by dosbox-automation" not in text, text


def test_the_one_program_left_after_autoexec_bat_runs(dosbox_e2e, tmp_path):
    members = {"GAME.BAT": b"@echo GAME-RAN\r\n", "AUTOEXEC.BAT": b"@echo AUTOEXEC-RAN\r\n"}
    inst = start_menu(dosbox_e2e, tmp_path, members)
    text = wait_text(inst.client, "GAME-RAN")
    assert "AUTOEXEC-RAN" not in text, text
    assert MENU_TITLE not in text, text


def test_programs_only_in_folders_get_the_menu(dosbox_e2e, tmp_path):
    members = {"README.TXT": b"read me\r\n",
               "KEEN/RUN.BAT": b"@echo RUN-RAN\r\n",
               "KEEN/SETUP.BAT": b"@echo SETUP-RAN\r\n"}
    inst = start_menu(dosbox_e2e, tmp_path, members)
    text = wait_text(inst.client, MENU_TITLE)
    assert "(KEEN\\RUN.BAT)" in text, text
    assert "(KEEN\\SETUP.BAT)" in text, text


def test_a_dosbox_bat_runs_as_is(dosbox_e2e, tmp_path):
    inst = start_menu(dosbox_e2e, tmp_path,
                      {**PROBE, "DOSBOX.BAT": b"@echo DOSBOX-BAT-RAN\r\n"})
    text = wait_text(inst.client, "DOSBOX-BAT-RAN")
    assert MENU_TITLE not in text, text


def test_a_foreign_y_drive_gets_no_menu_turn(dosbox_e2e, tmp_path):
    # A COM image under the EXE name: MOV AH,9 / MOV DX,108h / INT 21h / RET.
    foreign = bytes([0xB4, 0x09, 0xBA, 0x08, 0x01, 0xCD, 0x21, 0xC3]) + b"FOREIGN-MENU-RAN\r\n$"
    fake = tmp_path / "game" / "fakey" / "DOS"
    fake.mkdir(parents=True)
    (fake / "UNIMENU.EXE").write_bytes(foreign)
    inst = start_menu(dosbox_e2e, tmp_path, PROBE,
                      before=["@mount -u y", f"@mount y {fake.parent}"])
    text = wait_text(inst.client, "several programs")
    assert "mounted as Y drive" in text, f"the fake Y: never mounted:\n{text}"
    assert "FOREIGN-MENU-RAN" not in text, text


def test_an_overlay_on_y_gets_no_menu_turn(dosbox_e2e, tmp_path):
    foreign = bytes([0xB4, 0x09, 0xBA, 0x08, 0x01, 0xCD, 0x21, 0xC3]) + b"FOREIGN-MENU-RAN\r\n$"
    fake = tmp_path / "game" / "overlay" / "DOS"
    fake.mkdir(parents=True)
    (fake / "UNIMENU.EXE").write_bytes(foreign)
    # MOUNT keeps the shortest form of a path, and an overlay must be
    # addressed like its base: both go in relative to the engine's cwd.
    work = WORKSPACE / f"e2e-{secrets.token_hex(4)}"
    bundled = Path(DOSBOX_BIN).parent / "resources" / "drives" / "y"
    y_rel = os.path.relpath(bundled, work)
    overlay_rel = os.path.relpath(fake.parent, work)
    zip_path = build_zip(tmp_path / "game" / "probe.zip", PROBE)
    inst = dosbox_e2e(
        autoexec_lines=["@mount -u y", f"@mount y {y_rel}",
                        f"@mount -t overlay y {overlay_rel}",
                        f"mount c {zip_path} -run"],
        work_dir=work, conf_dir=tmp_path / "game",
        settings=settings_for(tmp_path),
    )
    text = wait_text(inst.client, "several programs")
    assert "on drive Y mounted" in text, f"the overlay never mounted:\n{text}"
    assert "FOREIGN-MENU-RAN" not in text, text


def test_a_lone_autoexec_bat_does_not_beat_the_folder_menu(dosbox_e2e, tmp_path):
    members = {"AUTOEXEC.BAT": b"@echo AUTOEXEC-RAN\r\n",
               "GAME/PLAY.BAT": b"@echo PLAY-RAN\r\n",
               "GAME/SETUP.BAT": b"@echo SETUP-RAN\r\n"}
    inst = start_menu(dosbox_e2e, tmp_path, members)
    text = wait_text(inst.client, MENU_TITLE)
    assert "AUTOEXEC-RAN" not in text, text
    assert "(GAME\\PLAY.BAT)" in text, text
