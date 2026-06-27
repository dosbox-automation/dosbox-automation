# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""E2E test helpers: manifest parsing, Lua script generation, frame
hashing, ZMBV-to-MKV conversion, and disk image downloading."""

import hashlib
import shutil
import subprocess
import tempfile
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

try:
    import tomllib
except ImportError:
    import tomli as tomllib


@dataclass
class PromptStep:
    wait: str | None = None
    type_text: str | None = None
    action: str | None = None
    key: str | None = None
    pause: int | None = None
    repeat: str | None = None
    until_mode: str | None = None
    change_drive: str | None = None


@dataclass
class GameManifest:
    name: str
    slug: str
    media: str
    license: str
    source_url: str | None
    source_sha256: str | None
    disc_images: list[str]
    installer_path: str
    source_drive: str
    target_drive: str
    prompts: list[PromptStep]
    verify_files: list[str]
    screenshot_at: list[int] = field(default_factory=list)
    settings: dict[str, str] = field(default_factory=dict)

    @classmethod
    def from_file(cls, path: Path) -> "GameManifest":
        with open(path, "rb") as f:
            data = tomllib.load(f)

        prompts = []
        for entry in data.get("prompts", {}).get("sequence", []):
            prompts.append(PromptStep(
                wait=entry.get("wait"),
                type_text=entry.get("type"),
                action=entry.get("action"),
                key=entry.get("key"),
                pause=entry.get("pause"),
                repeat=entry.get("repeat"),
                until_mode=entry.get("until"),
                change_drive=entry.get("change_drive"),
            ))

        settings = data.get("settings", {})

        source = data.get("source", {})
        return cls(
            name=data["game"]["name"],
            slug=data["game"]["slug"],
            media=data["game"]["media"],
            license=data["game"]["license"],
            source_url=source.get("url"),
            source_sha256=source.get("sha256"),
            disc_images=data["discs"]["images"],
            installer_path=data["installer"]["path"],
            source_drive=data["installer"]["source_drive"],
            target_drive=data["installer"]["target_drive"],
            prompts=prompts,
            verify_files=data.get("verify", {}).get("files", []),
            screenshot_at=data.get("capture", {}).get("screenshot_at", []),
            settings=settings,
        )


def _key_name(short_name):
    """Map short TOML key names to KBD_ constants used by dosbox.key()."""
    if short_name.startswith("KBD_"):
        return short_name
    mapping = {
        "enter": "KBD_enter", "escape": "KBD_esc", "esc": "KBD_esc",
        "up": "KBD_up", "down": "KBD_down", "left": "KBD_left",
        "right": "KBD_right", "space": "KBD_space", "tab": "KBD_tab",
        "backspace": "KBD_backspace",
    }
    if short_name in mapping:
        return mapping[short_name]
    if len(short_name) == 1 and short_name.isalnum():
        return f"KBD_{short_name.lower()}"
    return f"KBD_{short_name}"


def _emit_key_press(lines, key_short, gap_frames=8, indent=""):
    """Emit a press-wait-release sequence for a single key."""
    kbd = _key_name(key_short)
    lines.append(f'{indent}dosbox.key("{kbd}", true)')
    lines.append(f"{indent}dosbox.wait_frames({gap_frames})")
    lines.append(f'{indent}dosbox.key("{kbd}", false)')
    lines.append(f"{indent}dosbox.wait_frames(8)")


def generate_install_script(manifest: GameManifest) -> str:
    """Generate a Lua automation script from a game manifest."""
    lines = [
        "dosbox.capture_start()",
        "dosbox.wait_frames(30)",
    ]

    if manifest.media == "booter":
        lines.extend([
            "",
            f'dosbox.type("BOOT {manifest.source_drive}:\\n")',
            "dosbox.wait_frames(120)",
        ])
    elif manifest.media == "zip":
        lines.extend([
            "",
            f'dosbox.type("{manifest.target_drive}:\\n")',
            "dosbox.wait_frames(30)",
        ])
    else:
        lines.extend([
            "",
            f'dosbox.type("{manifest.source_drive}:\\n")',
            "dosbox.wait_frames(30)",
            f'dosbox.type("{manifest.installer_path}\\n")',
            "dosbox.wait_frames(60)",
        ])

    lines.append("")
    swap_idx = 0

    for i, prompt in enumerate(manifest.prompts):
        if prompt.change_drive is not None:
            drive = prompt.change_drive.rstrip(":\\")
            lines.append(f'dosbox.type("{drive}:\\n")')
            lines.append("dosbox.wait_frames(30)")
            continue

        if prompt.pause is not None:
            lines.append(f"dosbox.wait_frames({prompt.pause})")
            continue

        if prompt.repeat is not None and prompt.until_mode is not None:
            if prompt.until_mode == "gfx":
                lines.append("while dosbox.is_text_mode() do")
            else:
                lines.append("while not dosbox.is_text_mode() do")

            if prompt.repeat != "wait":
                lines.append("  local prev = dosbox.screen_text()")
                _emit_key_press(lines, prompt.repeat, gap_frames=8,
                                indent="  ")
                lines.append("  for _ = 1, 20 do")
                lines.append("    dosbox.wait_frames(10)")
                if prompt.until_mode == "gfx":
                    lines.append(
                        "    if not dosbox.is_text_mode()"
                        " or dosbox.screen_text() ~= prev"
                        " then break end"
                    )
                else:
                    lines.append(
                        "    if dosbox.is_text_mode()"
                        " or dosbox.screen_text() ~= prev"
                        " then break end"
                    )
                lines.append("  end")
            else:
                lines.append("  dosbox.wait_frames(30)")

            lines.append("end")
            continue

        if prompt.key is not None and prompt.wait is None:
            _emit_key_press(lines, prompt.key)
            continue

        if prompt.wait is not None:
            escaped_wait = prompt.wait.replace("\\", "\\\\").replace(
                '"', '\\"'
            )
            lines.append(
                f'local found_{i} = dosbox.wait_for_text('
                f'"{escaped_wait}", 1800)'
            )
            lines.append(
                f'dosbox.output["prompt_{i}"] = '
                f'found_{i} and "found" or "timeout"'
            )

            if i in manifest.screenshot_at:
                lines.append(
                    f'dosbox.output["screen_{i}"] = dosbox.screen_text()'
                )

        if prompt.action and prompt.action.startswith("swap:"):
            disc_num = int(prompt.action.split(":")[1])
            lines.append(
                f'dosbox.output["swap_{swap_idx}"] = {disc_num}'
            )
            swap_idx += 1
            lines.append("dosbox.wait_frames(150)")

        if prompt.type_text is not None:
            escaped = prompt.type_text.replace(
                "\\", "\\\\"
            ).replace('"', '\\"')
            lines.append(f'dosbox.type("{escaped}\\n")')
            lines.append("dosbox.wait_frames(30)")

        if prompt.key is not None:
            _emit_key_press(lines, prompt.key)

    lines.extend([
        "",
        "dosbox.capture_stop()",
        'dosbox.output["install_complete"] = "yes"',
    ])

    return "\n".join(lines)


def hash_frame(frame_path: Path) -> str:
    """SHA-256 hash of a frame image file."""
    return hashlib.sha256(frame_path.read_bytes()).hexdigest()


def export_frames(video_path: Path, output_dir: Path,
                  timestamps: list[float] | None = None) -> list[Path]:
    """Extract frames from a ZMBV AVI or MKV using ffmpeg."""
    output_dir.mkdir(parents=True, exist_ok=True)

    if timestamps:
        frames = []
        for i, ts in enumerate(timestamps):
            out = output_dir / f"frame_{i:04d}.png"
            subprocess.run([
                "ffmpeg", "-y", "-i", str(video_path),
                "-ss", f"{ts:.3f}",
                "-frames:v", "1",
                str(out),
            ], capture_output=True, check=True)
            frames.append(out)
        return frames
    else:
        pattern = output_dir / "frame_%04d.png"
        subprocess.run([
            "ffmpeg", "-y", "-i", str(video_path),
            str(pattern),
        ], capture_output=True, check=True)
        return sorted(output_dir.glob("frame_*.png"))


def convert_zmbv_to_mkv(avi_path: Path, mkv_path: Path) -> Path:
    """Convert ZMBV AVI to lossless MKV (FFV1 codec)."""
    subprocess.run([
        "ffmpeg", "-y",
        "-i", str(avi_path),
        "-c:v", "ffv1",
        "-level", "3",
        "-c:a", "flac",
        str(mkv_path),
    ], capture_output=True, check=True)
    return mkv_path


def load_golden_hashes(hash_file: Path) -> dict[str, str]:
    """Load golden hashes from a file (one 'hash filename' per line)."""
    if not hash_file.exists():
        return {}
    hashes = {}
    for line in hash_file.read_text().splitlines():
        parts = line.strip().split(maxsplit=1)
        if len(parts) == 2:
            hashes[parts[1]] = parts[0]
    return hashes


def save_golden_hashes(hash_file: Path, hashes: dict[str, str]):
    """Save golden hashes to a file."""
    lines = [f"{h} {name}" for name, h in sorted(hashes.items())]
    hash_file.write_text("\n".join(lines) + "\n")


def download_disk_images(manifest: GameManifest, game_dir: Path) -> bool:
    """Download and extract disk images for freely-licensed games.

    Returns True if all images are now present, False on failure.
    Only downloads if license permits (freeware, shareware, demo).
    """
    allowed_licenses = {"freeware", "shareware", "demo", "GPL-3.0"}

    if manifest.license not in allowed_licenses:
        return False

    if not manifest.source_url:
        return False

    # Check if already present.
    if all((game_dir / img).exists() for img in manifest.disc_images):
        return True

    url = manifest.source_url
    tmp_dir = Path(tempfile.mkdtemp(prefix="dosbox-e2e-dl-"))

    try:
        # Download the archive.
        ext = ".7z" if ".7z" in url else ".zip"
        archive_path = tmp_dir / f"download{ext}"
        urllib.request.urlretrieve(url, archive_path)

        if manifest.media == "zip":
            # The zip itself is the disc image. Copy it directly.
            target = game_dir / manifest.disc_images[0]
            if not target.exists():
                shutil.copy2(str(archive_path), str(target))
            return target.exists()

        # Extract based on type.
        if ext == ".7z":
            subprocess.run(
                ["7z", "x", "-y", f"-o{tmp_dir / 'extracted'}",
                 str(archive_path)],
                capture_output=True, check=True,
            )
        else:
            shutil.unpack_archive(str(archive_path), tmp_dir / "extracted")

        # Find and copy disk images to the game directory.
        extracted = tmp_dir / "extracted"
        for img_name in manifest.disc_images:
            target = game_dir / img_name
            if target.exists():
                continue

            # Search the extraction for the file.
            found = list(extracted.rglob(img_name))
            if not found:
                # Try case-insensitive match.
                found = [
                    p for p in extracted.rglob("*")
                    if p.name.lower() == img_name.lower()
                ]

            if found:
                if found[0].is_dir():
                    shutil.copytree(str(found[0]), str(target))
                else:
                    shutil.copy2(str(found[0]), str(target))

        return all((game_dir / img).exists() for img in manifest.disc_images)

    except (subprocess.CalledProcessError, urllib.error.URLError, OSError) as e:
        print(f"  Download failed for {manifest.slug}: {e}")
        return False
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


def _provenance_timestamp():
    """YYYYMMDD-HHMM timestamp for provenance filenames."""
    import datetime
    return datetime.datetime.now().strftime("%Y%m%d-%H%M")


def collect_provenance(work_dir: Path, game_dir: Path, slug: str):
    """Collect capture files and logs from a DOSBox run into provenance/.

    Produces timestamped files:
      {slug}-{YYYYMMDD-HHMM}-install-lossless.mkv  (FFV1/FLAC)
      {slug}-{YYYYMMDD-HHMM}-install.mkv            (x264/mp3 192k)
      {slug}-{YYYYMMDD-HHMM}-lua.log
      {slug}-{YYYYMMDD-HHMM}-dosbox.log
    """
    capture_dir = work_dir / "capture"
    provenance_dir = game_dir / "provenance"
    provenance_dir.mkdir(parents=True, exist_ok=True)

    timestamp = _provenance_timestamp()
    base = f"{slug}-{timestamp}"

    if capture_dir.exists():
        avis = sorted(capture_dir.glob("video*.avi"))
        if not avis:
            pass
        elif len(avis) == 1:
            source_avi = avis[0]
        else:
            # Multiple ZMBV segments: concatenate into one file first.
            concat_list = capture_dir / "concat.txt"
            with open(concat_list, "w") as f:
                for avi in avis:
                    f.write(f"file '{avi.name}'\n")
            source_avi = capture_dir / "combined.avi"
            print(f"  Concatenating {len(avis)} video segments...")
            try:
                subprocess.run([
                    "ffmpeg", "-y", "-f", "concat", "-safe", "0",
                    "-i", str(concat_list),
                    "-c", "copy",
                    str(source_avi),
                ], capture_output=True, check=True)
            except subprocess.CalledProcessError as e:
                print(f"  Concatenation failed: {e}")
                source_avi = avis[0]

        if avis:
            # Lossless FFV1/FLAC.
            lossless_path = provenance_dir / f"{base}-install-lossless.mkv"
            print(f"  Transcoding to lossless: {lossless_path.name}...")
            try:
                subprocess.run([
                    "ffmpeg", "-y", "-i", str(source_avi),
                    "-c:v", "ffv1", "-level", "3",
                    "-c:a", "flac",
                    str(lossless_path),
                ], capture_output=True, check=True)
                print(f"  Done: {lossless_path.name}")
            except subprocess.CalledProcessError as e:
                print(f"  FFV1 transcode failed: {e}")

            # Compressed x264/mp3 192k.
            compressed_path = provenance_dir / f"{base}-install.mkv"
            print(f"  Transcoding to compressed: {compressed_path.name}...")
            try:
                subprocess.run([
                    "ffmpeg", "-y", "-i", str(source_avi),
                    "-c:v", "libx264", "-preset", "medium", "-crf", "23",
                    "-c:a", "libmp3lame", "-b:a", "192k",
                    str(compressed_path),
                ], capture_output=True, check=True)
                print(f"  Done: {compressed_path.name}")
            except subprocess.CalledProcessError as e:
                print(f"  x264 transcode failed: {e}")

            # Export frames and golden hashes (from clean capture).
            frames_dir = provenance_dir / f"{base}-frames"
            try:
                frames = export_frames(source_avi, frames_dir)
                if frames:
                    hashes = {f.name: hash_frame(f) for f in frames}
                    save_golden_hashes(
                        provenance_dir / f"{base}.hashes", hashes
                    )
            except subprocess.CalledProcessError:
                pass

    # Copy Lua debug log (lives under the config dir, which uses
    # work_dir as HOME).
    lua_log_dir = work_dir / ".config" / "dosbox-automation" / "logs"
    if lua_log_dir.exists():
        for lua_log in sorted(lua_log_dir.glob("lua-debug-*.log")):
            dest = provenance_dir / f"{base}-lua.log"
            shutil.copy2(str(lua_log), str(dest))
            print(f"  Lua log: {dest.name}")

    # Copy DOSBox stderr log.
    stderr_log = work_dir / "dosbox-stderr.log"
    if stderr_log.exists():
        dest = provenance_dir / f"{base}-dosbox.log"
        shutil.copy2(str(stderr_log), str(dest))
        print(f"  DOSBox log: {dest.name}")


def save_run_stats(provenance_dir: Path, slug: str, stats: dict):
    """Write run statistics to a JSON file in provenance."""
    import json as _json
    timestamp = _provenance_timestamp()
    stats_path = provenance_dir / f"{slug}-{timestamp}-stats.json"
    stats_path.write_text(_json.dumps(stats, indent=2) + "\n")
    print(f"  Stats saved: {stats_path.name}")


def write_provenance_readme(provenance_dir: Path, slug: str,
                            manifest: "GameManifest"):
    """Write a README explaining how to reproduce this provenance run."""
    readme_path = provenance_dir / "README.md"

    disc_list = "\n".join(f"  - `{img}`" for img in manifest.disc_images)
    source_line = ""
    if manifest.source_url:
        source_line = f"Download: {manifest.source_url}\n"

    verify_list = ""
    if manifest.verify_files:
        verify_list = "Expected after install:\n"
        verify_list += "\n".join(f"  - `{f}`" for f in manifest.verify_files)
        verify_list += "\n"

    settings_block = ""
    if manifest.settings:
        pairs = ", ".join(f"{k}={v}" for k, v in manifest.settings.items())
        settings_block = f"Game settings: {pairs}\n"

    readme_path.write_text(f"""\
