// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_DOS_H
#define DOSBOX_WEBSERVER_DOS_H

#include "webserver/bridge.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "http/http.h"

namespace Webserver {

struct McbBlock {
	uint16_t segment     = 0;
	uint8_t type         = 0;
	uint16_t psp_segment = 0;
	uint16_t size_paras  = 0;
	std::string filename = {};
	bool is_last         = false;
};

using McbReader = std::function<McbBlock(uint16_t segment)>;

std::vector<McbBlock> WalkMcbChain(uint16_t start_segment,
                                   const McbReader& reader,
                                   int max_blocks);

// Get pointers to interesting data structures, this command is just to prevent
// breakages if these ever change and users hard-code these offsets. It's not
// a place to pull random info that can also be read by the client from these
// addresses directly.
class DosInternalsCommand : public Command {
	uint16_t list_of_lists      = {};
	uint16_t dos_swappable_area = {};
	uint16_t first_shell        = {};
	std::vector<McbBlock> memory_map = {};

public:
	void Execute() override;
	static void Get(const httplib::Request& req, httplib::Response& res);
};

enum class MemoryArea { Conv, Uma, Xms };

enum class AllocStrategy { FirstFit, BestFit, LastFit };

class AllocMemoryCommand : public Command {
public:
	AllocMemoryCommand(const uint16_t bytes, const MemoryArea area,
	                   const AllocStrategy strategy)
	        : area(area),
	          strategy(strategy),
	          bytes(bytes)

	{}

	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);

private:
	MemoryArea area        = {};
	AllocStrategy strategy = AllocStrategy::BestFit;
	uint32_t addr          = 0;
	uint16_t bytes         = 0;

	void AllocDos();
	void AllocXms();
};

class FreeMemoryCommand : public Command {
public:
	FreeMemoryCommand(const uint32_t addr) : addr(addr) {}

	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);

private:
	uint32_t addr = 0;
	bool success  = false;
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_DOS_H
