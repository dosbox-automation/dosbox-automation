# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
"""Images named inside an archive (design D7): a floppy mounted from a zip
keeps its writes in the sidecar copy across a remount, the copy carries
the member's date, BOOT and the CD loader take members, and the swap API
takes a member path."""

import bz2
import gzip
import io
import lzma
import os
import struct
import time
import zipfile
import zlib
from pathlib import Path

import pytest
from test_archive_mount import sha256, wait_text

STAMP = (1995, 6, 1, 0, 0, 0)


def fat12_floppy(label: bytes) -> bytes:
    """A blank 1.44 MB FAT12 floppy: BPB, two FATs, a root holding only
    the volume label."""
    img = bytearray(1474560)
    boot = bytearray(512)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", boot, 11,
                     512,   # bytes per sector
                     1,     # sectors per cluster
                     1,     # reserved sectors
                     2,     # FATs
                     224,   # root entries
                     2880,  # total sectors
                     0xF0,  # media
                     9,     # sectors per FAT
                     18,    # sectors per track
                     2,     # heads
                     0,     # hidden sectors
                     0)     # total sectors (32-bit)
    boot[510:512] = b"\x55\xAA"
    img[0:512] = boot
    for fat in (1, 10):
        img[fat * 512:fat * 512 + 3] = b"\xF0\xFF\xFF"
    root = 19 * 512
    img[root:root + 11] = label.ljust(11)
    img[root + 11] = 0x08  # a volume label entry
    return bytes(img)


def boot_floppy(message: bytes) -> bytes:
    """A floppy whose boot sector prints message through INT 10h and halts."""
    code = bytes([
        0x31, 0xC0,              # xor ax,ax
        0x8E, 0xD8,              # mov ds,ax
        0xBE, 0x19, 0x7C,        # mov si,7C19h (msg)
        0xAC,                    # .l: lodsb
        0x08, 0xC0,              # or al,al
        0x74, 0x09,              # jz .h
        0xB4, 0x0E,              # mov ah,0Eh
        0xBB, 0x07, 0x00,        # mov bx,7
        0xCD, 0x10,              # int 10h
        0xEB, 0xF2,              # jmp .l
        0xFA,                    # .h: cli
        0xF4,                    # hlt
        0xEB, 0xFD,              # jmp hlt
    ])
    assert len(code) == 0x19
    sector = bytearray(512)
    sector[0:len(code)] = code
    sector[0x19:0x19 + len(message) + 1] = message + b"\0"
    sector[510:512] = b"\x55\xAA"
    img = bytearray(1474560)
    img[0:512] = sector
    return bytes(img)


def minimal_iso() -> bytes:
    """The smallest ISO 9660 volume the ISO drive mounts: a primary volume
    descriptor (ECMA-119 8.4), the terminator and an empty root directory;
    the same bytes as the unit tests' MinimalIso()."""
    sector = 2048
    iso = bytearray(20 * sector)

    def both32(at, value):
        struct.pack_into("<I", iso, at, value)
        struct.pack_into(">I", iso, at + 4, value)

    def dir_record(at, ident):
        iso[at] = 34
        both32(at + 2, 18)
        both32(at + 10, sector)
        iso[at + 25] = 2
        iso[at + 28] = 1
        iso[at + 31] = 1
        iso[at + 32] = 1
        iso[at + 33] = ident

    pvd = 16 * sector
    iso[pvd] = 1
    iso[pvd + 1:pvd + 6] = b"CD001"
    iso[pvd + 6] = 1
    iso[pvd + 40:pvd + 72] = b"TEST".ljust(32)
    both32(pvd + 80, 20)
    iso[pvd + 120] = 1
    iso[pvd + 123] = 1
    iso[pvd + 124] = 1
    iso[pvd + 127] = 1
    iso[pvd + 129] = 8
    iso[pvd + 130] = 8
    dir_record(pvd + 156, 0)
    terminator = 17 * sector
    iso[terminator] = 255
    iso[terminator + 1:terminator + 6] = b"CD001"
    iso[terminator + 6] = 1
    root = 18 * sector
    dir_record(root, 0)
    dir_record(root + 34, 1)
    return bytes(iso)


