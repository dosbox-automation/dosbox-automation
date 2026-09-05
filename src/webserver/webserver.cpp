// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver.h"
#include "bridge.h"
#include "capture.h"
#include "control.h"
#include "drive.h"
#include "input.h"
#include "mixer.h"
#include "private/auth.h"
#include "private/cpu.h"
#include "private/dos.h"
#include "private/freeze.h"
#include "private/io_port.h"
#include "private/memory.h"
#include "private/shutdown.h"
#include "private/tools.h"
#include "video.h"

#include "lua/lua_bridge_commands.h"

#include "dos/programs/mount_policy.h"
#include "gui/mapper.h"
#include "gui/osd/osd.h"
#include "gui/osd/osd_port.h"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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

#include "augra/log.h"
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
	        "/cheat-workbench.html",
	        "/tools/cheat-workbench.html",
	        "/favicon.ico",
	};
	return public_paths.count(path) > 0;
}

static httplib::Server server;

static void setup_api_handlers()
{
	server.Get("/api/v1/cpu/state", CpuStateCommand::Get);
	server.Get("/api/v1/cpu/cycles", CyclesCommand::Get);
	server.Put("/api/v1/cpu/cycles", CyclesCommand::Put);

	server.Get("/api/v1/dos/internals", DosInternalsCommand::Get);

	server.Post("/api/v1/dosbox/shutdown", ShutdownCommand::Post);

	server.Post("/api/v1/memory/allocate", AllocMemoryCommand::Post);
	server.Post("/api/v1/memory/free", FreeMemoryCommand::Post);
	server.Post("/api/v1/memory/search", SearchMemoryCommand::Post);
	server.Post("/api/v1/memory/freeze", FreezeHandlers::Post);
	server.Get("/api/v1/memory/freeze", FreezeHandlers::Get);
	server.Delete("/api/v1/memory/freeze", FreezeHandlers::Delete);
	server.Get("/api/v1/memory/:offset/:len", ReadMemoryCommand::Get);
	server.Get("/api/v1/memory/:segment/:offset/:len", ReadMemoryCommand::Get);
	server.Put("/api/v1/memory/:offset", WriteMemoryCommand::Put);
	server.Put("/api/v1/memory/:segment/:offset", WriteMemoryCommand::Put);

	server.Get("/api/v1/io/port", PortReadCommand::Get);
	server.Put("/api/v1/io/port", PortWriteCommand::Put);
	server.Put("/api/v1/cpu/register", WriteRegisterCommand::Put);

	server.Post("/api/v1/input/sequence", InputSequenceCommand::Post);
	server.Post("/api/v1/input/type", InputTypeCommand::Post);

	server.Get("/api/v1/video/frame", VideoHandlers::GetFrame);
	server.Get("/api/v1/video/frame/info", VideoHandlers::GetFrameInfo);
	server.Get("/api/v1/video/text", ScreenTextCommand::Get);

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
	server.Get("/api/v1/capture/video/compression",
	           CaptureCompressionGetCommand::Get);
	server.Put("/api/v1/capture/video/compression",
	           CaptureCompressionSetCommand::Put);

	server.Post("/api/v1/capture/audio/start", AudioCaptureStartCommand::Post);
	server.Post("/api/v1/capture/audio/stop", AudioCaptureStopCommand::Post);
	server.Get("/api/v1/capture/audio/status", AudioCaptureStatusCommand::Get);

	server.Get("/api/v1/mixer", MixerStatusCommand::Get);
	server.Put("/api/v1/mixer/volume", MixerSetVolumeCommand::Put);
	server.Post("/api/v1/mixer/mute", MixerMuteCommand::Post);
	server.Post("/api/v1/mixer/unmute", MixerUnmuteCommand::Post);
	server.Put("/api/v1/mixer/channel/:name", MixerSetChannelVolumeCommand::Put);
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
		augra::log_warn("webserver",
		                "cannot create token dir '%s': %s",
		                dir.string().c_str(),
		                ec.message().c_str());
		return false;
	}

	const auto tmp = dir / "api_token.tmp";

	{
		auto out = std::ofstream(tmp, std::ios::binary | std::ios::trunc);
		if (!out.is_open()) {
			augra::log_warn("webserver",
			                "cannot write token file '%s'",
			                tmp.string().c_str());
			return false;
		}
		out << token;
	}

#if !defined(WIN32)
	fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, ec);
	if (ec) {
		augra::log_warn("webserver",
		                "cannot set permissions on '%s'",
		                tmp.string().c_str());
		fs::remove(tmp, ec);
		return false;
	}
