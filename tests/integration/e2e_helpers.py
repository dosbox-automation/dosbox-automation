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
                shutil.copy2(str(found[0]), str(target))

        return all((game_dir / img).exists() for img in manifest.disc_images)

    except (subprocess.CalledProcessError, urllib.error.URLError, OSError) as e:
        print(f"  Download failed for {manifest.slug}: {e}")
        return False
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


def collect_provenance(work_dir: Path, game_dir: Path, slug: str):
    """Collect capture files from a DOSBox run into the provenance folder.

    Finds ZMBV AVI files in the capture directory, converts to MKV,
    exports frames, and copies everything to provenance/.
    """
    capture_dir = work_dir / "capture"
    provenance_dir = game_dir / "provenance"
    provenance_dir.mkdir(parents=True, exist_ok=True)

    if not capture_dir.exists():
        return

    for avi in sorted(capture_dir.glob("video*.avi")):
        # Copy the raw ZMBV AVI.
        dest_avi = provenance_dir / f"{slug}-{avi.name}"
        shutil.copy2(str(avi), str(dest_avi))

        # Convert to lossless MKV.
        mkv_path = provenance_dir / f"{slug}-{avi.stem}.mkv"
        try:
            convert_zmbv_to_mkv(dest_avi, mkv_path)
        except subprocess.CalledProcessError:
            pass

        # Export frames.
        frames_dir = provenance_dir / f"{slug}-{avi.stem}-frames"
        try:
            frames = export_frames(dest_avi, frames_dir)
            # Save frame hashes.
            if frames:
                hashes = {f.name: hash_frame(f) for f in frames}
                save_golden_hashes(
                    provenance_dir / f"{slug}-{avi.stem}.hashes", hashes
                )
        except subprocess.CalledProcessError:
            pass
