// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/lua_api.h"

#include "lua/lua_coroutine.h"
#include "lua/lua_debug_log.h"

#include "capture/capture.h"
#include "gui/osd/osd.h"

#include "dos/programs/mount_policy.h"
#include "hardware/input/keyboard.h"
#include "hardware/input/mouse.h"
#include "hardware/memory.h"
#include "ints/int10.h"
#include "misc/logging.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace Lua {

static constexpr const char* CoroutineKey = "LuaCoroutine";
static constexpr const char* DebugLogKey  = "LuaDebugLog";

static LuaCoroutine* GetCoroutine(lua_State* L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, CoroutineKey);
	auto* co = static_cast<LuaCoroutine*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return co;
}

static DebugLog* GetDebugLog(lua_State* L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, DebugLogKey);
	auto* dl = static_cast<DebugLog*>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return dl;
}

static uint64_t CurrentFrame(lua_State* L)
{
	auto* co = GetCoroutine(L);
	return co ? co->CurrentFrame() : 0;
}

// -- Key name map (shared with webserver/input.cpp) --

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

// Maps single printable ASCII chars to KBD_KEYS for dosbox.type().
// Only handles unshifted keys. Shifted variants (uppercase, symbols
// on number keys) are sent as shift + base key.
struct TypeMapping {
	KBD_KEYS key = KBD_NONE;
	bool shift   = false;
};

static TypeMapping CharToKey(const char c)
{
	if (c >= 'a' && c <= 'z') {
		return {static_cast<KBD_KEYS>(KBD_a + (c - 'a')), false};
	}
	if (c >= 'A' && c <= 'Z') {
		return {static_cast<KBD_KEYS>(KBD_a + (c - 'A')), true};
	}
	if (c >= '0' && c <= '9') {
		return {static_cast<KBD_KEYS>(KBD_0 + (c - '0')), false};
	}

	switch (c) {
	case ' ': return {KBD_space, false};
	case '\n': return {KBD_enter, false};
	case '\t': return {KBD_tab, false};
	case '-': return {KBD_minus, false};
	case '=': return {KBD_equals, false};
	case '[': return {KBD_leftbracket, false};
	case ']': return {KBD_rightbracket, false};
	case '\\': return {KBD_backslash, false};
	case ';': return {KBD_semicolon, false};
	case '\'': return {KBD_quote, false};
	case '`': return {KBD_grave, false};
	case ',': return {KBD_comma, false};
	case '.': return {KBD_period, false};
	case '/': return {KBD_slash, false};
	// Shifted symbols on US layout
	case '!': return {KBD_1, true};
	case '@': return {KBD_2, true};
	case '#': return {KBD_3, true};
	case '$': return {KBD_4, true};
	case '%': return {KBD_5, true};
	case '^': return {KBD_6, true};
	case '&': return {KBD_7, true};
	case '*': return {KBD_8, true};
	case '(': return {KBD_9, true};
	case ')': return {KBD_0, true};
	case '_': return {KBD_minus, true};
	case '+': return {KBD_equals, true};
	case '{': return {KBD_leftbracket, true};
	case '}': return {KBD_rightbracket, true};
	case '|': return {KBD_backslash, true};
	case ':': return {KBD_semicolon, true};
	case '"': return {KBD_quote, true};
	case '~': return {KBD_grave, true};
	case '<': return {KBD_comma, true};
	case '>': return {KBD_period, true};
	case '?': return {KBD_slash, true};
	default: return {KBD_NONE, false};
	}
}

// ================================================================
// Input functions
// ================================================================

// dosbox.key(name, pressed)
static int LuaKey(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);
	luaL_checktype(L, 2, LUA_TBOOLEAN);
	const bool pressed = lua_toboolean(L, 2);

	auto it = key_name_map.find(name);
	if (it == key_name_map.end()) {
		return luaL_error(L, "unknown key name: %s", name);
	}

	KEYBOARD_AddKey(it->second, pressed);

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L),
		          "dosbox.key(%s, %s)",
		          name,
		          pressed ? "true" : "false");
	}

	return 0;
}

