// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "desktop_integration.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "config/config.h"
#include "config/setup.h"
#include "dosbox.h"
#include "misc/cross.h"
#include "misc/logging.h"
#include "misc/support.h"

static constexpr std::string_view DesktopFileName =
        "org.dosbox_automation.dosbox_automation.desktop";

static constexpr std::string_view IconBaseName =
        "org.dosbox_automation.dosbox_automation";

struct IconSize {
	int size;
	const char* filename;
};

static constexpr std::array IconSizes = {
        IconSize{16, "icon_16.png"},   IconSize{22, "icon_22.png"},
        IconSize{24, "icon_24.png"},   IconSize{32, "icon_32.png"},
        IconSize{48, "icon_48.png"},   IconSize{96, "icon_96.png"},
        IconSize{128, "icon_128.png"}, IconSize{256, "icon_256.png"},
        IconSize{512, "icon_512.png"},
};

static bool IsGnomeWaylandSession()
{
	const auto* session_type = std::getenv("XDG_SESSION_TYPE");
	if (!session_type || std::string_view(session_type) != "wayland") {
		return false;
	}

	const auto* desktop = std::getenv("XDG_CURRENT_DESKTOP");
	if (!desktop) {
		return false;
	}

	// XDG_CURRENT_DESKTOP can be colon-separated (e.g. "ubuntu:GNOME")
	const auto desktop_str = std::string(desktop);
	return desktop_str.find("GNOME") != std::string::npos;
}

static std_fs::path GetXdgDataHome()
{
	const auto* xdg = std::getenv("XDG_DATA_HOME");
	if (xdg && *xdg) {
		return std_fs::path(xdg);
	}

	const auto* home = std::getenv("HOME");
	if (home && *home) {
		return std_fs::path(home) / ".local" / "share";
	}

	return {};
}

static std_fs::path GetDesktopFilePath()
{
	const auto data_home = GetXdgDataHome();
	if (data_home.empty()) {
		return {};
	}
	return data_home / "applications" / DesktopFileName;
}

static bool DesktopFileIsInstalled()
{
	const auto path = GetDesktopFilePath();
	if (path.empty()) {
		return true; // cannot determine, skip the dialog
	}
	return std_fs::exists(path);
}

static std_fs::path GetExecutablePath()
{
	std::error_code ec;
	auto path = std_fs::read_symlink("/proc/self/exe", ec);
	if (ec) {
		return "dosbox";
	}
	return path;
}

static bool WriteDesktopFile(const std_fs::path& dest)
{
	std::error_code ec;
	std_fs::create_directories(dest.parent_path(), ec);
	if (ec) {
		return false;
	}

	std::ofstream out(dest);
	if (!out) {
		return false;
	}

	const auto exec_path = GetExecutablePath();

	out << "[Desktop Entry]\n"
	    << "Name=dosbox-automation\n"
	    << "GenericName=DOS emulator\n"
	    << "Comment=DOS emulator with an HTTP automation API and Lua scripting\n"
	    << "Exec=" << exec_path.string() << "\n"
	    << "Icon=" << IconBaseName << "\n"
	    << "Type=Application\n"
	    << "Terminal=false\n"
	    << "Keywords=dos;gaming;game;games;emulator;automation;\n"
	    << "Categories=Game;Emulator;\n";

	return out.good();
}

static bool InstallIcons(const std_fs::path& data_home)
{
	bool all_ok = true;

	for (const auto& [size, filename] : IconSizes) {
		const auto src = get_resource_path("icons/png", filename);
		if (!std_fs::exists(src)) {
			continue;
		}

		const auto size_str = std::to_string(size) + "x" +
		                      std::to_string(size);
		const auto dest_dir = data_home / "icons" / "hicolor" / size_str /
		                      "apps";

		std::error_code ec;
		std_fs::create_directories(dest_dir, ec);
		if (ec) {
			all_ok = false;
			continue;
		}

		const auto dest = dest_dir /
		                  (std::string(IconBaseName) + ".png");
		std_fs::copy_file(src, dest,
		                  std_fs::copy_options::overwrite_existing, ec);
		if (ec) {
			all_ok = false;
		}
	}

	const auto svg_src = get_resource_path("icons/svg",
	                                       "dosbox-automation.svg");
	if (std_fs::exists(svg_src)) {
		const auto svg_dir = data_home / "icons" / "hicolor" / "scalable" /
		                     "apps";
		std::error_code ec;
		std_fs::create_directories(svg_dir, ec);
		if (!ec) {
			const auto svg_dest = svg_dir /
			                     (std::string(IconBaseName) + ".svg");
			std_fs::copy_file(svg_src, svg_dest,
			                  std_fs::copy_options::overwrite_existing,
			                  ec);
			if (ec) {
				all_ok = false;
			}
		}
	}

	return all_ok;
}