#endif

	fs::rename(tmp, path, ec);
	if (ec) {
		augra::log_warn("webserver",
		                "cannot rename token file: %s",
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
			augra::log_warn("webserver",
			                "rejected request with Host header '%s'",
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
			augra::log_warn("webserver",
			                "rejected request with invalid token");
			res.status = httplib::StatusCode::Unauthorized_401;
			res.set_content("Unauthorized", "text/plain");
			return httplib::Server::HandlerResponse::Handled;
		}

		if (IsLoopbackPeer(req.remote_addr)) {
			NoteToolPageSeen(req.get_header_value("X-DOSBox-Tool"),
			                 std::chrono::steady_clock::now());
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

// Tool pages live outside the static mounts because httplib serves a
// mounted file before any registered handler runs; a mount would hand
// out the un-injected page. Loaded once: the page does not change while
// the engine runs.
static void setup_tool_pages(const std::string& api_token, const bool embed_token)
{
	const auto page_path = get_resource_path("webserver-tools") /
	                       "cheat-workbench.html";
	std::string page;
	{
		auto in = std::ifstream(page_path, std::ios::binary);
		if (!in) {
			augra::log_warn("webserver",
			                "tool page missing: %s",
			                page_path.string().c_str());
		} else {
			page.assign(std::istreambuf_iterator<char>(in), {});
		}
	}

	server.Get("/tools/cheat-workbench.html",
	           [page, api_token, embed_token](const httplib::Request& req,
	                                          httplib::Response& res) {
		           if (page.empty()) {
			           res.status = httplib::StatusCode::NotFound_404;
			           res.set_content("tool page not installed",
			                           "text/plain");
			           return;
		           }
		           res.set_header("Cache-Control", "no-store");
		           if (embed_token && IsLoopbackPeer(req.remote_addr)) {
			           if (const auto injected = InjectToken(page, api_token)) {
				           res.set_content(*injected,
				                           "text/html; charset=utf-8");
				           return;
			           }
			           augra::log_warn("webserver",
			                           "tool page has no <head>, serving "
			                           "it without the token");
		           }
		           res.set_content(page, "text/html; charset=utf-8");
	           });

	// The pre-0.85.1 address; bookmarks and the primer point at it.
	server.Get("/cheat-workbench.html",
	           [](const httplib::Request&, httplib::Response& res) {
		           res.set_redirect("/tools/cheat-workbench.html",
		                            httplib::StatusCode::MovedPermanently_301);
	           });
}

static void run(const std::string addr, const int port,
                const std::string resource_home, const bool use_token_file,
                const bool embed_tool_token)
{
	const auto config_home = (get_config_dir() / DefaultWebserverDir).string();

	// Channel A: a launcher can supply the token via env var so it
	// never needs to scrape stderr or read a file.
	std::string api_token;
	bool token_from_env = false;

	const char* env_token = std::getenv("DOSBOX_API_TOKEN");
	if (env_token) {
		std::string candidate(env_token);
		if (IsValidHexToken(candidate)) {
			api_token      = std::move(candidate);
			token_from_env = true;
		} else {
			augra::log_warn("webserver",
			                "DOSBOX_API_TOKEN set but invalid "
			                "(need 64 hex chars), generating token");
		}
	}

	if (api_token.empty()) {
		api_token = generate_api_token();
	}

	server.set_mount_point("/", config_home);
	server.set_mount_point("/", resource_home);

	setup_api_handlers();
	setup_tool_pages(api_token, embed_tool_token);
	setup_security(addr, port, api_token);

	server.set_exception_handler(error_handler);

	server.Get("/api/v1/dosbox/info",
	           [](const httplib::Request&, httplib::Response& res) {
		           json j;
		           j["version"]  = DOSBOX_GetDetailedVersion();
		           j["features"] = {
		                   {       "memory",  true},
		                   {        "input",  true},
		                   {"cpu_registers",  true},
		                   {  "cpu_control",  true},
		                   {      "port_io",  true},
		                   {       "freeze",  true},
		                   {     "debugger", false},
		           };
		           // For AppImage runs the executable path points into
		           // the transient squashfs mount; $APPIMAGE is the
		           // relaunchable file. Launcher generators prefer
		           // appimage over binary.
		           j["binary"] = get_executable_file().string();
		           const char* appimage = std::getenv("APPIMAGE");
		           j["appimage"] = appimage ? json(appimage) : json(nullptr);
		           send_json(res, j);
	           });

	// Channel B: write the auto-generated token to a file so launchers
	// can read it without scraping stderr.
	if (use_token_file && !token_from_env) {
		if (write_token_file(api_token)) {
			augra::log_info("webserver",
			                "token written to %s",
			                token_file_path.string().c_str());
		} else {
			augra::log_info("webserver",
			                "API token: %.8s...",
			                api_token.c_str());
		}
	} else if (token_from_env) {
		augra::log_info("webserver", "using API token from DOSBOX_API_TOKEN");
	} else {
		augra::log_info("webserver", "API token: %.8s...", api_token.c_str());
	}

	augra::log_info("webserver",
	                "starting HTTP REST API on http://%s:%d",
	                addr.c_str(),
	                port);

	auto ok = server.listen(addr, port);
	if (!ok) {
		augra::log_warn("webserver",
		                "failed to bind to %s:%d",
		                addr.c_str(),
		                port);
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

	auto tool_token = section.AddString("webserver_tool_token", OnlyAtStart, "embed");
	tool_token->SetValues({"embed", "prompt"});
	tool_token->SetHelp(
	        "How a tool page served by this instance, such as the Cheat Workbench,\n"
	        "obtains the API token ('embed' by default):\n"
	        "  embed:   Write the token into the page for a browser on this machine\n"
	        "           (loopback connections only). Other peers get the page without it.\n"
	        "  prompt:  Leave the token field for pasting.\n"
	        "Never affects the API itself, which requires the Authorization header\n"
	        "on every request.");

	auto osd = section.AddBool("webserver_osd", OnlyAtStart, true);
	osd->SetHelp(
	        "Show on-screen indicators while automation is driving the machine\n"
	        "(script running, recording, replay, injected input). Enabled by\n"
	        "default so it is always clear when the machine is under remote\n"
	        "control. Set to false to hide the overlay.");

	// The mount policy reads these two settings straight from the primary
	// config file (mount_policy.cpp) so that -conf files and command line
	// overrides cannot widen the mount roots. They are registered here so
	// the config system knows them: otherwise every config parse logs an
	// unknown-setting warning, and they would be missing from the
	// generated config and the setting documentation.
	auto mount_bases = section.AddString("mount_allowed_bases", OnlyAtStart, "");
	mount_bases->SetHelp(
	        "Additional base directories that MOUNT may expose to the guest, as\n"
	        "a semicolon-separated list (unset by default). Paths with symlink\n"
	        "components are rejected. For security, this setting is only honored\n"
	        "in the primary config file; -conf files and command line overrides\n"
	        "are ignored.");

	auto mount_image_roots = section.AddString("mount_allowed_image_roots",
	                                           OnlyAtStart,
	                                           "");
	mount_image_roots->SetHelp(
	        "Directories that floppy and CD images may be mounted or swapped\n"
	        "from, as a semicolon-separated list (unset by default). Follows the\n"
	        "same rules as mount_allowed_bases: only the primary config file is\n"
	        "honored.");
}

} // namespace Webserver

static bool is_webserver_enabled                 = false;
static std::optional<WebserverEndpoint> endpoint = {};

static bool is_remote_address(const std::string& addr)
{
	return addr == "0.0.0.0" || addr == "::";
}

void WEBSERVER_Init()
{
	MAPPER_AddHandler(WEBSERVER_WorkbenchHotkey,
	                  SDL_SCANCODE_W,
	                  PRIMARY_MOD | MMOD2,
	                  "workbench",
	                  "Workbench");

	MountPolicy::InitPolicyConfig(get_primary_config_path());

	auto section = get_section("webserver");

	if (section->GetBool("webserver_enabled")) {
		const auto addr = section->GetString("webserver_bind_address");

		if (is_remote_address(addr) &&
		    !section->GetBool("webserver_allow_remote")) {
			augra::log_warn("webserver",
			                "refusing to bind to %s without "
			                "webserver_allow_remote=true",
			                addr.c_str());
			return;
		}

		if (is_remote_address(addr)) {
			augra::log_warn("webserver",
			                "binding to %s - API is exposed to the network",
			                addr.c_str());
		}

		is_webserver_enabled = true;
		const auto port      = section->GetInt("webserver_port");
		endpoint             = WebserverEndpoint{addr, port};

		// Runs before AUTOEXEC_Init, so autoexec MOUNT and BOOT are
		// covered by the whitelist from the first line onwards.
		MountPolicy::SetInjectionPossible(true);

		OSD::OsdManager::Instance().SetEnabled(
		        section->GetBool("webserver_osd"));

		// Guest-side OSD access (osd.com) is part of the
		// automation surface, so it comes and goes with it
		OSDPORT_Init();

		const auto resource_home = get_resource_path("webserver").string();
		const auto use_token_file = section->GetBool("webserver_token_file");
		const bool embed_tool_token = section->GetString(
		                                      "webserver_tool_token") ==
		                              "embed";

		Webserver::InputRecording::InstallHooks();

		std::thread thread(Webserver::run,
		                   addr,
		                   port,
		                   resource_home,
		                   use_token_file,
		                   embed_tool_token);

		thread.detach();
	}
}

void WEBSERVER_Destroy()
{
	OSDPORT_Destroy();
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

std::optional<WebserverEndpoint> WEBSERVER_GetEndpoint()
{
	return endpoint;
}
