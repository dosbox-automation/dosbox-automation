# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import lzma
import re
import shutil
import struct
import subprocess
import wave
from dataclasses import dataclass
from pathlib import Path

SECTOR_SIZE = 2352
MODE1_USER_OFFSET = 16
MODE1_USER_SIZE = 2048
PCM_RATE = 44100
PCM_CHANNELS = 2
PCM_SAMPWIDTH = 2

TRACK_NAMES = ["spring-allegro", "summer-allegro", "winter-allegro"]

SILENCE_THRESHOLD = 50
MIN_NONSILENT_PCT = 80


@dataclass
class CueTrack:
    number: int
    track_type: str
    index01_msf: str


def parse_cue(cue_text: str) -> list[CueTrack]:
    tracks: list[CueTrack] = []
    current_number = 0
    current_type = ""
    for line in cue_text.splitlines():
        line = line.strip()
        m = re.match(r"TRACK (\d+) (.+)", line)
        if m:
            current_number = int(m.group(1))
            current_type = m.group(2)
            continue
        m = re.match(r"INDEX 01 (\d+:\d+:\d+)", line)
        if m and current_number:
            tracks.append(CueTrack(current_number, current_type,
                                   m.group(1)))
    return tracks


def msf_to_sectors(msf: str) -> int:
    m, s, f = (int(x) for x in msf.split(":"))
    return m * 60 * 75 + s * 75 + f


def expand_xz(xz_path: Path, bin_path: Path) -> None:
    with open(xz_path, "rb") as xz_in:
        data = lzma.decompress(xz_in.read())
    with open(bin_path, "wb") as f:
        f.write(data)


def extract_audio_tracks(bin_path: Path, cue_text: str,
                         output_dir: Path) -> list[Path]:
    tracks = parse_cue(cue_text)
    audio_tracks = [t for t in tracks if t.track_type == "AUDIO"]
    bin_size = bin_path.stat().st_size
    total_sectors = bin_size // SECTOR_SIZE

    wavs = []
    for i, track in enumerate(audio_tracks):
        start_sector = msf_to_sectors(track.index01_msf)
        if i + 1 < len(audio_tracks):
            end_sector = msf_to_sectors(audio_tracks[i + 1].index01_msf)
        else:
            end_sector = total_sectors
        num_sectors = end_sector - start_sector

        wav_path = output_dir / f"track{track.number:02d}.wav"
        with open(bin_path, "rb") as f:
            f.seek(start_sector * SECTOR_SIZE)
            pcm_data = f.read(num_sectors * SECTOR_SIZE)
        with wave.open(str(wav_path), "wb") as w:
            w.setnchannels(PCM_CHANNELS)
            w.setsampwidth(PCM_SAMPWIDTH)
            w.setframerate(PCM_RATE)
            w.writeframes(pcm_data)
        wavs.append(wav_path)
    return wavs


def extract_data_iso(bin_path: Path, cue_text: str,
                     output_path: Path) -> None:
    tracks = parse_cue(cue_text)
    start = msf_to_sectors(tracks[0].index01_msf)
    end = msf_to_sectors(tracks[1].index01_msf)
    with open(bin_path, "rb") as f:
        f.seek(start * SECTOR_SIZE)
        raw = f.read((end - start) * SECTOR_SIZE)
    iso_data = bytearray()
    for offset in range(0, len(raw), SECTOR_SIZE):
        iso_data.extend(raw[offset + MODE1_USER_OFFSET:
                            offset + MODE1_USER_OFFSET + MODE1_USER_SIZE])
    output_path.write_bytes(iso_data)


def encode_format(wav_path: Path, output_path: Path,
                  fmt: str) -> bool:
    if fmt == "mp3":
        tool = shutil.which("lame")
        if not tool:
            return False
        subprocess.run([tool, "--quiet", "-b", "192",
                        str(wav_path), str(output_path)],
                       check=True)
    elif fmt == "flac":
        tool = shutil.which("flac")
        if not tool:
            return False
        subprocess.run([tool, "--silent", "-f", "-o",
                        str(output_path), str(wav_path)],
                       check=True)
    elif fmt == "ogg":
        tool = shutil.which("oggenc")
        if not tool:
            return False
        subprocess.run([tool, "-Q", "-q", "6", "-o",
                        str(output_path), str(wav_path)],
                       check=True)
    else:
        return False
    return True


