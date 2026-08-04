<!-- This file is part of the dosbox-automation Project. -->
<!-- License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net -->
<!-- Generated from compatibility-matrix.toml - do not hand-edit. -->

# dosbox-automation Compatibility Matrix

Mount and boot verification of real-world media against the engine.
First-pass results are headless (offscreen video, no audio); the
playable tier requires a real window and is assessed separately.
Test media is not distributed and is not part of the repository.

## Games

| Name | Year | Media | Tier | Verified | Install | Engine | Tested | Notes |
|---|---|---|---|---|---|---|---|---|
| Archon | 1983 | PC booter, 200K 10-sector floppy | boots | boots | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Boots via boot <image> -l a to the title screen. Raw booter carries no FAT, so MOUNT refuses it; direct boot is the era-correct path. |
| Archon Ultra | 1994 | 1.44M floppy set (installer) | mounts | mounts reads vol | manual-needed | 0.84.0-da3+7e49a7956 | 2026-08-04 | Disk 1 mounts and reads, volume label ARCHV1_0D1. |
| Bruce Lee | 1984 | directory (cracked DOS build) | mounts | mounts reads | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Directory mount as C:. The PCjr booter original from staging issue #2039 was not available as a raw image for this pass. |
| Discworld | 1995 | CD image (CUE) and directory-as-CD | mounts | mounts reads vol | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Both shapes mount with MSCDEX: the CUE/IMG rip and a game directory mounted with -t cdrom (the regression shape from staging issue #4831). |
| Hard Hat Mack | 1984 | PC booter, 200K 10-sector floppy | boots | boots | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Boots to the title screen via direct boot. No FAT on the disk, MOUNT refuses by design. |
| King's Quest | 1984 | PC booter, 360K floppy | boots | mounts reads boots | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Boots to the title credits with the composite-mode hint. The disk is DOS-formatted and mounts too (empty volume label). |
| M.U.L.E. | 1983 | PC booter, 180K floppy | boots | boots | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Both dumps tested boot to the title screen. This is the game from dosbox-staging issue #1289. MOUNT is refused: the disk's FAT media descriptor claims two sides while the 180K geometry is single-sided, and the geometry sanity check fires. |
| Microsoft Flight Simulator 2.12 (Tandy) | 1985 | Tandy-format booter floppy | boots | mounts reads boots | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Boots to the display-selection screen. Mounts with an empty volume label (the warning from staging issue #4986). Running FS.COM from the mounted disk hangs - upstream #4986 inherited, tracked internally. |
| Murder on the Zinderneuf | 1983 | PC booter, 400K 10-sector floppy | boots | mounts reads vol boots | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Boots to the title screen and also mounts fully (EA MINI-DOS volume label). A 400K disk exercising the 10-sector geometry row through the whole mount path. |
| Pinball Construction Set | 1983 | PC booter, 400K 10-sector floppy | boots | mounts reads vol boots | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Boots to the Bill Budge title screen and mounts fully (PCS-IBM volume label). |
| Red Baron | 1990 | 1.44M floppy set (installer) | mounts | mounts reads vol | manual-needed | 0.84.0-da3+7e49a7956 | 2026-08-04 | Both disks mount; the volume labels with embedded spaces (RBDISK 1, RBDISK 2) read intact - the label class from staging issue #4743. |
| Seven Cities of Gold | 1984 | PC booter, 400K + 320K floppies | boots | boots | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Boots and draws its title in composite CGA artifact colors (verified with machine=cga and composite=on; on RGB/VGA machines the output is vertical stripes). Neither disk carries a FAT, MOUNT refuses both. |
| Ultima III: Exodus | 1984 | 160K floppy (flux-level dump) | nothing |  | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Only 86F/MFM flux-level images were available (copy protection preserved); raw-sector emulators cannot mount these formats. Not an engine defect. A raw 160K dump is still wanted for the pre-DOS-2.0 row. |
| Ultima IV: Quest of the Avatar | 1987 | 720K floppy | mounts | mounts reads | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | The cracked 720K image mounts and reads. |
| Ultima Underworld: The Stygian Abyss | 1992 | 1.2M floppy set (installer) | mounts | mounts reads vol | manual-needed | 0.84.0-da3+7e49a7956 | 2026-08-04 | Disk 1 of the 1.2M set mounts and reads, volume label UNDERWORLD1. |
| Wizardry: Proving Grounds of the Mad Overlord | 1984 | PC booter, 2x 320K floppies | boots | boots | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Boots to the title screen on machine=cga. On VGA and Hercules machines the p-System loader stops at 'Can't find system disk' - the 1984 loader is machine-sensitive, run it on CGA. MOUNT refused (no FAT, p-System filesystem). |

## Applications

| Name | Year | Media | Tier | Verified | Install | Engine | Tested | Notes |
|---|---|---|---|---|---|---|---|---|
| Borland Pascal 7.0 | 1992 | 11x 1.44M + 720K bonus floppy | mounts | mounts reads vol | manual-needed | 0.84.0-da3+7e49a7956 | 2026-08-04 | Install disk 1 and the 720K bonus disk mount. IDE and DPMI exercises are a later pass. |
| Norton Utilities 6.01 | 1991 | 2.88M ED floppy | mounts | mounts reads vol | manual-needed | 0.84.0-da3+7e49a7956 | 2026-08-04 | A 2949120-byte ED image mounts and reads (63 files), volume label NORTON. |
| OS/2 2.11 / Warp 4.52 (install diskettes) | 1993 | 1.44M floppy | mounts | mounts reads vol | manual-needed | 0.84.0-da3+7e49a7956 | 2026-08-04 | Boot diskettes of both releases mount and read (70 and 45 files, spaced volume labels intact). Booting OS/2 itself is out of scope for a DOS emulator. |
| PC DOS 7 | 1995 | XDF 1.84M floppy set | mounts | mounts reads vol | manual-needed | 0.84.0-da3+7e49a7956 | 2026-08-04 | Disk 2 (1884160 bytes, IBM XDF as a flat logical dump) mounts and reads. Hardware-level XDF dumps with mixed sector sizes are a different story; logical images work. |
| Windows 95 (upgrade, floppy edition) | 1995 | DMF 1.68M floppy set | mounts | mounts reads vol | manual-needed | 0.84.0-da3+7e49a7956 | 2026-08-04 | Disk 2 (1720320 bytes, Microsoft DMF) mounts and reads, volume label DISKETTE2. Disk 1 of the set is standard 1.44M. |

## Control media

| Name | Year | Media | Tier | Verified | Install | Engine | Tested | Notes |
|---|---|---|---|---|---|---|---|---|
| Boot test disk (house) |  | 1.44M booter floppy | boots | boots | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Hand-written boot sector control disk; boots to its marker screen. Proves the direct-boot path independent of game media. |
| Synthetic hdd image (control) |  | 33.5M hdd image | boots | mounts reads boots | n/a | 0.84.0-da3+7e49a7956 | 2026-08-04 | Auto-detected as a hard disk without -t, readable, and boot -l c executes its boot sector (marker verified). Also mounts with explicit -size geometry. |
