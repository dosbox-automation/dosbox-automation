<!-- This file is part of the dosbox-automation Project.
     License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net -->

# RAR fixtures for the archive reader tests

libarchive cannot write RAR, and RAR 7.x cannot write RAR4 archives any
more (the `-ma4` switch is gone), so the RAR4 fixtures come from
libarchive's own test suite and only the RAR5 one was made here.

Files from libarchive (https://github.com/libarchive/libarchive,
`libarchive/test/`, BSD-2-Clause, decoded from their `.uu` form with
the commit time of the source file kept as the mtime):

| Here | Upstream file | Content |
|---|---|---|
| plain-v4.rar | test_read_format_rar_compress_normal.rar | non-solid RAR4, two files, two directories, one symlink |
| solid-v4.rar | test_read_format_rar4_solid_encrypted.rar | solid and encrypted RAR4; the solid flag is what the test is for |
| encrypted-v4.rar | test_read_format_rar4_encrypted.rar | RAR4 with encrypted data, plain headers |
| encrypted-headers-v4.rar | test_read_format_rar_encryption_header.rar | RAR4 with encrypted headers |
| multi-v4.part1.rar | test_read_format_rar_multivolume.part0001.rar | first volume of four; the other three are not needed for the refusal |
| encrypted-v5.rar | test_read_format_rar5_encrypted.rar | RAR5 with encrypted data, plain headers |
| encrypted-headers-v5.rar | test_read_format_rar5_encrypted_filenames.rar | RAR5 with encrypted headers |

Made here, synthetic content (3000 bytes of `x` as GAME.EXE, 500 bytes
of `l` as data/level1.dat, `hello` as readme.txt, all dated 1990-01-01):

| Here | Tool | Command |
|---|---|---|
| plain-v5.rar | RAR 7.23 x64 on the Windows builder, 2026-09-27 | `rar a -ma5 -m3 plain-v5.rar GAME.EXE data readme.txt` |

No solid RAR4 without encryption exists in the house; the solid flag
is read from the main header before libarchive opens the file, so the
combined fixture exercises that check.
