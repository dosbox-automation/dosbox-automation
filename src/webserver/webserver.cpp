// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "webserver.h"
#include "bridge.h"
#include "control.h"
#include "drive.h"
#include "input.h"
#include "private/cpu.h"
#include "private/dos.h"
#include "private/dosbox.h"
#include "private/memory.h"
#include "video.h"

#include <random>
#include <set>
#include <string>
#include <thread>

#include "http/http.h"
#include "json/json.h"

#include "config/config.h"
#include "dosbox.h"
#include "misc/cross.h"
#include "misc/logging.h"
#include "misc/support.h"

using json = nlohmann::json;

namespace Webserver {

void send_json(httplib::Response& res, const nlohmann::json& j)
{
	res.set_content(j.dump(2), "application/json");
}

static void error_handler(const httplib::Request&, httplib::Response& res,
                          std::exception_ptr ep)
{
	json j;
	std::string msg;

	try {
		if (ep) {
			std::rethrow_exception(ep);
		}
	} catch (const std::exception& e) {
		msg = e.what();
	} catch (...) {
		msg = "Unknown error";
	}

	j["error"] = msg;
	res.status = httplib::StatusCode::InternalServerError_500;

	send_json(res, j);
}

static httplib::Server server;

static void setup_api_handlers()
{
	server.Get("/api/v1/cpu/state", CpuStateCommand::Get);

	server.Get("/api/v1/dos/internals", DosInternalsCommand::Get);

	server.Post("/api/v1/dosbox/shutdown", ShutdownCommand::Post);

	server.Post("/api/v1/memory/allocate", AllocMemoryCommand::Post);
	server.Post("/api/v1/memory/free", FreeMemoryCommand::Post);
	server.Get("/api/v1/memory/:offset/:len", ReadMemoryCommand::Get);
	server.Get("/api/v1/memory/:segment/:offset/:len", ReadMemoryCommand::Get);
	server.Put("/api/v1/memory/:offset", WriteMemoryCommand::Put);
	server.Put("/api/v1/memory/:segment/:offset", WriteMemoryCommand::Put);

	server.Post("/api/v1/input/sequence", InputSequenceCommand::Post);

	server.Get("/api/v1/video/frame", VideoHandlers::GetFrame);
	server.Get("/api/v1/video/frame/info", VideoHandlers::GetFrameInfo);

	server.Get("/api/v1/program/state", ControlHandlers::GetProgramState);
	server.Get("/api/v1/status", ControlHandlers::GetStatus);
	server.Post("/api/v1/control/shutdown", ShutdownCommand::Post);

	server.Post("/api/v1/drive/swap", DriveSwapCommand::Post);

	server.Post("/api/v1/input/record/start", RecordingHandlers::PostStart);
	server.Post("/api/v1/input/record/pause", RecordingHandlers::PostPause);
	server.Post("/api/v1/input/record/stop", RecordingHandlers::PostStop);
	server.Get("/api/v1/input/record/status", RecordingHandlers::GetStatus);
}

static std::string strip_port(const std::string& host)
{
	// IPv6 literal: [::1]:8080
	if (host.size() > 1 && host[0] == '[') {
		const auto bracket = host.rfind(']');
		if (bracket != std::string::npos) {
			return host.substr(0, bracket + 1);
		}
		return host;
	}

	// IPv4 or hostname: 127.0.0.1:8080
	const auto colon = host.rfind(':');
	if (colon != std::string::npos) {
		return host.substr(0, colon);
	}
	return host;
}

static std::string generate_api_token()
{
	std::random_device rd;
	std::uniform_int_distribution<int> dist(0, 255);
	constexpr char hex[] = "0123456789abcdef";
	std::string token;
	token.reserve(64);
	for (int i = 0; i < 32; ++i) {
		const auto byte = dist(rd);
		token += hex[(byte >> 4) & 0xF];
		token += hex[byte & 0xF];
	}
	return token;
}

static std::string extract_bearer_token(const std::string& auth_header)
{
	constexpr auto prefix = std::string_view("Bearer ");
	if (auth_header.size() > prefix.size() &&
	    auth_header.compare(0, prefix.size(), prefix) == 0) {
		return auth_header.substr(prefix.size());
	}
	return {};
}

static void setup_security(const std::string& addr, int port,
                           const std::string& api_token)
{
	std::set<std::string> allowed_hosts;

	const auto port_str = ":" + std::to_string(port);

	auto add = [&](const std::string& hostname) {
		allowed_hosts.emplace(hostname);
		allowed_hosts.emplace(hostname + port_str);
	};

	add(addr);

	if (addr == "127.0.0.1" || addr == "0.0.0.0") {
		add("localhost");
	}
	if (addr == "::1" || addr == "::") {
		add("localhost");
		add("[::1]");
	}

	server.set_pre_routing_handler([allowed_hosts = std::move(allowed_hosts),
	                                api_token](const httplib::Request& req,
	                                           httplib::Response& res) {
		const auto host = strip_port(req.get_header_value("Host"));

		if (allowed_hosts.find(host) == allowed_hosts.end()) {
			LOG_WARNING("WEBSERVER: Rejected request with Host header '%s'",
			            req.get_header_value("Host").c_str());
			res.status = httplib::StatusCode::Forbidden_403;
			res.set_content("Forbidden", "text/plain");
			return httplib::Server::HandlerResponse::Handled;
		}

		const auto token = extract_bearer_token(
		        req.get_header_value("Authorization"));

		if (token != api_token) {
			res.status = httplib::StatusCode::Unauthorized_401;
			res.set_content("Unauthorized", "text/plain");
			return httplib::Server::HandlerResponse::Handled;
		}

		return httplib::Server::HandlerResponse::Unhandled;
	});

	server.set_default_headers({
	        {"Access-Control-Allow-Origin",    "null"},
	        {     "X-Content-Type-Options", "nosniff"},
	});

	server.Options(".*", [](const httplib::Request&, httplib::Response& res) {
		res.status = httplib::StatusCode::Forbidden_403;
	});

	server.set_payload_max_length(10 * 1024 * 1024);
}

static void run(const std::string addr, const int port, const std::string resource_home)
{
	const auto config_home = (get_config_dir() / DefaultWebserverDir).string();
	const auto api_token = generate_api_token();

	server.set_mount_point("/", config_home);
	server.set_mount_point("/", resource_home);

	setup_api_handlers();
	setup_security(addr, port, api_token);

	server.set_exception_handler(error_handler);

	server.Get("/api/v1/dosbox/info",
	           [](const httplib::Request&, httplib::Response& res) {
		           json j;
		           j["version"] = DOSBOX_GetDetailedVersion();
		           send_json(res, j);
	           });

	LOG_INFO("WEBSERVER: Starting HTTP REST API on http://%s:%d",
	         addr.c_str(),
	         port);

	LOG_MSG("WEBSERVER: API token: %s", api_token.c_str());

	auto ok = server.listen(addr, port);
	if (!ok) {
		LOG_WARNING("WEBSERVER: Failed to bind to %s:%d", addr.c_str(), port);
	}
}

static void init_config_settings(SectionProp& section)
{
	using enum Property::Changeable::Value;

	auto enabled = section.AddBool("webserver_enabled", OnlyAtStart, false);
	enabled->SetHelp(
	        "Enable the HTTP REST API that exposes internal state and memory (disabled by\n"
	        "default). Open http://localhost:8386 in a browser (or use the configured port)\n"
	        "to view the API documentation.\n"
	        "\n"
	        "An API token is generated at startup and printed to the log output.\n"
	        "All API requests require Authorization: Bearer <token>.");

	auto bind_ip = section.AddString("webserver_bind_address",
	                                 OnlyAtStart,
	                                 "127.0.0.1");
	bind_ip->SetHelp(
	        "Bind to the given IP address. By default only local connections are\n"
	        "allowed. Binding to 0.0.0.0 or :: requires webserver_allow_remote=true.");

	auto bind_port = section.AddInt("webserver_port", OnlyAtStart, 8386);
	bind_port->SetMinMax(1, 0xFFFF);
	bind_port->SetHelp("TCP port to bind to.");

	auto allow_remote = section.AddBool("webserver_allow_remote", OnlyAtStart, false);
	allow_remote->SetHelp(
	        "Allow binding to non-localhost addresses (0.0.0.0 or ::). This exposes\n"
	        "the full API to the network. Do not enable unless you understand the\n"
	        "security implications.");
}

} // namespace Webserver

