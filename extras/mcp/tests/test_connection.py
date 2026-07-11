# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import httpx
import mcp.types as types
import pytest

from dosbox_mcp.client import DosboxClient
from dosbox_mcp.config import Config
from dosbox_mcp.connection import Connection, NotConnected, guard


def _make_conn(token=None, features=None):
    config = Config(base_url="http://127.0.0.1:8386", token=token)
    conn = Connection(config)
    if features is not None:
        transport = httpx.MockTransport(
            lambda req: httpx.Response(200, json={"ok": True})
        )
        conn._client = DosboxClient(
            config.base_url, token or "0" * 64, transport=transport
        )
        conn._features = features
    return conn


def test_starts_disconnected():
    conn = _make_conn()
    assert not conn.connected
    assert conn.features == {}


def test_no_token_raises_not_connected():
    conn = _make_conn(token=None)
    with pytest.raises(NotConnected, match="No API token"):
        conn.ensure_connected()


def test_connected_with_features():
    conn = _make_conn(token="0" * 64, features={"memory": True, "freeze": True})
    assert conn.connected
    assert conn.features["memory"] is True


def test_detach_clears_state():
    conn = _make_conn(token="0" * 64, features={"memory": True})
    assert conn.connected
    conn.detach()
    assert not conn.connected
    assert conn.features == {}


def test_guard_returns_error_when_not_connected():
    conn = _make_conn()

    def handler(args):
        return [types.TextContent(type="text", text="ok")]

    guarded = guard(conn, handler)
    result = guarded({})
    assert len(result) == 1
    assert "No API token" in result[0].text


def test_guard_returns_error_when_feature_disabled():
    conn = _make_conn(token="0" * 64, features={"memory": True, "freeze": False})

    def handler(args):
        return [types.TextContent(type="text", text="ok")]

    guarded = guard(conn, handler, feature="freeze")
    result = guarded({})
    assert len(result) == 1
    assert "not enabled" in result[0].text


def test_guard_passes_through_when_feature_enabled():
    conn = _make_conn(token="0" * 64, features={"memory": True})

    def handler(args):
        return [types.TextContent(type="text", text="ok")]

    guarded = guard(conn, handler, feature="memory")
    result = guarded({})
    assert result[0].text == "ok"


def test_guard_passes_through_when_no_feature_gate():
    conn = _make_conn(token="0" * 64, features={})

    def handler(args):
        return [types.TextContent(type="text", text="ok")]

    guarded = guard(conn, handler, feature=None)
    result = guarded({})
    assert result[0].text == "ok"
