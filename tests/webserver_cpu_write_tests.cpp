// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver/private/cpu.h"

#include <gtest/gtest.h>

using Webserver::RegisterKind;
using Webserver::RegClass;

namespace {

TEST(CpuWrite, GeneralRegistersRecognized)
{
	EXPECT_EQ(RegisterKind("eax").reg_class, RegClass::General);
	EXPECT_EQ(RegisterKind("ebx").reg_class, RegClass::General);
	EXPECT_EQ(RegisterKind("ecx").reg_class, RegClass::General);
	EXPECT_EQ(RegisterKind("edx").reg_class, RegClass::General);
	EXPECT_EQ(RegisterKind("esi").reg_class, RegClass::General);
	EXPECT_EQ(RegisterKind("edi").reg_class, RegClass::General);
	EXPECT_EQ(RegisterKind("esp").reg_class, RegClass::General);
	EXPECT_EQ(RegisterKind("ebp").reg_class, RegClass::General);
}

TEST(CpuWrite, SegmentRegistersRecognized)
{
	EXPECT_EQ(RegisterKind("cs").reg_class, RegClass::Segment);
	EXPECT_EQ(RegisterKind("ds").reg_class, RegClass::Segment);
	EXPECT_EQ(RegisterKind("es").reg_class, RegClass::Segment);
	EXPECT_EQ(RegisterKind("ss").reg_class, RegClass::Segment);
	EXPECT_EQ(RegisterKind("fs").reg_class, RegClass::Segment);
	EXPECT_EQ(RegisterKind("gs").reg_class, RegClass::Segment);
}

TEST(CpuWrite, UnknownRejected)
{
	EXPECT_EQ(RegisterKind("banana").reg_class, RegClass::Unknown);
	EXPECT_EQ(RegisterKind("eip").reg_class, RegClass::Unknown);
	EXPECT_EQ(RegisterKind("flags").reg_class, RegClass::Unknown);
}

TEST(CpuWrite, IndicesAreDistinct)
{
	auto eax = RegisterKind("eax");
	auto ebx = RegisterKind("ebx");
	EXPECT_NE(eax.index, ebx.index);
	EXPECT_EQ(eax.reg_class, ebx.reg_class);
}

} // namespace
