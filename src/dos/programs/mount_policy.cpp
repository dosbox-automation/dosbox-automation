// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
// Mount security policy - what paths MOUNT and drive-swap can reach.
//
// Two modes, selected by whether the webserver is running:
//
//   OwnerTrusted       (webserver off)
//     Human at the keyboard. Any path is allowed except system
//     directories (/etc, /proc, C:\Windows, etc). No whitelist.
//
//   WhitelistEnforced  (webserver on)
//     The REST API is an attack surface. Paths must be under one of:
//       1. The conf anchor - parent directory of the last -conf file
//          loaded on the command line. If DOSBox was started with
//          -conf /home/user/games/test.conf, the anchor is
//          /home/user/games/ and everything below it is reachable.
//       2. mount_allowed_bases - extra directory roots listed in the
//          [webserver] section of the primary config file.
//       3. mount_allowed_image_roots - same, but for image files
//          (floppies, CDs). Read from the primary config too.
//     Anything outside these roots is blocked, even from autoexec.
//     System directories are always blocked in both modes.
//
// The primary config is at $HOME/.config/dosbox-automation/ (Linux),
// ~/Library/Preferences/DOSBox/ (macOS), or %LOCALAPPDATA% (Windows).
// It is NOT the -conf file; it is the persistent user config.
//
// Common "why is MOUNT blocked?" situations:
//   - Temp directory (/tmp, %TEMP%) is outside conf anchor and not
//     in mount_allowed_bases. Put files under the conf anchor or add
//     the path to mount_allowed_bases in the primary config.
//   - Webserver enabled but no -conf file passed. Conf anchor is
//     empty, so only mount_allowed_bases paths work.
//   - Symlinks in the path. The policy rejects any path that has a
//     symlink component, even if the target is under an allowed root.
//
// IMGMOUNT is deprecated. Use MOUNT for all media types.
//

#include "mount_policy.h"

#if defined(WIN32)
#include "mount_policy_windows.h"
#else
#include "mount_policy_linux.h"
#endif

#include "dosbox.h"
#include "misc/support.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include "utils/checks.h"
#include "utils/env_utils.h"

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

#if defined(WIN32)
bool IsDeviceNamespacePath(const std::string& path)
{
	if (path.size() < 4) {
		return false;
	}
	return (path[0] == '\\' && path[1] == '\\' &&
	        (path[2] == '.' || path[2] == '?') && path[3] == '\\');
}
#endif

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

// Case-insensitive prefix check for path strings. On Windows,
// C:\Windows and c:\windows are the same path; byte-exact comparison
// can defeat the system-path denylist via case mismatch.
static bool PathStartsWith(const std::string& path, const std::string& prefix)
{
	if (path.size() < prefix.size()) {
		return false;
	}
#if defined(WIN32)
	for (size_t i = 0; i < prefix.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(path[i])) !=
		    std::tolower(static_cast<unsigned char>(prefix[i]))) {
			return false;
		}
	}
	return true;
#else
	return path.compare(0, prefix.size(), prefix) == 0;
#endif
}