// dosbox.type(text)
// Sends key press+release for each character, yielding between them
// for inter-key delay. Since we cannot yield from a C function
// directly, type() sends all keys immediately with no delay. A
// script that needs delays should use key() with wait_frames().
static int LuaType(lua_State* L)
{
	const char* text = luaL_checkstring(L, 1);

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L), "dosbox.type(\"%s\")", text);
	}

	while (*text) {
		const auto mapping = CharToKey(*text);
		if (mapping.key != KBD_NONE) {
			if (mapping.shift) {
				KEYBOARD_AddKey(KBD_leftshift, true);
			}
			KEYBOARD_AddKey(mapping.key, true);
			KEYBOARD_AddKey(mapping.key, false);
			if (mapping.shift) {
				KEYBOARD_AddKey(KBD_leftshift, false);
			}
		}
		++text;
	}

	return 0;
}

// dosbox.mouse_move(dx, dy)
static int LuaMouseMove(lua_State* L)
{
	const auto dx = static_cast<float>(luaL_checknumber(L, 1));
	const auto dy = static_cast<float>(luaL_checknumber(L, 2));

	MOUSE_InjectMoved(dx, dy);

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L),
		          "dosbox.mouse_move(%.1f, %.1f)",
		          static_cast<double>(dx),
		          static_cast<double>(dy));
	}

	return 0;
}

// dosbox.mouse_click(button)
static int LuaMouseClick(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);

	auto it = button_name_map.find(name);
	if (it == button_name_map.end()) {
		return luaL_error(L, "unknown mouse button: %s", name);
	}

	MOUSE_InjectButton(it->second, true);
	MOUSE_InjectButton(it->second, false);

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L), "dosbox.mouse_click(%s)", name);
	}

	return 0;
}

// ================================================================
// Memory functions
// ================================================================

static uint32_t ResolveAddress(lua_State* L, int seg_idx, int off_idx)
{
	const auto seg = static_cast<uint16_t>(luaL_checkinteger(L, seg_idx));
	const auto off = static_cast<uint16_t>(luaL_checkinteger(L, off_idx));
	return PhysicalMake(seg, off);
}

static void ValidateMemRange(lua_State* L, uint32_t addr, size_t len)
{
	const uint64_t mem_total = static_cast<uint64_t>(MEM_TotalPages()) *
	                           MemPageSize;
	const uint64_t end = static_cast<uint64_t>(addr) + len;
	if (end > mem_total) {
		luaL_error(L,
		           "memory access out of range: 0x%x + %d",
		           addr,
		           static_cast<int>(len));
	}
}

// dosbox.mem_read(seg, off, len) -> string
static int LuaMemRead(lua_State* L)
{
	const auto addr = ResolveAddress(L, 1, 2);
	const auto len  = static_cast<size_t>(luaL_checkinteger(L, 3));

	if (len == 0 || len > 1024 * 1024) {
		return luaL_error(L, "mem_read: length must be 1..1048576");
	}

	ValidateMemRange(L, addr, len);

	std::string buf(len, '\0');
	MEM_BlockRead(addr, buf.data(), len);

	lua_pushlstring(L, buf.data(), buf.size());

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L),
		          "dosbox.mem_read(0x%04x, 0x%04x, %d)",
		          static_cast<int>(lua_tointeger(L, 1)),
		          static_cast<int>(lua_tointeger(L, 2)),
		          static_cast<int>(len));
	}

	return 1;
}

// dosbox.mem_write(seg, off, data)
static int LuaMemWrite(lua_State* L)
{
	const auto addr  = ResolveAddress(L, 1, 2);
	size_t len       = 0;
	const char* data = luaL_checklstring(L, 3, &len);

	if (len == 0 || len > 1024 * 1024) {
		return luaL_error(L, "mem_write: data must be 1..1048576 bytes");
	}

	ValidateMemRange(L, addr, len);

	MEM_BlockWrite(addr, data, len);

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L),
		          "dosbox.mem_write(0x%04x, 0x%04x, %d bytes)",
		          static_cast<int>(lua_tointeger(L, 1)),
		          static_cast<int>(lua_tointeger(L, 2)),
		          static_cast<int>(len));
	}

	return 0;
}

