# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import httpx
import pytest

from dosbox_mcp.client import DosboxClient


def make_client(handler):
    transport = httpx.MockTransport(handler)
    return DosboxClient(
        base_url="http://127.0.0.1:8386",
        token="0" * 64,
        transport=transport,
    )


def test_get_sends_bearer_token():
    seen = {}

    def handler(request):
        seen["auth"] = request.headers.get("authorization")
        seen["client"] = request.headers.get("x-client")
        return httpx.Response(200, json={"ok": True})

    client = make_client(handler)
    body = client.get("/api/v1/status")
    assert body == {"ok": True}
    assert seen["auth"] == "Bearer " + "0" * 64
    assert seen["client"] == "mcp"


def test_post_sends_json_body():
    seen = {}

    def handler(request):
        seen["body"] = request.content
        seen["ctype"] = request.headers.get("content-type")
        return httpx.Response(200, json={"status": "ok"})

    client = make_client(handler)
    result = client.post("/api/v1/input/type", json={"text": "hi"})
    assert result == {"status": "ok"}
    assert b'"text"' in seen["body"]


def test_error_status_raises():
    def handler(request):
        return httpx.Response(400, json={"error": "bad"})

    client = make_client(handler)
    with pytest.raises(RuntimeError, match="bad"):
        client.get("/api/v1/status")


def test_binary_response_returned_as_bytes():
    def handler(request):
        return httpx.Response(
            200,
            content=b"\x89PNG",
            headers={"content-type": "image/png"},
        )

    client = make_client(handler)
    data = client.get("/api/v1/video/frame")
    assert data == b"\x89PNG"
