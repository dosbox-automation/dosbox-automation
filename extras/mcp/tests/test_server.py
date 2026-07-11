# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

from dosbox_mcp.connection import Connection
from dosbox_mcp.config import Config
from dosbox_mcp.server import build_server


def _make_conn():
    config = Config(base_url="http://127.0.0.1:8386", token=None)
    return Connection(config)


def _build():
    return build_server(_make_conn())


def test_always_on_tools_present():
    server = _build()
    names = server.registered_tool_names()
    assert "dosbox_status" in names
    assert "screen_text" in names
    assert "script_run" in names
    assert "video_capture_status" in names


def test_all_tools_registered_regardless_of_features():
    server = _build()
    names = server.registered_tool_names()
    assert "mem_read" in names
    assert "mem_write" in names
    assert "input_type" in names
    assert "input_key" in names
    assert "mem_search" in names
    assert "dos_memory_map" in names
    assert "freeze_set" in names
    assert "freeze_list" in names
    assert "freeze_clear" in names
    assert "port_read" in names
    assert "port_write" in names
    assert "cpu_write_register" in names
    for t in ("debug_status", "debug_pause", "debug_continue", "debug_step"):
        assert t in names


# ---------------------------------------------------------------------------
# Tool handlers must build the right REST calls. The registration tests
# above cannot catch a wrong route or missing Accept header; these do
# (aug-df86: mem_read hit the wrong route and got binary back).
# ---------------------------------------------------------------------------

from dosbox_mcp.tools.memory import _mem_read, _mem_write


class _FakeClient:
    def __init__(self):
        self.calls = []

    def get(self, path, params=None, headers=None):
        self.calls.append(("get", path, params, headers))
        return {"memory": {"data": "3q2+7w==", "addr": 4660}, "registers": {}}

    def put(self, path, json=None):
        self.calls.append(("put", path, json))
        return {"status": "ok"}


def test_mem_read_uses_linear_offset_route_and_json_accept():
    client = _FakeClient()
    result = _mem_read(client, {"offset": 0x1234, "length": 100})

    method, path, params, headers = client.calls[0]
    assert method == "get"
    # Linear offset and length in the path, nothing split into segments
    assert path == "/api/v1/memory/4660/100"
    # JSON (base64 payload) is selected by the Accept header
    assert headers == {"accept": "application/json"}
    # The base64 data must survive into the tool output
    assert "3q2+7w==" in result[0].text


def test_mem_write_uses_single_offset_route():
    client = _FakeClient()
    _mem_write(client, {"offset": 0x1234, "data": "AAECAw=="})

    method, path, body = client.calls[0]
    assert method == "put"
    assert path == "/api/v1/memory/4660"
    assert body == {"data": "AAECAw=="}


def test_session_info_registered():
    server = _build()
    assert "session_info" in server.registered_tool_names()


def test_session_info_returns_base_url_and_token(monkeypatch, tmp_path):
    from dosbox_mcp.tools.session import _session_info

    monkeypatch.setenv("DOSBOX_API_TOKEN", "a" * 64)

    class _FakeConn:
        base_url = "http://127.0.0.1:8386"

    result = _session_info(_FakeConn())
    import json as _json
    info = _json.loads(result[0].text)
    assert info["base_url"] == "http://127.0.0.1:8386"
    assert info["token"] == "a" * 64
    assert "Bearer" in info["example"]


def test_session_info_without_token(monkeypatch, tmp_path):
    from dosbox_mcp.tools.session import _session_info

    monkeypatch.delenv("DOSBOX_API_TOKEN", raising=False)
    # Point the token file lookup at an empty directory
    monkeypatch.setenv("DOSBOX_TOKEN_FILE", str(tmp_path / "no_token"))

    class _FakeConn:
        base_url = "http://127.0.0.1:8386"

    result = _session_info(_FakeConn())
    import json as _json
    info = _json.loads(result[0].text)
    assert info["token"] is None
    assert "note" in info


def test_script_run_sends_lua_as_text_not_json():
    # aug-bt7n: script/load wants a text/plain body; the old JSON post
    # 415'd. Verify the handler uses post_text with the raw source.
    from dosbox_mcp.tools.script import _script_run

    class _FakeClient:
        def __init__(self):
            self.text_calls = []
            self.json_calls = []

        def post_text(self, path, text, content_type="text/plain", params=None):
            self.text_calls.append((path, text, content_type))
            return {"status": "loaded"}

        def post(self, path, json=None):
            self.json_calls.append((path, json))
            return {"status": "running"}

    client = _FakeClient()
    _script_run(client, {"script": "dosbox.log('hi')"})

    # The script goes through post_text as raw source, not post(json=)
    assert len(client.text_calls) == 1
    path, text, ctype = client.text_calls[0]
    assert path == "/api/v1/script/load"
    assert text == "dosbox.log('hi')"
    assert ctype == "text/plain"
    # start is a plain POST with no body
    assert client.json_calls == [("/api/v1/script/start", None)]