// dosbox.mem_read_byte(seg, off) -> integer
static int LuaMemReadByte(lua_State* L)
{
	const auto addr = ResolveAddress(L, 1, 2);
	ValidateMemRange(L, addr, 1);
	lua_pushinteger(L, mem_readb(addr));
	return 1;
}

// dosbox.mem_read_word(seg, off) -> integer
static int LuaMemReadWord(lua_State* L)
{
	const auto addr = ResolveAddress(L, 1, 2);
	ValidateMemRange(L, addr, 2);
	lua_pushinteger(L, mem_readw(addr));
	return 1;
}

// ================================================================
// Screen reading
// ================================================================

static bool IsTextMode()
{
	return INT10_IsTextMode(*CurMode);
}

std::string ReadScreenText()
{
	if (!IsTextMode()) {
		return {};
	}

	const int cols = CurMode->twidth;
	const int rows = CurMode->theight;

	if (cols <= 0 || rows <= 0) {
		return {};
	}

	// Text buffer: character-attribute pairs (2 bytes per cell) at
	// the physical address CurMode->pstart. Access through mem_readw
	// so this works for all text-capable adapters (VGA, CGA, Hercules,
	// Tandy), not just VGA's linear buffer.
	std::string result;
	result.reserve(static_cast<size_t>((cols + 1) * rows));

	for (int row = 0; row < rows; ++row) {
		for (int col = 0; col < cols; ++col) {
			const PhysPt addr = CurMode->pstart +
			                    static_cast<uint32_t>(
			                            (row * cols + col) * 2);
			// Low byte = character, high byte = attribute.
			const uint8_t ch = mem_readb(addr);
			result += static_cast<char>(ch);
		}
		result += '\n';
	}

	return result;
}

// dosbox.screen_text() -> string
static int LuaScreenText(lua_State* L)
{
	const auto text = ReadScreenText();
	lua_pushlstring(L, text.data(), text.size());

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L),
		          "dosbox.screen_text() -> %d bytes",
		          static_cast<int>(text.size()));
	}

	return 1;
}

bool MatchSubstring(const std::string& haystack, const std::string& needle,
                    bool ignorecase)
{
	if (ignorecase) {
		auto lower_haystack = haystack;
		auto lower_needle   = needle;
		std::transform(lower_haystack.begin(),
		               lower_haystack.end(),
		               lower_haystack.begin(),
		               [](unsigned char c) {
			               return static_cast<char>(std::tolower(c));
		               });
		std::transform(lower_needle.begin(),
		               lower_needle.end(),
		               lower_needle.begin(),
		               [](unsigned char c) {
			               return static_cast<char>(std::tolower(c));
		               });
		return lower_haystack.find(lower_needle) != std::string::npos;
	}
	return haystack.find(needle) != std::string::npos;
}

// dosbox.screen_match(pattern [, {ignorecase=true}]) -> bool
static int LuaScreenMatch(lua_State* L)
{
	const char* pattern = luaL_checkstring(L, 1);
	bool ignorecase     = false;

	if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "ignorecase");
		if (lua_isboolean(L, -1)) {
			ignorecase = lua_toboolean(L, -1);
		}
		lua_pop(L, 1);
	}

	const auto text  = ReadScreenText();
	const bool match = MatchSubstring(text, pattern, ignorecase);
	lua_pushboolean(L, match);

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L),
		          "dosbox.screen_match('%s'%s) -> %s",
		          pattern,
		          ignorecase ? ", ignorecase" : "",
		          match ? "true" : "false");
	}

	return 1;
}