static bool is_webserver_enabled = false;

static bool is_remote_address(const std::string& addr)
{
	return addr == "0.0.0.0" || addr == "::";
}

void WEBSERVER_Init()
{
	auto section = get_section("webserver");

	if (section->GetBool("webserver_enabled")) {
		const auto addr = section->GetString("webserver_bind_address");

		if (is_remote_address(addr) &&
		    !section->GetBool("webserver_allow_remote")) {
			LOG_WARNING(
			        "WEBSERVER: Refusing to bind to %s without "
			        "webserver_allow_remote=true",
			        addr.c_str());
			return;
		}

		if (is_remote_address(addr)) {
			LOG_WARNING(
			        "WEBSERVER: Binding to %s — API is exposed "
			        "to the network",
			        addr.c_str());
		}

		is_webserver_enabled = true;

		const auto port = section->GetInt("webserver_port");
		const auto resource_home = get_resource_path("webserver").string();

		Webserver::InputRecording::InstallHooks();

		std::thread thread(Webserver::run, addr, port, resource_home);

		thread.detach();
	}
}

void WEBSERVER_Destroy()
{
	Webserver::server.stop();
}

void WEBSERVER_AddConfigSection(const ConfigPtr& conf)
{
	assert(conf);

	auto section = conf->AddSection("webserver");

	Webserver::init_config_settings(*section);
}

bool WEBSERVER_IsEnabled()
{
	return is_webserver_enabled;
}
