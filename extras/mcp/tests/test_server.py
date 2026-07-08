# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import httpx

from dosbox_mcp.client import DosboxClient
from dosbox_mcp.server import build_server


def _make_client():
    def handler(request):
        return httpx.Response(200, json={"ok": True})
    return DosboxClient("http://127.0.0.1:8386", "0" * 64,
                        transport=httpx.MockTransport(handler))


def test_always_on_tools_present():
    features = {}
    server = build_server(_make_client(), features)
    names = server.registered_tool_names()
    assert "dosbox_status" in names
    assert "screen_text" in names
    assert "script_run" in names
    assert "video_capture_status" in names


def test_memory_tools_present_when_flag_true():
    features = {"memory": True, "input": True}
    server = build_server(_make_client(), features)
    names = server.registered_tool_names()
    assert "mem_read" in names
    assert "mem_write" in names
    assert "input_type" in names
    assert "input_key" in names


def test_memory_tools_absent_when_flag_false():
    features = {"memory": False, "input": True}
    server = build_server(_make_client(), features)
    names = server.registered_tool_names()
    assert "mem_read" not in names
    assert "input_type" in names


def test_input_tools_absent_when_flag_false():
    features = {"memory": True, "input": False}
    server = build_server(_make_client(), features)
    names = server.registered_tool_names()
    assert "mem_read" in names
    assert "input_type" not in names
    assert "input_key" not in names


def test_search_and_map_present_with_memory():
    features = {"memory": True}
    server = build_server(_make_client(), features)
    names = server.registered_tool_names()
    assert "mem_search" in names
    assert "dos_memory_map" in names


def test_freeze_tools_present_when_flag_true():
    features = {"freeze": True}
    server = build_server(_make_client(), features)
    names = server.registered_tool_names()
    assert "freeze_set" in names
    assert "freeze_list" in names
    assert "freeze_clear" in names


def test_freeze_tools_absent_when_flag_false():
    features = {"freeze": False, "memory": True}
    server = build_server(_make_client(), features)
    names = server.registered_tool_names()
    assert "freeze_set" not in names
    assert "mem_read" in names


def test_port_io_tools_present_when_flag_true():
    features = {"port_io": True}
    server = build_server(_make_client(), features)
    names = server.registered_tool_names()
    assert "port_read" in names
    assert "port_write" in names


def test_port_io_tools_absent_when_flag_false():
    features = {"port_io": False}
    server = build_server(_make_client(), features)
    names = server.registered_tool_names()
    assert "port_read" not in names
