// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver.h"
#include "bridge.h"
#include "capture.h"
#include "control.h"
#include "drive.h"
#include "input.h"
#include "private/auth.h"
#include "private/cpu.h"
#include "private/dos.h"
#include "private/dosbox.h"
#include "private/memory.h"
#include "video.h"

#include "lua/lua_bridge_commands.h"

#include "dos/programs/mount_policy.h"
#include "gui/osd/osd.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

#if defined(WIN32)
#include <bcrypt.h>
#include <windows.h>
#endif

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
	res.set_content(j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace),
	                "application/json");
}

static void error_handler(const httplib::Request&, httplib::Response& res,
                          std::exception_ptr ep)
{
	json j;
	std::string msg;
	res.status = httplib::StatusCode::InternalServerError_500;

	try {
		if (ep) {
			std::rethrow_exception(ep);
		}
	} catch (const std::invalid_argument&) {
		msg        = "Invalid request parameter";
		res.status = httplib::StatusCode::BadRequest_400;
	} catch (const std::out_of_range&) {
		msg        = "Address out of range";
		res.status = httplib::StatusCode::BadRequest_400;
	} catch (const nlohmann::json::exception&) {
		msg        = "Malformed request body";
		res.status = httplib::StatusCode::BadRequest_400;
	} catch (const std::exception&) {
		msg = "Internal server error";
	} catch (...) {
		msg = "Internal server error";
	}

	j["error"] = msg;
	send_json(res, j);
}

bool IsPublicDocPath(const std::string& method, const std::string& path)
{
	if (method != "GET" && method != "HEAD") {
		return false;
	}

	// Exact match only. A prefix or normalized match would let
	// "/openapi.json/../api_token" or an encoded traversal reach the config
	// mount, which holds the API token file.
	static const std::set<std::string> public_paths = {
	        "/",
	        "/index.html",
	        "/style-index.css",
	        "/api.html",
	        "/openapi.json",
	        "/swagger-ui.css",
	        "/swagger-ui-bundle.js",
	};
	return public_paths.count(path) > 0;
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

	server.Post("/api/v1/mount/lock",
	            [](const httplib::Request&, httplib::Response& res) {
		            MountPolicy::Lock();
		            json j;
		            j["status"] = "locked";
		            send_json(res, j);
	            });
	server.Get("/api/v1/mount/lock",
	           [](const httplib::Request&, httplib::Response& res) {
		           json j;
		           j["locked"] = MountPolicy::IsLocked();
		           send_json(res, j);
	           });

	server.Post("/api/v1/input/record/start", RecordingHandlers::PostStart);
	server.Post("/api/v1/input/record/pause", RecordingHandlers::PostPause);
	server.Post("/api/v1/input/record/stop", RecordingHandlers::PostStop);
	server.Get("/api/v1/input/record/status", RecordingHandlers::GetStatus);

	server.Post("/api/v1/script/load", Lua::LuaLoadCommand::Post);
	server.Post("/api/v1/script/start", Lua::LuaStartCommand::Post);
	server.Post("/api/v1/script/stop", Lua::LuaStopCommand::Post);
	server.Get("/api/v1/script/status", Lua::LuaStatusCommand::Get);

	server.Post("/api/v1/capture/video/start", CaptureStartCommand::Post);
	server.Post("/api/v1/capture/video/stop", CaptureStopCommand::Post);
	server.Get("/api/v1/capture/video/status", CaptureStatusCommand::Get);
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
	uint8_t buf[32] = {};

#if defined(WIN32)
	// BCryptGenRandom is the Windows CSPRNG. std::random_device on
	// MinGW has been deterministic on some toolchains.
	const auto status = BCryptGenRandom(nullptr,
	                                    buf,
	                                    sizeof(buf),
	                                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	if (!BCRYPT_SUCCESS(status)) {
		E_Exit("WEBSERVER: BCryptGenRandom failed (0x%08lx)", status);
	}
#else
	// On Linux/macOS, /dev/urandom is the standard CSPRNG source.
	auto* f = fopen("/dev/urandom", "rb");
	if (!f || fread(buf, 1, sizeof(buf), f) != sizeof(buf)) {
		if (f) {
			fclose(f);
		}
		E_Exit("WEBSERVER: Failed to read /dev/urandom");
	}
	fclose(f);
#endif

	constexpr char hex[] = "0123456789abcdef";
	std::string token;
	token.reserve(64);
	for (const auto byte : buf) {
		token += hex[(byte >> 4) & 0xF];
		token += hex[byte & 0xF];
	}
	return token;
}

static std::filesystem::path token_file_path = {};

static std::filesystem::path get_token_file_dir()
{
	return get_config_dir() / DefaultWebserverDir;
}

static bool write_token_file(const std::string& token)
{
	namespace fs = std::filesystem;

	const auto dir  = get_token_file_dir();
	const auto path = dir / "api_token";

	std::error_code ec;
	fs::create_directories(dir, ec);
	if (ec) {
		LOG_WARNING("WEBSERVER: Cannot create token dir '%s': %s",
		            dir.string().c_str(),
		            ec.message().c_str());
		return false;
	}

	const auto tmp = dir / "api_token.tmp";

	{
		auto out = std::ofstream(tmp, std::ios::binary | std::ios::trunc);
		if (!out.is_open()) {
			LOG_WARNING("WEBSERVER: Cannot write token file '%s'",
			            tmp.string().c_str());
			return false;
		}
		out << token;
	}

#if !defined(WIN32)
	fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, ec);
	if (ec) {
		LOG_WARNING("WEBSERVER: Cannot set permissions on '%s'",
		            tmp.string().c_str());
		fs::remove(tmp, ec);
		return false;
	}
#endif

	fs::rename(tmp, path, ec);
	if (ec) {
		LOG_WARNING("WEBSERVER: Cannot rename token file: %s",
		            ec.message().c_str());
		fs::remove(tmp, ec);
		return false;
	}

	token_file_path = path;
	return true;
}

