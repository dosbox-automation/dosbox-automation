// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_LUA_TEXT_INPUT_H
#define DOSBOX_LUA_TEXT_INPUT_H

#include "hardware/input/keyboard.h"

#include <string_view>
#include <vector>

namespace Lua {

struct KeyStroke {
	KBD_KEYS key = KBD_NONE;
	bool shift   = false;
};

// Single owner of char-to-scancode translation (US layout). Both Lua
// type() and REST /input/type call this. Unmappable chars return KBD_NONE.
KeyStroke CharToKey(char c);

// Translate a whole string, dropping unmappable characters.
std::vector<KeyStroke> TextToStrokes(std::string_view text);

} // namespace Lua

#endif // DOSBOX_LUA_TEXT_INPUT_H
