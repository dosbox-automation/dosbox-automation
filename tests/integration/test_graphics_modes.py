# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Graphics-mode matrix across machine types.

Sets each standard BIOS graphics mode on the card that natively supports
it (CGA modes on cga, EGA modes on ega, VGA modes on svga_s3, Tandy modes
on tandy), then verifies the mode took and the frame renders at the right
resolution. Unlike the text-mode matrix, screen_text() does not apply
here (a graphics mode has no text buffer), so each case asserts:

  * the BIOS mode byte at 0040:0049 is the mode we set,
  * /api/v1/video/frame/info reports the expected width and height,
  * the script ran to completion (the instance kept rendering, no stall).

The Lua API cannot invoke INT 10h, so the mode is set by setmode.com on
the Y: tools drive (see resources/drives/y/dos/setmode.asm).

Known, deliberate gaps:
  * 0Fh/10h and 11h/12h each share a resolution; they differ only in
    colour depth, which lives in the attribute-controller programming and
    is not visible through frame/info. We pin the mode byte and resolution
    here; depth is out of scope for this path.
  * EGA vs VGA 640x350 (0Fh/10h) differ only in DAC width (6-bit vs
    18-bit), also not visible here.
  * Hercules 720x348 graphics is set through the HGC control ports, not
    INT 10h, so setmode.com does not reach it. Mode X (tweaked VGA) is not
    BIOS-settable either. Both are tracked separately and out of scope.
"""

import time

import pytest


def run_setmode(instance, mode_hex):
    client = instance.client
    client.wait_shell(timeout=15)
    time.sleep(2.1)
    source = (
        "dosbox.wait_frames(10)\n"
        f"dosbox.type('setmode {mode_hex}')\n"
        "dosbox.key('KBD_enter', true)\n"
        "dosbox.key('KBD_enter', false)\n"
        "dosbox.wait_frames(60)\n"
        "dosbox.output.mode = dosbox.mem_read_byte(0x40, 0x49)\n"
    )
    assert client.script_load(source, name="gfx-mode").status_code == 200
    assert client.script_start().status_code == 200
    data = client.wait_script_done(timeout=30)
    assert data["state"] == "completed", (
        f"Script did not complete: state={data['state']}, "
        f"error={data.get('error', '')}"
    )
    info = client.frame_info()
    assert info.status_code == 200, f"No frame after mode set: {info.status_code}"
    return data.get("output", {}), info.json()


# id, machine, mode arg, expected mode byte, expected width, expected height
GRAPHICS_MODE_MATRIX = [
    ("cga_04h",   "cga",     "04", 0x04, 320, 200),
    ("cga_05h",   "cga",     "05", 0x05, 320, 200),
    ("cga_06h",   "cga",     "06", 0x06, 640, 200),
    ("ega_0dh",   "ega",     "0d", 0x0d, 320, 200),
    ("ega_0eh",   "ega",     "0e", 0x0e, 640, 200),
    ("ega_0fh",   "ega",     "0f", 0x0f, 640, 350),
    ("ega_10h",   "ega",     "10", 0x10, 640, 350),
    ("vga_11h",   "svga_s3", "11", 0x11, 640, 480),
    ("vga_12h",   "svga_s3", "12", 0x12, 640, 480),
    ("vga_13h",   "svga_s3", "13", 0x13, 320, 200),
    ("tandy_08h", "tandy",   "08", 0x08, 320, 200),
    ("tandy_09h", "tandy",   "09", 0x09, 320, 200),
    ("tandy_0ah", "tandy",   "0a", 0x0a, 640, 200),
]


@pytest.mark.parametrize(
    "machine,mode_arg,expected_mode,expected_w,expected_h",
    [c[1:] for c in GRAPHICS_MODE_MATRIX],
    ids=[c[0] for c in GRAPHICS_MODE_MATRIX],
)
def test_graphics_mode(dosbox_e2e, machine, mode_arg, expected_mode,
                       expected_w, expected_h):
    inst = dosbox_e2e(extra_sets=[f"machine={machine}"])
    out, info = run_setmode(inst, mode_arg)

    assert out["mode"] == expected_mode, (
        f"mode byte 0x{out['mode']:02x} != expected 0x{expected_mode:02x}"
    )
    assert (info["width"], info["height"]) == (expected_w, expected_h), (
        f"frame {info['width']}x{info['height']} != "
        f"expected {expected_w}x{expected_h}"
    )
