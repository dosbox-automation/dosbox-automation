// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/dos.h"

#include <cstdint>
#include <string_view>

#include <gtest/gtest.h>

#include "dos/dos_system.h"
#include "dosbox_test_fixture.h"
#include "hardware/memory.h"

namespace {

class DOSRuntimeSettingsTest : public DOSBoxTestFixture {};

// Regression test for the callback table exhaustion bug.
// Before the fix, every runtime [dos] setting change rebuilt EMS and
// XMS unconditionally. EMS leaked one bare callback slot per cycle
// (call_int67 was never deallocated). With CB_MAX=128 and many slots
// pre-allocated at boot, the table exhausted after ~50 cycles,
// crashing with E_Exit("CALLBACK: Can't allocate handler.").
//
// The fix routes notify_dos_setting_updated by prop_name and makes
// the EMS/XMS pool allocations static. This test cycles 120 EMS
// reinits - enough to exhaust the old table twice over.
TEST_F(DOSRuntimeSettingsTest, EmsReinitDoesNotExhaustCallbackTable)
{
	for (int i = 0; i < 120; ++i) {
		DOS_NotifySettingUpdated("ems");
	}
}

// Verify that changing "ver" does not trigger EMS/XMS rebuilds.
// DOS_GetMemory is a bump allocator: each call returns the current
// watermark and advances it. Calling it once before and once after
// the test loop measures whether the loop consumed any paragraphs.
TEST_F(DOSRuntimeSettingsTest, VerChangeDoesNotRebuildEmsXms)
{
	const uint16_t before = DOS_GetMemory(1);

	for (int i = 0; i < 20; ++i) {
		DOS_NotifySettingUpdated("ver");
	}

	const uint16_t after = DOS_GetMemory(1);

	// before consumed 1 paragraph, after consumed 1. If the loop
	// allocated nothing, after == before + 1.
	EXPECT_EQ(after - before, 1);
}

// Same check for "country" - locale only, no EMS/XMS.
TEST_F(DOSRuntimeSettingsTest, CountryChangeDoesNotRebuildEmsXms)
{
	const uint16_t before = DOS_GetMemory(1);

	for (int i = 0; i < 20; ++i) {
		DOS_NotifySettingUpdated("country");
	}

	const uint16_t after = DOS_GetMemory(1);
	EXPECT_EQ(after - before, 1);
}

// ada-sk8u: the GEMMIS structure (EMMXXXX0 IOCTL read, subfunction 01h,
// asked for by Windows 3.x and 9x) took 0x20 paragraphs from the pool
// again after every EMS rebuild.
TEST_F(DOSRuntimeSettingsTest, EmsReinitKeepsTheGemmisStructure)
{
	const PhysPt buffer       = PhysicalMake(DOS_GetMemory(1), 0);
	const auto request_gemmis = [buffer]() -> uint32_t {
		for (auto* device : Devices) {
			if (device &&
			    std::string_view(device->GetName()) == "EMMXXXX0") {
				mem_writeb(buffer, 0x01);
				uint16_t retcode = 0;
				EXPECT_TRUE(device->ReadFromControlChannel(
				        buffer, 6, &retcode));
				return mem_readd(buffer);
			}
		}
		ADD_FAILURE() << "no EMMXXXX0 device";
		return 0;
	};

	const auto first      = request_gemmis();
	const uint16_t before = DOS_GetMemory(1);

	DOS_NotifySettingUpdated("ems");
	const auto second = request_gemmis();

	const uint16_t after = DOS_GetMemory(1);
	EXPECT_NE(first, 0u);
	EXPECT_EQ(second, first);
	EXPECT_EQ(after - before, 1);
}

// DOS hands its table pool out again after DOS_FreeTableMemory; a GEMMIS
// kept from before would sit in segments someone else now owns.
TEST_F(DOSRuntimeSettingsTest, GemmisIsTakenAgainAfterTheTablePoolIsFreed)
{
	const PhysPt buffer       = PhysicalMake(DOS_GetMemory(1), 0);
	const auto request_gemmis = [buffer]() -> uint16_t {
		for (auto* device : Devices) {
			if (device &&
			    std::string_view(device->GetName()) == "EMMXXXX0") {
				mem_writeb(buffer, 0x01);
				uint16_t retcode = 0;
				EXPECT_TRUE(device->ReadFromControlChannel(
				        buffer, 6, &retcode));
				return static_cast<uint16_t>(mem_readd(buffer) >> 4);
			}
		}
		ADD_FAILURE() << "no EMMXXXX0 device";
		return 0;
	};

	const auto first = request_gemmis();
	const uint16_t top = DOS_GetMemory(1);

	// Refill the pool to its old top, so the live tables stay untouched.
	DOS_FreeTableMemory();
	DOS_GetMemory(static_cast<uint16_t>(top + 1 - DOS_PRIVATE_SEGMENT));
	const auto second = request_gemmis();

	EXPECT_NE(second, first);
	EXPECT_GT(second, top);
}

} // namespace
