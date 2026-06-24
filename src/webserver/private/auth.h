// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_AUTH_H
#define DOSBOX_WEBSERVER_AUTH_H

#include <cstdint>
#include <string>

namespace Webserver {

// Compares every byte regardless of where mismatches occur, so the
// duration leaks nothing about the position of the first difference.
// The early length check is fine: the token length is public.
inline bool ConstantTimeEquals(const std::string& a, const std::string& b)
{
	if (a.size() != b.size()) {
		return false;
	}
	volatile uint8_t diff = 0;
	for (size_t i = 0; i < a.size(); ++i) {
		diff |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
	}
	return diff == 0;
}

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_AUTH_H
