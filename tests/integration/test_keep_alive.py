# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""The server closes each connection after its response, so no client can
reuse a socket in the instant httplib drops it as idle (5 s)."""

import http.client
import time
from urllib.parse import urlsplit

import pytest
import requests
from dosbox_client import DosboxClient

# Past CPPHTTPLIB_KEEPALIVE_TIMEOUT_SECOND (5 s, polled every 10 ms).
IDLE_PAST_TIMEOUT_S = 5.1


def _auth(dosbox: DosboxClient) -> dict[str, str]:
    return {"Authorization": dosbox.session.headers["Authorization"],
            "Host": "127.0.0.1"}


def test_server_answers_connection_close(dosbox):
    # The request itself does not ask for Connection: close.
    r = requests.get(f"{dosbox.base_url}/api/v1/status",
                     headers=_auth(dosbox), timeout=10)
    assert r.status_code == 200
    assert r.headers.get("Connection", "").lower() == "close", \
        f"server kept the connection open: {dict(r.headers)}"
    assert "Keep-Alive" not in r.headers


def test_client_reusing_its_connection_after_idle_is_answered(dosbox):
    url = urlsplit(dosbox.base_url)
    conn = http.client.HTTPConnection(url.hostname, url.port, timeout=10)
    try:
        conn.request("GET", "/api/v1/status", headers=_auth(dosbox))
        conn.getresponse().read()
        time.sleep(IDLE_PAST_TIMEOUT_S)
        try:
            conn.request("GET", "/api/v1/status", headers=_auth(dosbox))
            status = conn.getresponse().status
        except (http.client.RemoteDisconnected, ConnectionError) as e:
            pytest.fail(f"request on the idle connection got no answer: {e!r}")
        assert status == 200
    finally:
        conn.close()
