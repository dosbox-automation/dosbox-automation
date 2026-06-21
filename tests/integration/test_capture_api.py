# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Integration tests for the ZMBV video capture REST endpoints."""

import time

import pytest


def test_capture_status_when_idle(dosbox):
    r = dosbox.capture_status()
    assert r.status_code == 200
    assert r.json()["capturing"] is False


def test_capture_start_stop(dosbox):
    r = dosbox.capture_start()
    assert r.status_code == 200

    r = dosbox.capture_status()
    assert r.status_code == 200
    assert r.json()["capturing"] is True

    r = dosbox.capture_stop()
    assert r.status_code == 200

    r = dosbox.capture_status()
    assert r.status_code == 200
    assert r.json()["capturing"] is False


def test_capture_double_start(dosbox):
    dosbox.capture_start()
    r = dosbox.capture_start()
    # Should not fail, just keep recording.
    assert r.status_code == 200

    dosbox.capture_stop()


def test_capture_stop_when_not_recording(dosbox):
    r = dosbox.capture_stop()
    assert r.status_code == 200


def test_capture_from_lua(dosbox):
    time.sleep(2.1)  # rate limiter
    r = dosbox.script_load(
        "dosbox.capture_start()\n"
        "dosbox.wait_frames(10)\n"
        "dosbox.capture_stop()\n"
        "dosbox.output.done = 'yes'\n",
        name="capture-lua",
    )
    assert r.status_code == 200

    dosbox.script_start()
    data = dosbox.wait_script_done()
    assert data["state"] == "completed"
    assert data["output"]["done"] == "yes"
