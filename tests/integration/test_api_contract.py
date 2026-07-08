"""Contract tests for the dosbox-automation webserver API.

Every endpoint is exercised against a live headless DOSBox instance.
These tests define the API contract — any failure after a rebase
indicates cherry-pick damage.
"""

import base64
import json
import struct
import time
from io import BytesIO

import pytest
from PIL import Image


# ---------------------------------------------------------------------------
# Status & info endpoints
# ---------------------------------------------------------------------------

def test_dosbox_is_running(dosbox):
    r = dosbox.status()
    assert r.status_code == 200
    data = r.json()
    assert data["running"] is True


def test_status_shape(dosbox):
    r = dosbox.status()
    assert r.status_code == 200
    data = r.json()
    assert "running" in data
    assert "shutdown_requested" in data
    assert "is_booted" in data
    assert "program" in data
    assert "canonical_name" in data
    assert "is_shell" in data
    assert data["shutdown_requested"] is False
    assert data["is_shell"] is True


def test_program_state_at_shell(dosbox):
    r = dosbox.program_state()
    assert r.status_code == 200
    data = r.json()
    assert "segment_name" in data
    assert "canonical_name" in data
    assert "is_shell" in data
    assert "is_booted" in data
    assert data["is_shell"] is True


def test_dosbox_info(dosbox):
    r = dosbox.dosbox_info()
    assert r.status_code == 200
    data = r.json()
    assert "version" in data
    assert len(data["version"]) > 0
    assert "configHome" not in data
    assert "configWebserver" not in data


def test_dosbox_info_reports_features(dosbox):
    r = dosbox.dosbox_info()
    assert r.status_code == 200
    data = r.json()
    assert "features" in data
    features = data["features"]
    for key in ("memory", "input", "cpu_registers", "port_io", "freeze"):
        assert features.get(key) is True, f"{key} should be true"
    assert features["cpu_control"] is False
    assert features["debugger"] is False


# ---------------------------------------------------------------------------
# CPU & DOS internals
# ---------------------------------------------------------------------------

def test_cpu_state(dosbox):
    r = dosbox.cpu_state()
    assert r.status_code == 200
    data = r.json()
    regs = data["registers"]
    for name in ("eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp",
                 "eip", "flags", "cs", "ds", "es", "ss", "fs", "gs"):
        assert name in regs, f"Missing register: {name}"
        assert isinstance(regs[name], int)


def test_dos_internals(dosbox):
    r = dosbox.dos_internals()
    assert r.status_code == 200
    data = r.json()
    assert "listOfLists" in data
    assert "dosSwappableArea" in data
    assert "firstShell" in data
    assert data["listOfLists"] > 0
    assert data["dosSwappableArea"] > 0
    assert data["firstShell"] > 0


# ---------------------------------------------------------------------------
# Input injection — valid inputs
# ---------------------------------------------------------------------------

def test_input_keypress(dosbox):
    r = dosbox.press_key("KBD_a")
    assert r.status_code == 200
    data = r.json()
    assert data["status"] == "ok"
    assert data["events_scheduled"] == 1


def test_input_empty_events(dosbox):
    r = dosbox.input_sequence([])
    assert r.status_code == 200
    assert r.json()["events_scheduled"] == 0


def test_input_mouse_move(dosbox):
    r = dosbox.input_sequence([{
        "type": "mouse_move", "x_rel": 5.0, "y_rel": -3.0,
    }])
    assert r.status_code == 200
    assert r.json()["status"] == "ok"


def test_input_mouse_button(dosbox):
    r = dosbox.input_sequence([{
        "type": "mouse_button", "button": "left", "pressed": True,
    }])
    assert r.status_code == 200


def test_input_mouse_wheel(dosbox):
    r = dosbox.input_sequence([{
        "type": "mouse_wheel", "delta": 1.0,
    }])
    assert r.status_code == 200


def test_input_timed_events(dosbox):
    r = dosbox.input_sequence([
        {"t": 0, "type": "key", "key": "KBD_h", "pressed": True},
        {"t": 50, "type": "key", "key": "KBD_h", "pressed": False},
        {"t": 100, "type": "key", "key": "KBD_i", "pressed": True},
        {"t": 150, "type": "key", "key": "KBD_i", "pressed": False},
    ])
    assert r.status_code == 200
    assert r.json()["events_scheduled"] == 4


# ---------------------------------------------------------------------------
# Input injection — validation errors
# ---------------------------------------------------------------------------

def test_input_missing_events_key(dosbox):
    r = dosbox.input_sequence_raw(json.dumps({"wrong": []}))
    assert r.status_code == 400
    assert "events" in r.json()["error"].lower()


def test_input_events_not_array(dosbox):
    r = dosbox.input_sequence_raw(json.dumps({"events": "not-array"}))
    assert r.status_code == 400


def test_input_unknown_key(dosbox):
    r = dosbox.input_sequence([{"type": "key", "key": "KBD_NONEXISTENT"}])
    assert r.status_code == 400
    assert "KBD_NONEXISTENT" in r.json()["error"]


