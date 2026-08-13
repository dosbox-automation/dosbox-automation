# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""REST screen-text route (GET /api/v1/video/text).

The same character buffer that wait_for_text reads from inside a Lua
script, exposed over HTTP so a client can drive an installer by screen
state without loading a script first. These tests confirm the route
reads a real text screen, reports the right geometry, and correctly
reports a graphics mode as non-text.
"""

import time

MARKER = "RTTST77"


def settle_to_shell(client):
    client.wait_shell(timeout=15)
    time.sleep(2.1)


def echo_marker(client):
    source = (
        "dosbox.wait_frames(10)\n"
        f"dosbox.type('echo {MARKER}')\n"
        "dosbox.key('KBD_enter', true)\n"
        "dosbox.key('KBD_enter', false)\n"
        f"dosbox.wait_for_text('{MARKER}', 5000)\n"
    )
    assert client.script_load(source, name="marker").status_code == 200
    assert client.script_start().status_code == 200
    data = client.wait_script_done(timeout=30)
    assert data["state"] == "completed", (
        f"Script did not complete: state={data['state']}, "
        f"error={data.get('error', '')}"
    )


def set_graphics_mode(client, mode_hex):
    source = (
        "dosbox.wait_frames(10)\n"
        f"dosbox.type('setmode {mode_hex}')\n"
        "dosbox.key('KBD_enter', true)\n"
        "dosbox.key('KBD_enter', false)\n"
        "dosbox.wait_frames(60)\n"
    )
    assert client.script_load(source, name="gfx").status_code == 200
    assert client.script_start().status_code == 200
    data = client.wait_script_done(timeout=30)
    assert data["state"] == "completed", (
        f"Script did not complete: state={data['state']}, "
        f"error={data.get('error', '')}"
    )


def test_route_reads_text_screen(dosbox_e2e):
    inst = dosbox_e2e()
    client = inst.client
    settle_to_shell(client)
    echo_marker(client)

    r = client.screen_text()
    assert r.status_code == 200, f"screen_text route failed: {r.status_code}"
    body = r.json()

    assert body["is_text_mode"] is True
    assert body["bios_mode"] == 0x03
    assert body["columns"] == 80
    assert body["rows"] == 25
    assert body["page"] == 0
    assert MARKER in body["text"], (
        f"marker not found in screen text:\n{body['text']}"
    )
    # One newline-separated line per row.
    assert body["text"].count("\n") == body["rows"]


def test_route_reports_graphics_mode_as_non_text(dosbox_e2e):
    inst = dosbox_e2e(extra_sets=["machine=svga_s3"])
    client = inst.client
    settle_to_shell(client)
    set_graphics_mode(client, "13")  # VGA 320x200 256-color

    r = client.screen_text()
    assert r.status_code == 200, f"screen_text route failed: {r.status_code}"
    body = r.json()

    assert body["is_text_mode"] is False
    assert body["bios_mode"] == 0x13
    assert body["text"] == ""
