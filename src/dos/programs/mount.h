// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 dosbox-automation contributors

#ifndef DOSBOX_PROGRAM_MOUNT_H
#define DOSBOX_PROGRAM_MOUNT_H

#include "dos/dos.h"
#include "dos/programs.h"
#include "dos/archive/archive_management.h"
#include "dos/archive/member_path.h"
#include "dos/programs/mount_policy.h"
#include "shell/command_line.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ArchiveImages {
class Hold;
}

enum class MountType {
	FloppyImage,
	HardDiskImage,
	CdRomImage,
	Directory,
	Overlay,
	Archive
};

enum class MountFileSystemType { Fat16, Iso, None };

// Struct to hold all parameters required for a mount operation
struct MountParameters {
	char drive                 = '\0';
	MountType type             = MountType::Directory;
	MountFileSystemType fstype = MountFileSystemType::Fat16;

	std::vector<std::string> paths = {};

	std::string label = "";

	// Geometry: [0]=BytesPerSector, [1]=Sectors, [2]=Heads, [3]=Cylinders
	std::array<uint16_t, 4> sizes = {0, 0, 0, 0};

	bool roflag               = false;
	bool is_ide               = false;
	int8_t ide_index          = -1;
	bool is_second_cable_slot = false;

	// Defaulting to Hard Disk prevents obscure issues in games like Hyperspace
	uint8_t mediaid = MediaId::HardDisk;

	// 0-3 vs A-Z
	bool is_drive_number = false;

	bool is_image_mode = false;

	// Raw geometry switch values, consumed before path collection so
	// leftover switch tokens cannot land in paths and fail the fork's
	// image path validation. Applied by ParseGeometry.
	std::string freesize_arg = "";
	std::string size_arg     = "";
	std::string chs_arg      = "";

	// Archive mounts (archive mount design D10, D1)
	std::string archive_root = ""; // DOS path inside the archive, "" =
	                               // D10's rule
	bool no_sidecar = false;       // D1: no sidecar, read-only
	std::string sidecar_path = ""; // -sidecar PATH, else D2 decides
	bool scratch = false; // -scratch: writes into a cache scratch, gone at unmount
	// The scratch MOUNT made for a member mount, until the drive registers.
	std::optional<ArchiveMount::ArchiveCache::Scratch> scratch_made = {};
	// Design D7: host file of each image named inside an archive, with
	// what keeps it valid until the mount registers it.
	std::map<std::string, std::shared_ptr<ArchiveImages::Hold>> member_holds = {};
	// ada-pbr6: a refusal names the first path as typed, and a mounted line
	// names each image member by its archive, not the cache copy.
	std::string shown_path                          = "";
	std::map<std::string, std::string> member_shown = {};
	// Design D8: -run with its optional name, -exit to leave after it.
	std::optional<std::string> run = {};
	bool run_exit                  = false;
};

// What a path argument is written in: the guest's code page, or a host
// path the emulator has already mapped (SUBST, ada-jyw6).
enum class PathText { Dos, Host };

class MOUNT final : public Program {
public:
	MOUNT()
	{
		AddMessages();
		help_detail = {HELP_Filter::Common,
		               HELP_Category::Dosbox,
		               HELP_CmdType::Program,
		               "MOUNT"};
	}
	void Run() override;

	// A mount line built inside the emulator (SUBST), with Run()'s guard.
	void RunLine(std::unique_ptr<CommandLine> line, PathText path_text);

	// The host path a DOS path on a local drive names: its raw bytes as the
	// drive lists them first, then G13's code page reading; "" when neither
	// exists.
	static std::string HostPathOfDosPath(const std::string& dos_path);

	std::optional<MountParameters> ProcessArguments(
	        CommandLine* cmd, PathText path_text = PathText::Dos);

