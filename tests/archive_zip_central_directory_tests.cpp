// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/zip_central_directory.h"

#include "misc/cross.h"

#include <fcntl.h>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "archive_fixtures.h"
#include "test_temp_dir.h"

namespace {

namespace fs = std::filesystem;
using namespace ZipCentralDirectory;
using ArchiveFixtures::MakeStoredZip;
using ArchiveFixtures::StoredEntry;

class ZipCentralDirectoryTest : public testing::Test {
protected:
	fs::path tmp_dir = {};
	void SetUp() override
	{
		// DecodeName's code page table comes from the resources, whose
		// lookup starts at the config dir; main() initializes it.
		init_config_dir();
		tmp_dir = TestTempDir::MakeUnique("zip_cd_");
		ASSERT_FALSE(tmp_dir.empty());
	}
	void TearDown() override
	{
		std::error_code ec = {};
		fs::remove_all(tmp_dir, ec);
	}
	fs::path Write(const std::string& name, const std::vector<uint8_t>& bytes)
	{
		const auto path = tmp_dir / name;
		ArchiveFixtures::WriteBytes(path, bytes);
		return path;
	}
};

TEST_F(ZipCentralDirectoryTest, ReadsNamesSizesAndCrcs)
{
	const auto path   = Write("a.zip",
                                MakeStoredZip({
                                        {    "GAME.EXE",      "hello"},
                                        {"dir/data.bin", "0123456789"}
        }));
	const auto result = Read(path, 100);
	ASSERT_EQ(result.error, Error::None);
	ASSERT_EQ(result.records.size(), 2u);
	EXPECT_EQ(result.records[0].raw_name, "GAME.EXE");
	EXPECT_EQ(result.records[0].uncompressed_size, 5u);
	EXPECT_EQ(result.records[0].crc32, ArchiveFixtures::Crc32("hello"));
	EXPECT_EQ(result.records[1].raw_name, "dir/data.bin");
	EXPECT_EQ(result.records[1].local_header_offset, 30u + 8u + 5u);
	EXPECT_FALSE(result.records[0].utf8_flag);
	EXPECT_FALSE(result.records[0].encrypted);
}

TEST_F(ZipCentralDirectoryTest, ReadsTheSameDirectoryThroughADescriptor)
{
	const auto path = Write("fd.zip",
	                        MakeStoredZip({
	                                {"GAME.EXE", "hello"}
        }));
#if defined(WIN32)
	const auto fd = _wopen(path.wstring().c_str(), _O_RDONLY | _O_BINARY);
#else
	const auto fd = open(path.c_str(), O_RDONLY);
#endif
	ASSERT_GE(fd, 0);
	// from a position the read must put back, after its FILE is closed
#if defined(WIN32)
	_lseeki64(fd, 0, SEEK_SET);
#else
	lseek(fd, 0, SEEK_SET);
#endif
	const auto by_fd   = Read(fd, 100);
	const auto by_path = Read(path, 100);
	ASSERT_EQ(by_fd.error, Error::None);
	ASSERT_EQ(by_fd.records.size(), 1u);
	EXPECT_EQ(by_fd.records[0].raw_name, by_path.records[0].raw_name);
	EXPECT_EQ(by_fd.records[0].crc32, by_path.records[0].crc32);
	// The caller's descriptor is still at the start
	char first = 0;
#if defined(WIN32)
	EXPECT_EQ(_read(fd, &first, 1), 1);
	_close(fd);
#else
	EXPECT_EQ(read(fd, &first, 1), 1);
	close(fd);
#endif
	EXPECT_EQ(first, 'P');
}

TEST_F(ZipCentralDirectoryTest, ReadsTheFlags)
{
	auto utf8          = StoredEntry{"\xc3\xbc.txt", "x"};
	utf8.utf8_flag     = true;
	auto enc           = StoredEntry{"secret.txt", "y"};
	enc.encrypted_flag = true;
	const auto result = Read(Write("f.zip", MakeStoredZip({utf8, enc})), 100);
	ASSERT_EQ(result.error, Error::None);
	EXPECT_TRUE(result.records[0].utf8_flag);
	EXPECT_TRUE(result.records[1].encrypted);
}

TEST_F(ZipCentralDirectoryTest, DecodesCp437NamesWithoutTheUtf8Flag)
{
	// 0x81 is u-umlaut in code page 437
	const auto result = Read(Write("c.zip",
	                               MakeStoredZip({
	                                       {"\x81"
	                                        "bung.txt", "x"}
        })),
	                         100);
	ASSERT_EQ(result.error, Error::None);
	EXPECT_EQ(DecodeName(result.records[0]),
	          "\xc3\xbc"
	          "bung.txt");
}

TEST_F(ZipCentralDirectoryTest, ReadsAZip64Directory)
{
	const auto result = Read(Write("z64.zip",
	                               MakeStoredZip(
	                                       {
	                                               {"a.txt", "aaa"}
        },
	                                       "",
	                                       true)),
	                         100);
	ASSERT_EQ(result.error, Error::None);
	ASSERT_EQ(result.records.size(), 1u);
	EXPECT_EQ(result.records[0].uncompressed_size, 3u);
	EXPECT_EQ(result.records[0].local_header_offset, 0u);
}

// read_zip64_eocd's rules, copied: the record's size field must give 56
// to 16384 bytes, the locator only counts inside libarchive's search
// window, and the record's signature is never checked.
TEST_F(ZipCentralDirectoryTest, RefusesAZip64RecordWithABadSizeField)
{
	auto bytes = MakeStoredZip(
	        {
	                {"a.txt", "aaa"}
        },
	        "",
	        true);
	// zip64 end record sits 20 + 22 bytes before the end; its size
	// field is at offset 4
	const auto z64  = bytes.size() - 22 - 20 - 56;
	bytes[z64 + 4]  = 20;
	const auto path = Write("z64size.zip", bytes);
	EXPECT_EQ(Read(path, 100).error, Error::Malformed);
}

TEST_F(ZipCentralDirectoryTest, IgnoresALocatorOutsideLibarchivesWindow)
{
	// a comment pushes the end record to 10 bytes into the final 16 KiB,
	// so the locator straddles the window start and libarchive skips it
	auto options    = ArchiveFixtures::ZipOptions{};
	options.zip64   = true;
	options.comment = std::string(16384 - 22 - 10, 'c');
	const auto path = Write("z64window.zip",
	                        MakeStoredZip(
	                                {
	                                        {"a.txt", "aaa"}
        },
	                                options));
	EXPECT_EQ(Read(path, 100).error, Error::Malformed);
}

TEST_F(ZipCentralDirectoryTest, TakesAZip64RecordWhateverItsSignature)
{
	auto bytes = MakeStoredZip(
	        {
	                {"a.txt", "aaa"}
        },
	        "",
	        true);
	const auto z64    = bytes.size() - 22 - 20 - 56;
	bytes[z64]        = 'X';
	const auto path   = Write("z64sig.zip", bytes);
	const auto result = Read(path, 100);
	ASSERT_EQ(result.error, Error::None);
	EXPECT_EQ(result.records.size(), 1u);
}

TEST_F(ZipCentralDirectoryTest, ReadsADeclaredSizeOver4GiBFromTheZip64Extra)
{
	auto bomb          = StoredEntry{"huge.bin", "tiny"};
	bomb.declared_size = 5ull * 1024 * 1024 * 1024;
	const auto result = Read(Write("bomb.zip", MakeStoredZip({bomb})), 100);
	ASSERT_EQ(result.error, Error::None);
	EXPECT_EQ(result.records[0].uncompressed_size, 5ull * 1024 * 1024 * 1024);
}

TEST_F(ZipCentralDirectoryTest, FindsTheEndRecordBehindAComment)
{
	const auto result = Read(Write("c.zip",
	                               MakeStoredZip(
	                                       {
	                                               {"a.txt", "a"}
        },
	                                       std::string(1000, 'c'))),
	                         100);
	EXPECT_EQ(result.error, Error::None);
	EXPECT_EQ(result.records.size(), 1u);
}

// Prepended bytes (a self-extracting stub) leave every recorded offset
// short by their length; libarchive corrects by where it finds the
// directory, and the walk here must land on the same local headers.
TEST_F(ZipCentralDirectoryTest, CorrectsLocalOffsetsForPrependedBytes)
{
	auto options      = ArchiveFixtures::ZipOptions{};
	options.prefix    = std::string(100, 'S');
	const auto path   = Write("sfx.zip",
                                MakeStoredZip(
                                        {
                                                {"A.TXT", "aaaa"},
                                                {"B.TXT", "bbbb"}
        },
                                        options));
	const auto result = Read(path, 100);
	ASSERT_EQ(result.error, Error::None);
	ASSERT_EQ(result.records.size(), 2u);
	EXPECT_EQ(result.records[0].local_header_offset, 100u);
	// local header: 30 bytes fixed, 5 of name, 4 of data
	EXPECT_EQ(result.records[1].local_header_offset, 100u + 30 + 5 + 4);
}

TEST_F(ZipCentralDirectoryTest, RefusesANonZip)
{
	EXPECT_EQ(Read(Write("t.txt", std::vector<uint8_t>(3000, 'x')), 100).error,
	          Error::NotAZip);
	EXPECT_EQ(Read(Write("e.bin", {}), 100).error, Error::NotAZip);
}

TEST_F(ZipCentralDirectoryTest, RefusesATruncatedDirectory)
{
	auto bytes = MakeStoredZip({
	        {"a.txt", "a"},
                {"b.txt", "b"}
        });
	// Keep the end record, cut the directory: the end record's offset
	// and size now point past the data it describes.
	std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + 40);
	cut.insert(cut.end(), bytes.end() - 22, bytes.end());
	EXPECT_EQ(Read(Write("cut.zip", cut), 100).error, Error::Truncated);
}

