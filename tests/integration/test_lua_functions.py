# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Integration tests for individual dosbox.* Lua API functions.

Each test loads a script that exercises one function, writes results
to dosbox.output, and verifies via the status endpoint.
"""

import time

import pytest


# Helper: load, start, wait for completion, return output.
def run_script(dosbox, source, name="func-test", seed=None, timeout=10):
    time.sleep(2.1)  # rate limiter
    r = dosbox.script_load(source, name=name, seed=seed)
    assert r.status_code == 200, f"Load failed: {r.json()}"
    r = dosbox.script_start()
    assert r.status_code == 200, f"Start failed: {r.json()}"
    data = dosbox.wait_script_done(timeout=timeout)
    assert data["state"] == "completed", (
        f"Script failed: state={data['state']}, error={data.get('error', '')}"
    )
    return data.get("output", {})


def test_frame_returns_number(dosbox):
    out = run_script(dosbox, "dosbox.output.frame = dosbox.frame()\n")
    assert isinstance(out["frame"], int)
    assert out["frame"] >= 0


def test_wait_frames_advances(dosbox):
    out = run_script(dosbox, (
        "local before = dosbox.frame()\n"
        "dosbox.wait_frames(5)\n"
        "local after = dosbox.frame()\n"
        "dosbox.output.delta = after - before\n"
    ))
    assert out["delta"] >= 5


def test_log_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.log('test message')\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def test_screen_text_returns_string(dosbox):
    out = run_script(dosbox, (
        "local t = dosbox.screen_text()\n"
        "dosbox.output.len = #t\n"
        "dosbox.output.ttype = type(t)\n"
    ))
    assert out["ttype"] == "string"
    assert out["len"] > 0


def test_screen_match_finds_text(dosbox):
    # Default DOSBox shell is on Z: drive with no config loaded.
    out = run_script(dosbox, (
        "dosbox.wait_frames(30)\n"
        "local found = dosbox.screen_match('Z:')\n"
        "dosbox.output.found = found and 'yes' or 'no'\n"
    ))
    assert out["found"] == "yes"


def test_screen_match_ignorecase(dosbox):
    out = run_script(dosbox, (
        "dosbox.wait_frames(30)\n"
        "local found = dosbox.screen_match('z:', {ignorecase=true})\n"
        "dosbox.output.found = found and 'yes' or 'no'\n"
    ))
    assert out["found"] == "yes"


def test_wait_for_text_immediate(dosbox):
    # Z: should already be on screen from the shell prompt.
    out = run_script(dosbox, (
        "dosbox.wait_frames(30)\n"
        "local found = dosbox.wait_for_text('Z:', 60)\n"
        "dosbox.output.found = found and 'yes' or 'no'\n"
    ))
    assert out["found"] == "yes"


def test_wait_for_text_timeout(dosbox):
    out = run_script(dosbox, (
        "local found = dosbox.wait_for_text('NONEXISTENT_XYZ', 30)\n"
        "dosbox.output.found = found and 'yes' or 'no'\n"
    ))
    assert out["found"] == "no"


def test_key_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.key('KBD_a', true)\n"
        "dosbox.key('KBD_a', false)\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def test_type_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.type('hello')\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def test_osd_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.osd('test overlay')\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def test_osd_clear_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.osd('temp')\n"
        "dosbox.osd_clear()\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def test_mem_read_byte_returns_integer(dosbox):
    # Read from BIOS data area (segment 0x0040, offset 0x0010) which
    # contains the equipment word. Should be a non-negative integer.
    out = run_script(dosbox, (
        "local val = dosbox.mem_read_byte(0x0040, 0x0010)\n"
        "dosbox.output.val = val\n"
        "dosbox.output.vtype = type(val)\n"
    ))
    assert out["vtype"] == "number"
    assert isinstance(out["val"], int)
    assert 0 <= out["val"] <= 255


def test_mem_read_returns_correct_length(dosbox):
    out = run_script(dosbox, (
        "local data = dosbox.mem_read(0x0040, 0x0000, 16)\n"
        "dosbox.output.len = #data\n"
    ))
    assert out["len"] == 16


def test_mount_lock(dosbox):
    out = run_script(dosbox, (
        "dosbox.mount_lock()\n"
        "dosbox.output.locked = 'yes'\n"
    ))
    assert out["locked"] == "yes"


def test_capture_start_stop_does_not_error(dosbox):
    out = run_script(dosbox, (
        "dosbox.capture_start()\n"
        "dosbox.wait_frames(5)\n"
        "dosbox.capture_stop()\n"
        "dosbox.output.ok = 'yes'\n"
    ))
    assert out["ok"] == "yes"


def test_output_table_multiple_values(dosbox):
    out = run_script(dosbox, (
        "dosbox.output.str_val = 'hello'\n"
        "dosbox.output.int_val = 42\n"
        "dosbox.output.bool_val = true\n"
    ))
    assert out["str_val"] == "hello"
    assert out["int_val"] == 42
    assert out["bool_val"] is True