# Provenance: {manifest.name}

Automated install recording produced by dosbox-automation.

## What is in this folder

Each run produces a timestamped set of files:

- `{slug}-YYYYMMDD-HHMM-install-lossless.mkv` - lossless video (FFV1/FLAC)
- `{slug}-YYYYMMDD-HHMM-install.mkv` - compressed video (x264, mp3 192k)
- `{slug}-YYYYMMDD-HHMM-lua.log` - Lua automation debug log (frame + timecode)
- `{slug}-YYYYMMDD-HHMM-dosbox.log` - DOSBox engine stderr output
- `{slug}-YYYYMMDD-HHMM-stats.json` - run statistics (duration, files, prompts)
- `{slug}-YYYYMMDD-HHMM-frames/` - extracted video frames (PNG)
- `{slug}-YYYYMMDD-HHMM.hashes` - SHA-256 hashes of extracted frames

## Required tools

- dosbox-automation (built from source, debug or release)
- Python 3.12+ with pytest
- ffmpeg (for video transcoding, frame extraction)
- 7z (for extracting disk image archives, if downloading)

Python packages (install into the project venv):
- pytest
- requests
- tomllib (stdlib in 3.11+) or tomli

## Disk images

{disc_list}
{source_line}
Media type: {manifest.media}
License: {manifest.license}

## How to reproduce

1. Build dosbox-automation:

       cmake --preset debug-linux-vcpkg
       cmake --build build/debug-linux -- -j$(nproc)

2. Set up the Python environment:

       python3 -m venv .venv
       .venv/bin/pip install pytest requests

3. Make sure disk images are in `tests/files/disks/{slug}/`.
   {f"They can be downloaded automatically from the source URL above." if manifest.source_url else "Disk images must be provided manually."}

4. Run the E2E test:

       .venv/bin/pytest tests/integration/test_e2e_installs.py -k {slug} -v -s

   This runs headless. For a visible window (to see the OSD overlay):

       DOSBOX_VISIBLE=1 .venv/bin/pytest tests/integration/test_e2e_installs.py -k {slug} -v -s

5. Provenance files appear in `tests/files/disks/{slug}/provenance/`.

## Recipe

The install sequence is defined in `tests/files/disks/{slug}/manifest.toml`.
The TOML recipe maps directly to Lua API calls and can be used standalone
without the Python test harness.

{settings_block}{verify_list}""")
    print(f"  README: {readme_path.name}")