TEST_F(ZipCentralDirectoryTest, RefusesANameRunningPastTheDirectory)
{
	auto bytes = MakeStoredZip({
	        {"abcdef.txt", "a"}
        });
	// The central header's name length is at offset 28 of the record;
	// the record starts where the local header and data end.
	const size_t cd_start = 30 + 10 + 1;
	bytes[cd_start + 28]  = 0xff;
	bytes[cd_start + 29]  = 0x7f;
	EXPECT_EQ(Read(Write("name.zip", bytes), 100).error, Error::Truncated);
}

TEST_F(ZipCentralDirectoryTest, RefusesADirectoryOverTheEntryCap)
{
	std::vector<StoredEntry> many = {};
	for (auto i = 0; i < 5; ++i) {
		many.push_back({"f" + std::to_string(i), "x"});
	}
	EXPECT_EQ(Read(Write("many.zip", MakeStoredZip(many)), 4).error,
	          Error::TooManyEntries);
	EXPECT_EQ(Read(Write("many5.zip", MakeStoredZip(many)), 5).error,
	          Error::None);
}

TEST_F(ZipCentralDirectoryTest, RefusesASpannedArchive)
{
	auto bytes = MakeStoredZip({
	        {"a.txt", "a"}
        });
	// The end record's "number of this disk" field is 4 bytes in
	bytes[bytes.size() - 22 + 4] = 1;
	EXPECT_EQ(Read(Write("span.zip", bytes), 100).error, Error::MultiDisk);
}

