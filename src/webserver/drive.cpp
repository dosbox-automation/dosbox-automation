// SPDX-FileCopyrightText: 2026 The DOSBox Staging Team
// SPDX-FileCopyrightText: 2026 dosbox-automation Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "drive.h"
#include "bridge.h"
#include "webserver.h"

#include "dos/dos.h"
#include "dos/drives.h"
#include "dos/programs/mount_policy.h"
#include "ints/bios_disk.h"

#include "libs/json/json.h"

#include <cctype>
#include <filesystem>
#include <sys/stat.h>

using json = nlohmann::json;

namespace Webserver {

DriveSwapCommand::DriveSwapCommand(char drive_letter, std::string image_path)
        : drive_letter(drive_letter),
          image_path(std::move(image_path))
{}

void DriveSwapCommand::Execute()
{
	const auto verdict =
	        MountPolicy::ValidateImagePath(std::filesystem::path(image_path),
	                                       MountOrigin::Api,
	                                       MountPolicy::AllowedImageRoots());
	if (!verdict.allowed) {
		error = "Blocked by mount policy";
		LOG_WARNING("DRIVE-SWAP: Blocked - policy violation");
		return;
	}

	// Use the canonical path from validation, not the raw request string.
	// Mounting the validated object, not re-resolving an untrusted path.
	const auto& resolved = verdict.resolved.string();

	struct stat st = {};
	if (stat(resolved.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		error = "File not found";
		return;
	}

	const auto file_size_kb = static_cast<uint32_t>(st.st_size / 1024);
	bool is_floppy          = false;
	for (const auto& geom : BIOS_GetDiskGeometryList()) {
		if (geom.ksize == file_size_kb) {
			is_floppy = true;
			break;
		}
	}

	const auto drv_idx = static_cast<uint8_t>(
	        std::toupper(static_cast<unsigned char>(drive_letter)) - 'A');

	if (drv_idx >= DOS_DRIVES) {
		error = "Invalid drive letter";
		return;
	}

	Drives[drv_idx].reset();

	// API mounts are read-only by default
	auto new_drive = std::make_shared<fatDrive>(
	        resolved.c_str(), 512, 0, 0, 0, is_floppy ? 0xF0 : 0xF8, true);

	if (!new_drive->created_successfully) {
		error = "Failed to mount image";
		return;
	}

	Drives[drv_idx] = new_drive;

	if (drv_idx < MAX_DISK_IMAGES) {
		imageDiskList[drv_idx] = new_drive->loadedDisk;
	}
}

void DriveSwapCommand::Post(const httplib::Request& req, httplib::Response& res)
{
	auto body = json::parse(req.body);

	if (!body.contains("drive") || !body.contains("image")) {
		res.status = 400;
		json err;
		err["error"] = "Missing 'drive' or 'image' field";
		send_json(res, err);
		return;
	}

	const auto drive_str = body["drive"].get<std::string>();
	if (drive_str.empty() ||
	    !std::isalpha(static_cast<unsigned char>(drive_str[0]))) {
		res.status = 400;
		json err;
		err["error"] = "Invalid drive letter";
		send_json(res, err);
		return;
	}

	DriveSwapCommand cmd(drive_str[0], body["image"].get<std::string>());
	cmd.WaitForCompletion(5000);

	if (!cmd.error.empty()) {
		res.status = 400;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	json result;
	result["status"] = "ok";
	result["drive"]  = std::string(1,
                                      static_cast<char>(std::toupper(
                                              static_cast<unsigned char>(
                                                      drive_str[0]))));
	send_json(res, result);
}

} // namespace Webserver
