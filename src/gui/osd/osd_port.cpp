// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "gui/osd/osd_port.h"

#include <string>
#include <vector>

#include "augra/log.h"
#include "dos/programs/menu_session.h"
#include "gui/osd/osd.h"
#include "hardware/port.h"
#include "utils/checks.h"

CHECK_NARROWING();

namespace {

constexpr io_port_t PortCommand = 0x3e0;
constexpr io_port_t PortData    = 0x3e1;

// Read signature so guest code can probe for the interface; an unclaimed
// port reads 0xFF, and so does this one when nothing would answer.
constexpr uint8_t ReadSignature = 0x4f; // 'O'
constexpr uint8_t Unclaimed     = 0xff;

enum class Command : uint8_t {
	ShowText      = 0x01,
	ClearOverlay  = 0x02,
	DiscardBuffer = 0x03,
	ChoiceBegin   = 0x10,
	ChoiceCommit  = 0x11,
	ChoiceCancel  = 0x12,
	MenuDataBegin = 0x13,
	MenuDataEnd   = 0x14,
	// Older UniMenu builds sent this for setup entries; the engine now
	// decides the return from the entry, so it commits like 0x11.
	ChoiceReturn = 0x15,
};

// Overlays shown through this interface carry their own tag, so a new
// show replaces the previous one and clear only affects guest overlays
const std::string OverlayTag = "dos-osd";

// Pending text accumulated byte-wise from the guest. Capped at the
// OSD's own text limit; excess bytes are dropped, not wrapped.
constexpr size_t MaxPendingText = 256;
std::string pending_text        = {};

bool is_registered   = false;
bool overlay_enabled = false;

bool transfer_active = false;

// A choice is one byte, the entry number; anything longer is refused at
// commit, so the buffer only needs room to see the excess.
constexpr size_t MaxChoiceBytes  = 2;
std::vector<uint8_t> menu_choice = {};
bool menu_choice_active          = false;

void commit_menu_choice()
{
	if (!menu_choice_active) {
		return;
	}
	menu_choice_active = false;
	if (menu_choice.size() != 1) {
		augra::log_warn("menu_port",
		                "refused a choice of %zu bytes",
		                menu_choice.size());
		return;
	}
	MenuSession::Choose(menu_choice.front());
}

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

void overlay_command(const Command command)
{
	if (!overlay_enabled) {
		return;
	}
	switch (command) {
	case Command::ShowText: show_pending_text(); break;
	case Command::ClearOverlay:
		OSD::OsdManager::Instance().ClearByTag(OverlayTag);
		break;
	case Command::DiscardBuffer: pending_text.clear(); break;
	default: break;
	}
}

void port_write(const io_port_t port, const io_val_t val, const io_width_t width)
{
	// The protocol is byte-wide; ignore wider accesses
	if (width != io_width_t::byte) {
		return;
	}
	const auto byte = static_cast<uint8_t>(val & 0xff);

	if (port == PortData) {
		if (menu_choice_active) {
			if (menu_choice.size() < MaxChoiceBytes) {
				menu_choice.push_back(byte);
			}
		} else if (overlay_enabled && pending_text.size() < MaxPendingText) {
			pending_text.push_back(static_cast<char>(byte));
		}
		return;
	}

	const auto command = static_cast<Command>(byte);
	switch (command) {
	case Command::ShowText:
	case Command::ClearOverlay:
	case Command::DiscardBuffer: overlay_command(command); break;
	case Command::MenuDataBegin:
		MenuSession::Rewind();
		transfer_active = true;
		break;
	case Command::MenuDataEnd: transfer_active = false; break;
	case Command::ChoiceBegin:
		menu_choice.clear();
		menu_choice_active = MenuSession::IsOpen();
		break;
	case Command::ChoiceCommit:
	case Command::ChoiceReturn: commit_menu_choice(); break;
	case Command::ChoiceCancel:
		menu_choice.clear();
		menu_choice_active = false;
		MenuSession::Close();
		break;
	default: break;
	}
}

io_val_t port_read(const io_port_t port, [[maybe_unused]] const io_width_t width)
{
	if (port == PortData) {
		if (transfer_active) {
			if (const auto byte = MenuSession::NextByte()) {
				return *byte;
			}
		}
		return Unclaimed;
	}

	if (transfer_active) {
		if (MenuSession::Remaining() > 0) {
			return ReadSignature;
		}
		transfer_active = false;
		return static_cast<uint8_t>(Command::MenuDataEnd);
	}
	return (overlay_enabled || MenuSession::IsOpen()) ? ReadSignature : Unclaimed;
}

} // namespace

void OSDPORT_Init()
{
	if (is_registered) {
		return;
	}

	IO_RegisterWriteHandler(PortCommand, port_write, io_width_t::byte, 2);
	IO_RegisterReadHandler(PortCommand, port_read, io_width_t::byte, 2);

	is_registered = true;
}

void OSDPORT_Destroy()
{
	if (is_registered) {
		IO_FreeWriteHandler(PortCommand, io_width_t::byte, 2);
		IO_FreeReadHandler(PortCommand, io_width_t::byte, 2);

		pending_text.clear();
		transfer_active = false;
		menu_choice.clear();
		menu_choice_active = false;
		overlay_enabled    = false;
		MenuSession::Close();
		is_registered = false;
	}
}

void OSDPORT_EnableOverlay(const bool enabled)
{
	overlay_enabled = enabled;
	if (!enabled) {
		pending_text.clear();
	}
}