static bool PathEquals(const std::string& a, const std::string& b)
{
#if defined(WIN32)
	if (a.size() != b.size()) {
		return false;
	}
	return PathStartsWith(a, b);
#else
	return a == b;
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

		if (PathEquals(canonical_str, sys_str)) {
			return true;
		}

		// Check prefix: canonical must start with sys_path followed
		// by a path separator, so "/etc" blocks "/etc/shadow" but
		// not "/etcetera"
		if (canonical_str.size() > sys_str.size() &&
		    PathStartsWith(canonical_str, sys_str)) {
			const auto next = canonical_str[sys_str.size()];
#if defined(WIN32)
			if (next == '\\' || next == '/') {
				return true;
			}
#else
			if (next == '/') {
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

	// Roots are expected to be canonical already (from ParsePathList
	// or from CanonicalizeExisting at the call site).
	for (const auto& root : roots) {
		const auto root_str = root.string();

		if (PathEquals(canonical_str, root_str)) {
			return true;
		}

		if (canonical_str.size() > root_str.size() &&
		    PathStartsWith(canonical_str, root_str)) {
			const auto next = canonical_str[root_str.size()];
#if defined(WIN32)
			if (next == '\\' || next == '/') {
				return true;
			}
#else
			if (next == '/') {
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

	// A CUE sheet must have a FILE directive at the start of a line
	std::string line;
	while (std::getline(file, line)) {
		auto pos = line.find_first_not_of(" \t");
		if (pos != std::string::npos && line.compare(pos, 4, "FILE") == 0) {
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

#if defined(WIN32)
	if (IsDeviceNamespacePath(raw_path.string())) {
		verdict.reason = DenyReason::SystemPath;
		return verdict;
	}
#endif

	const auto canonical = CanonicalizeExisting(raw_path);
	if (!canonical) {
		verdict.reason = DenyReason::DoesNotResolve;
		return verdict;
	}
	verdict.resolved = *canonical;

	// Check the original path, not the canonical result: canonical
	// already resolved through any symlinks, so it would miss them.
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

#if defined(WIN32)
	if (IsDeviceNamespacePath(raw_path.string())) {
		verdict.reason = DenyReason::SystemPath;
		return verdict;
	}
#endif

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

	// Check the original path, not the canonical result
	if (HasSymlinkComponent(raw_path)) {
		verdict.reason = DenyReason::SymlinkComponent;
		return verdict;
	}

	if (IsUnderSystemPath(*canonical)) {
		verdict.reason = DenyReason::SystemPath;
		return verdict;
	}

	// API origin requires the path to be under an allowed root.
	// Empty list = no roots configured = deny all API image mounts.
	if (origin == MountOrigin::Api) {
		if (allowed_image_roots.empty() ||
		    !IsUnderAnyRoot(*canonical, allowed_image_roots)) {
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

// -- Config plumbing: primary-config-only reader --

static PolicyPaths policy_paths = {};
static bool policy_initialized  = false;

// Only these env vars are expanded in policy paths
static std::string ExpandPolicyEnvVars(const std::string& input)
{
	auto result = input;
	struct EnvMapping {
		const char* token;
		const char* var;
	};
	// Longest token first so $XDG_DATA_HOME matches before $HOME
	constexpr EnvMapping mappings[] = {
	        {"$XDG_DATA_HOME", "XDG_DATA_HOME"},
	        { "%USERPROFILE%",   "USERPROFILE"},
	        {         "$HOME",          "HOME"},
	};
	for (const auto& m : mappings) {
		auto pos = result.find(m.token);
		if (pos != std::string::npos) {
			const auto val = get_env_var(m.var);
			if (val.empty()) {
				return {};
			}
			result.replace(pos, std::strlen(m.token), val);
		}
	}
	return result;
}

static std::string TrimWhitespace(const std::string& s)
{
	auto start = s.find_first_not_of(" \t");
	if (start == std::string::npos) {
		return {};
	}
	auto end = s.find_last_not_of(" \t");
	return s.substr(start, end - start + 1);
}

static void ParsePathList(const std::string& val,
                          std::vector<std::filesystem::path>& out, int max_entries)
{
	auto stream = std::istringstream(val);
	auto entry  = std::string();
	while (std::getline(stream, entry, ';')) {
		entry = TrimWhitespace(entry);
		if (entry.empty()) {
			continue;
		}

		auto expanded = ExpandPolicyEnvVars(entry);
		if (expanded.empty()) {
			LOG_WARNING("MOUNT_POLICY: Dropped '%s' - env var unset",
			            entry.c_str());
			continue;
		}

		auto ec        = std::error_code();
		auto canonical = std::filesystem::canonical(expanded, ec);
		if (ec) {
			LOG_WARNING("MOUNT_POLICY: Dropped '%s' - does not resolve",
			            expanded.c_str());
			continue;
		}

		if (static_cast<int>(out.size()) >= max_entries) {
			LOG_WARNING("MOUNT_POLICY: Ignoring '%s' - max %d entries",
			            expanded.c_str(),
			            max_entries);
			continue;
		}
		out.push_back(canonical);
	}
}

PolicyPaths ParsePolicyConfig(const std::filesystem::path& config_path)
{
	constexpr int max_entries = 3;
	auto result               = PolicyPaths{};

	auto file = std::ifstream(config_path);
	if (!file.is_open()) {
		return result;
	}

	auto in_webserver_section = false;
	auto line                 = std::string();

	while (std::getline(file, line)) {
		line = TrimWhitespace(line);
		if (line.empty() || line[0] == '#') {
			continue;
		}

		if (line[0] == '[') {
			in_webserver_section = (line.find("[webserver]") == 0);
			continue;
		}

		if (!in_webserver_section) {
			continue;
		}

		auto eq = line.find('=');
		if (eq == std::string::npos) {
			continue;
		}

		auto key = TrimWhitespace(line.substr(0, eq));
		auto val = TrimWhitespace(line.substr(eq + 1));
		if (val.empty()) {
			continue;
		}

		if (key == "mount_allowed_bases") {
			ParsePathList(val, result.allowed_bases, max_entries);
		} else if (key == "mount_allowed_image_roots") {
			ParsePathList(val, result.allowed_image_roots, max_entries);
		}
	}
	return result;
}

void InitPolicyConfig(const std::filesystem::path& primary_config_path)
{
	policy_paths       = ParsePolicyConfig(primary_config_path);
	policy_initialized = true;

	// Bundled drives/[c] auto-mount at startup (autoexec.cpp) through the
	// same MOUNT path as guest/API mounts. First-party shipped content,
	// same trust level as the conf anchor.
	const auto drives_root = get_resource_path("drives");
	if (!drives_root.empty()) {
		std::error_code ec;
		const auto canonical = std::filesystem::canonical(drives_root, ec);
		if (!ec) {
			policy_paths.allowed_bases.push_back(canonical);
		}
	}

	for (const auto& p : policy_paths.allowed_bases) {
		LOG_MSG("MOUNT_POLICY: Allowed base: %s", p.string().c_str());
	}
	for (const auto& p : policy_paths.allowed_image_roots) {
		LOG_MSG("MOUNT_POLICY: Allowed image root: %s", p.string().c_str());
	}
}

const std::vector<std::filesystem::path>& AllowedBases()
{
	assert(policy_initialized);
	return policy_paths.allowed_bases;
}

const std::vector<std::filesystem::path>& AllowedImageRoots()
{
	assert(policy_initialized);
	return policy_paths.allowed_image_roots;
}

} // namespace MountPolicy
