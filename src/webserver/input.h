// SPDX-FileCopyrightText: 2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_WEBSERVER_INPUT_H
#define DOSBOX_WEBSERVER_INPUT_H

#include "bridge.h"
#include "libs/http/http.h"

#include <string>
#include <vector>

namespace Webserver {

struct InputEvent {
	double t_ms = 0;
	enum class Type { Key, MouseMove, MouseButton, MouseWheel } type = Type::Key;

	// Key params
	int key = 0;
	bool pressed = false;

	// Mouse move params
	float x_rel = 0, y_rel = 0;
	float x_abs = 0, y_abs = 0;

	// Mouse button params
	std::string button = {};

	// Mouse wheel params
	float wheel_delta = 0;
};

class InputSequenceCommand : public Command {
public:
	InputSequenceCommand(std::vector<InputEvent> events);
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);

private:
	std::vector<InputEvent> events = {};
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_INPUT_H