def build_images_zip(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    members = [
        ("disks/disk1.img", fat12_floppy(b"DISKONE")),
        ("disks/disk2.img", fat12_floppy(b"DISKTWO")),
        ("boot.img", boot_floppy(b"ARCHIVE BOOT OK")),
        ("cd/game.iso", minimal_iso()),
        ("cd/track.bin", minimal_iso()),
        ("cd/game.cue", b'FILE "track.bin" BINARY\r\n'
                        b"  TRACK 01 MODE1/2048\r\n    INDEX 01 00:00:00\r\n"),
    ]
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in members:
            info = zipfile.ZipInfo(name, date_time=STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    return path


def settings_for(tmp_path):
    return {
        "archive_cache_path": str(tmp_path / "cache"),
        "archive_sidecar_path": str(tmp_path / "saves"),
    }


def wait_file(path: Path, timeout=20.0) -> Path:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.is_file():
            return path
        time.sleep(0.2)
    raise AssertionError(f"{path} did not appear")


def wait_cd_listing(path: Path, timeout=10.0) -> bytes:
    """DIR's output on the CD, read from the file it wrote: the screen
    wraps long cache paths across the mount message."""
    wait_file(path)
    deadline = time.monotonic() + timeout
    while b"Volume in drive D" not in path.read_bytes() and time.monotonic() < deadline:
        time.sleep(0.2)
    return path.read_bytes()


def sidecar_copy(tmp_path, name) -> Path:
    dirs = [d for d in (tmp_path / "saves").iterdir() if d.is_dir()]
    assert len(dirs) == 1, dirs
    assert (dirs[0] / ".source-archive.toml").is_file()
    return dirs[0] / ".images" / "DISKS" / name


def test_floppy_writes_survive_a_remount(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    zip_path = build_images_zip(g / "game.zip")
    before = sha256(zip_path)
    member = f"{zip_path}/disks/disk1.img"
    inst = dosbox_e2e(
        autoexec_lines=[
            f"mount c {g}",
            f"mount a {member}",
            "echo hello> a:\\save.txt",
            "mount -u a",
            f"mount a {member}",
            "type a:\\save.txt > c:\\out.txt",
        ],
        conf_dir=g,
        settings=settings_for(tmp_path),
    )
    out = wait_file(g / "OUT.TXT")
    deadline = time.monotonic() + 10
    while out.read_bytes().strip() != b"hello" and time.monotonic() < deadline:
        time.sleep(0.2)
    assert out.read_bytes().strip() == b"hello"
    assert sidecar_copy(tmp_path, "DISK1.IMG").is_file()
    assert sha256(zip_path) == before
    inst.shutdown()


def test_the_copy_carries_the_members_date(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    zip_path = build_images_zip(g / "game.zip")
    inst = dosbox_e2e(
        autoexec_lines=[f"mount a {zip_path}/disks/disk1.img"],
        conf_dir=g,
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "mounted")
    copy = sidecar_copy(tmp_path, "DISK1.IMG")
    expected = int(time.mktime((1995, 6, 1, 0, 0, 0, 0, 0, -1)))
    assert int(copy.stat().st_mtime) == expected
    inst.shutdown()


def test_boot_from_inside_an_archive(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    zip_path = build_images_zip(g / "game.zip")
    before = sha256(zip_path)
    inst = dosbox_e2e(
        autoexec_lines=[f"boot {zip_path}/boot.img"],
        conf_dir=g,
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "ARCHIVE BOOT OK", timeout=20.0)
    assert sha256(zip_path) == before
    inst.shutdown()


def test_cd_from_inside_an_archive(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    zip_path = build_images_zip(g / "game.zip")
    inst = dosbox_e2e(
        autoexec_lines=[
            f"mount c {g}",
            f"mount d {zip_path}/cd/game.cue",
            "dir d:\\ > c:\\cd.txt",
        ],
        conf_dir=g,
        settings=settings_for(tmp_path),
    )
    listing = wait_cd_listing(g / "CD.TXT")
    assert b"Volume in drive D is TEST" in listing, listing
    assert not (tmp_path / "saves").exists()
    inst.shutdown()


def test_a_member_image_is_named_as_typed_when_mounted(dosbox_e2e, tmp_path):
    """ada-pbr6: the mounted line named the cache copy (.../files/N)."""
    g = tmp_path / "g"
    zip_path = build_images_zip(g / "game.zip")
    inst = dosbox_e2e(
        autoexec_lines=[f"mount d {zip_path}/cd/game.iso", "echo MOUNTED-MARK"],
        conf_dir=g,
        settings=settings_for(tmp_path),
    )
    deadline = time.monotonic() + 20.0
    joined = ""
    while time.monotonic() < deadline and "MOUNTED-MARK" not in joined:
        r = inst.client.screen_text()
        assert r.status_code == 200, r.text
        # Raw rows keep a wrapped line contiguous when joined.
        joined = "".join(r.json()["text"].split("\n"))
        time.sleep(0.2)
    assert f"{zip_path}/cd/game.iso mounted as D drive" in joined, joined
    assert "/files/" not in joined, joined
    inst.shutdown()


def test_the_swap_takes_a_member(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    zip_path = build_images_zip(g / "game.zip")
    inst = dosbox_e2e(
        autoexec_lines=[f"mount a {zip_path}/disks/disk1.img"],
        conf_dir=g,
        allowed_image_roots=[g],
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "mounted")
    r = inst.client.type_string("dir a:\n")
    assert r.status_code == 200, r.text
    wait_text(inst.client, "DISKONE")
    r = inst.client.drive_swap("A", "game.zip/disks/disk2.img")
    assert r.status_code == 200, r.text
    assert sidecar_copy(tmp_path, "DISK2.IMG").is_file()
    r = inst.client.type_string("dir a:\n")
    assert r.status_code == 200, r.text
    wait_text(inst.client, "DISKTWO")
    inst.shutdown()


def test_a_swapped_in_disk_takes_the_games_writes(dosbox_e2e, tmp_path):
    """Decision 1a (2026-10-08): the swap keeps the drive's mode, so a disk
    swapped in on a read-write drive takes writes like the first one; the
    FAT path of the swap had opened every disk read-only since 2026-08-01."""
    g = tmp_path / "g"
    zip_path = build_images_zip(g / "game.zip")
    inst = dosbox_e2e(
        autoexec_lines=[f"mount c {g}", f"mount a {zip_path}/disks/disk1.img", "dir a:"],
        conf_dir=g,
        allowed_image_roots=[g],
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "DISKONE")
    r = inst.client.drive_swap("A", "game.zip/disks/disk2.img")
    assert r.status_code == 200, r.text
    r = inst.client.type_string("echo hello> a:\\save.txt\ncopy a:\\save.txt c:\\out.txt\n")
    assert r.status_code == 200, r.text
    out = wait_file(g / "OUT.TXT")
    deadline = time.monotonic() + 10
    while out.read_bytes().strip() != b"hello" and time.monotonic() < deadline:
        time.sleep(0.2)
    assert out.read_bytes().strip() == b"hello"
    assert sidecar_copy(tmp_path, "DISK2.IMG").is_file()
    inst.shutdown()


def test_the_swap_on_a_read_only_drive_makes_no_sidecar(dosbox_e2e, tmp_path):
    """ada-ciru item 4: the drive's -ro holds through a swap, so the second
    disk is served from the cache like the first and no sidecar appears."""
    g = tmp_path / "g"
    zip_path = build_images_zip(g / "game.zip")
    inst = dosbox_e2e(
        autoexec_lines=[f"mount a {zip_path}/disks/disk1.img -ro", "dir a:"],
        conf_dir=g,
        allowed_image_roots=[g],
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "DISKONE")
    r = inst.client.drive_swap("A", "game.zip/disks/disk2.img")
    assert r.status_code == 200, r.text
    r = inst.client.type_string("dir a:\n")
    assert r.status_code == 200, r.text
    wait_text(inst.client, "DISKTWO")
    assert not (tmp_path / "saves").exists(), sorted(
        p.relative_to(tmp_path).as_posix() for p in (tmp_path / "saves").rglob("*"))
    inst.shutdown()


def test_the_swap_back_finds_the_first_disk_with_its_writes(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    zip_path = build_images_zip(g / "game.zip")
    inst = dosbox_e2e(
        autoexec_lines=[
            f"mount c {g}",
            f"mount a {zip_path}/disks/disk1.img",
            "echo hello> a:\\save.txt",
            "dir a:",
        ],
        conf_dir=g,
        allowed_image_roots=[g],
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "SAVE")
    r = inst.client.drive_swap("A", "game.zip/disks/disk2.img")
    assert r.status_code == 200, r.text
    r = inst.client.drive_swap("A", "game.zip/disks/disk1.img")
    assert r.status_code == 200, r.text
    r = inst.client.type_string("copy a:\\save.txt c:\\out.txt\n")
    assert r.status_code == 200, r.text
    out = wait_file(g / "OUT.TXT")
    deadline = time.monotonic() + 10
    while out.read_bytes().strip() != b"hello" and time.monotonic() < deadline:
        time.sleep(0.2)
    assert out.read_bytes().strip() == b"hello"
    inst.shutdown()


# Plan 4b: single-image containers. The ISZ layout follows the EZB
# specification v1.00 plus what UltraISO writes (3-byte pointers, the
# XORed table, "IsZ" over the bzip2 prefix, the 68-byte trailer).
ISZ_KEY = bytes([0xB6, 0x8C, 0xA5, 0xDE])


def isz_of(iso: bytes, kind: str, block: int = 65536) -> bytes:
    count = (len(iso) + block - 1) // block
    table = bytearray()
    data = bytearray()
    for i in range(count):
        chunk = iso[i * block:(i + 1) * block]
        if not any(chunk):
            kind_bits, stored = 0, b""
        elif kind == "zlib":
            kind_bits, stored = 2, zlib.compress(chunk, 9)
        else:
            kind_bits, stored = 3, b"IsZ" + bz2.compress(chunk, 9)[3:]
        table += ((kind_bits << 22) | len(stored)).to_bytes(3, "little")
        data += stored
    table = bytes(b ^ ISZ_KEY[i % 4] for i, b in enumerate(table))
    data_offs = (48 + len(table) + 3) // 4 * 4
    head = bytearray(data_offs)
    head[0:4] = b"IsZ!"
    head[4] = 48
    head[5] = 1
    struct.pack_into("<H", head, 10, 2048)
    struct.pack_into("<I", head, 12, len(iso) // 2048)
    struct.pack_into("<I", head, 25, count)
    struct.pack_into("<I", head, 29, block)
    head[33] = 3
    struct.pack_into("<I", head, 35, 48)
    struct.pack_into("<I", head, 43, data_offs)
    head[48:48 + len(table)] = table
    trailer = bytes(head[:48]) + bytes(16) + (68).to_bytes(4, "little")
    return bytes(head) + bytes(data) + trailer


def write_dated(path: Path, data: bytes) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    stamp = time.mktime(STAMP + (0, 0, -1))
    os.utime(path, (stamp, stamp))
    return path


def imz_of(path: Path, name: str, data: bytes) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as z:
        info = zipfile.ZipInfo(name, date_time=STAMP)
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)
    return path


def lone_copy(tmp_path, name) -> Path:
    dirs = [d for d in (tmp_path / "saves").iterdir() if d.is_dir()]
    assert len(dirs) == 1, dirs
    return dirs[0] / ".images" / name


def test_floppy_from_a_compressed_image_keeps_its_writes(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    image = write_dated(g / "disk1.img.xz", lzma.compress(fat12_floppy(b"DISKONE")))
    before = sha256(image)
    inst = dosbox_e2e(
        autoexec_lines=[
            f"mount c {g}",
            f"mount a {image}",
            "echo hello> a:\\save.txt",
            "mount -u a",
            f"mount a {image}",
            "type a:\\save.txt > c:\\out.txt",
        ],
        conf_dir=g,
        settings=settings_for(tmp_path),
    )
    out = wait_file(g / "OUT.TXT")
    deadline = time.monotonic() + 10
    while out.read_bytes().strip() != b"hello" and time.monotonic() < deadline:
        time.sleep(0.2)
    assert out.read_bytes().strip() == b"hello"
    assert lone_copy(tmp_path, "DISK1.IMG").is_file()
    assert sha256(image) == before
    inst.shutdown()


def test_the_copy_of_a_compressed_image_carries_its_date(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    image = write_dated(g / "disk1.img.xz", lzma.compress(fat12_floppy(b"DISKONE")))
    inst = dosbox_e2e(
        autoexec_lines=[f"mount a {image}"],
        conf_dir=g,
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "mounted")
    copy = lone_copy(tmp_path, "DISK1.IMG")
    assert int(copy.stat().st_mtime) == int(image.stat().st_mtime)
    inst.shutdown()


def test_boot_from_a_compressed_image(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    image = write_dated(g / "boot.img.gz",
                        gzip.compress(boot_floppy(b"ARCHIVE BOOT OK"), mtime=0))
    before = sha256(image)
    inst = dosbox_e2e(
        autoexec_lines=[f"boot {image}"],
        conf_dir=g,
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "ARCHIVE BOOT OK", timeout=20.0)
    assert sha256(image) == before
    inst.shutdown()


@pytest.mark.parametrize("kind", ["zlib", "bzip2"])
def test_an_isz_mounts_as_a_cd(dosbox_e2e, tmp_path, kind):
    g = tmp_path / "g"
    image = write_dated(g / "game.isz", isz_of(minimal_iso(), kind))
    before = sha256(image)
    inst = dosbox_e2e(
        autoexec_lines=[
            f"mount c {g}",
            f"mount d {image}",
            "dir d:\\ > c:\\cd.txt",
        ],
        conf_dir=g,
        settings=settings_for(tmp_path),
    )
    listing = wait_cd_listing(g / "CD.TXT")
    assert b"Volume in drive D is TEST" in listing, listing
    assert not (tmp_path / "saves").exists()
    assert sha256(image) == before
    inst.shutdown()


def test_the_swap_takes_an_imz(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    first = write_dated(g / "disk1.img.xz", lzma.compress(fat12_floppy(b"DISKONE")))
    imz_of(g / "disk2.imz", "DISK2.IMA", fat12_floppy(b"DISKTWO"))
    inst = dosbox_e2e(
        autoexec_lines=[f"mount a {first}"],
        conf_dir=g,
        allowed_image_roots=[g],
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "mounted")
    r = inst.client.type_string("dir a:\n")
    assert r.status_code == 200, r.text
    wait_text(inst.client, "DISKONE")
    r = inst.client.drive_swap("A", "disk2.imz")
    assert r.status_code == 200, r.text
    r = inst.client.type_string("dir a:\n")
    assert r.status_code == 200, r.text
    wait_text(inst.client, "DISKTWO")
    inst.shutdown()


# ---------------------------------------------------------------------------
# Nested archives Phase 4: the ISO level, CD in zip in zip, the consumers
# ---------------------------------------------------------------------------


def iso_with_files(files: dict[str, bytes]) -> bytes:
    """The unit tests' IsoWithFiles(): files at the root or one directory
    down, ECMA-119 level 1 names with ";1", both path tables, the file
    structure version, at least 24 sectors (libarchive's bidder reads
    48 KiB before it looks at sector 16)."""
    sector = 2048
    dirs: dict[str, list[str]] = {"": []}
    for name in files:
        d, _, _ = name.rpartition("/")
        dirs.setdefault(d, []).append(name)
    table_l, table_m = 18, 19
    dir_extent = {d: 20 + i for i, d in enumerate(dirs)}
    nxt = 20 + len(dirs)
    file_extent = {}
    for name, data in files.items():
        file_extent[name] = nxt
        nxt += (len(data) + sector - 1) // sector
    total = max(nxt, 24)
    iso = bytearray(total * sector)

    def both32(at, value):
        struct.pack_into("<I", iso, at, value)
        struct.pack_into(">I", iso, at + 4, value)

    def record(at, ident: bytes, extent, length, is_dir):
        n = 33 + len(ident) + (1 if len(ident) % 2 == 0 else 0)
        iso[at] = n
        both32(at + 2, extent)
        both32(at + 10, length)
        iso[at + 18:at + 21] = bytes([100, 1, 1])
        iso[at + 25] = 2 if is_dir else 0
        iso[at + 28] = 1
        iso[at + 31] = 1
        iso[at + 32] = len(ident)
        iso[at + 33:at + 33 + len(ident)] = ident
        return n

    for d, names in dirs.items():
        at = dir_extent[d] * sector
        at += record(at, b"\x00", dir_extent[d], sector, True)
        at += record(at, b"\x01", dir_extent[""], sector, True)
        if d == "":
            for sub in dirs:
                if sub:
                    at += record(at, sub.upper().encode(), dir_extent[sub], sector, True)
        for name in names:
            leaf = name.rpartition("/")[2].upper().encode() + b";1"
            at += record(at, leaf, file_extent[name], len(files[name]), False)
    for name, data in files.items():
        at = file_extent[name] * sector
        iso[at:at + len(data)] = data
    table_size = 0
    for big, where in ((False, table_l), (True, table_m)):
        at = where * sector
        for d in dirs:
            ident = b"\x00" if d == "" else d.upper().encode()
            iso[at] = len(ident)
            struct.pack_into(">I" if big else "<I", iso, at + 2, dir_extent[d])
            struct.pack_into(">H" if big else "<H", iso, at + 6, 1)
            iso[at + 8:at + 8 + len(ident)] = ident
            at += 8 + len(ident) + (len(ident) % 2)
        table_size = at - where * sector
    pvd = 16 * sector
    iso[pvd] = 1
    iso[pvd + 1:pvd + 6] = b"CD001"
    iso[pvd + 6] = 1
    iso[pvd + 40:pvd + 72] = b"TEST".ljust(32)
    both32(pvd + 80, total)
    iso[pvd + 120] = 1
    iso[pvd + 123] = 1
    iso[pvd + 124] = 1
    iso[pvd + 127] = 1
    iso[pvd + 129] = 8
    iso[pvd + 130] = 8
    both32(pvd + 132, table_size)
    struct.pack_into("<I", iso, pvd + 140, table_l)
    struct.pack_into(">I", iso, pvd + 148, table_m)
    record(pvd + 156, b"\x00", dir_extent[""], sector, True)
    iso[pvd + 881] = 1
    term = 17 * sector
    iso[term] = 255
    iso[term + 1:term + 6] = b"CD001"
    iso[term + 6] = 1
    return bytes(iso)


def zip_bytes(members: dict[str, bytes], stored: bool = True) -> bytes:
    out = io.BytesIO()
    method = zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED
    with zipfile.ZipFile(out, "w", compression=method) as z:
        for name, data in members.items():
            z.writestr(zipfile.ZipInfo(name, date_time=STAMP), data, compress_type=method)
    return out.getvalue()


def test_a_cd_inside_a_zip_inside_a_zip_mounts(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    g.mkdir(parents=True, exist_ok=True)
    inner = zip_bytes({"cd/game.iso": minimal_iso()})
    (g / "outer.zip").write_bytes(zip_bytes({"mid.zip": inner}))
    inst = dosbox_e2e(
        autoexec_lines=[
            f"mount c {g}",
            f"mount d {g / 'outer.zip'}/mid.zip/cd/game.iso",
            "dir d:\\ > c:\\cd.txt",
        ],
        conf_dir=g,
        settings=settings_for(tmp_path),
    )
    listing = wait_cd_listing(g / "CD.TXT")
    assert b"Volume in drive D is TEST" in listing, listing
    assert not (tmp_path / "saves").exists()
    inst.shutdown()


def test_a_floppy_inside_a_zip_inside_an_iso_boots(dosbox_e2e, tmp_path):
    """The trap disc's shape (N2): a CD with a zip on it, the disk inside."""
    g = tmp_path / "g"
    g.mkdir(parents=True, exist_ok=True)
    inner = zip_bytes({"boot.img": boot_floppy(b"ARCHIVE BOOT OK")})
    (g / "disc.iso").write_bytes(iso_with_files({"GAMES/THEXDER.ZIP": inner}))
    inst = dosbox_e2e(
        autoexec_lines=[f"boot {g / 'disc.iso'}/GAMES/THEXDER.ZIP/boot.img"],
        conf_dir=g,
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "ARCHIVE BOOT OK", timeout=20.0)
    inst.shutdown()


def test_the_swap_and_lua_take_a_nested_member(dosbox_e2e, tmp_path):
    g = tmp_path / "g"
    g.mkdir(parents=True, exist_ok=True)
    inner = zip_bytes({"disks/disk1.img": fat12_floppy(b"DISKONE"),
                       "disks/disk2.img": fat12_floppy(b"DISKTWO"),
                       "disks/disk3.img": fat12_floppy(b"DISKTHREE")})
    (g / "outer.zip").write_bytes(zip_bytes({"game.zip": inner}))
    chain = f"{g / 'outer.zip'}/game.zip"
    inst = dosbox_e2e(
        autoexec_lines=[f"mount a {chain}/disks/disk1.img"],
        conf_dir=g,
        allowed_image_roots=[g],
        settings=settings_for(tmp_path),
    )
    wait_text(inst.client, "mounted")
    r = inst.client.drive_swap("A", f"{chain}/disks/disk2.img")
    assert r.status_code == 200, r.text
    assert sidecar_copy(tmp_path, "DISK2.IMG").is_file()
    r = inst.client.type_string("dir a:\n")
    assert r.status_code == 200, r.text
    wait_text(inst.client, "DISKTWO")
    # Lua goes through the same swap with the script's API trust; a long
    # bracket string keeps a Windows path's backslashes out of Lua's escapes.
    r = inst.client.script_load(
        f'dosbox.drive_swap("A", [[{chain}/disks/disk3.img]])', name="nested")
    assert r.status_code == 200, r.text
    assert inst.client.script_start().status_code == 200
    done = inst.client.wait_script_done()
    assert done["state"] == "completed", done
    assert sidecar_copy(tmp_path, "DISK3.IMG").is_file()
    r = inst.client.type_string("dir a:\n")
    assert r.status_code == 200, r.text
    wait_text(inst.client, "DISKTHREE")
    inst.shutdown()
