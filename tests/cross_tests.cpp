// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "misc/cross.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#include "utils/env_utils.h"

namespace {

#if defined(WIN32)
TEST(StandardFontDirs, UnsetWindirGivesNoDirectoryInsteadOfCrashing)
{
	const auto saved = get_env_var("WINDIR");
	// An empty value removes the variable on Windows
	_putenv_s("WINDIR", "");

	const auto directories = get_standard_font_dirs();

	_putenv_s("WINDIR", saved.c_str());
	EXPECT_TRUE(directories.empty());
}
#endif

TEST(MallocAligned, SizeRoundsUpToAlignmentMultiple)
{
	EXPECT_EQ(aligned_alloc_size(13, 8), 16u);
	EXPECT_EQ(aligned_alloc_size(16, 8), 16u);
	EXPECT_EQ(aligned_alloc_size(1, 8), 8u);
	EXPECT_EQ(aligned_alloc_size(0, 8), 0u);
}

TEST(MallocAligned, SizeThatCannotBeRoundedIsReportedAsZero)
{
	EXPECT_EQ(aligned_alloc_size(SIZE_MAX, 8), 0u);
	EXPECT_EQ(aligned_alloc_size(SIZE_MAX - 6, 8), 0u);
	EXPECT_EQ(aligned_alloc_size(SIZE_MAX - 7, 8), SIZE_MAX - 7);
	EXPECT_EQ(aligned_alloc_size(5, 0), 5u);
}

TEST(MallocAligned, HugeSizeReturnsNullInsteadOfAZeroLengthBlock)
{
	EXPECT_EQ(malloc_aligned(SIZE_MAX, 8), nullptr);
}

TEST(MallocAligned, OddSizeAllocatesAndIsAligned)
{
	void* p = malloc_aligned(13, 8);
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 8, 0u);
	free_aligned(p);
}

} // namespace
