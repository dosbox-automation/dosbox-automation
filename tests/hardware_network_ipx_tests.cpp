// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dosbox_test_fixture.h"

#include "hardware/network/ipx.h"
#include "dos/dos.h"
#include "dos/dos_system.h"

namespace {

bool ipxnet_exists()
{
	constexpr auto IpxNetPath = "Z:\\IPXNET.COM";
	const auto fat_attributes = FatAttributeFlags{};
	constexpr auto WantFirstOnly = false;

	return DOS_FindFirst(IpxNetPath, fat_attributes, WantFirstOnly);
}

// IPX is a file-static instance, so it must be destroyed while DOS is still up.
class HardwareNetworkIpxTest : public DOSBoxTestFixture {
	void TearDown() override
	{
		IPX_Destroy();
		DOSBoxTestFixture::TearDown();
	}
};

TEST_F(HardwareNetworkIpxTest, enabling_ipx_at_runtime_creates_ipxnet_com)
{
	EXPECT_FALSE(ipxnet_exists());

	set_section_property_value("ipx", "ipx", "on");
	IPX_Init();

	EXPECT_TRUE(ipxnet_exists());
}

TEST_F(HardwareNetworkIpxTest, disabling_ipx_removes_ipxnet_com)
{
	set_section_property_value("ipx", "ipx", "on");
	IPX_Init();
	ASSERT_TRUE(ipxnet_exists());

	IPX_Destroy();

	EXPECT_FALSE(ipxnet_exists());
}

TEST_F(HardwareNetworkIpxTest, ipx_off_does_not_create_ipxnet_com)
{
	set_section_property_value("ipx", "ipx", "off");
	IPX_Init();

	EXPECT_FALSE(ipxnet_exists());
}

} // namespace
