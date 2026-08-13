// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver/private/io_port.h"

#include <gtest/gtest.h>

using Webserver::ValidatePortRequest;

namespace {

TEST(IoPort, AcceptsByteWidth)
{
	EXPECT_NO_THROW(ValidatePortRequest(0x3B8, 1));
}

TEST(IoPort, AcceptsWordWidth)
{
	EXPECT_NO_THROW(ValidatePortRequest(0x3C4, 2));
}

TEST(IoPort, AcceptsMaxPort)
{
	EXPECT_NO_THROW(ValidatePortRequest(0xFFFF, 1));
}

TEST(IoPort, AcceptsZeroPort)
{
	EXPECT_NO_THROW(ValidatePortRequest(0x0000, 1));
}

TEST(IoPort, RejectsWidth4)
{
	EXPECT_THROW(ValidatePortRequest(0x60, 4), std::invalid_argument);
}

TEST(IoPort, RejectsWidth0)
{
	EXPECT_THROW(ValidatePortRequest(0x60, 0), std::invalid_argument);
}

TEST(IoPort, RejectsPortAbove0xFFFF)
{
	EXPECT_THROW(ValidatePortRequest(0x10000, 1), std::invalid_argument);
}

} // namespace
