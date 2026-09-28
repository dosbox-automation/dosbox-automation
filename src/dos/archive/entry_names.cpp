// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/entry_names.h"

#include "dos/dos_system.h"
#include "dos/drives.h"
#include "dos/programs/mount_policy.h"
#include "utils/checks.h"

CHECK_NARROWING();

namespace ArchiveNames {

const char* RejectionText(const Rejection rejection)
{
	switch (rejection) {
	case Rejection::None: return "";
	case Rejection::AbsolutePath: return "absolute path";
	case Rejection::DotComponent: return "'.' or '..' component";
	case Rejection::EmptyComponent: return "empty component";
	case Rejection::ControlCharacter: return "control character in name";
	case Rejection::ComponentTooLong:
		return "component longer than 255 bytes";
	case Rejection::ReservedDeviceName: return "reserved device name";
	}
	return "";
}

static bool is_separator(const char c)
{
	return c == '/' || c == '\\';
}

static std::string trim_trailing_dots_and_spaces(const std::string_view component)
{
	const auto end = component.find_last_not_of(". ");
	if (end == std::string_view::npos) {
		return {};
	}
	return std::string(component.substr(0, end + 1));
}

std::optional<SplitPath> SplitEntryPath(const std::string_view utf8_path,
                                        Rejection* rejection)
{
	const auto reject = [&](const Rejection why) {
		if (rejection) {
			*rejection = why;
		}
		return std::optional<SplitPath>{};
	};
	if (rejection) {
		*rejection = Rejection::None;
	}
	if (utf8_path.empty()) {
		return reject(Rejection::EmptyComponent);
	}
	if (is_separator(utf8_path.front())) {
		return reject(Rejection::AbsolutePath);
	}
	if (utf8_path.size() >= 2 && utf8_path[1] == ':') {
		return reject(Rejection::AbsolutePath);
	}

	auto result   = SplitPath{};
	result.is_dir = is_separator(utf8_path.back());

	size_t start = 0;
	while (start <= utf8_path.size()) {
		auto sep = start;
		while (sep < utf8_path.size() && !is_separator(utf8_path[sep])) {
			++sep;
		}
		const auto raw = utf8_path.substr(start, sep - start);
		if (raw.empty()) {
			if (sep == utf8_path.size() && result.is_dir) {
				break;
			}
			return reject(Rejection::EmptyComponent);
		}
		for (const auto c : raw) {
			const auto byte = static_cast<unsigned char>(c);
			if (byte < 0x20 || byte == 0x7f) {
				return reject(Rejection::ControlCharacter);
			}
		}
		if (raw.size() > static_cast<size_t>(MaxComponentBytes)) {
			return reject(Rejection::ComponentTooLong);
		}
		if (raw == "..") {
			return reject(Rejection::DotComponent);
		}
		// tar -C dir . names every member ./x; the component says nothing
		if (raw == ".") {
			if (sep == utf8_path.size()) {
				result.is_dir = true;
				break;
			}
			start = sep + 1;
			continue;
		}
		const auto trimmed = trim_trailing_dots_and_spaces(raw);
		if (trimmed.empty()) {
			return reject(Rejection::EmptyComponent);
		}
		if (MountPolicy::IsWindowsReservedDeviceName(trimmed)) {
			return reject(Rejection::ReservedDeviceName);
		}
		result.components.push_back(trimmed);
		if (sep == utf8_path.size()) {
			break;
		}
		start = sep + 1;
	}
	// No components left: the archive's root itself ("." or "./")
	if (result.components.empty() && !result.is_dir) {
		return reject(Rejection::EmptyComponent);
	}
	// a drive letter behind a dropped "." is still a drive letter
	if (!result.components.empty() && result.components.front().size() >= 2 &&
	    result.components.front()[1] == ':') {
		return reject(Rejection::AbsolutePath);
	}
	return result;
}

// Archive DOS names stay ASCII: the sidecar's localDrive on Windows and
// the union must agree on every name, and CP437 high bytes in host file
// names misbehave on every platform. Each non-ASCII code point folds to _.
static std::string ascii_only(const std::string_view utf8)
{
	auto out = std::string();
	size_t i = 0;
	while (i < utf8.size()) {
		const auto lead = static_cast<unsigned char>(utf8[i]);
		if (lead < 0x80) {
			out.push_back(static_cast<char>(lead));
			++i;
			continue;
		}
		// A stray continuation or an invalid lead is one character, not
		// the start of a sequence that would swallow the next letter.
		size_t length = (lead >= 0xf0 && lead <= 0xf4) ? 4
		              : (lead >= 0xe0)                 ? 3
		              : (lead >= 0xc2)                 ? 2
		                                               : 1;
		for (size_t k = 1; k < length; ++k) {
			const auto next = i + k < utf8.size()
			                        ? static_cast<unsigned char>(
			                                  utf8[i + k])
			                        : 0u;
			if (next < 0x80 || next > 0xbf) {
				length = 1;
				break;
			}
		}
		out.push_back('_');
		i += length;
	}
	return out;
}

std::string FoldComponent(const std::string_view utf8_component,
                          const unsigned int number)
{
	const auto ascii = ascii_only(utf8_component);
	const auto basis = sfn_clean_basis(ascii.c_str());
	if (number == 1 && !basis.not_8x3) {
		auto name = std::string(basis.name);
		if (basis.ext[0] != '\0') {
			name += '.';
			name += basis.ext;
		}
		return name;
	}
	return generate_8x3(ascii.c_str(), number);
}

std::optional<std::string> DosNameTable::Assign(const std::string& parent_dos_path,
                                                const std::string_view utf8_component)
{
	auto& taken = names_per_dir[parent_dos_path];
	for (auto number = 1u; number <= MaxNumberedNames; ++number) {
		const auto name = FoldComponent(utf8_component, number);
		if (name.empty()) {
			return {};
		}
		if (taken.contains(name)) {
			continue;
		}
		auto dos_path = parent_dos_path.empty()
		                      ? name
		                      : parent_dos_path + "\\" + name;
		// DOS_MakeName wants "\\" + path under DOS_PATHLENGTH, so 78 is
		// the longest path DOS itself can open.
		if (dos_path.size() > DOS_PATHLENGTH - 2) {
			return {};
		}
		taken.insert(name);
		return dos_path;
	}
	return {};
}

bool DosNameTable::Contains(const std::string& dos_path) const
{
	const auto sep    = dos_path.find_last_of('\\');
	const auto parent = (sep == std::string::npos) ? std::string()
	                                               : dos_path.substr(0, sep);
	const auto name   = (sep == std::string::npos) ? dos_path
	                                               : dos_path.substr(sep + 1);
	const auto it     = names_per_dir.find(parent);
	return it != names_per_dir.end() && it->second.contains(name);
}

} // namespace ArchiveNames