static void DisableDialog()
{
	set_section_property_value("sdl", "ask_for_appicon_installation",
	                           "false");
	control->WriteConfig(get_primary_config_path());
}

enum DialogChoice { Install = 0, NotNow = 1, NeverAsk = 2 };

static DialogChoice ShowInstallDialog()
{
	const SDL_MessageBoxButtonData buttons[] = {
	        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, Install, "Install"},
	        {0, NotNow, "Not now"},
	        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, NeverAsk,
	         "Never ask again"},
	};

	const SDL_MessageBoxData data = {
	        SDL_MESSAGEBOX_INFORMATION,
	        nullptr,
	        "GNOME dock icon",
	        "GNOME does not show the application icon in the dock or "
	        "task switcher unless a matching desktop file is installed.\n\n"
	        "Install the dosbox-automation icon and desktop file into\n"
	        "~/.local/share/ so the dock shows the correct icon?",
	        3,
	        buttons,
	        nullptr,
	};

	int button_id = NotNow;
	SDL_ShowMessageBox(&data, &button_id);
	return static_cast<DialogChoice>(button_id);
}

static void ShowSuccessPopup()
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
	                         "Icon installed",
	                         "The desktop file and icons were installed to\n"
	                         "~/.local/share/. The dock icon should appear\n"
	                         "after restarting dosbox-automation.\n\n"
	                         "To remove:\n"
	                         "  rm ~/.local/share/applications/"
	                         "org.dosbox_automation.dosbox_automation.desktop\n"
	                         "  rm -r ~/.local/share/icons/hicolor/*/apps/"
	                         "org.dosbox_automation.*",
	                         nullptr);
}

static void ShowReenablePopup()
{
	SDL_ShowSimpleMessageBox(
	        SDL_MESSAGEBOX_INFORMATION,
	        "Dialog disabled",
	        "This dialog will not appear again.\n\n"
	        "To re-enable it, set this in your dosbox-automation.conf:\n"
	        "  [sdl]\n"
	        "  ask_for_appicon_installation = true\n\n"
	        "or delete the line entirely.",
	        nullptr);
}

void MaybeOfferAppIconInstallation()
{
#ifndef __linux__
	return;
#endif

	auto* section = static_cast<SectionProp*>(
	        control->GetSection("sdl"));
	if (!section) {
		return;
	}

	if (!section->GetBool("ask_for_appicon_installation")) {
		return;
	}

	if (!IsGnomeWaylandSession()) {
		return;
	}

	if (DesktopFileIsInstalled()) {
		return;
	}

	const auto choice = ShowInstallDialog();

	if (choice == NeverAsk) {
		DisableDialog();
		ShowReenablePopup();
		return;
	}

	if (choice != Install) {
		return;
	}

	const auto data_home = GetXdgDataHome();
	if (data_home.empty()) {
		LOG_WARNING("DESKTOP: Cannot determine XDG_DATA_HOME");
		return;
	}

	const auto desktop_path = GetDesktopFilePath();
	const bool desktop_ok   = WriteDesktopFile(desktop_path);
	const bool icons_ok     = InstallIcons(data_home);

	if (desktop_ok && icons_ok) {
		LOG_MSG("DESKTOP: Installed desktop file and icons to %s",
		        data_home.string().c_str());
		ShowSuccessPopup();
	} else {
		const auto msg = "Failed to install some files to " +
		                 data_home.string() +
		                 ". Check directory permissions.";
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
		                         "Installation failed",
		                         msg.c_str(),
		                         nullptr);
	}
}
