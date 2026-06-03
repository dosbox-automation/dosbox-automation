// SPDX-FileCopyrightText: 2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "input.h"
#include "bridge.h"
#include "webserver.h"

#include "gui/titlebar.h"
#include "hardware/input/keyboard.h" // KBD_KEYS, KEYBOARD_AddKey, keyboard_input_hook
#include "hardware/input/mouse.h" // MOUSE_Event*, mouse_*_hook
#include "hardware/pic.h"

#include <algorithm>
#include <chrono>

#include "libs/json/json.h"

#include <mutex>
#include <queue>
#include <unordered_map>

using json = nlohmann::json;

namespace Webserver {

static const std::unordered_map<std::string, KBD_KEYS> key_name_map = {
        {        "KBD_NONE",         KBD_NONE},
        {           "KBD_1",            KBD_1},
        {           "KBD_2",            KBD_2},
        {           "KBD_3",            KBD_3},
        {           "KBD_4",            KBD_4},
        {           "KBD_5",            KBD_5},
        {           "KBD_6",            KBD_6},
        {           "KBD_7",            KBD_7},
        {           "KBD_8",            KBD_8},
        {           "KBD_9",            KBD_9},
        {           "KBD_0",            KBD_0},
        {           "KBD_q",            KBD_q},
        {           "KBD_w",            KBD_w},
        {           "KBD_e",            KBD_e},
        {           "KBD_r",            KBD_r},
        {           "KBD_t",            KBD_t},
        {           "KBD_y",            KBD_y},
        {           "KBD_u",            KBD_u},
        {           "KBD_i",            KBD_i},
        {           "KBD_o",            KBD_o},
        {           "KBD_p",            KBD_p},
        {           "KBD_a",            KBD_a},
        {           "KBD_s",            KBD_s},
        {           "KBD_d",            KBD_d},
        {           "KBD_f",            KBD_f},
        {           "KBD_g",            KBD_g},
        {           "KBD_h",            KBD_h},
        {           "KBD_j",            KBD_j},
        {           "KBD_k",            KBD_k},
        {           "KBD_l",            KBD_l},
        {           "KBD_z",            KBD_z},
        {           "KBD_x",            KBD_x},
        {           "KBD_c",            KBD_c},
        {           "KBD_v",            KBD_v},
        {           "KBD_b",            KBD_b},
        {           "KBD_n",            KBD_n},
        {           "KBD_m",            KBD_m},
        {          "KBD_f1",           KBD_f1},
        {          "KBD_f2",           KBD_f2},
        {          "KBD_f3",           KBD_f3},
        {          "KBD_f4",           KBD_f4},
        {          "KBD_f5",           KBD_f5},
        {          "KBD_f6",           KBD_f6},
        {          "KBD_f7",           KBD_f7},
        {          "KBD_f8",           KBD_f8},
        {          "KBD_f9",           KBD_f9},
        {         "KBD_f10",          KBD_f10},
        {         "KBD_f11",          KBD_f11},
        {         "KBD_f12",          KBD_f12},
        {         "KBD_esc",          KBD_esc},
        {         "KBD_tab",          KBD_tab},
        {   "KBD_backspace",    KBD_backspace},
        {       "KBD_enter",        KBD_enter},
        {       "KBD_space",        KBD_space},
        {     "KBD_leftalt",      KBD_leftalt},
        {    "KBD_rightalt",     KBD_rightalt},
        {    "KBD_leftctrl",     KBD_leftctrl},
        {   "KBD_rightctrl",    KBD_rightctrl},
        {     "KBD_leftgui",      KBD_leftgui},
        {    "KBD_rightgui",     KBD_rightgui},
        {   "KBD_leftshift",    KBD_leftshift},
        {  "KBD_rightshift",   KBD_rightshift},
        {    "KBD_capslock",     KBD_capslock},
        {  "KBD_scrolllock",   KBD_scrolllock},
        {     "KBD_numlock",      KBD_numlock},
        {       "KBD_grave",        KBD_grave},
        {       "KBD_minus",        KBD_minus},
        {      "KBD_equals",       KBD_equals},
        {   "KBD_backslash",    KBD_backslash},
        { "KBD_leftbracket",  KBD_leftbracket},
        {"KBD_rightbracket", KBD_rightbracket},
        {   "KBD_semicolon",    KBD_semicolon},
        {       "KBD_quote",        KBD_quote},
        {      "KBD_oem102",       KBD_oem102},
        {      "KBD_period",       KBD_period},
        {       "KBD_comma",        KBD_comma},
        {       "KBD_slash",        KBD_slash},
        {       "KBD_abnt1",        KBD_abnt1},
        { "KBD_printscreen",  KBD_printscreen},
        {       "KBD_pause",        KBD_pause},
        {      "KBD_insert",       KBD_insert},
        {        "KBD_home",         KBD_home},
        {      "KBD_pageup",       KBD_pageup},
        {      "KBD_delete",       KBD_delete},
        {         "KBD_end",          KBD_end},
        {    "KBD_pagedown",     KBD_pagedown},
        {        "KBD_left",         KBD_left},
        {          "KBD_up",           KBD_up},
        {        "KBD_down",         KBD_down},
        {       "KBD_right",        KBD_right},
        {         "KBD_kp1",          KBD_kp1},
        {         "KBD_kp2",          KBD_kp2},
        {         "KBD_kp3",          KBD_kp3},
        {         "KBD_kp4",          KBD_kp4},
        {         "KBD_kp5",          KBD_kp5},
        {         "KBD_kp6",          KBD_kp6},
        {         "KBD_kp7",          KBD_kp7},
        {         "KBD_kp8",          KBD_kp8},
        {         "KBD_kp9",          KBD_kp9},
        {         "KBD_kp0",          KBD_kp0},
        {    "KBD_kpdivide",     KBD_kpdivide},
        {  "KBD_kpmultiply",   KBD_kpmultiply},
        {     "KBD_kpminus",      KBD_kpminus},
        {      "KBD_kpplus",       KBD_kpplus},
        {     "KBD_kpenter",      KBD_kpenter},
        {    "KBD_kpperiod",     KBD_kpperiod},
};

static const std::unordered_map<std::string, MouseButtonId> button_name_map = {
        {  "left",   MouseButtonId::Left},
        { "right",  MouseButtonId::Right},
        {"middle", MouseButtonId::Middle},
};

static std::mutex pending_mutex;
static std::queue<InputEvent> pending_events;
static bool in_replay_dispatch = false;

static void dispatch_input_event(const InputEvent& ev)
{
	in_replay_dispatch = true;

	switch (ev.type) {
	case InputEvent::Type::Key:
		KEYBOARD_AddKey(static_cast<KBD_KEYS>(ev.key), ev.pressed);
		break;
	case InputEvent::Type::MouseMove:
		MOUSE_InjectMoved(ev.x_rel, ev.y_rel);
		break;
	case InputEvent::Type::MouseButton: {
		auto it = button_name_map.find(ev.button);
		if (it != button_name_map.end()) {
			MOUSE_InjectButton(it->second, ev.pressed);
		}
		break;
	}
	case InputEvent::Type::MouseWheel:
		MOUSE_InjectWheel(ev.wheel_delta);
		break;
	}

	in_replay_dispatch = false;
}

static size_t pending_total      = 0;
static size_t pending_dispatched = 0;

static void pic_input_handler(uint32_t)
{
	std::lock_guard<std::mutex> lock(pending_mutex);
	if (pending_events.empty()) {
		return;
	}
	auto ev = pending_events.front();
	pending_events.pop();
	++pending_dispatched;

	switch (ev.type) {
	case InputEvent::Type::Key:
		LOG_DEBUG("REPLAY [%zu/%zu] t=%.1fms key %d %s",
		          pending_dispatched,
		          pending_total,
		          ev.t_ms,
		          ev.key,
		          ev.pressed ? "DOWN" : "UP");
		break;
	case InputEvent::Type::MouseMove:
		LOG_DEBUG("REPLAY [%zu/%zu] t=%.1fms mouse_move rel=(%.1f,%.1f)",
		          pending_dispatched,
		          pending_total,
		          ev.t_ms,
		          ev.x_rel,
		          ev.y_rel);
		break;
	case InputEvent::Type::MouseButton:
		LOG_DEBUG("REPLAY [%zu/%zu] t=%.1fms mouse_button %s %s",
		          pending_dispatched,
		          pending_total,
		          ev.t_ms,
		          ev.button.c_str(),
		          ev.pressed ? "DOWN" : "UP");
		break;
	case InputEvent::Type::MouseWheel:
		LOG_DEBUG("REPLAY [%zu/%zu] t=%.1fms mouse_wheel delta=%.1f",
		          pending_dispatched,
		          pending_total,
		          ev.t_ms,
		          ev.wheel_delta);
		break;
	}

	dispatch_input_event(ev);

	if (!pending_events.empty()) {
		const auto& next = pending_events.front();
		const auto delay = std::max(next.t_ms - ev.t_ms, 0.0);
		PIC_AddEvent(pic_input_handler, delay);
	} else {
		TITLEBAR_NotifyApiReplayStatus(false);
		LOG_DEBUG("REPLAY chain complete: %zu/%zu events dispatched",
		          pending_dispatched,
		          pending_total);
	}
}

InputSequenceCommand::InputSequenceCommand(std::vector<InputEvent> events)
        : events(std::move(events))
{}

void InputSequenceCommand::Execute()
{
	std::lock_guard<std::mutex> lock(pending_mutex);

	if (!pending_events.empty()) {
		error = "Replay already in progress";
		return;
	}

	for (auto& ev : events) {
		if (ev.t_ms <= 0) {
			dispatch_input_event(ev);
		}
	}

	for (auto& ev : events) {
		if (ev.t_ms > 0) {
			pending_events.push(ev);
		}
	}
	pending_total      = pending_events.size();
	pending_dispatched = 0;
	LOG_DEBUG("REPLAY starting chain: %zu timed events, first at %.1fms, last at %.1fms",
	          pending_total,
	          pending_events.empty() ? 0.0 : pending_events.front().t_ms,
	          events.empty() ? 0.0 : events.back().t_ms);
	if (!pending_events.empty()) {
		TITLEBAR_NotifyApiReplayStatus(true);
		PIC_AddEvent(pic_input_handler, pending_events.front().t_ms);
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

	constexpr size_t max_events = 10000;
	if (body["events"].size() > max_events) {
		res.status = 400;
		json err;
		err["error"] = "Too many events (max " +
		               std::to_string(max_events) + ")";
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
			ev.type             = InputEvent::Type::Key;
			const auto key_name = jev.value("key", "KBD_NONE");
			auto it             = key_name_map.find(key_name);
			if (it == key_name_map.end()) {
				res.status = 400;
				json err;
				err["error"] = "Unknown key: " + key_name;
				send_json(res, err);
				return;
			}
			ev.key     = static_cast<int>(it->second);
			ev.pressed = jev.value("pressed", true);
		} else if (type_str == "mouse_move") {
			ev.type  = InputEvent::Type::MouseMove;
			ev.x_rel = jev.value("x_rel", 0.0f);
			ev.y_rel = jev.value("y_rel", 0.0f);
			ev.x_abs = jev.value("x_abs", 0.0f);
			ev.y_abs = jev.value("y_abs", 0.0f);
		} else if (type_str == "mouse_button") {
			ev.type    = InputEvent::Type::MouseButton;
			ev.button  = jev.value("button", "left");
			ev.pressed = jev.value("pressed", true);
			if (button_name_map.find(ev.button) == button_name_map.end()) {
				res.status = 400;
				json err;
				err["error"] = "Unknown button: " + ev.button;
				send_json(res, err);
				return;
			}
		} else if (type_str == "mouse_wheel") {
			ev.type        = InputEvent::Type::MouseWheel;
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

	if (!cmd.error.empty()) {
		res.status = 409;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	json result;
	result["status"]           = "ok";
	result["events_scheduled"] = body["events"].size();
	send_json(res, result);
}

// --- Input Recording ---

static std::mutex rec_mutex;
static bool rec_active = false;
static bool rec_paused = false;
static std::vector<InputEvent> rec_buffer;
static double rec_start_pic_ms;

static const std::unordered_map<int, std::string> button_id_to_name = {
        {0,   "left"},
        {1,  "right"},
        {2, "middle"},
};

static const std::unordered_map<int, std::string> key_id_to_name = [] {
	std::unordered_map<int, std::string> m;
	for (const auto& [name, key] : key_name_map) {
		m[static_cast<int>(key)] = name;
	}
	return m;
}();

static double rec_elapsed_ms_precise()
{
	return PIC_FullIndex() - rec_start_pic_ms;
}

static double rec_elapsed_ms_approx()
{
	return PIC_AtomicIndex() - rec_start_pic_ms;
}

void InputRecording::StartOnEmulationThread()
{
	std::lock_guard<std::mutex> lock(rec_mutex);
	rec_buffer.clear();
	rec_active       = true;
	rec_paused       = false;
	rec_start_pic_ms = PIC_FullIndex();
	TITLEBAR_NotifyApiRecordingStatus(true);
}

void StartRecordingCommand::Execute()
{
	InputRecording::StartOnEmulationThread();
}

void InputRecording::Pause()
{
	std::lock_guard<std::mutex> lock(rec_mutex);
	if (rec_active) {
		rec_paused = !rec_paused;
	}
}

bool InputRecording::Stop(std::vector<InputEvent>& out_events)
{
	std::lock_guard<std::mutex> lock(rec_mutex);
	if (!rec_active) {
		return false;
	}
	rec_active = false;
	rec_paused = false;
	out_events = std::move(rec_buffer);
	rec_buffer.clear();
	TITLEBAR_NotifyApiRecordingStatus(false);
	return true;
}

bool InputRecording::IsRecording()
{
	std::lock_guard<std::mutex> lock(rec_mutex);
	return rec_active;
}

bool InputRecording::IsPaused()
{
	std::lock_guard<std::mutex> lock(rec_mutex);
	return rec_paused;
}

size_t InputRecording::EventCount()
{
	std::lock_guard<std::mutex> lock(rec_mutex);
	return rec_buffer.size();
}

double InputRecording::DurationMs()
{
	std::lock_guard<std::mutex> lock(rec_mutex);
	if (!rec_active) {
		return 0;
	}
	return rec_elapsed_ms_approx();
}

void InputRecording::OnKeyEvent(int key, bool pressed)
{
	std::lock_guard<std::mutex> lock(rec_mutex);
	if (!rec_active || rec_paused) {
		return;
	}
	InputEvent ev;
	ev.t_ms    = rec_elapsed_ms_precise();
	ev.type    = InputEvent::Type::Key;
	ev.key     = key;
	ev.pressed = pressed;
	rec_buffer.push_back(std::move(ev));
}

void InputRecording::OnMouseMove(float x_rel, float y_rel, float x_abs, float y_abs)
{
	std::lock_guard<std::mutex> lock(rec_mutex);
	if (!rec_active || rec_paused) {
		return;
	}
	InputEvent ev;
	ev.t_ms  = rec_elapsed_ms_precise();
	ev.type  = InputEvent::Type::MouseMove;
	ev.x_rel = x_rel;
	ev.y_rel = y_rel;
	ev.x_abs = x_abs;
	ev.y_abs = y_abs;
	rec_buffer.push_back(std::move(ev));
}

void InputRecording::OnMouseButton(const std::string& button, bool pressed)
{
	std::lock_guard<std::mutex> lock(rec_mutex);
	if (!rec_active || rec_paused) {
		return;
	}
	InputEvent ev;
	ev.t_ms    = rec_elapsed_ms_precise();
	ev.type    = InputEvent::Type::MouseButton;
	ev.button  = button;
	ev.pressed = pressed;
	rec_buffer.push_back(std::move(ev));
}

void InputRecording::OnMouseWheel(float delta)
{
	std::lock_guard<std::mutex> lock(rec_mutex);
	if (!rec_active || rec_paused) {
		return;
	}
	InputEvent ev;
	ev.t_ms        = rec_elapsed_ms_precise();
	ev.type        = InputEvent::Type::MouseWheel;
	ev.wheel_delta = delta;
	rec_buffer.push_back(std::move(ev));
}

static void hook_keyboard(int key, bool pressed)
{
	if (in_replay_dispatch) {
		return;
	}
	InputRecording::OnKeyEvent(key, pressed);
}

static void hook_mouse_move(float x_rel, float y_rel, float x_abs, float y_abs)
{
	if (in_replay_dispatch) {
		return;
	}
	InputRecording::OnMouseMove(x_rel, y_rel, x_abs, y_abs);
}

static void hook_mouse_button(int button_id, bool pressed)
{
	if (in_replay_dispatch) {
		return;
	}
	auto it = button_id_to_name.find(button_id);
	if (it != button_id_to_name.end()) {
		InputRecording::OnMouseButton(it->second, pressed);
	}
}

static void hook_mouse_wheel(float delta)
{
	if (in_replay_dispatch) {
		return;
	}
	InputRecording::OnMouseWheel(delta);
}

void InputRecording::InstallHooks()
{
	keyboard_input_hook = hook_keyboard;
	mouse_move_hook     = hook_mouse_move;
	mouse_button_hook   = hook_mouse_button;
	mouse_wheel_hook    = hook_mouse_wheel;
}

static json event_to_json(const InputEvent& ev)
{
	json j;
	j["t"] = ev.t_ms;

	switch (ev.type) {
	case InputEvent::Type::Key: {
		j["type"] = "key";
		auto it   = key_id_to_name.find(ev.key);
		j["key"] = (it != key_id_to_name.end()) ? it->second : "KBD_NONE";
		j["pressed"] = ev.pressed;
		break;
	}
	case InputEvent::Type::MouseMove:
		j["type"]  = "mouse_move";
		j["x_rel"] = ev.x_rel;
		j["y_rel"] = ev.y_rel;
		j["x_abs"] = ev.x_abs;
		j["y_abs"] = ev.y_abs;
		break;
	case InputEvent::Type::MouseButton:
		j["type"]    = "mouse_button";
		j["button"]  = ev.button;
		j["pressed"] = ev.pressed;
		break;
	case InputEvent::Type::MouseWheel:
		j["type"]  = "mouse_wheel";
		j["delta"] = ev.wheel_delta;
		break;
	}
	return j;
}

void RecordingHandlers::PostStart(const httplib::Request&, httplib::Response& res)
{
	if (InputRecording::IsRecording()) {
		res.status = 409;
		json err;
		err["error"] = "Already recording";
		send_json(res, err);
		return;
	}
	StartRecordingCommand cmd;
	cmd.WaitForCompletion(1000);
	json j;
	j["status"] = "recording";
	send_json(res, j);
}

void RecordingHandlers::PostPause(const httplib::Request&, httplib::Response& res)
{
	if (!InputRecording::IsRecording()) {
		res.status = 409;
		json err;
		err["error"] = "Not recording";
		send_json(res, err);
		return;
	}
	InputRecording::Pause();
	json j;
	j["status"] = InputRecording::IsPaused() ? "paused" : "recording";
	send_json(res, j);
}

void RecordingHandlers::PostStop(const httplib::Request&, httplib::Response& res)
{
	std::vector<InputEvent> events;
	if (!InputRecording::Stop(events)) {
		res.status = 409;
		json err;
		err["error"] = "Not recording";
		send_json(res, err);
		return;
	}

	json j;
	j["event_count"] = events.size();
	j["events"]      = json::array();
	double duration  = 0;
	for (const auto& ev : events) {
		j["events"].push_back(event_to_json(ev));
		if (ev.t_ms > duration) {
			duration = ev.t_ms;
		}
	}
	j["duration_ms"] = duration;
	send_json(res, j);
}

void RecordingHandlers::GetStatus(const httplib::Request&, httplib::Response& res)
{
	json j;
	j["recording"]   = InputRecording::IsRecording();
	j["paused"]      = InputRecording::IsPaused();
	j["event_count"] = InputRecording::EventCount();
	j["duration_ms"] = InputRecording::DurationMs();
	send_json(res, j);
}

} // namespace Webserver
