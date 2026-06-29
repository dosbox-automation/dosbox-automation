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
    resolve_cycle_settings,
    save_run_stats,
    write_provenance_readme,
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
    elif manifest.media == "zip":
        lines.append(f"mount {manifest.target_drive} \"{hd_dir}\"")
        lines.append(f"mount {manifest.source_drive} \"{game_dir}\"")
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
    client.shutdown()
    return True


def run_via_lua(client, manifest, game_dir):
    """Drive the installer via Lua text matching.

    Returns (status_dict, generated_lua_script).
    """
    script = generate_install_script(manifest)
    time.sleep(0.3)
    r = client.script_load(script, name=f"install-{manifest.slug}", debug=True)
    assert r.status_code == 200, f"Script load failed: {r.json()}"

    r = client.script_start()
    assert r.status_code == 200, f"Script start failed: {r.json()}"

    handled_swaps = set()
    deadline = time.monotonic() + 300
    while time.monotonic() < deadline:
        r = client.script_status()
        if r.status_code != 200:
            break
        data = r.json()

        for key, val in data.get("output", {}).items():
            if key.startswith("swap_") and isinstance(val, int):
                if key not in handled_swaps:
                    disc_path = str(game_dir / manifest.disc_images[val - 1])
                    client.drive_swap(manifest.source_drive, disc_path)
                    handled_swaps.add(key)

        if data["state"] in ("completed", "error"):
            break
        time.sleep(0.5)

    r = client.script_status()
    assert r.status_code == 200, f"Status failed: {r.status_code}"
    return r.json(), script


