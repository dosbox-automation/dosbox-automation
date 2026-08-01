// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/drive_swap.h"

#include "dos/dos.h"
#include "dos/drives.h"
#include "dos/programs/mount_policy.h"
#include "ints/bios_disk.h"
#include "misc/logging.h"
#include "utils/checks.h"

#include <cctype>
#include <memory>
#include <sys/stat.h>

CHECK_NARROWING();

namespace DriveSwap {

std::filesystem::path ResolveImagePath(const std::filesystem::path& image_path,
                                       const std::filesystem::path& conf_anchor,
                                       const std::vector<std::filesystem::path>& allowed_image_roots)
{
	if (image_path.is_absolute()) {
		return image_path;
	}

	auto candidates = std::vector<std::filesystem::path>{};
	if (!conf_anchor.empty()) {
		candidates.push_back(conf_anchor / image_path);
	}
	for (const auto& root : allowed_image_roots) {
		candidates.push_back(root / image_path);
	}

	for (const auto& candidate : candidates) {
		auto ec = std::error_code();
		if (std::filesystem::exists(candidate, ec) && !ec) {
			return candidate;
		}
	}

	return image_path;
}

Result Swap(char drive_letter, const std::filesystem::path& image_path,
            bool mount_locked, const std::filesystem::path& conf_anchor,
            const std::vector<std::filesystem::path>& allowed_image_roots)
{
	auto result = Result{};

	// Once mounts are locked, the configuration is frozen for everyone,
	// the guest commands (BOOT, IMGMOUNT, MOUNT) and the API alike.
	if (mount_locked) {
		result.error = "mount is locked";
		LOG_WARNING("DRIVE-SWAP: Blocked - locked");
		return result;
	}

	if (!std::isalpha(static_cast<unsigned char>(drive_letter))) {
		result.error = "Invalid drive letter";
		return result;
	}

	const auto drv_idx = static_cast<uint8_t>(
	        std::toupper(static_cast<unsigned char>(drive_letter)) - 'A');

	if (drv_idx >= DOS_DRIVES) {
		result.error = "Invalid drive letter";
		return result;
	}

	// Both callers carry API-origin trust: the REST request arrives over
	// HTTP and the Lua script was loaded through the same webserver.
	const auto resolved_input = ResolveImagePath(image_path,
	                                             conf_anchor,
	                                             allowed_image_roots);

	const auto verdict = MountPolicy::ValidateImagePath(resolved_input,
	                                                    MountOrigin::Api,
	                                                    allowed_image_roots,
	                                                    conf_anchor);
	if (!verdict.allowed) {
		result.error = "Blocked by mount policy";
		LOG_WARNING("DRIVE-SWAP: Blocked - policy violation");
		return result;
	}

	// Use the canonical path from validation, not the raw request string.
	// Mounting the validated object, not re-resolving an untrusted path.
	const auto& resolved = verdict.resolved.string();

	struct stat st = {};
	if (stat(resolved.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		result.error = "File not found";
		return result;
	}

	const auto file_size_kb = static_cast<uint32_t>(st.st_size / 1024);
	bool is_floppy          = false;
	for (const auto& geom : BIOS_GetDiskGeometryList()) {
		if (geom.ksize == file_size_kb) {
			is_floppy = true;
			break;
		}
	}

	// Build the new drive before releasing the old one so a
	// construction failure does not leave the slot empty.
	auto new_drive = std::make_shared<fatDrive>(
	        resolved.c_str(), 512, 0, 0, 0, is_floppy ? 0xF0 : 0xF8, true);

	if (!new_drive->created_successfully) {
		result.error = "Failed to mount image";
		return result;
	}

	Drives[drv_idx].reset();

	Drives[drv_idx] = new_drive;

	if (drv_idx < MAX_DISK_IMAGES) {
		imageDiskList[drv_idx] = new_drive->loadedDisk;
	}

	result.ok = true;
	return result;
}

} // namespace DriveSwap