// dosbox.wait_for_text(pattern, timeout [, {ignorecase=true}])
// Yields until pattern appears or timeout (in frames) expires.
// Returns true if found, false on timeout.
static int LuaWaitForText(lua_State* L)
{
	const char* pattern = luaL_checkstring(L, 1);
	const auto timeout  = static_cast<int64_t>(luaL_checkinteger(L, 2));
	bool ignorecase     = false;

	if (timeout < 0) {
		return luaL_error(L, "wait_for_text: timeout must be non-negative");
	}

	if (lua_istable(L, 3)) {
		lua_getfield(L, 3, "ignorecase");
		if (lua_isboolean(L, -1)) {
			ignorecase = lua_toboolean(L, -1);
		}
		lua_pop(L, 1);
	}

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L),
		          "dosbox.wait_for_text('%s', %lld%s)",
		          pattern,
		          static_cast<long long>(timeout),
		          ignorecase ? ", ignorecase" : "");
	}

	// Check immediately before yielding.
	const auto text = ReadScreenText();
	if (MatchSubstring(text, pattern, ignorecase)) {
		lua_pushboolean(L, true);

		if (dl && dl->IsOpen()) {
			dl->Trace(CurrentFrame(L),
			          "dosbox.wait_for_text -> true (immediate)");
		}

		return 1;
	}

	// Store the wait parameters for the coroutine to poll.
	// We use upvalues on a continuation function.
	auto* co = GetCoroutine(L);
	if (!co) {
		return luaL_error(L, "wait_for_text: no coroutine context");
	}

	co->SetWaitForText(pattern,
	                   ignorecase,
	                   co->CurrentFrame() + static_cast<uint64_t>(timeout));

	return lua_yield(L, 0);
}

// ================================================================
// Drive management
// ================================================================

// dosbox.mount_lock()
static int LuaMountLock(lua_State* L)
{
	MountPolicy::Lock();

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L), "dosbox.mount_lock()");
	}

	return 0;
}

// ================================================================
// OSD
// ================================================================

static OSD::Color ParseColor(const char* name)
{
	if (!name) {
		return OSD::ColorWhite();
	}
	if (std::string(name) == "green") {
		return OSD::ColorGreen();
	}
	if (std::string(name) == "yellow") {
		return OSD::ColorYellow();
	}
	if (std::string(name) == "red") {
		return OSD::ColorRed();
	}
	if (std::string(name) == "cyan") {
		return OSD::ColorCyan();
	}
	if (std::string(name) == "white") {
		return OSD::ColorWhite();
	}
	return OSD::ColorWhite();
}

// dosbox.osd(text [, opts])
static int LuaOsd(lua_State* L)
{
	const char* text = luaL_checkstring(L, 1);

	OSD::TextOverlay overlay;
	overlay.text     = text;
	overlay.tag      = "lua-osd";
	overlay.position = OSD::Position::BottomCenter;

	if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "color");
		if (lua_isstring(L, -1)) {
			overlay.color = ParseColor(lua_tostring(L, -1));
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "size");
		if (lua_isstring(L, -1)) {
			const std::string sz = lua_tostring(L, -1);
			if (sz == "small") {
				overlay.size = OSD::FontSize::Small;
			} else if (sz == "large") {
				overlay.size = OSD::FontSize::Large;
			}
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "x");
		if (lua_isinteger(L, -1)) {
			overlay.custom_x = static_cast<int>(lua_tointeger(L, -1));
			overlay.position = OSD::Position::Custom;
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "y");
		if (lua_isinteger(L, -1)) {
			overlay.custom_y = static_cast<int>(lua_tointeger(L, -1));
			overlay.position = OSD::Position::Custom;
		} else if (lua_isstring(L, -1)) {
			const std::string pos = lua_tostring(L, -1);
			if (pos == "bottom") {
				overlay.position = OSD::Position::BottomCenter;
			} else if (pos == "top") {
				overlay.position = OSD::Position::TopLeft;
			}
		}
		lua_pop(L, 1);

		lua_getfield(L, 2, "duration");
		if (lua_isinteger(L, -1)) {
			auto* co          = GetCoroutine(L);
			const auto frames = lua_tointeger(L, -1);
			if (co && frames > 0) {
				overlay.expire_frame = static_cast<int64_t>(
				        co->CurrentFrame() +
				        static_cast<uint64_t>(frames));
			}
		}
		lua_pop(L, 1);
	}

	OSD::OsdManager::Instance().ShowText(overlay);

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L), "dosbox.osd(\"%s\")", text);
	}

	return 0;
}

// dosbox.osd_clear()
static int LuaOsdClear(lua_State* L)
{
	OSD::OsdManager::Instance().ClearByTag("lua-osd");

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L), "dosbox.osd_clear()");
	}

	return 0;
}

// ================================================================
// Logging
// ================================================================