// APPNOTE 4.4.5: libarchive reads bzip2 (12), lzma (14), xz (95) and
// zstd (93) only when built with the codec.
TEST(ZipCentralDirectoryParse, MethodsNeedTheirDecoder)
{
	using ZipCentralDirectory::Decoders;
	using ZipCentralDirectory::MethodReadable;
	EXPECT_TRUE(MethodReadable(0, Decoders{false, false, false}));
	EXPECT_TRUE(MethodReadable(8, Decoders{false, false, false}));
	EXPECT_FALSE(MethodReadable(12, Decoders{false, true, true}));
	EXPECT_TRUE(MethodReadable(12, Decoders{true, false, false}));
	EXPECT_FALSE(MethodReadable(14, Decoders{true, false, true}));
	EXPECT_FALSE(MethodReadable(95, Decoders{true, false, true}));
	EXPECT_FALSE(MethodReadable(93, Decoders{true, true, false}));
	EXPECT_TRUE(MethodReadable(93, Decoders{false, false, true}));
	EXPECT_FALSE(MethodReadable(6, Decoders{true, true, true}));
}

// The local header is read for every record; a differing size, CRC or
// method refuses, a zeroed data-descriptor header defers to the directory.
TEST_F(ZipCentralDirectoryTest, RefusesALocalHeaderWhoseSizesDiffer)
{
	auto csize        = StoredEntry{"a.txt", "aaaa"};
	csize.local_csize = 3;
	EXPECT_EQ(Read(Write("csize.zip", MakeStoredZip({csize})), 100).error,
	          Error::LocalHeaderMismatch);
	auto usize        = StoredEntry{"a.txt", "aaaa"};
	usize.local_usize = 5;
	EXPECT_EQ(Read(Write("usize.zip", MakeStoredZip({usize})), 100).error,
	          Error::LocalHeaderMismatch);
}