	// Archive mounts (archive mount design, Phase 2c)
	bool HandleReconnect();
	// Design 6.1: MOUNT -cache shows the store, -cache trim runs the
	// cleanup now. True when the line was a cache command.
	bool HandleCache();
	static std::string CacheOverviewText(const ArchiveManagement::CacheOverview& overview);
	// The -cache text for the engine's settings: the path check first.
	static std::string CacheOverviewForSettings();
	// What -cache trim removed; nothing when the store cannot be opened.
	static std::optional<ArchiveMount::ArchiveCache::CleanupReport> TrimCache(
	        std::string* detail);
	bool MountArchive(MountParameters& params);
	// The engine's one cache store is opened on the first archive
	// mount; the tests close it between cases.
	static void ResetArchiveCacheForTests();
	// Opens the archive cache from the [dos] settings and runs its cleanup
	// (design D14), at engine start, before any mount.
	static void ArchiveCacheStartup();
	static void ReportMountDenied(const std::string& path,
	                              const MountVerdict& verdict,
	                              const std::string& detail = {});
	// Owner-trusted without the webserver, whitelist-enforced with it.
	static DirMountPolicy CurrentDirPolicy();
	// Drops the in-process sidecar key and lock of a drive being unmounted;
	// a no-op for other drives (3.2).
	static void ReleaseSidecar(char drive_letter);
	// Phase 6.4: the scratch a -scratch mount writes into, kept per drive
	// letter and discarded by ReleaseSidecar.
	static void KeepScratch(char drive_letter,
	                        ArchiveMount::ArchiveCache::Scratch&& scratch);
	// The scratch of this MOUNT line, made once; false with the reason set.
	static bool EnsureScratch(MountParameters& params, std::string* why);
	// PROGRAM_MOUNT_ARCHIVE_UNSUPPORTED's text; the unit tests link a
	// message stub, so they read it here.
	static const char* const ArchiveUnsupportedMessage;
	// The refusals that name a path, by message id, read by the tests too.
	static const std::vector<std::pair<std::string, std::string>>& RefusalTexts();

private:
	static void AddMessages();
	void ShowUsage();

	void ListMounts();

	bool HandleUnmount();

	bool AddWildcardPaths(const std::string& path_arg,
	                      std::vector<std::string>& paths);

	enum class OptionValue { None, Optional, Required };

	struct MountOption {
		bool present                     = false;
		std::optional<std::string> value = {};
	};

	std::optional<MountOption> TakeOption(const std::string& name,
	                                      OptionValue value_kind);

	bool HasUnknownOptions();

	void QueueRun(const MountParameters& params);

	bool ParseArguments(MountParameters& params, bool& explicit_fs,
	                    bool& path_relative_to_last_config);

	bool ParseGeometry(MountParameters& params);
	bool ParseDrive(MountParameters& params, bool explicit_fs);

	std::string ApplyRelativePath(const std::string& path,
	                              bool is_relative_to_last_config) const;

	static std::string GetDosMappedHostPath(const std::string& dos_path);

	std::string HostText(const std::string& arg) const;

	std::string ResolveHostPath(const std::string& arg,
	                            bool relative_to_last_config) const;

	void ProcessPaths(const std::string first_path, MountParameters& params,
	                  bool path_relative_to_last_config);

	bool AddArchiveMember(const ArchiveMount::MemberPath& member,
	                      const std::string& path_arg,
	                      const std::string& shown, MountParameters& params);
	bool MountPaths(MountParameters& params);

	void MountLocal(MountParameters& params, const std::string& local_path);

	bool MountImage(MountParameters& params);

	bool MountImageFat(MountParameters& params);
	bool MountImageIso(const MountParameters& params);
	bool MountImageRaw(MountParameters& params);

	void WriteMountStatus(const std::string& image_type,
	                      const std::vector<std::string>& images,
	                      char drive_letter, bool readonly);

	PathText path_text = PathText::Dos;
};

#endif // DOSBOX_PROGRAM_MOUNT_H