static void remove_token_file()
{
	if (token_file_path.empty()) {
		return;
	}
	std::error_code ec;
	std::filesystem::remove(token_file_path, ec);
	token_file_path.clear();
}

static bool is_valid_hex_token(const std::string& s)
{
	if (s.size() != 64) {
		return false;
	}
	for (const char c : s) {
		if (!std::isxdigit(static_cast<unsigned char>(c))) {
			return false;
		}
	}
	return true;
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

		// Documentation assets are browsable without a token (Host
		// check still applies). The /api/v1 endpoints below are not.
		if (IsPublicDocPath(req.method, req.path)) {
			return httplib::Server::HandlerResponse::Unhandled;
		}

		const auto token = extract_bearer_token(
		        req.get_header_value("Authorization"));

		if (!ConstantTimeEquals(token, api_token)) {
			LOG_WARNING("WEBSERVER: Rejected request with invalid token");
			res.status = httplib::StatusCode::Unauthorized_401;
			res.set_content("Unauthorized", "text/plain");
			return httplib::Server::HandlerResponse::Handled;
		}

		return httplib::Server::HandlerResponse::Unhandled;
	});

	server.set_default_headers({
	        {"X-Content-Type-Options", "nosniff"},
	});

	server.Options(".*", [](const httplib::Request&, httplib::Response& res) {
		res.status = httplib::StatusCode::Forbidden_403;
	});

	server.set_payload_max_length(10 * 1024 * 1024);
}

static void run(const std::string addr, const int port,
                const std::string resource_home, const bool use_token_file)
{
	const auto config_home = (get_config_dir() / DefaultWebserverDir).string();

	// Channel A: a launcher can supply the token via env var so it
	// never needs to scrape stderr or read a file.
	std::string api_token;
	bool token_from_env = false;

	const char* env_token = std::getenv("DOSBOX_API_TOKEN");
	if (env_token) {
		std::string candidate(env_token);
		if (is_valid_hex_token(candidate)) {
			api_token      = std::move(candidate);
			token_from_env = true;
		} else {
			LOG_WARNING(
			        "WEBSERVER: DOSBOX_API_TOKEN set but invalid "
			        "(need 64 hex chars), generating token");
		}
	}

	if (api_token.empty()) {
		api_token = generate_api_token();
	}

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

	// Channel B: write the auto-generated token to a file so launchers
	// can read it without scraping stderr.
	if (use_token_file && !token_from_env) {
		if (write_token_file(api_token)) {
			LOG_MSG("WEBSERVER: Token written to %s",
			        token_file_path.string().c_str());
		} else {
			LOG_MSG("WEBSERVER: API token: %.8s...", api_token.c_str());
		}
	} else if (token_from_env) {
		LOG_MSG("WEBSERVER: Using API token from DOSBOX_API_TOKEN");
	} else {
		LOG_MSG("WEBSERVER: API token: %.8s...", api_token.c_str());
	}

	LOG_INFO("WEBSERVER: Starting HTTP REST API on http://%s:%d",
	         addr.c_str(),
	         port);

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

	auto token_file = section.AddBool("webserver_token_file", OnlyAtStart, false);
	token_file->SetHelp(
	        "Write the API token to a file instead of printing it to the log.\n"
	        "The file is written to the webserver config directory with restricted\n"
	        "permissions (0600) and removed on clean shutdown. Launchers and tools\n"
	        "can read the token from this file instead of scraping log output.\n"
	        "Has no effect when DOSBOX_API_TOKEN is set via environment variable.");

	auto osd = section.AddBool("webserver_osd", OnlyAtStart, true);
	osd->SetHelp(
	        "Show on-screen indicators while automation is driving the machine\n"
	        "(script running, recording, replay, injected input). Enabled by\n"
	        "default so it is always clear when the machine is under remote\n"
	        "control. Set to false to hide the overlay.");
}

} // namespace Webserver

static bool is_webserver_enabled = false;

static bool is_remote_address(const std::string& addr)
{
	return addr == "0.0.0.0" || addr == "::";
}

void WEBSERVER_Init()
{
	MountPolicy::InitPolicyConfig(get_primary_config_path());

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

		OSD::OsdManager::Instance().SetEnabled(
		        section->GetBool("webserver_osd"));

		const auto port = section->GetInt("webserver_port");
		const auto resource_home = get_resource_path("webserver").string();
		const auto use_token_file = section->GetBool("webserver_token_file");

		Webserver::InputRecording::InstallHooks();

		std::thread thread(Webserver::run, addr, port, resource_home, use_token_file);

		thread.detach();
	}
}

void WEBSERVER_Destroy()
{
	Webserver::server.stop();
	Webserver::remove_token_file();
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
