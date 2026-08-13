// This file is part of the dosbox-automation Project.
// License: GPL-3.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_DRIVE_SWAP_H
#define DOSBOX_DRIVE_SWAP_H

#include <filesystem>
#include <string>
#include <vector>

// Shared image-swap core for the REST endpoint (POST /api/v1/drive/swap)
// and the Lua API (dosbox.drive_swap). Both callers run on the emulation
// thread; Swap touches Drives[] and must never be called from another one.

namespace DriveSwap {

struct Result {
	bool ok           = false;
	std::string error = {};
};

// Resolve a relative image path against the policy roots: conf anchor
// first, then each allowed image root, first existing hit wins. Absolute
// paths and misses pass through unchanged; validation rejects them later.
std::filesystem::path ResolveImagePath(
        const std::filesystem::path& image_path,
        const std::filesystem::path& conf_anchor,
        const std::vector<std::filesystem::path>& allowed_image_roots);

// mount_locked is a parameter rather than a MountPolicy::IsLocked() call
// so the caller's own latch read is authoritative and tests stay
// independent of the process-wide one-way lock.
Result Swap(char drive_letter, const std::filesystem::path& image_path,
            bool mount_locked, const std::filesystem::path& conf_anchor,
            const std::vector<std::filesystem::path>& allowed_image_roots);

} // namespace DriveSwap

#endif // DOSBOX_DRIVE_SWAP_H
