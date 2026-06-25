# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""screen_text() tests across machine types and column modes.

Each test boots a DOSBox instance with a specific machine type, types a
marker string, waits for it to appear on screen, then reads the screen
and verifies content and column count. This catches regressions in the
text buffer base address handling (CGA at 0xB8000, Hercules at 0xB0000)
and column count detection.
"""

import time

import pytest


def run_script_on(instance, source, name="machine-test", timeout=30):
    client = instance.client
    client.wait_shell(timeout=15)
    time.sleep(2.1)
    r = client.script_load(source, name=name)
    assert r.status_code == 200, f"Load failed: {r.json()}"
    r = client.script_start()
    assert r.status_code == 200, f"Start failed: {r.json()}"
    data = client.wait_script_done(timeout=timeout)
    assert data["state"] == "completed", (
        f"Script failed: state={data['state']}, error={data.get('error', '')}"
    )
    return data.get("output", {})


MARKER_SCRIPT = (
    "dosbox.wait_frames(10)\n"
    "dosbox.type('echo SCRNTST92')\n"
    "dosbox.key('KBD_enter', true)\n"
    "dosbox.key('KBD_enter', false)\n"
    "dosbox.wait_for_text('SCRNTST92', 5000)\n"
    "local t = dosbox.screen_text()\n"
    "dosbox.output.has_marker = string.find(t, 'SCRNTST92') and 'yes' or 'no'\n"
    "local first_line = string.match(t, '([^\\n]*)')\n"
    "dosbox.output.line_len = #first_line\n"
    "dosbox.output.total_len = #t\n"
    "local cols = dosbox.mem_read_word(0x40, 0x4a)\n"
    "dosbox.output.bios_cols = cols\n"
)



def test_screen_text_cga_80col(dosbox_e2e):
    inst = dosbox_e2e(extra_sets=["machine=cga"])
    out = run_script_on(inst, MARKER_SCRIPT)
    assert out["has_marker"] == "yes"
    assert out["bios_cols"] == 80
    assert out["line_len"] == 80



def test_screen_text_cga_40col(dosbox_e2e):
    inst = dosbox_e2e(extra_sets=["machine=cga"])
    script = (
        "dosbox.wait_frames(10)\n"
        "dosbox.type('mode co40')\n"
        "dosbox.key('KBD_enter', true)\n"
        "dosbox.key('KBD_enter', false)\n"
        "dosbox.wait_for_text('Resident', 5000)\n"
        "dosbox.type('echo TST40COL')\n"
        "dosbox.key('KBD_enter', true)\n"
        "dosbox.key('KBD_enter', false)\n"
        "dosbox.wait_for_text('TST40COL', 5000)\n"
        "local t = dosbox.screen_text()\n"
        "dosbox.output.has_marker = string.find(t, 'TST40COL') and 'yes' or 'no'\n"
        "local first_line = string.match(t, '([^\\n]*)')\n"
        "dosbox.output.line_len = #first_line\n"
        "local cols = dosbox.mem_read_word(0x40, 0x4a)\n"
        "dosbox.output.bios_cols = cols\n"
    )
    out = run_script_on(inst, script)
    assert out["has_marker"] == "yes"
    assert out["bios_cols"] == 40
    assert out["line_len"] == 40



def test_screen_text_hercules(dosbox_e2e):
    inst = dosbox_e2e(extra_sets=["machine=hercules"])
    out = run_script_on(inst, MARKER_SCRIPT)
    assert out["has_marker"] == "yes"
    assert out["bios_cols"] == 80
    assert out["line_len"] == 80



def test_screen_text_ega(dosbox_e2e):
    inst = dosbox_e2e(extra_sets=["machine=ega"])
    out = run_script_on(inst, MARKER_SCRIPT)
    assert out["has_marker"] == "yes"
    assert out["bios_cols"] == 80
    assert out["line_len"] == 80



def test_screen_text_tandy(dosbox_e2e):
    inst = dosbox_e2e(extra_sets=["machine=tandy"])
    out = run_script_on(inst, MARKER_SCRIPT)
    assert out["has_marker"] == "yes"
    assert out["bios_cols"] == 80
    assert out["line_len"] == 80
