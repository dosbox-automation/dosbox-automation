// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "gui/osd/osd_port.h"

#include <string>

#include "gui/osd/osd.h"
#include "hardware/port.h"
#include "utils/checks.h"

CHECK_NARROWING();

namespace {

constexpr io_port_t PortCommand = 0x3e0;
constexpr io_port_t PortData    = 0x3e1;

// Read signature so guest code can probe for the interface; unclaimed
// ports read back 0xFF
constexpr uint8_t ReadSignature = 0x4f; // 'O'

enum class Command : uint8_t {
	ShowText      = 0x01,
	ClearOverlay  = 0x02,
	DiscardBuffer = 0x03,
};

// Overlays shown through this interface carry their own tag, so a new
// show replaces the previous one and clear only affects guest overlays
const std::string OverlayTag = "dos-osd";

// Pending text accumulated byte-wise from the guest. Capped at the
// OSD's own text limit; excess bytes are dropped, not wrapped.
constexpr size_t MaxPendingText = 256;
std::string pending_text        = {};

bool is_registered = false;

void show_pending_text()
{
	// Showing an empty buffer clears instead of leaving a ghost box
	if (pending_text.empty()) {
		OSD::OsdManager::Instance().ClearByTag(OverlayTag);
		return;
	}

	OSD::TextOverlay overlay;
	overlay.text     = pending_text;
	overlay.tag      = OverlayTag;
	overlay.position = OSD::Position::BottomCenter;

	OSD::OsdManager::Instance().ShowText(std::move(overlay));

	pending_text.clear();
}

void port_write(const io_port_t port, const io_val_t val, const io_width_t width)
{
	// The protocol is byte-wide; ignore wider accesses
	if (width != io_width_t::byte) {
		return;
	}
	const auto byte = static_cast<uint8_t>(val & 0xff);

	if (port == PortData) {
		if (pending_text.size() < MaxPendingText) {
			pending_text.push_back(static_cast<char>(byte));
		}
		return;
	}

	switch (static_cast<Command>(byte)) {
	case Command::ShowText: show_pending_text(); break;
	case Command::ClearOverlay:
		OSD::OsdManager::Instance().ClearByTag(OverlayTag);
		break;
	case Command::DiscardBuffer: pending_text.clear(); break;
	default:
		// Unknown commands are ignored; the interface must stay
		// harmless for stray writes
		break;
	}
}

io_val_t port_read([[maybe_unused]] const io_port_t port,
                   [[maybe_unused]] const io_width_t width)
{
	return ReadSignature;
}

} // namespace

void OSDPORT_Init()
{
	if (is_registered) {
		return;
	}

	IO_RegisterWriteHandler(PortCommand, port_write, io_width_t::byte, 2);
	IO_RegisterReadHandler(PortCommand, port_read, io_width_t::byte);

	is_registered = true;
}

void OSDPORT_Destroy()
{
	if (is_registered) {
		IO_FreeWriteHandler(PortCommand, io_width_t::byte, 2);
		IO_FreeReadHandler(PortCommand, io_width_t::byte);

		pending_text.clear();
		is_registered = false;
	}
}
