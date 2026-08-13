// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_IO_PORT_H
#define DOSBOX_WEBSERVER_IO_PORT_H

#include "webserver/bridge.h"

#include <cstdint>
#include <stdexcept>

#include "http/http.h"

namespace Webserver {

void ValidatePortRequest(uint32_t port, int width);

class PortReadCommand : public Command {
public:
	PortReadCommand(uint32_t port, int width)
	        : port(port), width(width)
	{}

	void Execute() override;
	static void Get(const httplib::Request& req, httplib::Response& res);

	uint32_t Value() const { return value; }

private:
	uint32_t port  = 0;
	int width      = 1;
	uint32_t value = 0;
};

class PortWriteCommand : public Command {
public:
	PortWriteCommand(uint32_t port, int width, uint32_t value)
	        : port(port), width(width), value(value)
	{}

	void Execute() override;
	static void Put(const httplib::Request& req, httplib::Response& res);

private:
	uint32_t port  = 0;
	int width      = 1;
	uint32_t value = 0;
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_IO_PORT_H
