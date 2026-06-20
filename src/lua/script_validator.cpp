// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/script_validator.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace Lua {

ValidationResult ScriptValidator::ValidateBody(const std::string& body)
{
	if (body.empty()) {
		return {false, 400, "empty script body"};
	}

	if (body.size() > MaxBodySize) {
		return {false, 413, "script body exceeds 256 KB limit"};
	}

	if (body.find('\0') != std::string::npos) {
		return {false, 400, "body contains null bytes"};
	}

	// Count bytes outside printable ASCII and standard whitespace.
	size_t non_text = 0;
	for (const auto c : body) {
		const auto u            = static_cast<unsigned char>(c);
		const bool is_printable = (u >= 0x20 && u <= 0x7E);
		const bool is_whitespace = (u == 0x09 || u == 0x0A || u == 0x0D);
		if (!is_printable && !is_whitespace) {
			++non_text;
		}
	}

	const auto ratio = static_cast<double>(non_text) /
	                   static_cast<double>(body.size());
	if (ratio > MaxBinaryRatio) {
		return {false, 400, "body contains binary content"};
	}

	return {true, 0, {}};
}

ValidationResult ScriptValidator::ValidateContentType(const std::string& content_type)
{
	if (content_type.empty()) {
		return {false, 415, "missing Content-Type header"};
	}

	// Extract media type before any parameters (semicolon).
	auto media_type = content_type;
	const auto semi = media_type.find(';');
	if (semi != std::string::npos) {
		media_type = media_type.substr(0, semi);
	}

	while (!media_type.empty() && media_type.back() == ' ') {
		media_type.pop_back();
	}

	std::transform(media_type.begin(),
	               media_type.end(),
	               media_type.begin(),
	               [](unsigned char c) {
		               return static_cast<char>(std::tolower(c));
	               });

	if (media_type == "text/plain" || media_type == "application/lua") {
		return {true, 0, {}};
	}

	return {false, 415, "unsupported Content-Type: " + content_type};
}

ValidationResult ScriptValidator::ValidateParams(const std::string& name,
                                                 const std::string& seed,
                                                 const std::string& debug,
                                                 ScriptParams& out)
{
	out = ScriptParams{};

	if (!name.empty()) {
		if (name.size() > 64) {
			return {false, 400, "name exceeds 64 character limit"};
		}
		for (const auto c : name) {
			const bool valid = std::isalnum(
			                           static_cast<unsigned char>(c)) ||
			                   c == '-' || c == '_';
			if (!valid) {
				return {false, 400, "name contains invalid character"};
			}
		}
		out.name = name;
	}

	if (!seed.empty()) {
		const char* first = seed.data();
		const char* last  = seed.data() + seed.size();
		auto [ptr, ec]    = std::from_chars(first, last, out.seed);
		if (ec != std::errc{} || ptr != last) {
			return {false, 400, "seed is not a valid integer"};
		}
	}

	if (!debug.empty()) {
		if (debug == "true" || debug == "1") {
			out.debug = true;
		} else if (debug == "false" || debug == "0") {
			out.debug = false;
		} else {
			return {false, 400, "debug must be true or false"};
		}
	}

	return {true, 0, {}};
}

} // namespace Lua
