// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/archive_reader.h"

#include "misc/cross.h"

#include <archive.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <gtest/gtest.h>

#include "archive_fixtures.h"
#include "test_temp_dir.h"

#include <cstdlib>
#include <ctime>

namespace {

namespace fs = std::filesystem;
using namespace ArchiveMount;
using ArchiveFixtures::FileEntry;
using ArchiveFixtures::Kind;
using ArchiveFixtures::MakeStoredZip;
using ArchiveFixtures::StoredEntry;
using ArchiveFixtures::WriteArchive;
using ArchiveFixtures::ZipOptions;

int64_t MtimeOf(const fs::path& path)
{
#if defined(WIN32)
	struct _stat64 st = {};
	_wstat64(path.wstring().c_str(), &st);
#else
	struct stat st = {};
	stat(path.c_str(), &st);
#endif
	return static_cast<int64_t>(st.st_mtime);
}

const Entry* EntryNamed(const ArchiveReader& reader, const std::string& utf8_path)
{
	for (const auto& e : reader.Entries()) {
		if (e.utf8_path == utf8_path) {
			return &e;
		}
	}
	return nullptr;
}

void SetTimeZone(const char* tz)
{
#if defined(WIN32)
	_putenv_s("TZ", tz);
	_tzset();
#else
	setenv("TZ", tz, 1);
	tzset();
#endif
}

std::string Read(ArchiveReader& reader, const int ordinal)
{
	std::vector<uint8_t> out = {};
	auto detail              = std::string();
	EXPECT_EQ(reader.Materialize(ordinal, out, &detail), Error::None) << detail;
	return std::string(out.begin(), out.end());
}

// 3.4.0 is the first release with the RAR 5.0 reader (libarchive NEWS,
// Oct 06 2018 / Jun 11 2019); the design's D5 table depends on it.
TEST(ArchiveReader, LinksALibarchiveWithRar5Support)
{
	EXPECT_GE(archive_version_number(), 3004000);
}

class ArchiveReaderTest : public testing::Test {
protected:
	fs::path tmp_dir = {};
	Limits limits    = {};