def write_variant_cue(output_path: Path, fmt: str,
                      data_iso_name: str) -> None:
    filetype = {"wav": "WAVE", "mp3": "MP3", "flac": "FLAC",
                "ogg": "OGG"}[fmt]
    lines = [f'FILE "{data_iso_name}" BINARY',
             "  TRACK 01 MODE1/2048", "    INDEX 01 00:00:00"]
    for i, name in enumerate(TRACK_NAMES, start=2):
        lines.append(f'FILE "{name}.{fmt}" {filetype}')
        lines.append(f"  TRACK {i:02d} AUDIO")
        lines.append("    INDEX 01 00:00:00")
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def validate_audio(wav_path: Path) -> tuple[bool, str]:
    with wave.open(str(wav_path), "rb") as w:
        frames = w.readframes(w.getnframes())
    if len(frames) == 0:
        return False, "empty WAV"
    samples = struct.unpack(f"<{len(frames) // 2}h", frames)
    nonzero = sum(1 for s in samples if abs(s) > SILENCE_THRESHOLD)
    pct = 100.0 * nonzero / len(samples)
    peak = max(abs(s) for s in samples)
    if pct < MIN_NONSILENT_PCT:
        return False, (f"{pct:.1f}% non-silent "
                       f"(need {MIN_NONSILENT_PCT}%), peak={peak}")
    return True, f"{pct:.1f}% non-silent, peak={peak}"


def prepare_test_disc(fixtures_dir: Path,
                      cache_dir: Path) -> dict[str, Path]:
    xz_path = fixtures_dir / "cd-audio-test.bin.xz"
    cue_path = fixtures_dir / "cd-audio-test.cue"
    bin_path = cache_dir / "cd-audio-test.bin"
    stamp = cache_dir / ".prepared"

    if stamp.exists() and stamp.stat().st_mtime >= xz_path.stat().st_mtime:
        return _collect_variants(cache_dir)

    cache_dir.mkdir(parents=True, exist_ok=True)

    cue_text = cue_path.read_text(encoding="utf-8")
    expand_xz(xz_path, bin_path)

    # The master CUE references cd-audio-test.bin by relative path.
    # Copy it into cache_dir so DOSBox finds the expanded .bin there.
    shutil.copy2(cue_path, cache_dir / "cd-audio-test.cue")

    wavs = extract_audio_tracks(bin_path, cue_text, cache_dir)
    for wav_file, name in zip(wavs, TRACK_NAMES):
        wav_file.rename(cache_dir / f"{name}.wav")

    extract_data_iso(bin_path, cue_text,
                     cache_dir / "cd-audio-test-data.iso")

    for fmt in ("mp3", "flac", "ogg"):
        available = True
        for name in TRACK_NAMES:
            src = cache_dir / f"{name}.wav"
            dst = cache_dir / f"{name}.{fmt}"
            if not encode_format(src, dst, fmt):
                available = False
                break
        if available:
            write_variant_cue(cache_dir / f"cd-audio-test-{fmt}.cue",
                              fmt, "cd-audio-test-data.iso")

    write_variant_cue(cache_dir / "cd-audio-test-wav.cue",
                      "wav", "cd-audio-test-data.iso")

    stamp.write_text("ok", encoding="utf-8")
    return _collect_variants(cache_dir)


def _collect_variants(cache_dir: Path) -> dict[str, Path]:
    variants: dict[str, Path] = {}
    master_cue = cache_dir / "cd-audio-test.cue"
    if master_cue.exists():
        variants["master"] = master_cue
    for fmt in ("wav", "mp3", "flac", "ogg"):
        cue = cache_dir / f"cd-audio-test-{fmt}.cue"
        if cue.exists():
            variants[fmt] = cue
    return variants
