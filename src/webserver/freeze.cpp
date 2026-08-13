// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "private/freeze.h"
#include "webserver.h"

#include "hardware/memory.h"

#include "json/json.h"

using json = nlohmann::json;
using httplib::Request, httplib::Response;

namespace Webserver {

FreezeRegistry& FreezeRegistry::Instance()
{
	static FreezeRegistry instance;
	return instance;
}

bool FreezeRegistry::Add(const uint32_t address, const uint32_t value,
                         const int width)
{
	if (width != 1 && width != 2 && width != 4) {
		return false;
	}

	std::lock_guard<std::mutex> lock(mtx);

	for (auto& e : entries) {
		if (e.address == address) {
			e.value = value;
			e.width = width;
			return true;
		}
	}

	if (static_cast<int>(entries.size()) >= MaxEntries) {
		return false;
	}

	entries.push_back({address, value, width});
	return true;
}

bool FreezeRegistry::Remove(const uint32_t address)
{
	std::lock_guard<std::mutex> lock(mtx);
	for (auto it = entries.begin(); it != entries.end(); ++it) {
		if (it->address == address) {
			entries.erase(it);
			return true;
		}
	}
	return false;
}

void FreezeRegistry::Clear()
{
	std::lock_guard<std::mutex> lock(mtx);
	entries.clear();
}

std::vector<FreezeEntry> FreezeRegistry::List() const
{
	std::lock_guard<std::mutex> lock(mtx);
	return entries;
}

void ApplyFreezes()
{
	auto& reg = FreezeRegistry::Instance();
	const auto entries = reg.List();
	for (const auto& e : entries) {
		uint8_t buf[4] = {};
		for (int i = 0; i < e.width; ++i) {
			buf[i] = static_cast<uint8_t>((e.value >> (8 * i)) & 0xFF);
		}
		MEM_BlockWrite(e.address, buf, e.width);
	}
}

void FreezeHandlers::Post(const Request& req, Response& res)
{
	auto j = json::parse(req.body);

	const uint32_t address = j.at("address").get<uint32_t>();
	const uint32_t value   = j.at("value").get<uint32_t>();
	const int width        = j.value("width", 1);

	if (width != 1 && width != 2 && width != 4) {
		res.status = 400;
		json err;
		err["error"] = "width must be 1, 2, or 4";
		send_json(res, err);
		return;
	}

	const uint64_t mem_total = static_cast<uint64_t>(MEM_TotalPages()) *
	                           MemPageSize;
	if (address + width > mem_total) {
		res.status = 400;
		json err;
		err["error"] = "address out of range";
		send_json(res, err);
		return;
	}

	if (!FreezeRegistry::Instance().Add(address, value, width)) {
		res.status = 409;
		json err;
		err["error"] = "freeze limit reached (" +
		               std::to_string(FreezeRegistry::MaxEntries) + ")";
		send_json(res, err);
		return;
	}

	json result;
	result["status"]  = "ok";
	result["address"] = address;
	result["value"]   = value;
	result["width"]   = width;
	send_json(res, result);
}

void FreezeHandlers::Get(const Request&, Response& res)
{
	const auto entries = FreezeRegistry::Instance().List();
	json j = json::array();
	for (const auto& e : entries) {
		json entry;
		entry["address"] = e.address;
		entry["value"]   = e.value;
		entry["width"]   = e.width;
		j.push_back(entry);
	}
	json result;
	result["freezes"] = j;
	result["count"]   = entries.size();
	send_json(res, result);
}

void FreezeHandlers::Delete(const Request& req, Response& res)
{
	if (req.body.empty()) {
		FreezeRegistry::Instance().Clear();
		json result;
		result["status"] = "cleared";
		send_json(res, result);
		return;
	}

	auto j = json::parse(req.body);
	if (j.contains("address")) {
		const uint32_t address = j.at("address").get<uint32_t>();
		if (FreezeRegistry::Instance().Remove(address)) {
			json result;
			result["status"]  = "removed";
			result["address"] = address;
			send_json(res, result);
		} else {
			res.status = 404;
			json err;
			err["error"] = "no freeze at that address";
			send_json(res, err);
		}
	} else {
		FreezeRegistry::Instance().Clear();
		json result;
		result["status"] = "cleared";
		send_json(res, result);
	}
}

} // namespace Webserver