	void SetUp() override
	{
		// zip names decode through the code page table in the
		// resources, whose lookup starts at the config dir.
		init_config_dir();
		tmp_dir = TestTempDir::MakeUnique("archive_reader_");
		ASSERT_FALSE(tmp_dir.empty());
	}
	void TearDown() override
	{
		std::error_code ec = {};
		fs::remove_all(tmp_dir, ec);
	}
	fs::path Built(const std::string& name, const Kind kind,
	               const std::vector<FileEntry>& entries,
	               const char* passphrase = nullptr)
	{
		const auto path = tmp_dir / name;
		auto error      = std::string();
		EXPECT_TRUE(WriteArchive(path, kind, entries, passphrase, &error))
		        << error;
		return path;
	}
	fs::path Raw(const std::string& name, const std::vector<uint8_t>& bytes)
	{
		const auto path = tmp_dir / name;
		ArchiveFixtures::WriteBytes(path, bytes);
		return path;
	}
	static std::vector<FileEntry> Game()
	{
		return {
		        {"GAME.EXE", std::string(3000, 'x')},
		        {"data/", "", true},
		        {"data/level1.dat", std::string(500, 'l')},
		        {"readme.txt", "hello"}
                };
	}
};

TEST_F(ArchiveReaderTest, OpensAZipAndListsItsEntriesWithDosNames)
{
	auto opened = ArchiveReader::Open(Built("g.zip", Kind::ZipDeflate, Game()),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(opened.reader->GetFormat(), Format::Zip);
	EXPECT_EQ(opened.reader->GetAccess(), Access::PerMember);
	const auto& entries = opened.reader->Entries();
	ASSERT_EQ(entries.size(), 4u);
	EXPECT_EQ(entries[0].dos_path, "GAME.EXE");
	EXPECT_EQ(entries[0].size, 3000);
	EXPECT_EQ(entries[0].mtime, 631152000);
	EXPECT_TRUE(entries[0].has_crc);
	EXPECT_EQ(entries[1].dos_path, "DATA");
	EXPECT_TRUE(entries[1].is_dir);
	EXPECT_EQ(entries[2].dos_path, "DATA\\LEVEL1.DAT");
	EXPECT_EQ(entries[2].utf8_path, "data/level1.dat");
	EXPECT_EQ(entries[3].dos_path, "README.TXT");
	EXPECT_EQ(opened.reader->TotalBytes(), 3505);
	EXPECT_EQ(opened.reader->ArchiveLength(),
	          static_cast<int64_t>(fs::file_size(tmp_dir / "g.zip")));
}

TEST_F(ArchiveReaderTest, ClassifiesSevenZipAndTarAsSequential)
{
	auto seven = ArchiveReader::Open(Built("g.7z", Kind::SevenZip, Game()),
	                                 limits);
	ASSERT_EQ(seven.error, Error::None) << seven.detail;
	EXPECT_EQ(seven.reader->GetFormat(), Format::SevenZip);
	EXPECT_EQ(seven.reader->GetAccess(), Access::Sequential);
	EXPECT_FALSE(seven.reader->Entries()[0].has_crc);

	for (const auto& [name, kind] : {
	             std::pair{ "g.tar.gz",  Kind::TarGz},
	             std::pair{ "g.tar.xz",  Kind::TarXz},
	             std::pair{"g.tar.zst", Kind::TarZst}
        }) {
		auto tar = ArchiveReader::Open(Built(name, kind, Game()), limits);
		ASSERT_EQ(tar.error, Error::None) << name << ": " << tar.detail;
		EXPECT_EQ(tar.reader->GetFormat(), Format::Tar) << name;
		EXPECT_EQ(tar.reader->GetAccess(), Access::Sequential) << name;
		EXPECT_EQ(tar.reader->Entries().size(), 4u) << name;
	}
}

TEST_F(ArchiveReaderTest, OpensATarMadeFromTheCurrentDirectory)
{
	auto opened = ArchiveReader::Open(
	        Built("dot.tar.gz",
	              Kind::TarGz,
	              {
	                      {"./", "", true},
	                      {"./GAME.EXE", std::string(300, 'x')},
	                      {"./data/", "", true},
	                      {"./data/level1.dat", "l"}
        }),
	        limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	ASSERT_EQ(opened.reader->Entries().size(), 3u);
	EXPECT_EQ(opened.reader->Entries()[0].dos_path, "GAME.EXE");
	EXPECT_EQ(opened.reader->Entries()[2].dos_path, "DATA\\LEVEL1.DAT");
	EXPECT_EQ(Read(*opened.reader, opened.reader->Entries()[2].ordinal), "l");
}

// A regular file named "readme/." would become the directory "readme"
// with its bytes gone. (A file named "./" cannot be built: a zip types it
// by its slash and libarchive's tar writer breaks the archive.)
TEST_F(ArchiveReaderTest, RefusesAFileSpelledAsADirectory)
{
	auto as_dir = ArchiveReader::Open(Raw("dir-file.zip",
	                                      MakeStoredZip({
	                                              {"readme/.", "these bytes"}
        })),
	                                  limits);
	EXPECT_EQ(as_dir.error, Error::EntryRejected) << as_dir.detail;
}

TEST_F(ArchiveReaderTest, DropsSymlinksAndKeepsTheRest)
{
	auto entries = Game();
	entries.push_back({"link.exe", "GAME.EXE", false, true});
	auto opened = ArchiveReader::Open(Built("l.tar.gz", Kind::TarGz, entries),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(opened.reader->Entries().size(), 4u);
}

TEST_F(ArchiveReaderTest, RefusesAnEncryptedZip)
{
	auto opened = ArchiveReader::Open(
	        Built("e.zip", Kind::ZipDeflate, Game(), "secret"), limits);
	EXPECT_EQ(opened.error, Error::Encrypted);
	EXPECT_EQ(opened.reader, nullptr);
}

TEST_F(ArchiveReaderTest, RefusesWhatIsNotAnArchive)
{
	auto opened = ArchiveReader::Open(Raw("t.txt",
	                                      std::vector<uint8_t>(4096, 'x')),
	                                  limits);
	EXPECT_EQ(opened.error, Error::NotAnArchive);
	auto missing = ArchiveReader::Open(tmp_dir / "missing.zip", limits);
	EXPECT_EQ(missing.error, Error::OpenFailed);
}

TEST_F(ArchiveReaderTest, RefusesTooManyEntries)
{
	limits.max_entries = 3;
	auto opened = ArchiveReader::Open(Built("g.zip", Kind::ZipDeflate, Game()),
	                                  limits);
	EXPECT_EQ(opened.error, Error::TooManyEntries);
}

TEST_F(ArchiveReaderTest, RefusesADeclaredTotalOverTheCap)
{
	limits.max_total_bytes = 3000;
	auto opened = ArchiveReader::Open(Built("g.zip", Kind::ZipDeflate, Game()),
	                                  limits);
	EXPECT_EQ(opened.error, Error::TooLarge);
}

TEST_F(ArchiveReaderTest, RefusesAMemberOverTheCapBeforeReadingAnyByte)
{
	limits.max_member_bytes = 1000;
	auto opened = ArchiveReader::Open(Built("g.zip", Kind::ZipDeflate, Game()),
	                                  limits);
	EXPECT_EQ(opened.error, Error::TooLarge);
}

TEST_F(ArchiveReaderTest, RefusesAZip64DeclaredSizeBomb)
{
	auto bomb          = StoredEntry{"huge.bin", "tiny"};
	bomb.declared_size = 5ull * 1024 * 1024 * 1024;
	auto opened = ArchiveReader::Open(Raw("bomb.zip", MakeStoredZip({bomb})),
	                                  limits);
	EXPECT_EQ(opened.error, Error::TooLarge);
}

TEST_F(ArchiveReaderTest, RefusesTraversalAndReservedNames)
{
	auto traversal = ArchiveReader::Open(Raw("tr.zip",
	                                         MakeStoredZip({
	                                                 {"../evil.exe", "x"}
        })),
	                                     limits);
	EXPECT_EQ(traversal.error, Error::EntryRejected);
	EXPECT_NE(traversal.detail.find("evil.exe"), std::string::npos);
	auto absolute = ArchiveReader::Open(Raw("abs.zip",
	                                        MakeStoredZip({
	                                                {"/etc/x", "x"}
        })),
	                                    limits);
	EXPECT_EQ(absolute.error, Error::EntryRejected);
	auto device = ArchiveReader::Open(Raw("dev.zip",
	                                      MakeStoredZip({
	                                              {"CON.TXT", "x"}
        })),
	                                  limits);
	EXPECT_EQ(device.error, Error::EntryRejected);
	// Not "nul.zip": on Windows that name is the NUL device
	auto nul = ArchiveReader::Open(Raw("nulbyte.zip",
	                                   MakeStoredZip({
	                                           {std::string("a\0b", 3), "x"}
        })),
	                               limits);
	EXPECT_EQ(nul.error, Error::EntryRejected);
}

// A RAR holding a zip (or an ISO with zips on it) carries a zip end
// record in its last 64 KiB, and libarchive's zip bidder outbids the
// RAR reader on that; the leading signature decides the reader instead.
TEST_F(ArchiveReaderTest, ARarWithAZipEndRecordInItsTailIsStillARar)
{
	const auto rar = fs::path("tests/files/archives/plain-v4.rar");
	ASSERT_TRUE(fs::exists(rar)) << "run from the source tree";
	auto bytes = std::vector<uint8_t>{};
	{
		auto in = std::ifstream(rar, std::ios::binary);
		bytes.assign(std::istreambuf_iterator<char>(in),
		             std::istreambuf_iterator<char>());
	}
	const auto tail = MakeStoredZip({
	        {"inner.txt", "i"}
        });
	bytes.insert(bytes.end(), tail.begin(), tail.end());
	auto opened = ArchiveReader::Open(Raw("rar-with-zip-tail.rar", bytes), limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(opened.reader->GetFormat(), Format::Rar);
	EXPECT_GE(opened.reader->Entries().size(), 3u);
}

TEST_F(ArchiveReaderTest, ATruncatedZipIsCorruptNotUnrecognized)
{
	auto bytes = MakeStoredZip({
	        {"RUNME.BAT", "@echo off"},
                {"GAME.EXE", std::string(500, 'x')}
        });
	bytes.resize(bytes.size() / 2);
	auto opened = ArchiveReader::Open(Raw("cut.zip", bytes), limits);
	EXPECT_EQ(opened.error, Error::Corrupt);
	EXPECT_NE(opened.detail.find("truncated"), std::string::npos)
	        << opened.detail;
}

// PKZIP 1.x shrank and imploded; libarchive reads neither, and a game
// archive from 1991 says so at mount time, not at the first read.
TEST_F(ArchiveReaderTest, RefusesAZipWithAnUnreadableMethodAtIndex)
{
	auto imploded   = StoredEntry{"MONTY.EXE", "x"};
	imploded.method = 6;
	auto opened     = ArchiveReader::Open(Raw("old.zip",
                                              MakeStoredZip({
                                                      {"README.TXT", "r"},
                                                      imploded
        })),
                                          limits);
	EXPECT_EQ(opened.error, Error::Unsupported);
	EXPECT_NE(opened.detail.find("imploded"), std::string::npos) << opened.detail;
	EXPECT_NE(opened.detail.find("MONTY.EXE"), std::string::npos)
	        << opened.detail;
}

// A lone RAR 5.0 volume from the store failed six members in; the archive
// flags in the main header (rarlab technote, bit 0 = volume) say so before
// any read. The layout is in archive_reader.cpp at Rar5ArchiveFlagVolume.
TEST_F(ArchiveReaderTest, RefusesARar5VolumeByItsMainHeader)
{
	std::vector<uint8_t> bytes = {'R',
	                              'a',
	                              'r',
	                              '!',
	                              0x1a,
	                              0x07,
	                              0x01,
	                              0x00,
	                              0,
	                              0,
	                              0,
	                              0,  // CRC32, unchecked here
	                              4,  // header size
	                              1,  // main header
	                              0,  // header flags
	                              1,  // archive flags: volume
	                              0}; // volume number
	bytes.resize(64, 0);
	auto opened = ArchiveReader::Open(Raw("lone-volume.rar", bytes), limits);
	EXPECT_EQ(opened.error, Error::MultiVolume) << opened.detail;
}

// libarchive walks a zip by local header offset, not by directory
// order; a name must follow its own record's bytes either way.
TEST_F(ArchiveReaderTest, PairsNamesWithTheirDataWhenTheDirectoryIsOutOfOrder)
{
	auto options          = ZipOptions{};
	options.central_order = {2, 0, 1};
	auto opened           = ArchiveReader::Open(Raw("order.zip",
                                              MakeStoredZip(
                                                      {
                                                              {"A.TXT", "aaaa"},
                                                              {"B.TXT", "bbbb"},
                                                              {"C.TXT", "cccc"}
        },
                                                      options)),
                                          limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	ASSERT_EQ(opened.reader->Entries().size(), 3u);
	for (const auto& [name, data] : {
	             std::pair{"A.TXT", "aaaa"},
	             std::pair{"B.TXT", "bbbb"},
	             std::pair{"C.TXT", "cccc"}
        }) {
		const auto* entry = EntryNamed(*opened.reader, name);
		ASSERT_NE(entry, nullptr) << name;
		EXPECT_EQ(Read(*opened.reader, entry->ordinal), data) << name;
	}
}

// libarchive keeps one entry per offset and drops the rest silently,
// which would shift every later name onto the wrong bytes.
TEST_F(ArchiveReaderTest, RefusesAZipWithTwoRecordsAtOneLocalHeaderOffset)
{
	auto second           = StoredEntry{"B.TXT", "bbbb"};
	second.central_offset = 0;
	auto opened           = ArchiveReader::Open(Raw("dupoff.zip",
                                              MakeStoredZip({
                                                      {"A.TXT", "aaaa"},
                                                      second
        })),
                                          limits);
	EXPECT_EQ(opened.error, Error::Corrupt) << opened.detail;
	EXPECT_NE(opened.detail.find("offset"), std::string::npos) << opened.detail;
}

// BBS and XMODEM downloads are padded to 128-byte blocks with 0x1A.
TEST_F(ArchiveReaderTest, OpensAZipWithTrailingPadding)
{
	auto options   = ZipOptions{};
	options.suffix = std::string(128, '\x1a');
	auto opened    = ArchiveReader::Open(Raw("padded.zip",
                                              MakeStoredZip(
                                                      {
                                                              {"A.TXT", "aaaa"}
        },
                                                      options)),
                                          limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(Read(*opened.reader, 0), "aaaa");
}

// A self-extracting stub or any prepended bytes leave the recorded
// offsets short by the same amount; libarchive corrects for it.
TEST_F(ArchiveReaderTest, OpensAZipWithAPrependedStub)
{
	auto options   = ZipOptions{};
	options.prefix = "MZ" + std::string(198, 'S');
	auto opened    = ArchiveReader::Open(Raw("sfx.zip",
                                              MakeStoredZip(
                                                      {
                                                              {"A.TXT", "aaaa"},
                                                              {"B.TXT", "bbbb"}
        },
                                                      options)),
                                          limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(Read(*opened.reader, 1), "bbbb");
}

// libarchive's writer (and zip -fz) writes the zip64 records but keeps
// the real values in the end record; libarchive goes by the locator.
TEST_F(ArchiveReaderTest, OpensAZip64WhoseEndRecordCarriesRealValues)
{
	auto options                 = ZipOptions{};
	options.zip64                = true;
	options.zip64_eocd_sentinels = false;
	auto opened                  = ArchiveReader::Open(Raw("z64real.zip",
                                              MakeStoredZip(
                                                      {
                                                              {"A.TXT", "aaaa"},
                                                              {"B.TXT", "bbbb"}
        },
                                                      options)),
                                          limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(Read(*opened.reader, 1), "bbbb");
}

// libarchive hides __MACOSX resource entries where copyfile.h exists
// (the Mini) and walks them elsewhere; the index must see one set.
TEST_F(ArchiveReaderTest, WalksAFinderZipTheSameOnEveryHost)
{
	auto opened = ArchiveReader::Open(Raw("finder.zip",
	                                      MakeStoredZip({
	                                              {           "A.TXT","aaaa"	                                                                  },
	                                              {       "__MACOSX/",     ""},
	                                              {"__MACOSX/._A.TXT",
	                                               "resource fork"           }
        })),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_NE(EntryNamed(*opened.reader, "__MACOSX/._A.TXT"), nullptr);
	EXPECT_EQ(Read(*opened.reader, 0), "aaaa");
}

// An end record whose directory cannot be read must refuse the archive
// even when libarchive walks nothing behind it.
TEST_F(ArchiveReaderTest, RefusesAZipWhoseDirectoryIsUnreadableBehindAnEmptyWalk)
{
	auto bytes = MakeStoredZip({
	        {"A.TXT", "aaaa"}
        });
	// end record: count at 8 and 10, directory size at 12
	const auto eocd = bytes.size() - 22;
	bytes[eocd + 8] = bytes[eocd + 10] = 3;
	bytes[eocd + 12] = bytes[eocd + 13] = 0;
	auto opened = ArchiveReader::Open(Raw("nodir.zip", bytes), limits);
	EXPECT_EQ(opened.error, Error::Corrupt) << opened.detail;
}

// libarchive's bid refuses an end record it does not like and the open
// fails as "unrecognized"; the directory's own verdict must come first.
TEST_F(ArchiveReaderTest, RefusesTheLastPartOfASpannedZipAsMultiVolume)
{
	auto bytes      = MakeStoredZip({
                {"A.TXT", "aaaa"}
        });
	const auto eocd = bytes.size() - 22;
	bytes[eocd + 4] = bytes[eocd + 6] = 1; // this disk, directory's disk
	auto opened = ArchiveReader::Open(Raw("span.zip", bytes), limits);
	EXPECT_EQ(opened.error, Error::MultiVolume) << opened.detail;
}

TEST_F(ArchiveReaderTest, RefusesAZipWhoseDirectoryRunsPastTheEndRecord)
{
	auto bytes       = MakeStoredZip({
                {"A.TXT", "aaaa"}
        });
	const auto eocd  = bytes.size() - 22;
	bytes[eocd + 13] = 0x7f; // directory size high byte
	auto opened      = ArchiveReader::Open(Raw("past.zip", bytes), limits);
	EXPECT_EQ(opened.error, Error::Corrupt) << opened.detail;
}

// libarchive reports a local header whose CRC disagrees with the
// directory as a warning and then checks the data against the local
// value, so the bytes would pass under the directory's name and CRC.
TEST_F(ArchiveReaderTest, RefusesAZipWhoseLocalHeaderDisagreesWithTheDirectory)
{
	auto lying_crc      = StoredEntry{"GAME.EXE", "these bytes"};
	lying_crc.local_crc = 0x12345678;
	auto by_crc         = ArchiveReader::Open(Raw("crc-lie.zip",
                                              MakeStoredZip({
                                                      {"A.TXT", "aaaa"},
                                                      lying_crc
        })),
                                          limits);
	EXPECT_EQ(by_crc.error, Error::Corrupt) << by_crc.detail;
	EXPECT_NE(by_crc.detail.find("local header"), std::string::npos)
	        << by_crc.detail;

	auto lying_method         = StoredEntry{"GAME.EXE", "these bytes"};
	lying_method.local_method = 6;
	auto by_method            = ArchiveReader::Open(Raw("method-lie.zip",
                                                 MakeStoredZip({lying_method})),
                                             limits);
	EXPECT_EQ(by_method.error, Error::Corrupt) << by_method.detail;
}

// An end record may claim fewer entries than the directory holds; the
// records must fill the directory exactly, as libarchive walks it whole.
TEST_F(ArchiveReaderTest, RefusesADirectoryLongerThanItsEntryCount)
{
	auto bytes      = MakeStoredZip({
                {"A.TXT", "aaaa"},
                {"B.TXT", "bbbb"}
        });
	const auto eocd = bytes.size() - 22;
	bytes[eocd + 8] = bytes[eocd + 10] = 1;
	auto opened = ArchiveReader::Open(Raw("undercount.zip", bytes), limits);
	EXPECT_EQ(opened.error, Error::Corrupt) << opened.detail;
}

// The central directory probe parses whatever zip sits in a file's tail;
// its verdict on names belongs to zips only.
TEST_F(ArchiveReaderTest, ATarHoldingAZipIsNotJudgedByTheInnerNames)
{
	const auto inner = MakeStoredZip({
	        {std::string("a\0b", 3), "x"}
        });
	auto opened      = ArchiveReader::Open(
                Built("outer.tar",
                      Kind::Tar,
	                   {
                              {"readme.txt", "r"},
                              {"inner.zip", std::string(inner.begin(), inner.end())}
        }),
                limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(opened.reader->Entries().size(), 2u);
}

// The index and libarchive must agree on which end record is the one;
// where libarchive finds none or a broken one, the answer is a refusal,
// never an index libarchive will not honour.
TEST_F(ArchiveReaderTest, RefusesAZipWhoseCommentHidesAnEndRecord)
{
	auto options    = ZipOptions{};
	options.comment = "xx" + std::string("PK\x05\x06", 4) +
	                  std::string(18, '\xff');
	auto opened = ArchiveReader::Open(Raw("fake-eocd.zip",
	                                      MakeStoredZip(
	                                              {
	                                                      {"A.TXT", "aaaa"}
        },
	                                              options)),
	                                  limits);
	EXPECT_NE(opened.error, Error::None);
	EXPECT_EQ(opened.reader, nullptr);
}

TEST_F(ArchiveReaderTest, RefusesAZipWhoseCommentIsLongerThanLibarchiveSearches)
{
	auto options    = ZipOptions{};
	options.comment = std::string(17000, 'c');
	auto opened     = ArchiveReader::Open(Raw("longcomment.zip",
                                              MakeStoredZip(
                                                      {
                                                              {"A.TXT", "aaaa"}
        },
                                                      options)),
                                          limits);
	EXPECT_NE(opened.error, Error::None);
	EXPECT_EQ(opened.reader, nullptr);
}

TEST_F(ArchiveReaderTest, RefusesAFileAndADirectoryWithOnePath)
{
	auto file_first = ArchiveReader::Open(Raw("ff.zip",
	                                          MakeStoredZip({
	                                                  {  "a", "x"},
                                                          {"a/x", "y"}
        })),
	                                      limits);
	EXPECT_EQ(file_first.error, Error::EntryRejected) << file_first.detail;
	auto dir_first = ArchiveReader::Open(Raw("df.zip",
	                                         MakeStoredZip({
	                                                 {"a/x", "y"},
                                                         {  "a", "x"}
        })),
	                                     limits);
	EXPECT_EQ(dir_first.error, Error::EntryRejected) << dir_first.detail;
}

TEST_F(ArchiveReaderTest, ReportsTheFormatOfAnEmptyArchive)
{
	auto seven = ArchiveReader::Open(Built("empty.7z", Kind::SevenZip, {}),
	                                 limits);
	ASSERT_EQ(seven.error, Error::None) << seven.detail;
	EXPECT_EQ(seven.reader->GetFormat(), Format::SevenZip);
	auto tar = ArchiveReader::Open(Built("empty.tar.gz", Kind::TarGz, {}), limits);
	ASSERT_EQ(tar.error, Error::None) << tar.detail;
	EXPECT_EQ(tar.reader->GetFormat(), Format::Tar);
	EXPECT_EQ(tar.reader->GetAccess(), Access::Sequential);
}

// libarchive's lrzip and grzip filters run external programs on the
// data; only the filters D12 lists are registered.
TEST_F(ArchiveReaderTest, RefusesAFilterThatWouldRunAProgram)
{
	auto bytes = std::vector<uint8_t>{'L', 'R', 'Z', 'I', 0, 6};
	bytes.resize(4096, 0);
	auto opened = ArchiveReader::Open(Raw("x.tar.lrz", bytes), limits);
	EXPECT_EQ(opened.error, Error::NotAnArchive) << opened.detail;
}

TEST_F(ArchiveReaderTest, KeepsTheFirstOfDuplicateNames)
{
	auto opened = ArchiveReader::Open(Raw("dup.zip",
	                                      MakeStoredZip({
	                                              {"A.TXT",  "first"},
	                                              {"a.txt", "second"}
        })),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	ASSERT_EQ(opened.reader->Entries().size(), 2u);
	EXPECT_EQ(opened.reader->Entries()[0].dos_path, "A.TXT");
	EXPECT_EQ(opened.reader->Entries()[1].dos_path, "A~2.TXT");
}

TEST_F(ArchiveReaderTest, DecodesCp437NamesAndKeepsUtf8Ones)
{
	// 0x81 is u-umlaut in code page 437; the second name is a-umlaut
	// stored as UTF-8 with the flag set
	auto utf8 = StoredEntry{
	        "\xc3\xa4"
	        "rger.txt",
	        "u"};
	utf8.utf8_flag = true;
	auto opened    = ArchiveReader::Open(Raw("n.zip",
                                              MakeStoredZip({
                                                      {"\x81"
	                                                     "bung.txt", "c"},
                                                      utf8
        })),
                                          limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	ASSERT_EQ(opened.reader->Entries().size(), 2u);
	EXPECT_EQ(opened.reader->Entries()[0].utf8_path,
	          "\xc3\xbc"
	          "bung.txt");
	EXPECT_EQ(opened.reader->Entries()[0].dos_path, "_BUNG.TXT");
	EXPECT_EQ(opened.reader->Entries()[1].utf8_path,
	          "\xc3\xa4"
	          "rger.txt");
	EXPECT_EQ(opened.reader->Entries()[1].dos_path, "_RGER.TXT");
}

TEST_F(ArchiveReaderTest, TwoEncodingsOfOneNameAreOneEntry)
{
	auto utf8 = StoredEntry{
	        "\xc3\xbc"
	        "bung.txt",
	        "u"};
	utf8.utf8_flag = true;
	auto opened    = ArchiveReader::Open(Raw("n2.zip",
                                              MakeStoredZip({
                                                      {"\x81"
	                                                     "bung.txt", "c"},
                                                      utf8
        })),
                                          limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	ASSERT_EQ(opened.reader->Entries().size(), 1u);
	EXPECT_EQ(opened.reader->Entries()[0].ordinal, 0);
	EXPECT_EQ(opened.reader->Entries()[0].dos_path, "_BUNG.TXT");
}

TEST_F(ArchiveReaderTest, AZipInsideAZipIsAFile)
{
	const auto inner = MakeStoredZip({
	        {"inner.txt", "i"}
        });
	auto opened      = ArchiveReader::Open(
                Raw("outer.zip",
                    MakeStoredZip({
                            {"inner.zip", std::string(inner.begin(), inner.end())}
        })),
                limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	ASSERT_EQ(opened.reader->Entries().size(), 1u);
	EXPECT_EQ(opened.reader->Entries()[0].dos_path, "INNER.ZIP");
}

// zip and RAR4 store DOS date and time; libarchive turns them into
// epoch seconds through mktime, which follows the host's time zone.
// The identity keys the sidecar, so it must not.
TEST_F(ArchiveReaderTest, IdentityOfADosTimedArchiveDoesNotFollowTheTimeZone)
{
	const auto zip = Raw("tz.zip",
	                     MakeStoredZip({
	                             {"A.TXT", "aaaa"}
        }));
	const auto rar = fs::path("tests/files/archives/plain-v4.rar");
	ASSERT_TRUE(fs::exists(rar)) << "run from the source tree";
	SetTimeZone("UTC0");
	auto zip_utc = ArchiveReader::Open(zip, limits);
	auto rar_utc = ArchiveReader::Open(rar, limits);
	SetTimeZone("EST5");
	auto zip_east = ArchiveReader::Open(zip, limits);
	auto rar_east = ArchiveReader::Open(rar, limits);
	SetTimeZone("");
	ASSERT_EQ(zip_utc.error, Error::None);
	ASSERT_EQ(rar_utc.error, Error::None);
	EXPECT_EQ(zip_utc.reader->IdentityHash(), zip_east.reader->IdentityHash());
	EXPECT_EQ(rar_utc.reader->IdentityHash(), rar_east.reader->IdentityHash());
}

TEST_F(ArchiveReaderTest, IdentityIsStableAcrossOpensAndMovesWithContent)
{
	const auto path = Built("id.zip", Kind::ZipDeflate, Game());
	auto first      = ArchiveReader::Open(path, limits);
	auto second     = ArchiveReader::Open(path, limits);
	ASSERT_EQ(first.error, Error::None);
	EXPECT_EQ(first.reader->IdentityHash(), second.reader->IdentityHash());
	EXPECT_EQ(first.reader->IdentityHash().size(), 64u);

	auto changed    = Game();
	changed[3].data = "hellp";
	auto other = ArchiveReader::Open(Built("id2.zip", Kind::ZipDeflate, changed),
	                                 limits);
	ASSERT_EQ(other.error, Error::None);
	EXPECT_NE(first.reader->IdentityHash(), other.reader->IdentityHash());
}

class ArchiveMaterializeTest : public ArchiveReaderTest {
protected:
	std::vector<uint8_t> big = {};
	void SetUp() override
	{
		ArchiveReaderTest::SetUp();
		big.resize(3 * 1024 * 1024);
		uint32_t x = 12345;
		for (auto& b : big) {
			x = x * 1103515245u + 12345u;
			b = static_cast<uint8_t>(x >> 16);
		}
	}
	std::vector<FileEntry> WithBig()
	{
		auto entries = Game();
		entries.push_back({"big.bin", std::string(big.begin(), big.end())});
		return entries;
	}
	// 7z writes the directory entries after the files, so a member is
	// found by path and read by the ordinal the index recorded for it.
	static int OrdinalOf(const ArchiveReader& reader, const std::string& utf8_path)
	{
		for (const auto& e : reader.Entries()) {
			if (e.utf8_path == utf8_path) {
				return e.ordinal;
			}
		}
		return -1;
	}
};

TEST_F(ArchiveMaterializeTest, ReadsADeflatedMemberIntoMemory)
{
	auto opened = ArchiveReader::Open(Built("m.zip", Kind::ZipDeflate, WithBig()),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	std::vector<uint8_t> out = {};
	auto detail              = std::string();
	ASSERT_EQ(opened.reader->Materialize(4, out, &detail), Error::None) << detail;
	EXPECT_EQ(out, big);
	ASSERT_EQ(opened.reader->Materialize(3, out, &detail), Error::None) << detail;
	EXPECT_EQ(std::string(out.begin(), out.end()), "hello");
}

TEST_F(ArchiveMaterializeTest, ReadsFromASevenZipAndATar)
{
	for (const auto& [name, kind] : {
	             std::pair{    "m.7z", Kind::SevenZip},
	             std::pair{"m.tar.xz",    Kind::TarXz}
        }) {
		auto opened = ArchiveReader::Open(Built(name, kind, WithBig()),
		                                  limits);
		ASSERT_EQ(opened.error, Error::None) << name << ": " << opened.detail;
		std::vector<uint8_t> out = {};
		auto detail              = std::string();
		const auto ordinal       = OrdinalOf(*opened.reader, "big.bin");
		ASSERT_GE(ordinal, 0) << name;
		ASSERT_EQ(opened.reader->Materialize(ordinal, out, &detail),
		          Error::None)
		        << name << ": " << detail;
		EXPECT_EQ(out, big) << name;
	}
}

TEST_F(ArchiveMaterializeTest, RefusesAnUnknownOrdinal)
{
	auto opened = ArchiveReader::Open(Built("o.zip", Kind::ZipDeflate, Game()),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None);
	std::vector<uint8_t> out = {};
	EXPECT_EQ(opened.reader->Materialize(99, out, nullptr), Error::NotFound);
	EXPECT_EQ(opened.reader->Materialize(-1, out, nullptr), Error::NotFound);
}

TEST_F(ArchiveMaterializeTest, RefusesAMemberWhoseCrcDoesNotMatch)
{
	const auto path = Built("crc.zip", Kind::ZipDeflate, WithBig());
	auto opened     = ArchiveReader::Open(path, limits);
	ASSERT_EQ(opened.error, Error::None);
	// Flip a byte deep inside the compressed data of big.bin: the
	// local header sits after the four small entries, so byte 200000
	// of the file is inside the deflate stream of the big member.
	{
		auto f = std::fstream(path,
		                      std::ios::binary | std::ios::in | std::ios::out);
		char c = 0;
		f.seekg(200000);
		f.get(c);
		c = static_cast<char>(c ^ 0x55);
		f.seekp(200000);
		f.put(c);
	}
	// The mtime may not have moved within the second; force the stat
	// pair to match by reopening after the write.
	auto reopened = ArchiveReader::Open(path, limits);
	ASSERT_EQ(reopened.error, Error::None) << reopened.detail;
	std::vector<uint8_t> out = {};
	auto detail              = std::string();
	EXPECT_EQ(reopened.reader->Materialize(4, out, &detail), Error::Corrupt)
	        << detail;
}

TEST_F(ArchiveMaterializeTest, RefusesAMemberShorterThanDeclared)
{
	auto bomb          = StoredEntry{"short.bin", "0123456789"};
	bomb.declared_size = 20;
	auto opened = ArchiveReader::Open(Raw("short.zip", MakeStoredZip({bomb})),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	std::vector<uint8_t> out = {};
	auto detail              = std::string();
	EXPECT_EQ(opened.reader->Materialize(0, out, &detail), Error::Corrupt)
	        << detail;
}

// A declared size just under the member cap with ten bytes behind it
// must not cost the declared size in memory before the first read.
TEST_F(ArchiveMaterializeTest, DoesNotReserveTheDeclaredSizeUpFront)
{
	auto bomb          = StoredEntry{"bomb.bin", "0123456789"};
	bomb.declared_size = 2047ull * 1024 * 1024;
	auto opened = ArchiveReader::Open(Raw("bigbomb.zip", MakeStoredZip({bomb})),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	std::vector<uint8_t> out = {};
	auto detail              = std::string();
	EXPECT_EQ(opened.reader->Materialize(0, out, &detail), Error::Corrupt)
	        << detail;
	EXPECT_LT(out.capacity(), 1024u * 1024);
}

TEST_F(ArchiveMaterializeTest, RefusesToReadAfterTheArchiveChanged)
{
	const auto path = Built("ch.zip", Kind::ZipDeflate, Game());
	auto opened     = ArchiveReader::Open(path, limits);
	ASSERT_EQ(opened.error, Error::None);
	{
		auto f = std::ofstream(path, std::ios::binary | std::ios::app);
		f << "trailing garbage";
	}
	std::vector<uint8_t> out = {};
	auto detail              = std::string();
	EXPECT_EQ(opened.reader->Materialize(3, out, &detail), Error::Changed)
	        << detail;
}

TEST_F(ArchiveMaterializeTest, WritesAMemberToAFileWithItsMtime)
{
	auto opened = ArchiveReader::Open(Built("f.zip", Kind::ZipDeflate, WithBig()),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None);
	const auto target = tmp_dir / "out" / "big.bin";
	auto detail       = std::string();
	ASSERT_EQ(opened.reader->MaterializeToFile(4, target, &detail), Error::None)
	        << detail;
	ASSERT_TRUE(fs::exists(target));
	EXPECT_EQ(fs::file_size(target), big.size());
	EXPECT_EQ(MtimeOf(target), 631152000);
	EXPECT_EQ(std::distance(fs::directory_iterator(tmp_dir / "out"),
	                        fs::directory_iterator()),
	          1)
	        << "no temporary file left behind";
}

#if !defined(WIN32)
// A link planted at the target itself is replaced by the rename, never
// written through; the temporary's own protection is OpenExclusive's.
TEST_F(ArchiveMaterializeTest, ReplacesAPlantedLinkAtTheTarget)
{
	auto opened = ArchiveReader::Open(Built("s.zip", Kind::ZipDeflate, Game()),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None);
	const auto out_dir = tmp_dir / "out";
	fs::create_directories(out_dir);
	const auto victim = tmp_dir / "victim.txt";
	{
		std::ofstream(victim) << "untouched";
	}
	const auto target = out_dir / "readme.txt";
	fs::create_symlink(victim, target);
	auto detail = std::string();
	ASSERT_EQ(opened.reader->MaterializeToFile(3, target, &detail), Error::None)
	        << detail;
	auto victim_text = std::string();
	std::getline(std::ifstream(victim), victim_text);
	EXPECT_EQ(victim_text, "untouched");
	EXPECT_TRUE(fs::is_regular_file(fs::symlink_status(target)));
}
#endif

// A FILETIME of zero in a 7z or an odd tar mtime lands far outside what
// file_time_type can hold on libstdc++; the stamp is clamped, not
// overflowed.
TEST_F(ArchiveMaterializeTest, ClampsAnMtimeTheFileClockCannotHold)
{
	auto entries     = Game();
	entries[3].mtime = -11644473600;
	auto opened = ArchiveReader::Open(Built("old.tar.gz", Kind::TarGz, entries),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	const auto target = tmp_dir / "out" / "readme.txt";
	auto detail       = std::string();
	ASSERT_EQ(opened.reader->MaterializeToFile(3, target, &detail), Error::None)
	        << detail;
	EXPECT_EQ(MtimeOf(target), 0);
}

TEST_F(ArchiveMaterializeTest, ExtractsAWholeSequentialArchiveByOrdinal)
{
	auto opened = ArchiveReader::Open(Built("all.tar.gz", Kind::TarGz, WithBig()),
	                                  limits);
	ASSERT_EQ(opened.error, Error::None);
	const auto dir = tmp_dir / "all";
	auto detail    = std::string();
	ASSERT_EQ(opened.reader->MaterializeAll(dir, &detail), Error::None) << detail;
	EXPECT_TRUE(fs::exists(dir / "0"));
	EXPECT_FALSE(fs::exists(dir / "1")); // the directory entry
	EXPECT_TRUE(fs::exists(dir / "2"));
	EXPECT_TRUE(fs::exists(dir / "3"));
	EXPECT_EQ(fs::file_size(dir / "4"), big.size());
}

// ctest runs from the source tree, so the fixture path is relative; the
// fixtures are checked in, so a missing one is a failure, not a skip.
class RarFixtureTest : public ArchiveReaderTest {
protected:
	static fs::path Fixture(const std::string& name)
	{
		return fs::path("tests/files/archives") / name;
	}
	static bool Missing(const fs::path& fixture)
	{
		EXPECT_TRUE(fs::exists(fixture))
		        << fixture.string()
		        << " is missing; run from the source tree";
		return !fs::exists(fixture);
	}
};

TEST_F(RarFixtureTest, OpensANonSolidRarV4PerMember)
{
	const auto fixture = Fixture("plain-v4.rar");
	if (Missing(fixture)) {
		return;
	}
	auto opened = ArchiveReader::Open(fixture, limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(opened.reader->GetFormat(), Format::Rar);
	EXPECT_EQ(opened.reader->GetAccess(), Access::PerMember);
	ASSERT_GE(opened.reader->Entries().size(), 3u);
	// The last regular file, wherever the writer put the directories
	const Entry* last_file = nullptr;
	for (const auto& e : opened.reader->Entries()) {
		if (!e.is_dir) {
			last_file = &e;
		}
	}
	ASSERT_NE(last_file, nullptr);
	std::vector<uint8_t> out = {};
	auto detail              = std::string();
	ASSERT_EQ(opened.reader->Materialize(last_file->ordinal, out, &detail),
	          Error::None)
	        << detail;
	EXPECT_EQ(out.size(), static_cast<size_t>(last_file->size));
}

TEST_F(RarFixtureTest, RefusesASolidRarV4)
{
	const auto fixture = Fixture("solid-v4.rar");
	if (Missing(fixture)) {
		return;
	}
	auto opened = ArchiveReader::Open(fixture, limits);
	EXPECT_EQ(opened.error, Error::SolidUnsupported) << opened.detail;
}

TEST_F(RarFixtureTest, ClassifiesRar5AsSequential)
{
	const auto fixture = Fixture("plain-v5.rar");
	if (Missing(fixture)) {
		return;
	}
	auto opened = ArchiveReader::Open(fixture, limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(opened.reader->GetFormat(), Format::Rar5);
	EXPECT_EQ(opened.reader->GetAccess(), Access::Sequential);
}

TEST_F(RarFixtureTest, RefusesAnEncryptedRar)
{
	const auto fixture = Fixture("encrypted-v4.rar");
	if (Missing(fixture)) {
		return;
	}
	auto opened = ArchiveReader::Open(fixture, limits);
	EXPECT_EQ(opened.error, Error::Encrypted) << opened.detail;
}

TEST_F(RarFixtureTest, RefusesARarWithEncryptedHeaders)
{
	const auto fixture = Fixture("encrypted-headers-v4.rar");
	if (Missing(fixture)) {
		return;
	}
	auto opened = ArchiveReader::Open(fixture, limits);
	EXPECT_EQ(opened.error, Error::Encrypted) << opened.detail;
}

// libarchive's RAR5 reader says "Encryption is not supported", capital
// E; the classification must not hang on the wording.
TEST_F(RarFixtureTest, RefusesAnEncryptedRar5)
{
	const auto fixture = Fixture("encrypted-v5.rar");
	if (Missing(fixture)) {
		return;
	}
	auto opened = ArchiveReader::Open(fixture, limits);
	EXPECT_EQ(opened.error, Error::Encrypted) << opened.detail;
}

TEST_F(RarFixtureTest, RefusesARar5WithEncryptedHeaders)
{
	const auto fixture = Fixture("encrypted-headers-v5.rar");
	if (Missing(fixture)) {
		return;
	}
	auto opened = ArchiveReader::Open(fixture, limits);
	EXPECT_EQ(opened.error, Error::Encrypted) << opened.detail;
}

TEST_F(RarFixtureTest, RefusesAMultiVolumeRar)
{
	const auto fixture = Fixture("multi-v4.part1.rar");
	if (Missing(fixture)) {
		return;
	}
	auto opened = ArchiveReader::Open(fixture, limits);
	EXPECT_EQ(opened.error, Error::MultiVolume) << opened.detail;
}

} // namespace
