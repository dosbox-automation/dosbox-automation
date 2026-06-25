# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Text-mode matrix across machine types.

Boots each machine type, optionally switches text mode, prints a marker,
then reads back the screen and the BIOS data area. Verifies three things
per case: the marker is present (screen reading works), the BIOS mode
byte at 0040:0049 is the mode we expect, and the column count at
0040:004A matches. This catches regressions in the text-buffer base
address handling (CGA at 0xB8000, Hercules at 0xB0000) and in column and
mode detection.

The DOS `mode` command only reaches the color text modes 01h (40x25) and
03h (80x25); `bw40`/`bw80` resolve to the same color modes, not the
mono-suppressed 00h/02h. Mode 07h is the mono adapter default. The true
00h/02h modes and all graphics modes need an INT 10h driver and live in
the graphics-mode matrix, not here.
"""

import time

import pytest

MARKER = "MXTST42"


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


def build_script(mode_cmd):
    lines = ["dosbox.wait_frames(10)\n"]
    if mode_cmd:
        # The mode command prints nothing, so settle with wait_frames rather
        # than waiting on output that never appears.
        lines += [
            f"dosbox.type('{mode_cmd}')\n",
            "dosbox.key('KBD_enter', true)\n",
            "dosbox.key('KBD_enter', false)\n",
            "dosbox.wait_frames(60)\n",
        ]
    lines += [
        f"dosbox.type('echo {MARKER}')\n",
        "dosbox.key('KBD_enter', true)\n",
        "dosbox.key('KBD_enter', false)\n",
        f"dosbox.wait_for_text('{MARKER}', 5000)\n",
        "local t = dosbox.screen_text()\n",
        f"dosbox.output.has_marker = string.find(t, '{MARKER}') and 'yes' or 'no'\n",
        "local first_line = string.match(t, '([^\\n]*)')\n",
        "dosbox.output.line_len = #first_line\n",
        "dosbox.output.mode = dosbox.mem_read_byte(0x40, 0x49)\n",
        "dosbox.output.bios_cols = dosbox.mem_read_word(0x40, 0x4a)\n",
    ]
    return "".join(lines)


# id, machine, mode command (None = boot default), expected mode byte, expected cols
TEXT_MODE_MATRIX = [
    ("cga_80",      "cga",      None,        0x03, 80),
    ("cga_40",      "cga",      "mode co40", 0x01, 40),
    ("cga_mono_80", "cga_mono", None,        0x03, 80),
    ("ega_80",      "ega",      None,        0x03, 80),
    ("ega_40",      "ega",      "mode co40", 0x01, 40),
    ("tandy_80",    "tandy",    None,        0x03, 80),
    ("tandy_40",    "tandy",    "mode co40", 0x01, 40),
    ("vga_80",      "svga_s3",  None,        0x03, 80),
    ("vga_40",      "svga_s3",  "mode co40", 0x01, 40),
    ("hercules_70", "hercules", None,        0x07, 80),
]


@pytest.mark.parametrize(
    "machine,mode_cmd,expected_mode,expected_cols",
    [c[1:] for c in TEXT_MODE_MATRIX],
    ids=[c[0] for c in TEXT_MODE_MATRIX],
)
def test_text_mode(dosbox_e2e, machine, mode_cmd, expected_mode, expected_cols):
    inst = dosbox_e2e(extra_sets=[f"machine={machine}"])
    out = run_script_on(inst, build_script(mode_cmd))

    assert out["has_marker"] == "yes"
    assert out["mode"] == expected_mode, (
        f"mode byte 0x{out['mode']:02x} != expected 0x{expected_mode:02x}"
    )
    assert out["bios_cols"] == expected_cols
    assert out["line_len"] == expected_cols
