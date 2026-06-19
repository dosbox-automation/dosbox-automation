// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_PROGRAM_MOUNT_POLICY_H
#define DOSBOX_PROGRAM_MOUNT_POLICY_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

enum class DenyReason {
	None,
	DoesNotResolve,
	NotRegularFile,
	SymlinkComponent,
	SystemPath,
	OutsideWhitelist,
	NotADiskImage,
};

struct MountVerdict {
	bool allowed                   = false;
	DenyReason reason              = DenyReason::None;
	std::filesystem::path resolved = {};
};

enum class MountOrigin { Interactive, Api };

// OwnerTrusted: human at the keyboard, webserver off, no injector possible.
// WhitelistEnforced: webserver on, or autoexec-driven mount.
enum class DirMountPolicy { WhitelistEnforced, OwnerTrusted };

namespace MountPolicy {

// Primitives

std::optional<std::filesystem::path> CanonicalizeExisting(
        const std::filesystem::path& host_path);

bool HasSymlinkComponent(const std::filesystem::path& canonical_path);

bool IsUnderSystemPath(const std::filesystem::path& canonical_path);

bool IsUnderAnyRoot(const std::filesystem::path& canonical_path,
                    const std::vector<std::filesystem::path>& roots);

bool ValidateDiskImageStructure(const std::filesystem::path& host_path);

// Policy entry points

MountVerdict ValidateDirectoryMount(const std::filesystem::path& raw_path,
                                    const std::filesystem::path& conf_anchor,
                                    const std::vector<std::filesystem::path>& allowed_bases,
                                    DirMountPolicy policy);

MountVerdict ValidateImagePath(
        const std::filesystem::path& raw_path, MountOrigin origin,
        const std::vector<std::filesystem::path>& allowed_image_roots);

// One-way latch. Once locked, directory mounts are refused entirely
// and image mounts require the whitelist. Cannot be unlocked.
void Lock();
bool IsLocked();

// Read allowed_bases and allowed_image_roots from the primary config
// file. Called once at startup. Values are read-only after init.
void InitPolicyConfig(const std::filesystem::path& primary_config_path);
const std::vector<std::filesystem::path>& AllowedBases();
const std::vector<std::filesystem::path>& AllowedImageRoots();

struct PolicyPaths {
	std::vector<std::filesystem::path> allowed_bases       = {};
	std::vector<std::filesystem::path> allowed_image_roots = {};
};
PolicyPaths ParsePolicyConfig(const std::filesystem::path& config_path);

} // namespace MountPolicy

#endif // DOSBOX_PROGRAM_MOUNT_POLICY_H
