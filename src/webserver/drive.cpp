// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "drive.h"
#include "bridge.h"
#include "webserver.h"

#include "dos/drive_swap.h"
#include "dos/programs/mount_policy.h"

#include "libs/json/json.h"

#include <cctype>
#include <filesystem>

using json = nlohmann::json;

namespace Webserver {

DriveSwapCommand::DriveSwapCommand(char drive_letter, std::string image_path)
        : drive_letter(drive_letter),
          image_path(std::move(image_path))
{}

void DriveSwapCommand::Execute()
{
	// The Post handler checks the lock latch early for a clean 403, but
	// it can flip between that check and this one; the read passed here,
	// on the emulation thread where the swap happens, is authoritative.
	const auto result = DriveSwap::Swap(drive_letter,
	                                    std::filesystem::path(image_path),
	                                    MountPolicy::IsLocked(),
	                                    MountPolicy::ConfAnchor(),
	                                    MountPolicy::AllowedImageRoots());
	if (!result.ok) {
		error = result.error;
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

	if (MountPolicy::IsLocked()) {
		res.status = 403;
		json err;
		err["error"] = "mount is locked";
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