def test_input_unknown_event_type(dosbox):
    r = dosbox.input_sequence([{"type": "teleport"}])
    assert r.status_code == 400
    assert "teleport" in r.json()["error"]


def test_input_unknown_button(dosbox):
    r = dosbox.input_sequence([{"type": "mouse_button", "button": "extra"}])
    assert r.status_code == 400
    assert "extra" in r.json()["error"]


# ---------------------------------------------------------------------------
# Recording lifecycle
# ---------------------------------------------------------------------------

def test_recording_lifecycle(dosbox):
    # Start
    r = dosbox.recording_start()
    assert r.status_code == 200
    assert r.json()["status"] == "recording"

    # Status while recording
    r = dosbox.recording_status()
    assert r.status_code == 200
    assert r.json()["recording"] is True
    assert r.json()["paused"] is False

    # Pause
    r = dosbox.recording_pause()
    assert r.status_code == 200
    assert r.json()["status"] == "paused"

    # Status while paused
    r = dosbox.recording_status()
    assert r.json()["paused"] is True
    assert r.json()["recording"] is True

    # Resume
    r = dosbox.recording_pause()
    assert r.status_code == 200
    assert r.json()["status"] == "recording"

    # Stop (empty)
    r = dosbox.recording_stop()
    assert r.status_code == 200
    data = r.json()
    assert "event_count" in data
    assert "duration_ms" in data
    assert "events" in data
    assert isinstance(data["events"], list)


def test_recording_error_stop_when_not_recording(dosbox):
    r = dosbox.recording_stop()
    assert r.status_code == 409
    assert "not recording" in r.json()["error"].lower()


def test_recording_error_pause_when_not_recording(dosbox):
    r = dosbox.recording_pause()
    assert r.status_code == 409
    assert "not recording" in r.json()["error"].lower()


def test_recording_error_start_when_already_recording(dosbox):
    dosbox.recording_start()
    try:
        r = dosbox.recording_start()
        assert r.status_code == 409
        assert "already" in r.json()["error"].lower()
    finally:
        dosbox.recording_stop()


def test_recording_round_trip(dosbox):
    """Start recording, inject keys, stop, verify the lifecycle works.

    API-injected keys are excluded from recording by design (the
    in_replay_dispatch flag prevents a replay from re-recording
    itself). So this test verifies the recording lifecycle and
    data shape, not that injected keys appear in the capture.
    """
    dosbox.recording_start()
    time.sleep(0.3)

    dosbox.press_key("KBD_a", pressed=True)
    time.sleep(0.1)
    dosbox.press_key("KBD_a", pressed=False)
    time.sleep(0.3)

    r = dosbox.recording_stop()
    assert r.status_code == 200
    data = r.json()
    assert "event_count" in data
    assert "duration_ms" in data
    assert "events" in data
    assert isinstance(data["events"], list)
    assert data["duration_ms"] >= 0


# ---------------------------------------------------------------------------
# Video frame capture
# ---------------------------------------------------------------------------

def test_frame_jpeg(dosbox):
    r = dosbox.frame(fmt="jpeg")
    assert r.status_code == 200
    assert "image/jpeg" in r.headers.get("Content-Type", "")
    img = Image.open(BytesIO(r.content))
    assert img.width > 0
    assert img.height > 0
    assert img.mode == "RGB"


def test_frame_png(dosbox):
    r = dosbox.frame(fmt="png")
    assert r.status_code == 200
    assert "image/png" in r.headers.get("Content-Type", "")
    assert r.content[:4] == b"\x89PNG"
    img = Image.open(BytesIO(r.content))
    assert img.width > 0
    assert img.height > 0


def test_frame_raw(dosbox):
    r = dosbox.frame(fmt="raw")
    assert r.status_code == 200
    assert "application/octet-stream" in r.headers.get("Content-Type", "")
    header = struct.unpack_from("<IIiB H", r.content)
    width, height, pitch, pf, pal_count = header
    assert width > 0
    assert height > 0
    assert abs(pitch) >= width


def test_frame_info_shape(dosbox):
    r = dosbox.frame_info()
    assert r.status_code == 200
    data = r.json()
    assert "width" in data
    assert "height" in data
    assert "pixel_format" in data
    assert "pitch" in data
    assert "is_paletted" in data
    assert data["width"] > 0
    assert data["height"] > 0


def test_frame_quality_parameter(dosbox):
    r_low = dosbox.frame(fmt="jpeg", quality=10)
    r_high = dosbox.frame(fmt="jpeg", quality=98)
    assert r_low.status_code == 200
    assert r_high.status_code == 200
    assert len(r_low.content) < len(r_high.content)


def test_frame_accept_header_png(dosbox):
    r = dosbox.session.get(
        dosbox._url("/api/v1/video/frame"),
        headers={"Accept": "image/png", "Host": "127.0.0.1"},
        timeout=dosbox.timeout,
    )
    assert r.status_code == 200
    assert "image/png" in r.headers.get("Content-Type", "")


