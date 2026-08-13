// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/text_input.h"

namespace Lua {

KeyStroke CharToKey(const char c)
{
	// KBD_KEYS are in QWERTY row order, not alphabetical.
	static constexpr KBD_KEYS alpha_keys[26] = {
	        KBD_a, KBD_b, KBD_c, KBD_d, KBD_e, KBD_f, KBD_g, KBD_h, KBD_i,
	        KBD_j, KBD_k, KBD_l, KBD_m, KBD_n, KBD_o, KBD_p, KBD_q, KBD_r,
	        KBD_s, KBD_t, KBD_u, KBD_v, KBD_w, KBD_x, KBD_y, KBD_z,
	};

	// KBD_KEYS orders the digit row 1..9 then 0, so KBD_0 sits at the end
	// of the run (right before KBD_q). Arithmetic from KBD_0 would walk
	// into the QWERTY letters (e.g. '7' -> KBD_u), so map digits through a
	// table too.
	static constexpr KBD_KEYS digit_keys[10] = {
	        KBD_0, KBD_1, KBD_2, KBD_3, KBD_4,
	        KBD_5, KBD_6, KBD_7, KBD_8, KBD_9,
	};

	if (c >= 'a' && c <= 'z') {
		return {alpha_keys[c - 'a'], false};
	}
	if (c >= 'A' && c <= 'Z') {
		return {alpha_keys[c - 'A'], true};
	}
	if (c >= '0' && c <= '9') {
		return {digit_keys[c - '0'], false};
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

std::vector<KeyStroke> TextToStrokes(const std::string_view text)
{
	std::vector<KeyStroke> strokes;
	strokes.reserve(text.size());
	for (const char c : text) {
		const auto mapping = CharToKey(c);
		if (mapping.key != KBD_NONE) {
			strokes.push_back(mapping);
		}
	}
	return strokes;
}

} // namespace Lua