TEST_F(ZipCentralDirectoryTest, AZeroedLocalHeaderDefersToTheDirectory)
{
	auto descriptor        = StoredEntry{"a.txt", "aaaa"};
	descriptor.local_crc   = 0;
	descriptor.local_csize = 0;
	descriptor.local_usize = 0;
	const auto result = Read(Write("desc.zip", MakeStoredZip({descriptor})), 100);
	ASSERT_EQ(result.error, Error::None);
	EXPECT_EQ(result.records[0].uncompressed_size, 4u);
}

TEST_F(ZipCentralDirectoryTest, RefusesAWrongLocalSignatureAndAnOffsetPastTheEnd)
{
	auto bytes = MakeStoredZip({
	        {"a.txt", "aaaa"}
        });
	bytes[0]   = 'X';
	EXPECT_EQ(Read(Write("badsig.zip", bytes), 100).error,
	          Error::LocalHeaderMismatch);
	// not "far": a Windows header macro
	auto beyond           = StoredEntry{"a.txt", "aaaa"};
	beyond.central_offset = 1000000;
	EXPECT_EQ(Read(Write("beyond.zip", MakeStoredZip({beyond})), 100).error,
	          Error::Truncated);
	auto twin           = StoredEntry{"b.txt", "bbbb"};
	twin.central_offset = 0;
	EXPECT_EQ(Read(Write("twin.zip",
	                     MakeStoredZip({
	                             {"a.txt", "aaaa"},
                                     twin
        })),
	               100)
	                  .error,
	          Error::DuplicateOffset);
}

TEST(ZipCentralDirectoryParse, RefusesRecordsBeyondTheEntryCount)
{
	const auto bytes = MakeStoredZip({
	        {"a.txt", "a"},
                {"b.txt", "b"}
        });
	// two central records of 46 + 5 bytes each, before the end record
	const auto cd_size   = 2 * (46 + 5);
	const auto cd_start  = bytes.size() - 22 - cd_size;
	const auto directory = std::span<const uint8_t>(bytes.data() + cd_start,
	                                                cd_size);
	EXPECT_EQ(Parse(directory, 2, 100).error, Error::None);
	EXPECT_EQ(Parse(directory, 1, 100).error, Error::Malformed);
}

TEST(ZipCentralDirectoryParse, RefusesAWrongSignature)
{
	std::vector<uint8_t> junk(46, 0);
	EXPECT_EQ(Parse(junk, 1, 100).error, Error::Malformed);
}

} // namespace
