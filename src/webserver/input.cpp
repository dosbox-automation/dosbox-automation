// SPDX-FileCopyrightText: 2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "input.h"
#include "bridge.h"
#include "webserver.h"

#include "hardware/input/keyboard.h"
#include "hardware/input/mouse.h"
#include "hardware/pic.h"

#include "libs/json/json.h"

#include <unordered_map>
#include <queue>
#include <mutex>

using json = nlohmann::json;

namespace Webserver {

static const std::unordered_map<std::string, KBD_KEYS> key_name_map = {
	{"KBD_NONE", KBD_NONE},
	{"KBD_1", KBD_1}, {"KBD_2", KBD_2}, {"KBD_3", KBD_3},
	{"KBD_4", KBD_4}, {"KBD_5", KBD_5}, {"KBD_6", KBD_6},
	{"KBD_7", KBD_7}, {"KBD_8", KBD_8}, {"KBD_9", KBD_9}, {"KBD_0", KBD_0},
	{"KBD_q", KBD_q}, {"KBD_w", KBD_w}, {"KBD_e", KBD_e}, {"KBD_r", KBD_r},
	{"KBD_t", KBD_t}, {"KBD_y", KBD_y}, {"KBD_u", KBD_u}, {"KBD_i", KBD_i},
	{"KBD_o", KBD_o}, {"KBD_p", KBD_p},
	{"KBD_a", KBD_a}, {"KBD_s", KBD_s}, {"KBD_d", KBD_d}, {"KBD_f", KBD_f},
	{"KBD_g", KBD_g}, {"KBD_h", KBD_h}, {"KBD_j", KBD_j}, {"KBD_k", KBD_k},
	{"KBD_l", KBD_l},
	{"KBD_z", KBD_z}, {"KBD_x", KBD_x}, {"KBD_c", KBD_c}, {"KBD_v", KBD_v},
	{"KBD_b", KBD_b}, {"KBD_n", KBD_n}, {"KBD_m", KBD_m},
	{"KBD_f1", KBD_f1}, {"KBD_f2", KBD_f2}, {"KBD_f3", KBD_f3},
	{"KBD_f4", KBD_f4}, {"KBD_f5", KBD_f5}, {"KBD_f6", KBD_f6},
	{"KBD_f7", KBD_f7}, {"KBD_f8", KBD_f8}, {"KBD_f9", KBD_f9},
	{"KBD_f10", KBD_f10}, {"KBD_f11", KBD_f11}, {"KBD_f12", KBD_f12},
	{"KBD_esc", KBD_esc}, {"KBD_tab", KBD_tab},
	{"KBD_backspace", KBD_backspace}, {"KBD_enter", KBD_enter},
	{"KBD_space", KBD_space},
	{"KBD_leftalt", KBD_leftalt}, {"KBD_rightalt", KBD_rightalt},
	{"KBD_leftctrl", KBD_leftctrl}, {"KBD_rightctrl", KBD_rightctrl},
	{"KBD_leftgui", KBD_leftgui}, {"KBD_rightgui", KBD_rightgui},
	{"KBD_leftshift", KBD_leftshift}, {"KBD_rightshift", KBD_rightshift},
	{"KBD_capslock", KBD_capslock}, {"KBD_scrolllock", KBD_scrolllock},
	{"KBD_numlock", KBD_numlock},
	{"KBD_grave", KBD_grave}, {"KBD_minus", KBD_minus},
	{"KBD_equals", KBD_equals}, {"KBD_backslash", KBD_backslash},
	{"KBD_leftbracket", KBD_leftbracket}, {"KBD_rightbracket", KBD_rightbracket},
	{"KBD_semicolon", KBD_semicolon}, {"KBD_quote", KBD_quote},
	{"KBD_oem102", KBD_oem102},
	{"KBD_period", KBD_period}, {"KBD_comma", KBD_comma},
	{"KBD_slash", KBD_slash}, {"KBD_abnt1", KBD_abnt1},
	{"KBD_printscreen", KBD_printscreen}, {"KBD_pause", KBD_pause},
	{"KBD_insert", KBD_insert}, {"KBD_home", KBD_home},
	{"KBD_pageup", KBD_pageup}, {"KBD_delete", KBD_delete},
	{"KBD_end", KBD_end}, {"KBD_pagedown", KBD_pagedown},
	{"KBD_left", KBD_left}, {"KBD_up", KBD_up},
	{"KBD_down", KBD_down}, {"KBD_right", KBD_right},
	{"KBD_kp1", KBD_kp1}, {"KBD_kp2", KBD_kp2}, {"KBD_kp3", KBD_kp3},
	{"KBD_kp4", KBD_kp4}, {"KBD_kp5", KBD_kp5}, {"KBD_kp6", KBD_kp6},
	{"KBD_kp7", KBD_kp7}, {"KBD_kp8", KBD_kp8}, {"KBD_kp9", KBD_kp9},
	{"KBD_kp0", KBD_kp0},
	{"KBD_kpdivide", KBD_kpdivide}, {"KBD_kpmultiply", KBD_kpmultiply},
	{"KBD_kpminus", KBD_kpminus}, {"KBD_kpplus", KBD_kpplus},
	{"KBD_kpenter", KBD_kpenter}, {"KBD_kpperiod", KBD_kpperiod},
};

static const std::unordered_map<std::string, MouseButtonId> button_name_map = {
	{"left", MouseButtonId::Left},
	{"right", MouseButtonId::Right},
	{"middle", MouseButtonId::Middle},
};

static std::mutex pending_mutex;
static std::queue<InputEvent> pending_events;

static void pic_input_handler(uint32_t)
{
	std::lock_guard<std::mutex> lock(pending_mutex);
	if (pending_events.empty()) {
		return;
	}
	auto ev = pending_events.front();
	pending_events.pop();

	switch (ev.type) {
	case InputEvent::Type::Key:
		KEYBOARD_AddKey(static_cast<KBD_KEYS>(ev.key), ev.pressed);
		break;
	case InputEvent::Type::MouseMove:
		MOUSE_EventMoved(ev.x_rel, ev.y_rel, ev.x_abs, ev.y_abs);
		break;
	case InputEvent::Type::MouseButton: {
		auto it = button_name_map.find(ev.button);
		if (it != button_name_map.end()) {
			MOUSE_EventButton(it->second, ev.pressed);
		}
		break;
	}
	case InputEvent::Type::MouseWheel:
		MOUSE_EventWheel(ev.wheel_delta);
		break;
	}
}

InputSequenceCommand::InputSequenceCommand(std::vector<InputEvent> events)
        : events(std::move(events))
{
}

void InputSequenceCommand::Execute()
{
	for (auto& ev : events) {
		if (ev.t_ms <= 0) {
			switch (ev.type) {
			case InputEvent::Type::Key:
				KEYBOARD_AddKey(static_cast<KBD_KEYS>(ev.key), ev.pressed);
				break;
			case InputEvent::Type::MouseMove:
				MOUSE_EventMoved(ev.x_rel, ev.y_rel, ev.x_abs, ev.y_abs);
				break;
			case InputEvent::Type::MouseButton: {
				auto it = button_name_map.find(ev.button);
				if (it != button_name_map.end()) {
					MOUSE_EventButton(it->second, ev.pressed);
				}
				break;
			}
			case InputEvent::Type::MouseWheel:
				MOUSE_EventWheel(ev.wheel_delta);
				break;
			}
		} else {
			std::lock_guard<std::mutex> lock(pending_mutex);
			pending_events.push(ev);
			PIC_AddEvent(pic_input_handler, ev.t_ms);
		}
	}
}

void InputSequenceCommand::Post(const httplib::Request& req, httplib::Response& res)
{
	auto body = json::parse(req.body);

	if (!body.contains("events") || !body["events"].is_array()) {
		res.status = 400;
		json err;
		err["error"] = "Missing or invalid 'events' array";
		send_json(res, err);
		return;
	}

	std::vector<InputEvent> events;
	for (const auto& jev : body["events"]) {
		InputEvent ev = {};

		if (jev.contains("t")) {
			ev.t_ms = jev["t"].get<double>();
		}

		const auto type_str = jev.value("type", "key");

		if (type_str == "key") {
			ev.type = InputEvent::Type::Key;
			const auto key_name = jev.value("key", "KBD_NONE");
			auto it = key_name_map.find(key_name);
			if (it == key_name_map.end()) {
				res.status = 400;
				json err;
				err["error"] = "Unknown key: " + key_name;
				send_json(res, err);
				return;
			}
			ev.key = static_cast<int>(it->second);
			ev.pressed = jev.value("pressed", true);
		} else if (type_str == "mouse_move") {
			ev.type = InputEvent::Type::MouseMove;
			ev.x_rel = jev.value("x_rel", 0.0f);
			ev.y_rel = jev.value("y_rel", 0.0f);
			ev.x_abs = jev.value("x_abs", 0.0f);
			ev.y_abs = jev.value("y_abs", 0.0f);
		} else if (type_str == "mouse_button") {
			ev.type = InputEvent::Type::MouseButton;
			ev.button = jev.value("button", "left");
			ev.pressed = jev.value("pressed", true);
			if (button_name_map.find(ev.button) == button_name_map.end()) {
				res.status = 400;
				json err;
				err["error"] = "Unknown button: " + ev.button;
				send_json(res, err);
				return;
			}
		} else if (type_str == "mouse_wheel") {
			ev.type = InputEvent::Type::MouseWheel;
			ev.wheel_delta = jev.value("delta", 0.0f);
		} else {
			res.status = 400;
			json err;
			err["error"] = "Unknown event type: " + type_str;
			send_json(res, err);
			return;
		}

		events.push_back(std::move(ev));
	}

	InputSequenceCommand cmd(std::move(events));
	cmd.WaitForCompletion(5000);

	json result;
	result["status"] = "ok";
	result["events_scheduled"] = body["events"].size();
	send_json(res, result);
}

} // namespace Webserver