def test_frame_capture_to_file(dosbox, tmp_path):
    path = dosbox.capture_frame(tmp_path / "shot.jpg")
    assert path.exists()
    assert path.stat().st_size > 1000
    img = Image.open(path)
    assert img.width == 720
    assert img.height == 400


# ---------------------------------------------------------------------------
# Drive swap validation
# ---------------------------------------------------------------------------

def test_drive_swap_missing_drive(dosbox):
    r = dosbox.drive_swap_raw(json.dumps({"image": "/tmp/fake.img"}))
    assert r.status_code == 400


def test_drive_swap_missing_image(dosbox):
    r = dosbox.drive_swap_raw(json.dumps({"drive": "A"}))
    assert r.status_code == 400


def test_drive_swap_invalid_drive_letter(dosbox):
    r = dosbox.drive_swap("1", "/tmp/fake.img")
    assert r.status_code == 400


def test_drive_swap_nonexistent_file(dosbox):
    r = dosbox.drive_swap("A", "/tmp/does-not-exist-ever.img")
    assert r.status_code == 400


# ---------------------------------------------------------------------------
# Memory operations
# ---------------------------------------------------------------------------

def test_memory_read_binary(dosbox):
    r = dosbox.memory_read(0, 16)
    assert r.status_code == 200
    assert len(r.content) == 16


def test_memory_read_json(dosbox):
    r = dosbox.memory_read_json(0, 16)
    assert r.status_code == 200
    data = r.json()
    assert "registers" in data
    assert "memory" in data
    assert "addr" in data["memory"]
    assert "data" in data["memory"]
    decoded = base64.b64decode(data["memory"]["data"])
    assert len(decoded) == 16


def test_memory_allocate_and_free(dosbox):
    r = dosbox.memory_allocate(256, area="CONV")
    assert r.status_code == 200
    data = r.json()
    assert "addr" in data
    assert data["addr"] > 0

    r = dosbox.memory_free(data["addr"])
    assert r.status_code == 200


def test_memory_allocate_invalid_area(dosbox):
    r = dosbox.session.post(
        dosbox._url("/api/v1/memory/allocate"),
        json={"size": 256, "area": "FAKE"},
        timeout=dosbox.timeout,
    )
    assert r.status_code == 400


def test_memory_allocate_xms_non_bestfit(dosbox):
    r = dosbox.session.post(
        dosbox._url("/api/v1/memory/allocate"),
        json={"size": 256, "area": "XMS", "strategy": "FIRST_FIT"},
        timeout=dosbox.timeout,
    )
    assert r.status_code == 400


# ---------------------------------------------------------------------------
# Host validation
# ---------------------------------------------------------------------------

def test_host_validation_rejects_bad_host(dosbox):
    r = dosbox.get_with_host("/api/v1/status", "evil.example.com")
    assert r.status_code == 403


def test_host_validation_accepts_localhost(dosbox):
    r = dosbox.get_with_host("/api/v1/status", "localhost")
    assert r.status_code == 200


# ---------------------------------------------------------------------------
# Token authentication
# ---------------------------------------------------------------------------

def test_token_auth_rejects_no_token(dosbox):
    r = dosbox.get_without_token("/api/v1/status")
    assert r.status_code == 401


def test_token_auth_rejects_wrong_token(dosbox):
    import requests as req
    r = req.get(
        dosbox._url("/api/v1/status"),
        headers={
            "Host": "127.0.0.1",
            "Authorization": "Bearer 0000000000000000000000000000000000000000000000000000000000000000",
        },
        timeout=dosbox.timeout,
    )
    assert r.status_code == 401


def test_security_headers_present(dosbox):
    r = dosbox.status()
    assert r.status_code == 200
    assert r.headers.get("X-Content-Type-Options") == "nosniff"


def test_options_preflight_rejected(dosbox):
    import requests as req
    r = req.options(
        dosbox._url("/api/v1/status"),
        headers={
            "Host": "127.0.0.1",
            "Authorization": f"Bearer {dosbox.session.headers.get('Authorization', '').replace('Bearer ', '')}",
        },
        timeout=dosbox.timeout,
    )
    assert r.status_code == 403


def test_event_array_size_cap(dosbox):
    giant = [{"type": "key", "key": "KBD_a", "pressed": True}] * 32001
    r = dosbox.input_sequence(giant)
    assert r.status_code == 400
    assert "max" in r.json()["error"].lower()


def test_event_array_at_limit(dosbox):
    events = [{"type": "key", "key": "KBD_a", "pressed": True}] * 32000
    r = dosbox.input_sequence(events)
    assert r.status_code == 200


def test_drive_swap_no_path_leak(dosbox):
    r = dosbox.drive_swap("A", "/tmp/does-not-exist-ever.img")
    assert r.status_code == 400
    assert "/tmp" not in r.json().get("image", "")