// dosbox.log(msg)
static int LuaLog(lua_State* L)
{
	const char* msg = luaL_checkstring(L, 1);
	LOG_MSG("LUA: %s", msg);

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L), "dosbox.log(\"%s\")", msg);
	}

	return 0;
}

// dosbox.debugmsg(msg)
static int LuaDebugMsg(lua_State* L)
{
	const char* msg = luaL_checkstring(L, 1);

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->TraceMessage(CurrentFrame(L), msg);
	}

	return 0;
}

// dosbox.abort(msg)
static int LuaAbort(lua_State* L)
{
	const char* msg = luaL_checkstring(L, 1);

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L), "dosbox.abort(\"%s\")", msg);
	}

	return luaL_error(L, "%s", msg);
}

// ================================================================
// Capture
// ================================================================

// dosbox.capture_start() - start ZMBV video recording
static int LuaCaptureStart(lua_State* L)
{
	CAPTURE_StartVideoCapture();

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L), "dosbox.capture_start()");
	}
	return 0;
}

// dosbox.capture_stop() - stop ZMBV video recording
static int LuaCaptureStop(lua_State* L)
{
	CAPTURE_StopVideoCapture();

	auto* dl = GetDebugLog(L);
	if (dl && dl->IsOpen()) {
		dl->Trace(CurrentFrame(L), "dosbox.capture_stop()");
	}
	return 0;
}

// ================================================================
// Registration
// ================================================================

void RegisterDosboxApi(lua_State* L, LuaCoroutine* coroutine, DebugLog* debug_log)
{
	// Store pointers for the C callbacks.
	lua_pushlightuserdata(L, debug_log);
	lua_setfield(L, LUA_REGISTRYINDEX, DebugLogKey);

	// The dosbox table must already exist (created by
	// LuaCoroutine::RegisterApi).
	lua_getglobal(L, "dosbox");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	// Input
	lua_pushcfunction(L, LuaKey);
	lua_setfield(L, -2, "key");
	lua_pushcfunction(L, LuaType);
	lua_setfield(L, -2, "type");
	lua_pushcfunction(L, LuaMouseMove);
	lua_setfield(L, -2, "mouse_move");
	lua_pushcfunction(L, LuaMouseClick);
	lua_setfield(L, -2, "mouse_click");

	// Memory
	lua_pushcfunction(L, LuaMemRead);
	lua_setfield(L, -2, "mem_read");
	lua_pushcfunction(L, LuaMemWrite);
	lua_setfield(L, -2, "mem_write");
	lua_pushcfunction(L, LuaMemReadByte);
	lua_setfield(L, -2, "mem_read_byte");
	lua_pushcfunction(L, LuaMemReadWord);
	lua_setfield(L, -2, "mem_read_word");

	// Screen
	lua_pushcfunction(L, LuaScreenText);
	lua_setfield(L, -2, "screen_text");
	lua_pushcfunction(L, LuaScreenMatch);
	lua_setfield(L, -2, "screen_match");
	lua_pushcfunction(L, LuaWaitForText);
	lua_setfield(L, -2, "wait_for_text");

	// Drive management
	lua_pushcfunction(L, LuaMountLock);
	lua_setfield(L, -2, "mount_lock");

	// OSD
	lua_pushcfunction(L, LuaOsd);
	lua_setfield(L, -2, "osd");
	lua_pushcfunction(L, LuaOsdClear);
	lua_setfield(L, -2, "osd_clear");

	// Capture
	lua_pushcfunction(L, LuaCaptureStart);
	lua_setfield(L, -2, "capture_start");
	lua_pushcfunction(L, LuaCaptureStop);
	lua_setfield(L, -2, "capture_stop");

	// Output table
	lua_newtable(L);
	lua_setfield(L, -2, "output");

	// Logging
	lua_pushcfunction(L, LuaLog);
	lua_setfield(L, -2, "log");
	lua_pushcfunction(L, LuaDebugMsg);
	lua_setfield(L, -2, "debugmsg");
	lua_pushcfunction(L, LuaAbort);
	lua_setfield(L, -2, "abort");

	lua_pop(L, 1); // pop dosbox table
}

} // namespace Lua
