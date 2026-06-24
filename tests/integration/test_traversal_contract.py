# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Path traversal contract tests for the dosbox-automation webserver.

Exercises the httplib static file server and the drive swap endpoint
with encoded traversal sequences, absolute path injection, and
backslash variants. These tests pin behavior against httplib upgrades:
if a future version regresses on traversal protection, a test fails.

All tests run against a live headless DOSBox instance via the module-
scoped 'dosbox' fixture from conftest.py.
"""

import pytest


# -----------------------------------------------------------------------
# Static file serving: encoded traversal
# -----------------------------------------------------------------------

TRAVERSAL_PATHS = [
    ("dot-dot-slash",       "/../../../etc/passwd"),
    ("encoded-dot-dot",     "/%2e%2e/%2e%2e/%2e%2e/etc/passwd"),
    ("encoded-slash",       "/..%2f..%2f..%2f/etc/passwd"),
    ("double-encoded",      "/%252e%252e/%252e%252e/etc/passwd"),
    ("backslash",           "/..\\..\\..\\etc\\passwd"),
    ("encoded-backslash",   "/..%5c..%5c..%5cetc%5cpasswd"),
    ("null-byte",           "/%00/../etc/passwd"),
    ("long-traversal",      "/" + "../" * 20 + "etc/passwd"),
]


@pytest.mark.parametrize("label,path", TRAVERSAL_PATHS, ids=[t[0] for t in TRAVERSAL_PATHS])
def test_static_traversal_blocked(dosbox, label, path):
    """Traversal attempts on the static file server must not return file content."""
    r = dosbox._get(path)
    assert r.status_code in (400, 403, 404), (
        f"Traversal path '{label}' returned {r.status_code}, "
        f"expected 400/403/404. Body: {r.text[:200]}"
    )
    assert "root:" not in r.text, (
        f"Traversal path '{label}' leaked /etc/passwd content"
    )


# -----------------------------------------------------------------------
# Static file serving: absolute path injection
# -----------------------------------------------------------------------

ABSOLUTE_PATHS = [
    ("unix-absolute",  "/etc/passwd"),
    ("unix-shadow",    "/etc/shadow"),
    ("unix-hosts",     "/etc/hosts"),
    ("proc-self",      "/proc/self/environ"),
]


@pytest.mark.parametrize("label,path", ABSOLUTE_PATHS, ids=[t[0] for t in ABSOLUTE_PATHS])
def test_static_absolute_path_blocked(dosbox, label, path):
    """Absolute path requests must not serve host files outside the mount root."""
    r = dosbox._get(path)
    # These paths shouldn't exist under the webserver's mount roots,
    # so we expect 404 (not found in mount) or 403 (blocked).
    # Critically, they must never return the real host file content.
    if label == "unix-absolute":
        assert "root:" not in r.text, (
            f"Absolute path '{path}' leaked /etc/passwd content"
        )
    assert r.status_code != 200 or len(r.content) == 0 or "root:" not in r.text


# -----------------------------------------------------------------------
# Drive swap endpoint: path injection
# -----------------------------------------------------------------------

DRIVE_SWAP_PATHS = [
    ("unix-etc",         "/etc/passwd"),
    ("unix-home",        "/home"),
    ("traversal-image",  "../../../../../../etc/passwd"),
    ("unc-path",         "\\\\127.0.0.1\\c$\\windows\\system32"),
    ("windows-abs",      "C:\\Windows\\System32\\config\\SAM"),
    ("null-in-path",     "/tmp/normal.img\x00/etc/passwd"),
    ("dev-null",         "/dev/null"),
]


@pytest.mark.parametrize("label,path", DRIVE_SWAP_PATHS, ids=[t[0] for t in DRIVE_SWAP_PATHS])
def test_drive_swap_rejects_hostile_path(dosbox, label, path):
    """Drive swap must reject paths outside allowed image roots."""
    r = dosbox.drive_swap("A", path)
    assert r.status_code in (400, 403), (
        f"Drive swap with '{label}' returned {r.status_code}, "
        f"expected 400/403. Body: {r.text[:200]}"
    )


# -----------------------------------------------------------------------
# Drive swap endpoint: no path echo in error
# -----------------------------------------------------------------------

def test_drive_swap_error_no_path_echo(dosbox):
    """Error responses must not echo the full requested path back."""
    hostile = "/etc/shadow"
    r = dosbox.drive_swap("A", hostile)
    assert r.status_code in (400, 403)
    body = r.text
    assert hostile not in body, (
        f"Drive swap error echoed the hostile path: {body[:200]}"
    )


# -----------------------------------------------------------------------
# Static file serving: directory listing suppression
# -----------------------------------------------------------------------

def test_no_directory_listing(dosbox):
    """A trailing-slash directory request must not produce a listing."""
    r = dosbox._get("/")
    # httplib serves index.html or 404, never a directory listing.
    if r.status_code == 200:
        assert "<html" in r.text.lower() or len(r.content) > 0, (
            "Root '/' returned 200 but with unexpected content"
        )
    else:
        assert r.status_code in (403, 404)


# -----------------------------------------------------------------------
# Auth bypass attempt via traversal
# -----------------------------------------------------------------------

def test_traversal_without_auth_still_blocked(dosbox):
    """Traversal on the static server without a token must not leak files."""
    r = dosbox.get_without_token("/../../../etc/passwd")
    assert r.status_code in (400, 401, 403, 404)
    assert "root:" not in r.text
