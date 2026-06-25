# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Documentation endpoint integration tests.

The OpenAPI spec, the landing page, the API explorer, and the vendored
Swagger UI assets are served without a bearer token so the docs are
browsable. Everything under /api/v1 still requires the token, and the
token file is never reachable through the doc carve-out.
"""

import json

import requests

# The server rejects any Host it does not recognise, so unauthenticated
# probes still have to present a valid Host header.
HEADERS = {"Host": "127.0.0.1"}


def _get(client, path):
    return requests.get(client.base_url + path, headers=HEADERS, timeout=10)


def test_openapi_spec_served_without_token(dosbox):
    resp = _get(dosbox, "/openapi.json")
    assert resp.status_code == 200
    spec = json.loads(resp.text)
    assert spec["openapi"] == "3.1.0"
    assert "/api/v1/status" in spec["paths"]


def test_landing_and_explorer_served_without_token(dosbox):
    for path in ("/", "/index.html", "/api.html"):
        resp = _get(dosbox, path)
        assert resp.status_code == 200, path


def test_swagger_assets_served_without_token(dosbox):
    for path in ("/swagger-ui.css", "/swagger-ui-bundle.js"):
        resp = _get(dosbox, path)
        assert resp.status_code == 200, path


def test_api_still_requires_token(dosbox):
    resp = _get(dosbox, "/api/v1/status")
    assert resp.status_code == 401


def test_api_works_with_token(dosbox):
    # Sanity: the same endpoint succeeds once the fixture's token is sent.
    assert dosbox.status().status_code == 200


def test_token_file_not_exposed_via_docs(dosbox):
    # Not on the allowlist, so the bearer check applies and rejects it.
    resp = _get(dosbox, "/api_token")
    assert resp.status_code == 401
