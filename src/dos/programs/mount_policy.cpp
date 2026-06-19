// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "mount_policy.h"

#if defined(WIN32)
#include "mount_policy_windows.h"
#else
#include "mount_policy_linux.h"
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

#include "utils/checks.h"

CHECK_NARROWING();

namespace MountPolicy {

// One-way latch, set from the emulator thread via the Bridge.
// std::atomic because the webserver handler reads it from another thread.
static std::atomic<bool> mount_locked{false};

void Lock()
{
	mount_locked.store(true, std::memory_order_release);
}

bool IsLocked()
{
	return mount_locked.load(std::memory_order_acquire);
}

std::optional<std::filesystem::path> CanonicalizeExisting(const std::filesystem::path& host_path)
{
	auto ec     = std::error_code();
	auto result = std::filesystem::canonical(host_path, ec);
	if (ec) {
		return std::nullopt;
	}
	return result;
}

bool HasSymlinkComponent(const std::filesystem::path& canonical_path)
{
	auto ec          = std::error_code();
	auto accumulated = std::filesystem::path();

	for (const auto& component : canonical_path) {
		accumulated /= component;

		// Root components ("/" or "C:\") are never symlinks
		if (accumulated == canonical_path.root_path()) {
			continue;
		}

		if (std::filesystem::is_symlink(accumulated, ec)) {
			return true;
		}
		if (ec) {
			// Cannot stat a component - treat as hostile
			return true;
		}
	}
	return false;
}

static bool IsBareRoot(const std::filesystem::path& canonical_path)
{
#if defined(WIN32)
	// "C:\", "D:\", etc.
	const auto s = canonical_path.string();
	return (s.size() == 3 && std::isalpha(static_cast<unsigned char>(s[0])) &&
	        s[1] == ':' && (s[2] == '\\' || s[2] == '/'));
#else
	return canonical_path == "/";
#endif
}

bool IsUnderSystemPath(const std::filesystem::path& canonical_path)
{
	if (IsBareRoot(canonical_path)) {
		return true;
	}

	const auto& system_paths = SystemPaths();
	const auto canonical_str = canonical_path.string();

	for (const auto& sys_path : system_paths) {
		const auto sys_str = sys_path.string();

		if (canonical_str == sys_str) {
			return true;
		}

		// Check prefix: canonical must start with sys_path followed
		// by a path separator, so "/etc" blocks "/etc/shadow" but
		// not "/etcetera"
		if (canonical_str.size() > sys_str.size() &&
		    canonical_str.compare(0, sys_str.size(), sys_str) == 0) {
#if defined(WIN32)
			const auto next = canonical_str[sys_str.size()];
			if (next == '\\' || next == '/') {
				return true;
			}
#else
			if (canonical_str[sys_str.size()] == '/') {
				return true;
			}
#endif
		}
	}
	return false;
}

bool IsUnderAnyRoot(const std::filesystem::path& canonical_path,
                    const std::vector<std::filesystem::path>& roots)
{
	const auto canonical_str = canonical_path.string();

	for (const auto& root : roots) {
		auto ec = std::error_code();
		const auto canonical_root = std::filesystem::canonical(root, ec);
		if (ec) {
			continue;
		}

		const auto root_str = canonical_root.string();

		if (canonical_str == root_str) {
			return true;
		}

		if (canonical_str.size() > root_str.size() &&
		    canonical_str.compare(0, root_str.size(), root_str) == 0) {
#if defined(WIN32)
			const auto next = canonical_str[root_str.size()];
			if (next == '\\' || next == '/') {
				return true;
			}
#else
			if (canonical_str[root_str.size()] == '/') {
				return true;
			}
#endif
		}
	}
	return false;
}

// ISO 9660 primary volume descriptor at sector 16 (offset 0x8001)
static constexpr std::array<uint8_t, 5> iso9660_magic = {'C', 'D', '0', '0', '1'};

// FAT boot sector signature at offset 510
static constexpr uint16_t fat_boot_signature = 0xAA55;

// Known floppy image sizes in bytes
static bool IsKnownFloppySize(std::uintmax_t size)
{
	// 360K, 720K, 1.2M, 1.44M, 2.88M
	constexpr std::array<std::uintmax_t, 5> sizes = {
	        368640, 737280, 1228800, 1474560, 2949120};

	for (const auto& s : sizes) {
		if (size == s) {
			return true;
		}
	}
	return false;
}

static bool CheckIso9660(std::ifstream& file)
{
	std::array<uint8_t, 5> buf = {};
	file.seekg(0x8001);
	file.read(reinterpret_cast<char*>(buf.data()), buf.size());
	return file.good() && buf == iso9660_magic;
}

static bool CheckFatBootSector(std::ifstream& file)
{
	std::array<uint8_t, 2> buf = {};
	file.seekg(510);
	file.read(reinterpret_cast<char*>(buf.data()), buf.size());
	if (!file.good()) {
		return false;
	}
	// x86 little-endian: 0x55 at 510, 0xAA at 511
	const auto sig = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
	return sig == fat_boot_signature;
}

static bool CheckCueSheet(const std::filesystem::path& host_path)
{
	std::ifstream file(host_path);
	if (!file.is_open()) {
		return false;
	}

	// A CUE sheet must reference at least one FILE
	std::string line;
	while (std::getline(file, line)) {
		if (line.find("FILE") != std::string::npos) {
			return true;
		}
	}
	return false;
}

bool ValidateDiskImageStructure(const std::filesystem::path& host_path)
{
	auto ec         = std::error_code();
	const auto size = std::filesystem::file_size(host_path, ec);
	if (ec || size == 0) {
		return false;
	}

	// CUE sheets are text, check them separately
	const auto ext = host_path.extension().string();
	if (ext == ".cue" || ext == ".CUE") {
		return CheckCueSheet(host_path);
	}

	// Known floppy sizes pass without magic byte check
	if (IsKnownFloppySize(size)) {
		return true;
	}

	std::ifstream file(host_path, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}

	// Try each known format. Clear stream state between attempts
	// because a failed seek/read on a small file leaves failbit set.
	if (CheckIso9660(file)) {
		return true;
	}
	file.clear();
	if (CheckFatBootSector(file)) {
		return true;
	}

	return false;
}

// -- Policy entry points --

MountVerdict ValidateDirectoryMount(const std::filesystem::path& raw_path,
                                    const std::filesystem::path& conf_anchor,
                                    const std::vector<std::filesystem::path>& allowed_bases,
                                    DirMountPolicy policy)
{
	auto verdict = MountVerdict{};

	const auto canonical = CanonicalizeExisting(raw_path);
	if (!canonical) {
		verdict.reason = DenyReason::DoesNotResolve;
		return verdict;
	}
	verdict.resolved = *canonical;

	if (HasSymlinkComponent(raw_path)) {
		verdict.reason = DenyReason::SymlinkComponent;
		return verdict;
	}

	if (IsUnderSystemPath(*canonical)) {
		verdict.reason = DenyReason::SystemPath;
		return verdict;
	}

	// OwnerTrusted skips the whitelist: human at the keyboard,
	// no webserver, no injector can exist
	if (policy == DirMountPolicy::OwnerTrusted) {
		verdict.allowed = true;
		verdict.reason  = DenyReason::None;
		return verdict;
	}

	// WhitelistEnforced: must be under conf anchor or an allowed base
	auto all_roots = std::vector<std::filesystem::path>{};
	if (!conf_anchor.empty()) {
		all_roots.push_back(conf_anchor);
	}
	all_roots.insert(all_roots.end(), allowed_bases.begin(), allowed_bases.end());

	if (IsUnderAnyRoot(*canonical, all_roots)) {
		verdict.allowed = true;
		verdict.reason  = DenyReason::None;
	} else {
		verdict.reason = DenyReason::OutsideWhitelist;
	}
	return verdict;
}

MountVerdict ValidateImagePath(const std::filesystem::path& raw_path,
                               MountOrigin origin,
                               const std::vector<std::filesystem::path>& allowed_image_roots)
{
	auto verdict = MountVerdict{};

	const auto canonical = CanonicalizeExisting(raw_path);
	if (!canonical) {
		verdict.reason = DenyReason::DoesNotResolve;
		return verdict;
	}
	verdict.resolved = *canonical;

	// Must be a regular file
	auto ec = std::error_code();
	if (!std::filesystem::is_regular_file(*canonical, ec) || ec) {
		verdict.reason = DenyReason::NotRegularFile;
		return verdict;
	}

	if (HasSymlinkComponent(raw_path)) {
		verdict.reason = DenyReason::SymlinkComponent;
		return verdict;
	}

	if (IsUnderSystemPath(*canonical)) {
		verdict.reason = DenyReason::SystemPath;
		return verdict;
	}

	// API origin requires the path to be under an allowed root
	if (origin == MountOrigin::Api && !allowed_image_roots.empty()) {
		if (!IsUnderAnyRoot(*canonical, allowed_image_roots)) {
			verdict.reason = DenyReason::OutsideWhitelist;
			return verdict;
		}
	}

	if (!ValidateDiskImageStructure(*canonical)) {
		verdict.reason = DenyReason::NotADiskImage;
		return verdict;
	}

	verdict.allowed = true;
	verdict.reason  = DenyReason::None;
	return verdict;
}

} // namespace MountPolicy
