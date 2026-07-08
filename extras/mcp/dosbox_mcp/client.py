# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import httpx


class DosboxClient:
    """Thin authenticated HTTP wrapper. Every request carries the bearer
    token and the X-Client: mcp header that drives the OSD activity signal."""

    def __init__(self, base_url, token, transport=None):
        self._base = base_url.rstrip("/")
        self._client = httpx.Client(
            headers={
                "Authorization": f"Bearer {token}",
                "X-Client": "mcp",
            },
            transport=transport,
            timeout=30.0,
        )

    def _handle(self, resp):
        if resp.status_code >= 400:
            try:
                msg = resp.json().get("error", resp.text)
            except Exception:
                msg = resp.text
            raise RuntimeError(f"{resp.status_code}: {msg}")
        ctype = resp.headers.get("content-type", "")
        if ctype.startswith("application/json"):
            return resp.json()
        return resp.content

    def get(self, path, params=None):
        return self._handle(
            self._client.get(self._base + path, params=params)
        )

    def post(self, path, json=None):
        return self._handle(
            self._client.post(self._base + path, json=json)
        )

    def put(self, path, json=None):
        return self._handle(
            self._client.put(self._base + path, json=json)
        )

    def delete(self, path, json=None):
        return self._handle(
            self._client.request("DELETE", self._base + path, json=json)
        )
