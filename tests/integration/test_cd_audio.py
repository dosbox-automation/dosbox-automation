# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Integration tests for the CD audio decoder paths.

Each test mounts a disc variant (master BIN/CUE, WAV, MP3, FLAC, OGG),
plays audio track 2 via CDPLAY.EXE on the data track, captures audio
through the REST API, and validates non-silence. Formats whose encoder
tools are missing on the host get skipped.
"""

import secrets
import time
from pathlib import Path

import pytest

from cd_audio_helpers import prepare_test_disc, validate_audio
from conftest import WORKSPACE, start_dosbox_instance

FIXTURES_DIR = (Path(__file__).resolve().parent.parent
                / "files" / "audiocd")
CACHE_DIR = (Path(__file__).resolve().parent.parent.parent
             / ".workspace" / "cd-audio-cache")


@pytest.fixture(scope="module")
def cd_variants():
    return prepare_test_disc(FIXTURES_DIR, CACHE_DIR)


def _run_decoder_test(variant_name: str, cue_path: Path) -> None:
    work_dir = WORKSPACE / f"cd-audio-{variant_name}-{secrets.token_hex(4)}"
    disc_dir = cue_path.parent

    instance = start_dosbox_instance(
        work_dir,
        autoexec_lines=[
            f'mount d "{cue_path}" -t cdrom',
            "d:",
            "CDPLAY T 2",
        ],
        settings={"machine": "svga_s3"},
        allowed_image_roots=[disc_dir],
    )

    try:
        client = instance.client
        client.wait_shell(timeout=15)
        time.sleep(5)

        capture_dir = work_dir / "capture"
        capture_dir.mkdir(parents=True, exist_ok=True)
        wav_path = capture_dir / f"{variant_name}.wav"

        r = client.audio_capture_start(str(wav_path))
        assert r.status_code == 200, (
            f"audio_capture_start failed: {r.status_code} {r.text}")

        time.sleep(5)

        r = client.audio_capture_stop()
        assert r.status_code == 200, (
            f"audio_capture_stop failed: {r.status_code} {r.text}")

        time.sleep(0.5)

        assert wav_path.exists(), f"no WAV captured for {variant_name}"
        ok, msg = validate_audio(wav_path)
        assert ok, f"{variant_name} decoder: {msg}"
    finally:
        instance.shutdown()


def test_cd_audio_master(cd_variants):
    cue = cd_variants.get("master")
    assert cue, "master CUE not found in cache"
    _run_decoder_test("master", cue)


def test_cd_audio_wav(cd_variants):
    cue = cd_variants.get("wav")
    if not cue:
        pytest.skip("WAV variant not available")
    _run_decoder_test("wav", cue)


def test_cd_audio_mp3(cd_variants):
    cue = cd_variants.get("mp3")
    if not cue:
        pytest.skip("lame not installed")
    _run_decoder_test("mp3", cue)


def test_cd_audio_flac(cd_variants):
    cue = cd_variants.get("flac")
    if not cue:
        pytest.skip("flac not installed")
    _run_decoder_test("flac", cue)


def test_cd_audio_ogg(cd_variants):
    cue = cd_variants.get("ogg")
    if not cue:
        pytest.skip("oggenc not installed")
    _run_decoder_test("ogg", cue)
