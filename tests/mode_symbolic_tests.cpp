// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/programs/mode.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

// MODE's symbolic names resolve to BIOS mode numbers (RBIL INT 10/AH=00h,
// table 00010): BW40=00h, CO40=01h, BW80=02h, CO80=03h, MONO=07h.
// Adapters without the mode refuse it instead of aliasing to a
// neighbour, which is the bug this table guards against.

using Expected = std::optional<uint16_t>;
using Row      = std::pair<std::string, Expected>;

const std::vector<Row> ega_vga_rows = {
        {"bw40", 0x00}, {"co40", 0x01}, {"bw80", 0x02}, {"co80", 0x03}, {"mono", 0x07}};

const std::vector<Row> cga_family_rows = {
        {"bw40", 0x00}, {"co40", 0x01}, {"bw80", 0x02}, {"co80", 0x03}, {"mono", std::nullopt}};

const std::vector<Row> hercules_rows = {
        {"bw40", std::nullopt}, {"co40", std::nullopt}, {"bw80", 0x07}, {"co80", 0x07}, {"mono", 0x07}};

void expect_rows(const MachineType machine_type, const std::vector<Row>& rows)
{
	for (const auto& [name, expected] : rows) {
		EXPECT_EQ(ResolveSymbolicVideoMode(name, machine_type), expected)
		        << "name=" << name << " machine=" << static_cast<int>(machine_type);
	}
}

TEST(ResolveSymbolicVideoMode, vga_supports_all_five_names)
{
	expect_rows(MachineType::Vga, ega_vga_rows);
}

TEST(ResolveSymbolicVideoMode, ega_supports_all_five_names)
{
	expect_rows(MachineType::Ega, ega_vga_rows);
}

TEST(ResolveSymbolicVideoMode, cga_color_has_bw_modes_but_no_mono_adapter)
{
	expect_rows(MachineType::CgaColor, cga_family_rows);
}

TEST(ResolveSymbolicVideoMode, cga_mono_has_bw_modes_but_no_mono_adapter)
{
	expect_rows(MachineType::CgaMono, cga_family_rows);
}

TEST(ResolveSymbolicVideoMode, pcjr_has_bw_modes_but_no_mono_adapter)
{
	expect_rows(MachineType::Pcjr, cga_family_rows);
}

TEST(ResolveSymbolicVideoMode, tandy_has_bw_modes_but_no_mono_adapter)
{
	expect_rows(MachineType::Tandy, cga_family_rows);
}

TEST(ResolveSymbolicVideoMode, hercules_only_has_mode_7)
{
	expect_rows(MachineType::Hercules, hercules_rows);
}

TEST(ResolveSymbolicVideoMode, unset_machine_resolves_nothing)
{
	for (const auto& [name, unused] : ega_vga_rows) {
		EXPECT_EQ(ResolveSymbolicVideoMode(name, MachineType::None), std::nullopt)
		        << "name=" << name;
	}
}

TEST(ResolveSymbolicVideoMode, only_exact_lowercase_names_resolve)
{
	const std::vector<std::string> rejected = {
	        "", "co", "co80 ", " co80", "CO80", "80x25", "80", "40", "mono7", "bw"};
	for (const auto& name : rejected) {
		EXPECT_EQ(ResolveSymbolicVideoMode(name, MachineType::Vga), std::nullopt)
		        << "name='" << name << "'";
	}
}

} // namespace
