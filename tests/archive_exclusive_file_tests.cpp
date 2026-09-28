// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/descriptor.h"
#include "dos/archive/exclusive_file.h"

#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "test_temp_dir.h"

namespace {

namespace fs = std::filesystem;

class ExclusiveFileTest : public testing::Test {
protected:
	fs::path tmp_dir = {};
	void SetUp() override
	{
		tmp_dir = TestTempDir::MakeUnique("exclusive_file_");
		ASSERT_FALSE(tmp_dir.empty());
	}
	void TearDown() override
	{
		std::error_code ec = {};
		fs::remove_all(tmp_dir, ec);
	}
};

TEST_F(ExclusiveFileTest, CreatesAFreshFile)
{
	const auto path = tmp_dir / "fresh.part";
	FILE* f         = ArchiveMount::OpenExclusive(path);
	ASSERT_NE(f, nullptr);
	EXPECT_GE(fputs("data", f), 0);
	EXPECT_EQ(fclose(f), 0);
	EXPECT_TRUE(fs::is_regular_file(path));
	EXPECT_EQ(fs::file_size(path), 4u);
}

#if !defined(WIN32)
TEST_F(ExclusiveFileTest, TheFileIsNotInheritedByChildren)
{
	FILE* f = ArchiveMount::OpenExclusive(tmp_dir / "cloexec.part");
	ASSERT_NE(f, nullptr);
	EXPECT_TRUE(fcntl(fileno(f), F_GETFD) & FD_CLOEXEC);
	fclose(f);
}

TEST_F(ExclusiveFileTest, ADuplicateIsNotInheritedByChildren)
{
	const auto path = tmp_dir / "dup.txt";
	{
		std::ofstream(path) << "abc";
	}
	const auto fd = open(path.c_str(), O_RDONLY);
	ASSERT_GE(fd, 0);
	lseek(fd, 2, SEEK_SET);
	const auto dup_fd = ArchiveMount::DuplicateNoInherit(fd);
	ASSERT_GE(dup_fd, 0);
	EXPECT_TRUE(fcntl(dup_fd, F_GETFD) & FD_CLOEXEC);
	// the same open file description: one offset
	EXPECT_EQ(lseek(dup_fd, 0, SEEK_CUR), 2);
	close(dup_fd);
	close(fd);
}
#endif

TEST_F(ExclusiveFileTest, RefusesAnExistingFile)
{
	const auto path = tmp_dir / "taken.part";
	{
		std::ofstream(path) << "keep";
	}
	EXPECT_EQ(ArchiveMount::OpenExclusive(path), nullptr);
	EXPECT_EQ(fs::file_size(path), 4u);
}

#if !defined(WIN32)
TEST_F(ExclusiveFileTest, DoesNotFollowAPlantedLink)
{
	const auto victim = tmp_dir / "victim.txt";
	{
		std::ofstream(victim) << "untouched";
	}
	const auto link = tmp_dir / "planted.part";
	fs::create_symlink(victim, link);
	EXPECT_EQ(ArchiveMount::OpenExclusive(link), nullptr);
	auto text = std::string();
	std::getline(std::ifstream(victim), text);
	EXPECT_EQ(text, "untouched");
	// a dangling link is no better: the target must not be created
	const auto dangling = tmp_dir / "dangling.part";
	fs::create_symlink(tmp_dir / "never.txt", dangling);
	EXPECT_EQ(ArchiveMount::OpenExclusive(dangling), nullptr);
	EXPECT_FALSE(fs::exists(tmp_dir / "never.txt"));
}
#endif

} // namespace
