// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "private/io_port.h"
#include "webserver.h"

#include "hardware/port.h"

#include "json/json.h"

using json = nlohmann::json;
using httplib::Request, httplib::Response;

namespace Webserver {

void ValidatePortRequest(const uint32_t port, const int width)
{
	if (port > 0xFFFF) {
		throw std::invalid_argument("port must be 0x0000..0xFFFF");
	}
	if (width != 1 && width != 2) {
		throw std::invalid_argument("width must be 1 (byte) or 2 (word)");
	}
}

void PortReadCommand::Execute()
{
	if (width == 2) {
		value = IO_ReadW(static_cast<io_port_t>(port));
	} else {
		value = IO_ReadB(static_cast<io_port_t>(port));
	}
}

void PortReadCommand::Get(const Request& req, Response& res)
{
	const auto port_str  = req.get_param_value("port");
	const auto width_str = req.get_param_value("width");

	if (port_str.empty()) {
		throw std::invalid_argument("missing 'port' query parameter");
	}

	const uint32_t port = static_cast<uint32_t>(std::stoul(port_str, nullptr, 0));
	const int width     = width_str.empty() ? 1 : std::stoi(width_str);

	ValidatePortRequest(port, width);

	PortReadCommand cmd(port, width);
	cmd.WaitForCompletion();
	if (!cmd.error.empty()) {
		throw std::runtime_error(cmd.error);
	}

	json j;
	j["port"]  = port;
	j["width"] = width;
	j["value"] = cmd.Value();
	send_json(res, j);
}

void PortWriteCommand::Execute()
{
	if (width == 2) {
		IO_WriteW(static_cast<io_port_t>(port),
		          static_cast<io_val_t>(value));
	} else {
		IO_WriteB(static_cast<io_port_t>(port),
		          static_cast<io_val_t>(value));
	}
}

void PortWriteCommand::Put(const Request& req, Response& res)
{
	auto j = json::parse(req.body);

	const uint32_t port = j.at("port").get<uint32_t>();
	const int width     = j.value("width", 1);
	const uint32_t val  = j.at("value").get<uint32_t>();

	ValidatePortRequest(port, width);

	PortWriteCommand cmd(port, width, val);
	cmd.WaitForCompletion();
	if (!cmd.error.empty()) {
		throw std::runtime_error(cmd.error);
	}

	json result;
	result["status"] = "ok";
	result["port"]   = port;
	result["width"]  = width;
	result["value"]  = val;
	send_json(res, result);
}

} // namespace Webserver
