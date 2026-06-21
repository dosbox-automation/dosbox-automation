// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_LUA_SCRIPT_VALIDATOR_H
#define DOSBOX_LUA_SCRIPT_VALIDATOR_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Lua {

struct ValidationResult {
	bool ok           = false;
	int http_status   = 0;
	std::string error = {};
};

struct ScriptParams {
	std::string name              = "unnamed";
	std::optional<int64_t> seed   = std::nullopt;
	bool debug                    = false;
};

class ScriptValidator {
public:
	static constexpr size_t MaxBodySize    = 256 * 1024;
	static constexpr double MaxBinaryRatio = 0.01;

	static ValidationResult ValidateBody(const std::string& body);
	static ValidationResult ValidateContentType(const std::string& content_type);
	static ValidationResult ValidateParams(const std::string& name,
	                                       const std::string& seed,
	                                       const std::string& debug,
	                                       ScriptParams& out);
};

} // namespace Lua

#endif
