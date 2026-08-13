// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "shell/shell_banner.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "dosbox.h"
#include "misc/ansi_code_markup.h"
#include "utils/string_utils.h"

namespace {

std::vector<std::string> split_lines(const std::string& text)
{
	std::vector<std::string> lines = {};
	std::string::size_type start   = 0;
	while (start < text.size()) {
		auto end = text.find('\n', start);
		if (end == std::string::npos) {
			lines.push_back(text.substr(start));
			break;
		}
		lines.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	return lines;
}

std::string render_stripped_banner(const std::string& banner_markup,
                                   const std::string& tag_markup)
{
	return format_str(strip_ansi_markup(banner_markup),
	                  DOSBOX_GetDetailedVersion(),
	                  strip_ansi_markup(tag_markup).c_str());
}

// Minimal .po reader: returns the unescaped msgstr for a msgctxt, or an
// empty string when the entry is absent or untranslated.
std::string read_po_msgstr(const std::string& po_path, const std::string& msgctxt)
{
	std::ifstream po_file(po_path);
	if (!po_file) {
		return {};
	}

	auto unquote = [](const std::string& line) {
		const auto first = line.find('"');
		const auto last  = line.rfind('"');
		if (first == std::string::npos || last <= first) {
			return std::string{};
		}
		return line.substr(first + 1, last - first - 1);
	};

	auto unescape = [](const std::string& text) {
		std::string result = {};
		for (size_t i = 0; i < text.size(); ++i) {
			if (text[i] == '\\' && i + 1 < text.size()) {
				++i;
				switch (text[i]) {
				case 'n': result += '\n'; break;
				case 't': result += '\t'; break;
				default: result += text[i]; break;
				}
			} else {
				result += text[i];
			}
		}
		return result;
	};

	const std::string wanted = "msgctxt \"" + msgctxt + "\"";

	std::string line          = {};
	bool in_wanted_entry      = false;
	bool in_msgstr            = false;
	std::string msgstr_joined = {};

	while (std::getline(po_file, line)) {
		if (line.rfind("msgctxt ", 0) == 0) {
			if (in_wanted_entry && in_msgstr) {
				break;
			}
			in_wanted_entry = (line == wanted);
			in_msgstr       = false;
			continue;
		}
		if (!in_wanted_entry) {
			continue;
		}
		if (line.rfind("msgstr ", 0) == 0) {
			in_msgstr = true;
			msgstr_joined += unquote(line);
		} else if (in_msgstr && !line.empty() && line[0] == '"') {
			msgstr_joined += unquote(line);
		} else if (in_msgstr) {
			break;
		}
	}
	return unescape(msgstr_joined);
}

void expect_lines_fit(const std::string& stripped_banner, const std::string& context)
{
	for (const auto& banner_line : split_lines(stripped_banner)) {
		EXPECT_LE(banner_line.size(),
		          static_cast<size_t>(ShellBannerMaxColumns))
		        << context << " line too wide: '" << banner_line << "'";
	}
}

TEST(ShellBanner, rendered_lines_fit_the_column_limit)
{
	expect_lines_fit(render_stripped_banner(ShellBannerFormat,
	                                        ShellBannerWebserverEnabledTag),
	                 "enabled");
	expect_lines_fit(render_stripped_banner(ShellBannerFormat,
	                                        ShellBannerWebserverDisabledTag),
	                 "disabled");
}

TEST(ShellBanner, first_line_carries_version_and_webserver_state)
{
	const auto enabled  = render_stripped_banner(ShellBannerFormat,
                                                    ShellBannerWebserverEnabledTag);
	const auto disabled = render_stripped_banner(ShellBannerFormat,
	                                             ShellBannerWebserverDisabledTag);

	const auto enabled_line1  = split_lines(enabled).at(0);
	const auto disabled_line1 = split_lines(disabled).at(0);

	EXPECT_NE(enabled_line1.find(DOSBOX_PROJECT_NAME), std::string::npos);
	EXPECT_NE(enabled_line1.find(DOSBOX_GetDetailedVersion()), std::string::npos);
	EXPECT_NE(enabled_line1.find("[webserver enabled]"), std::string::npos);
	EXPECT_NE(disabled_line1.find("[webserver disabled]"), std::string::npos);
}

TEST(ShellBanner, second_line_carries_url_and_command_hints)
{
	const auto banner = render_stripped_banner(ShellBannerFormat,
	                                           ShellBannerWebserverEnabledTag);
	const auto line2  = split_lines(banner).at(1);

	EXPECT_NE(line2.find("https://www.dosbox-automation.org"), std::string::npos);
	EXPECT_NE(line2.find("command list: HELP"), std::string::npos);
	EXPECT_NE(line2.find("instructions: MANUAL"), std::string::npos);
}

TEST(ShellBanner, banner_ends_with_a_blank_line)
{
	const auto banner = render_stripped_banner(ShellBannerFormat,
	                                           ShellBannerWebserverEnabledTag);
	ASSERT_GE(banner.size(), 2u);
	EXPECT_EQ(banner.substr(banner.size() - 2), "\n\n");
}

// The German catalog carries its own banner text; its rendered width must
// obey the same limit (runs against the real de.po, cwd is the source root).
TEST(ShellBanner, german_translation_fits_the_column_limit)
{
	const std::string po_path = "resources/translations/de.po";

	const auto banner_de = read_po_msgstr(po_path, "SHELL_STARTUP_BANNER");
	if (banner_de.empty()) {
		GTEST_SKIP() << "no German banner translation present";
	}

	auto tag_or_default = [&](const std::string& ctxt, const char* fallback) {
		const auto tag = read_po_msgstr(po_path, ctxt);
		return tag.empty() ? std::string(fallback) : tag;
	};

	const auto enabled_tag = tag_or_default("SHELL_STARTUP_WEBSERVER_ENABLED",
	                                        ShellBannerWebserverEnabledTag);
	const auto disabled_tag = tag_or_default("SHELL_STARTUP_WEBSERVER_DISABLED",
	                                         ShellBannerWebserverDisabledTag);

	expect_lines_fit(render_stripped_banner(banner_de, enabled_tag),
	                 "German enabled");
	expect_lines_fit(render_stripped_banner(banner_de, disabled_tag),
	                 "German disabled");
}

} // namespace
