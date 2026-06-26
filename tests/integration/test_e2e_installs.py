# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""End-to-end installer automation tests.

Each test starts its own headless DOSBox instance with appropriate
drives mounted, records ZMBV video, and verifies expected files exist
after completion.

Two automation modes:
  - Recording replay: if recording.json exists in the game's disk
    directory, replay the recorded input events. Created by
    record_install.py from a manual session.
  - Lua text matching: if no recording exists, generate a Lua script
    from the manifest's prompt sequence and drive the installer via
    screen text matching. Works for plain text-mode installers.

Tests skip if disk images are not present AND cannot be downloaded.
"""

import json
import secrets
import shutil
import time
import warnings
from pathlib import Path

import pytest

from e2e_helpers import (
    GameManifest,
    collect_provenance,
    download_disk_images,
    generate_install_script,
)

from conftest import WORKSPACE

DISKS_DIR = Path(__file__).resolve().parents[1] / "files" / "disks"


def discover_games() -> list[tuple[str, Path]]:
    """Find all game manifests in the disks directory."""
    games = []
    if DISKS_DIR.exists():
        for manifest_path in sorted(DISKS_DIR.glob("*/manifest.toml")):
            slug = manifest_path.parent.name
            games.append((slug, manifest_path))
    return games


GAMES = discover_games()


def ensure_disk_images(manifest: GameManifest, game_dir: Path) -> bool:
    """Make sure disk images are present, downloading if permitted."""
    if all((game_dir / img).exists() for img in manifest.disc_images):
        return True
    return download_disk_images(manifest, game_dir)


def build_autoexec(manifest: GameManifest, game_dir: Path,
                   hd_dir: Path) -> list[str]:
    """Build autoexec lines to mount drives for the game."""
    lines = []

    if manifest.media == "booter":
        disk_path = game_dir / manifest.disc_images[0]
        lines.append(f"mount a \"{disk_path}\" -t floppy")
    elif manifest.media in ("cdrom-iso", "cdrom-cue"):
        lines.append(f"mount {manifest.target_drive} \"{hd_dir}\"")
        disk_path = game_dir / manifest.disc_images[0]
        lines.append(
            f"mount {manifest.source_drive} \"{disk_path}\" -t cdrom"
        )
    elif manifest.media == "directory":
        disk_path = game_dir / manifest.disc_images[0]
        lines.append(f"mount {manifest.target_drive} \"{hd_dir}\"")
        lines.append(f"mount {manifest.source_drive} \"{disk_path}\"")
    else:
        lines.append(f"mount {manifest.target_drive} \"{hd_dir}\"")
        disk_path = game_dir / manifest.disc_images[0]
        lines.append(
            f"mount {manifest.source_drive} \"{disk_path}\" -t floppy"
        )

    return lines


def split_at_swaps(events):
    """Split an event list into (input_chunk, swap_event) pairs.

    Returns a list of tuples: [(events, None), (events, swap), ...].
    The last tuple always has swap=None.
    """
    chunks = []
    current = []
    for event in events:
        if event.get("type") == "drive_swap":
            chunks.append((current, event))
            current = []
        else:
            current.append(event)
    chunks.append((current, None))
    return chunks


def run_via_recording(client, game_dir):
    """Replay a recorded input session. Returns True on success."""
    recording_path = game_dir / "recording.json"
    with open(recording_path) as f:
        recording = json.load(f)

    events = recording.get("events", [])
    if not events:
        return False

    client.capture_start()
    time.sleep(0.5)

    chunks = split_at_swaps(events)

    for i, (input_events, swap) in enumerate(chunks):
        if input_events:
            last_t = max(e.get("t", 0) for e in input_events)
            first_t = min(e.get("t", 0) for e in input_events)
            chunk_duration_s = (last_t - first_t) / 1000.0

            r = client.input_sequence(input_events)
            if r.status_code != 200:
                return False

            time.sleep(chunk_duration_s + 3.0)

        if swap:
            image_path = str(game_dir / swap["image"])
            swap_t = swap.get("t", 0)

            next_chunk_events = chunks[i + 1][0] if i + 1 < len(chunks) else []
            if next_chunk_events:
                next_first_t = min(e.get("t", 0) for e in next_chunk_events)
                gap_s = (next_first_t - swap_t) / 1000.0
            else:
                gap_s = 1.0

            r = client.drive_swap(swap["drive"], image_path)
            if r.status_code != 200:
                return False

            time.sleep(gap_s)

    duration_s = recording.get("duration_ms", 60000) / 1000.0
    wait_time = duration_s + 10
    deadline = time.monotonic() + wait_time

    while time.monotonic() < deadline:
        r = client.recording_status()
        if r.status_code == 200:
            ps = client.program_state()
            if ps.status_code == 200 and ps.json().get("is_shell"):
                time.sleep(2)
                break
        time.sleep(1)

    client.capture_stop()
    return True


def run_via_lua(client, manifest, game_dir):
    """Drive the installer via Lua text matching. Returns status dict."""
    script = generate_install_script(manifest)
    time.sleep(0.3)
    r = client.script_load(script, name=f"install-{manifest.slug}", debug=True)
    assert r.status_code == 200, f"Script load failed: {r.json()}"

    r = client.script_start()
    assert r.status_code == 200, f"Script start failed: {r.json()}"

    current_disc = 1
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        r = client.script_status()
        if r.status_code != 200:
            break
        data = r.json()

        for key, val in data.get("output", {}).items():
            if key.startswith("swap_") and isinstance(val, int):
                if val != current_disc:
                    disc_path = str(game_dir / manifest.disc_images[val - 1])
                    client.drive_swap(manifest.source_drive, disc_path)
                    current_disc = val

        if data["state"] in ("completed", "error"):
            break
        time.sleep(0.5)

    r = client.script_status()
    assert r.status_code == 200, f"Status failed: {r.status_code}"
    return r.json()


@pytest.mark.parametrize("slug,manifest_path", GAMES,
                         ids=[g[0] for g in GAMES])
def test_install(slug, manifest_path, dosbox_e2e):
    manifest = GameManifest.from_file(manifest_path)
    game_dir = DISKS_DIR / slug

    if not ensure_disk_images(manifest, game_dir):
        pytest.skip(f"disk images not available for {slug}")

    has_recording = (game_dir / "recording.json").exists()
    has_prompts = len(manifest.prompts) > 0

    if not has_recording and not has_prompts:
        pytest.skip(f"no recording or prompts for {slug}")

    run_id = secrets.token_hex(4)
    hd_dir = game_dir / f"run-{run_id}"
    hd_dir.mkdir()

    work_dir = WORKSPACE / f"e2e-{run_id}"

    autoexec = build_autoexec(manifest, game_dir, hd_dir)
    instance = dosbox_e2e(
        autoexec_lines=autoexec, work_dir=work_dir,
        conf_dir=game_dir,
    )
    client = instance.client
    client.wait_shell(timeout=15)

    if has_recording:
        ok = run_via_recording(client, game_dir)
        assert ok, "Recording replay failed"
    else:
        status = run_via_lua(client, manifest, game_dir)
        assert status["state"] == "completed", (
            f"Install failed: state={status['state']}, "
            f"error={status.get('error', '')}"
        )
        assert status.get("output", {}).get("install_complete") == "yes"
        for key, val in status.get("output", {}).items():
            if key.startswith("prompt_"):
                assert val == "found", f"{key} timed out"

    # Verify installed files exist (skip for booters).
    if manifest.media != "booter":
        for expected_file in manifest.verify_files:
            rel = expected_file.split(":\\", 1)[1].replace("\\", "/")
            host_path = hd_dir / rel
            assert host_path.exists(), f"Expected file missing: {expected_file}"

    # Collect provenance.
    collect_provenance(instance.work_dir, game_dir, slug)

    # Clean up HD directory.
    resolved_hd = hd_dir.resolve()
    resolved_game = game_dir.resolve()
    if (resolved_hd.parent == resolved_game
            and resolved_hd.name.startswith("run-")
            and resolved_hd.is_dir()):
        shutil.rmtree(resolved_hd, ignore_errors=True)
    else:
        warnings.warn(
            f"Refusing to remove HD dir: expected run-* under "
            f"{resolved_game}, got {resolved_hd}"
        )
