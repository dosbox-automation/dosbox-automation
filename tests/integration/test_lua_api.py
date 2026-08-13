# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Integration tests for the Lua scripting REST endpoints."""

import time

import pytest


def test_script_load_valid(dosbox):
    r = dosbox.script_load("dosbox.log('hello')", name="test-load")
    assert r.status_code == 200
    data = r.json()
    assert data["status"] == "loaded"
    assert data["name"] == "test-load"


def test_script_load_empty_body(dosbox):
    time.sleep(2.1)
    r = dosbox.script_load("")
    assert r.status_code == 400


def test_script_load_invalid_content_type(dosbox):
    time.sleep(2.1)
    r = dosbox._post(
        "/api/v1/script/load",
        data="print('hi')",
        headers={"Content-Type": "application/octet-stream"},
    )
    assert r.status_code == 415


def test_script_start_after_completion(dosbox):
    # After a script completes, starting again without a fresh load
    # should re-run the already-loaded script (not error).
    # This tests the contract: "start" on a completed script state.
    time.sleep(2.1)
    dosbox.script_load("dosbox.output.x = 1", name="rerun")
    dosbox.script_start()
    data = dosbox.wait_script_done(timeout=5)
    assert data["state"] == "completed"


def test_script_lifecycle(dosbox):
    time.sleep(2.1)  # rate limiter cooldown
    r = dosbox.script_load(
        "dosbox.output.result = 'done'\n",
        name="lifecycle-test",
    )
    assert r.status_code == 200

    r = dosbox.script_start()
    assert r.status_code == 200

    data = dosbox.wait_script_done(timeout=10)
    assert data["state"] == "completed"
    assert data["output"]["result"] == "done"


def test_script_stop_while_running(dosbox):
    time.sleep(2.1)
    r = dosbox.script_load(
        "while true do dosbox.wait_frames(1) end",
        name="infinite",
    )
    assert r.status_code == 200
    dosbox.script_start()
    time.sleep(0.5)

    r = dosbox.script_stop()
    assert r.status_code == 200


def test_script_status_idle(dosbox):
    # After stop, state should be idle.
    time.sleep(0.3)
    r = dosbox.script_status()
    assert r.status_code == 200
    assert r.json()["state"] == "idle"


def test_script_seed_determinism(dosbox):
    results = []
    for i in range(2):
        time.sleep(2.1)
        dosbox.script_load(
            "dosbox.output.val = tostring(math.random(1, 10000))\n",
            name=f"seed-test-{i}",
            seed=42,
        )
        dosbox.script_start()
        data = dosbox.wait_script_done()
        results.append(data["output"]["val"])
    assert results[0] == results[1]


def test_script_error_reported(dosbox):
    time.sleep(2.1)
    dosbox.script_load("error('intentional')", name="err-test")
    dosbox.script_start()
    data = dosbox.wait_script_done()
    assert data["state"] == "error"
    assert "intentional" in data.get("error", "")


def test_script_double_start_rejected(dosbox):
    time.sleep(2.1)
    dosbox.script_load(
        "while true do dosbox.wait_frames(1) end",
        name="double-start",
    )
    dosbox.script_start()
    time.sleep(0.3)

    r = dosbox.script_start()
    assert r.status_code == 400
    assert "already" in r.json()["error"].lower()

    dosbox.script_stop()
    time.sleep(0.3)


def test_script_rate_limit(dosbox):
    # Wait for any prior cooldown to expire, then do two loads in
    # quick succession. The second one must be rejected.
    time.sleep(2.5)

    r1 = dosbox.script_load("dosbox.log('a')", name="rate-a")
    assert r1.status_code == 200

    # Immediate second load should be rate-limited.
    r2 = dosbox.script_load("dosbox.log('b')", name="rate-b")
    assert r2.status_code == 429
    assert "Retry-After" in r2.headers
