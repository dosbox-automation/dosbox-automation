// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_FREEZE_H
#define DOSBOX_WEBSERVER_FREEZE_H

#include "webserver/bridge.h"

#include <cstdint>
#include <mutex>
#include <vector>

#include "http/http.h"

namespace Webserver {

struct FreezeEntry {
	uint32_t address = 0;
	uint32_t value   = 0;
	int width        = 1;
};

class FreezeRegistry {
public:
	static constexpr int MaxEntries = 256;

	bool Add(uint32_t address, uint32_t value, int width);
	bool Remove(uint32_t address);
	void Clear();
	std::vector<FreezeEntry> List() const;

	static FreezeRegistry& Instance();

private:
	mutable std::mutex mtx  = {};
	std::vector<FreezeEntry> entries = {};
};

void ApplyFreezes();

struct FreezeHandlers {
	static void Post(const httplib::Request&, httplib::Response&);
	static void Get(const httplib::Request&, httplib::Response&);
	static void Delete(const httplib::Request&, httplib::Response&);
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_FREEZE_H