def _dump_diagnostics(slug, instance, client, status, lua_script,
                      work_dir, game_dir, failed):
    """Print diagnostics and collect provenance regardless of outcome."""
    state_label = "FAILED" if failed else "GREEN"

    stderr_log = work_dir / "dosbox-stderr.log"

    print(f"\n{'=' * 60}")
    print(f"  {slug}  [{state_label}]")
    print(f"  work_dir: {work_dir}")
    if stderr_log.exists():
        print(f"  stderr:   {stderr_log}")
    print(f"{'=' * 60}")

    if status:
        output = status.get("output", {})
        print(f"  script state: {status.get('state', '?')}")
        if status.get("error"):
            print(f"  script error: {status['error']}")
        for key in sorted(output):
            print(f"    {key} = {output[key]}")

    try:
        r = client.screen_text()
        if r.status_code == 200:
            screen = r.json().get("text", "")
            if screen.strip():
                print(f"  --- screen text at end ---")
                for line in screen.splitlines():
                    stripped = line.rstrip()
                    if stripped:
                        print(f"  | {stripped}")
                print(f"  --- end screen text ---")
    except Exception:
        pass

    if lua_script:
        script_path = work_dir / "generated-install.lua"
        script_path.parent.mkdir(parents=True, exist_ok=True)
        script_path.write_text(lua_script)
        print(f"  lua script: {script_path}")

    if stderr_log.exists():
        try:
            lines = stderr_log.read_text().splitlines()
            tail = lines[-30:] if len(lines) > 30 else lines
            print(f"  --- dosbox stderr (last {len(tail)} lines) ---")
            for line in tail:
                print(f"  | {line}")
            print(f"  --- end stderr ---")
        except Exception:
            pass

    print(f"{'=' * 60}")

    collect_provenance(instance.work_dir, game_dir, slug)

    if lua_script:
        provenance_dir = game_dir / "provenance"
        provenance_dir.mkdir(parents=True, exist_ok=True)
        (provenance_dir / f"{slug}-install.lua").write_text(lua_script)


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

    # Replay has to run at the same fixed CPU rate the recording was made at,
    # or the frame-indexed input events drift out of sync with the game. Pin
    # both the real and protected mode rates; the recorded values win over the
    # manifest, and both beat DOSBox's auto/60000 defaults.
    recording_data = None
    if has_recording:
        with open(game_dir / "recording.json") as f:
            recording_data = json.load(f)
    settings = dict(manifest.settings)
    settings.update(resolve_cycle_settings(recording_data, manifest.settings))

    instance = dosbox_e2e(
        autoexec_lines=autoexec, work_dir=work_dir,
        conf_dir=game_dir,
        allowed_image_roots=[game_dir],
        settings=settings,
    )
    client = instance.client
    client.wait_shell(timeout=15)

    start_time = time.monotonic()
    status = None
    lua_script = None
    recording_ok = None
    failures = []

    # --- run the installer ---
    if has_recording:
        recording_ok = run_via_recording(client, game_dir)
    else:
        status, lua_script = run_via_lua(client, manifest, game_dir)

    duration_s = round(time.monotonic() - start_time, 2)

    # --- check results, collect all failures before raising ---
    if has_recording:
        if not recording_ok:
            failures.append("Recording replay failed")
    else:
        if status["state"] != "completed":
            failures.append(
                f"state={status['state']}, "
                f"error={status.get('error', '')}"
            )
        elif status.get("output", {}).get("install_complete") != "yes":
            failures.append("install_complete not set to 'yes'")
        else:
            for key, val in status.get("output", {}).items():
                if key.startswith("prompt_") and val != "found":
                    failures.append(f"{key} timed out")

    # Verify installed files exist (skip for booters).
    file_stats = {}
    if manifest.media != "booter" and not failures:
        for expected_file in manifest.verify_files:
            rel = expected_file.split(":\\", 1)[1].replace("\\", "/")
            host_path = hd_dir / rel
            if host_path.exists():
                file_stats[expected_file] = host_path.stat().st_size
            else:
                failures.append(f"missing: {expected_file}")

    # --- diagnostics and provenance always run ---
    _dump_diagnostics(slug, instance, client, status, lua_script,
                      work_dir, game_dir, failed=bool(failures))

    # --- run statistics ---
    run_stats = {
        "game": slug,
        "state": "GREEN" if not failures else "FAILED",
        "duration_s": duration_s,
    }

    if status and not has_recording:
        output = status.get("output", {})
        prompts_found = sum(1 for k, v in output.items()
                            if k.startswith("prompt_") and v == "found")
        prompts_timeout = sum(1 for k, v in output.items()
                              if k.startswith("prompt_") and v == "timeout")
        swaps = sum(1 for k in output if k.startswith("swap_")
                    and not k.endswith("_ack"))
        run_stats["prompts_found"] = prompts_found
        run_stats["prompts_timeout"] = prompts_timeout
        run_stats["disk_swaps"] = swaps

    if file_stats:
        run_stats["installed_files"] = file_stats
        run_stats["files_found"] = len(file_stats)
        run_stats["files_expected"] = len(manifest.verify_files)

    run_stats["cpu_cycles"] = settings["cpu_cycles"]
    run_stats["cpu_cycles_protected"] = settings["cpu_cycles_protected"]

    print(f"  Duration: {duration_s}s")
    if "disk_swaps" in run_stats:
        print(f"  Swaps:    {run_stats['disk_swaps']}")
    if "prompts_found" in run_stats:
        print(f"  Prompts:  {run_stats['prompts_found']} found, "
              f"{run_stats.get('prompts_timeout', 0)} timeout")
    if file_stats:
        print(f"  Files:    {len(file_stats)}"
              f"/{len(manifest.verify_files)}")
        for path, size in file_stats.items():
            print(f"    {path}: {size:,} bytes")

    provenance_dir = game_dir / "provenance"
    provenance_dir.mkdir(parents=True, exist_ok=True)
    save_run_stats(provenance_dir, slug, run_stats)
    write_provenance_readme(provenance_dir, slug, manifest)

    # --- assert after everything is collected ---
    assert not failures, (
        f"Install failed for {slug}:\n  " + "\n  ".join(failures)
    )

    # Clean up HD directory only on success.
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
