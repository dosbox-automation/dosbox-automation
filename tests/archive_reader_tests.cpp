// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/archive_reader.h"
#include "dos/archive/descriptor.h"
#include "dos/archive/rar4_headers.h"
#include "dos/archive/zip_central_directory.h"

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#if defined(WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
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
using ArchiveFixtures::Filter;
using ArchiveFixtures::IszChunk;
using ArchiveFixtures::IszOptions;
using ArchiveFixtures::Kind;
using ArchiveFixtures::MakeStoredZip;
using ArchiveFixtures::MinimalIso;
using ArchiveFixtures::MtimeOf;
using ArchiveFixtures::StoredEntry;
using ArchiveFixtures::WriteArchive;
using ArchiveFixtures::WriteCompressed;
using ArchiveFixtures::WriteIsz;
using ArchiveFixtures::ZipOptions;

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
	             std::pair{  "g.tar.gz",   Kind::TarGz},
	             std::pair{  "g.tar.xz",   Kind::TarXz},
	             std::pair{ "g.tar.zst",  Kind::TarZst},
	             std::pair{"g.tar.lzma", Kind::TarLzma}
        }) {
		auto tar = ArchiveReader::Open(Built(name, kind, Game()), limits);
		ASSERT_EQ(tar.error, Error::None) << name << ": " << tar.detail;
		EXPECT_EQ(tar.reader->GetFormat(), Format::Tar) << name;
		EXPECT_EQ(tar.reader->GetAccess(), Access::Sequential) << name;
		EXPECT_EQ(tar.reader->Entries().size(), 4u) << name;
	}
}

TEST_F(ArchiveReaderTest, OpensAnIsoAndListsItsFilesWithoutTheVersionSuffix)
{
	// A CD with a zip on it: the trap disc's shape (nested design N2).
	const auto inner = Built("inner.zip", Kind::ZipDeflate, Game());
	const auto iso   = ArchiveFixtures::IsoWithFiles({
                {       "README.TXT",                       "read me"},
                {"GAMES/THEXDER.ZIP", ArchiveFixtures::ReadAll(inner)},
        });
	auto opened      = ArchiveReader::Open(
                Raw("disc.iso", std::vector<uint8_t>(iso.begin(), iso.end())), limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(opened.reader->GetFormat(), Format::Iso9660);
	EXPECT_EQ(opened.reader->GetAccess(), Access::PerMember);
	auto paths = std::vector<std::string>();
	for (const auto& e : opened.reader->Entries()) {
		paths.push_back(e.utf8_path);
	}
	// libarchive chops the ";1" (archive_read_support_format_iso9660.c:1951)
	// and skips the root "."; the walk's extent order is not promised.
	std::sort(paths.begin(), paths.end());
	EXPECT_EQ(paths,
	          (std::vector<std::string>{"GAMES", "GAMES/THEXDER.ZIP", "README.TXT"}));
	const Entry* zip = nullptr;
	for (const auto& e : opened.reader->Entries()) {
		if (e.utf8_path == "GAMES/THEXDER.ZIP") {
			zip = &e;
		}
	}
	ASSERT_NE(zip, nullptr);
	EXPECT_FALSE(zip->is_dir);
	EXPECT_EQ(zip->size, static_cast<int64_t>(fs::file_size(inner)));
	EXPECT_FALSE(zip->has_crc);
	EXPECT_EQ(zip->stored_offset, -1);
	std::vector<uint8_t> bytes = {};
	auto detail                = std::string();
	ASSERT_EQ(opened.reader->Materialize(zip->ordinal, bytes, &detail),
	          Error::None)
	        << detail;
	EXPECT_EQ(std::string(bytes.begin(), bytes.end()),
	          ArchiveFixtures::ReadAll(inner));
}

// G17: Joliet names past mkisofs' 103-character "-joliet-long" limit, as
// a Windows authoring tool wrote them on the SJAANIBOX disc.
static std::string NameOfLength(const size_t length)
{
	auto name = std::string("Game ");
	while (name.size() + 4 < length) {
		name += static_cast<char>('a' + name.size() % 26);
	}
	return name + ".zip";
}

static std::vector<std::string> JolietListing(const fs::path& dir,
                                              const std::string& name,
                                              const Limits& limits)
{
	const auto iso  = ArchiveFixtures::IsoWithJolietFile(name, "payload");
	const auto path = dir / "joliet.iso";
	ArchiveFixtures::WriteBytes(path,
	                            std::vector<uint8_t>(iso.begin(), iso.end()));
	auto opened = ArchiveReader::Open(path, limits);
	EXPECT_EQ(opened.error, Error::None) << opened.detail;
	auto paths = std::vector<std::string>();
	if (opened.reader) {
		for (const auto& e : opened.reader->Entries()) {
			paths.push_back(e.utf8_path);
		}
	}
	return paths;
}

TEST_F(ArchiveReaderTest, AJolietNameOf103CharactersIsReadWhole)
{
	const auto name = NameOfLength(103);
	EXPECT_EQ(JolietListing(tmp_dir, name, limits),
	          (std::vector<std::string>{name}));
}

// Every libarchive reads 103 whole; a probe that says no here would
// silently skip the long-name tests on every host.
TEST(ArchiveReader, TheJolietProbeReadsA103CharacterName)
{
	EXPECT_TRUE(ArchiveFixtures::LibarchiveReadsJolietName(103));
}

TEST_F(ArchiveReaderTest, AJolietNamePast103CharactersIsReadWhole)
{
	if (!ArchiveFixtures::LibarchiveReadsJolietName(110)) {
		GTEST_SKIP() << "warning: the linked libarchive cuts Joliet names "
		                "at 103 characters (unpatched system library)";
	}
	const auto name = NameOfLength(110);
	EXPECT_EQ(JolietListing(tmp_dir, name, limits),
	          (std::vector<std::string>{name}));
}

TEST_F(ArchiveReaderTest, AJolietNamePast103CharactersLosesOnlyItsVersion)
{
	if (!ArchiveFixtures::LibarchiveReadsJolietName(110)) {
		GTEST_SKIP() << "warning: the linked libarchive cuts Joliet names "
		                "at 103 characters (unpatched system library)";
	}
	const auto name = NameOfLength(108);
	EXPECT_EQ(JolietListing(tmp_dir, name + ";1", limits),
	          (std::vector<std::string>{name}));
}

TEST_F(ArchiveReaderTest, AnIsoWithOnlyARootListsNothingAndIsStillAnIso)
{
	const auto iso = ArchiveFixtures::IsoWithFiles({});
	auto opened    = ArchiveReader::Open(
                Raw("empty.iso", std::vector<uint8_t>(iso.begin(), iso.end())),
                limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(opened.reader->GetFormat(), Format::Iso9660);
	EXPECT_TRUE(opened.reader->Entries().empty());
}

TEST_F(ArchiveReaderTest, AnIsoShorterThanLibarchivesBidIsNotRecognized)
{
	// The 40 KiB MinimalIso mounts in isoDrive and sniffs as an ISO, but
	// libarchive's bidder wants 48 KiB before it looks at sector 16, so
	// such a volume cannot be a chain level. The refusal names that.
	const auto iso = MinimalIso();
	ASSERT_LT(iso.size(), 48u * 1024);
	auto opened = ArchiveReader::Open(
	        Raw("short.iso", std::vector<uint8_t>(iso.begin(), iso.end())),
	        limits);
	EXPECT_EQ(opened.error, Error::NotAnArchive);
	EXPECT_NE(opened.detail.find("Unrecognized"), std::string::npos)
	        << opened.detail;
}

TEST_F(ArchiveReaderTest, AnIsoIsNeverReadThroughAWindow)
{
	// N6: a window holds a zip and nothing else; the sector-16 probe
	// does not run for a window, so an ISO's bytes there are "no zip
	// signature". The member's range comes from the outer directory.
	const auto iso   = ArchiveFixtures::IsoWithFiles({
                {"A.TXT", "a"}
        });
	const auto outer = Built("outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"disc.iso", iso}
        });
	const auto cd    = ZipCentralDirectory::Read(outer, 64);
	ASSERT_EQ(cd.error, ZipCentralDirectory::Error::None);
	ASSERT_EQ(cd.records.size(), 1u);
	const auto bytes = ArchiveFixtures::ReadAll(outer);
	const auto local = static_cast<size_t>(cd.records[0].local_header_offset);
	const auto name_len = static_cast<uint8_t>(bytes[local + 26]) |
	                      (static_cast<uint8_t>(bytes[local + 27]) << 8);
	const auto extra_len = static_cast<uint8_t>(bytes[local + 28]) |
	                       (static_cast<uint8_t>(bytes[local + 29]) << 8);
	const auto window = ByteWindow{static_cast<int64_t>(local + 30 +
	                                                    name_len + extra_len),
	                               static_cast<int64_t>(iso.size())};
	ASSERT_EQ(bytes.substr(static_cast<size_t>(window.offset), 7),
	          std::string(7, '\0'));
	const int fd = OpenReadOnlyShared(outer);
	ASSERT_GE(fd, 0);
	auto opened = ArchiveReader::Open(fd, outer / "disc.iso", limits, window);
	CloseDescriptor(fd);
	EXPECT_EQ(opened.error, Error::NotAnArchive) << opened.detail;
}

TEST_F(ArchiveReaderTest, AnLzmaStreamWithoutATarIsNotAnArchive)
{
	const auto path = tmp_dir / "notes.lzma";
	auto error      = std::string();
	ASSERT_TRUE(WriteCompressed(path, Filter::Lzma, std::string(4096, 'n'), &error))
	        << error;
	auto opened = ArchiveReader::Open(path, limits);
	EXPECT_EQ(opened.error, Error::NotAnArchive) << opened.detail;
	EXPECT_FALSE(opened.reader);
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
	// Reduce (2-5): Info-ZIP dropped it and so do we; shrink and implode
	// are read since ada-unr8.
	auto reduced   = StoredEntry{"MONTY.EXE", "x"};
	reduced.method = 2;
	auto opened    = ArchiveReader::Open(Raw("old.zip",
                                              MakeStoredZip({
                                                      {"README.TXT", "r"},
                                                      reduced
        })),
                                          limits);
	EXPECT_EQ(opened.error, Error::Unsupported);
	EXPECT_NE(opened.detail.find("reduced"), std::string::npos) << opened.detail;
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

TEST_F(ArchiveReaderTest, ADuplicatePathWithDifferentBytesRefusesTheArchive)
{
	auto opened = ArchiveReader::Open(Raw("dup2.zip",
	                                      MakeStoredZip({
	                                              {"a.txt", "first"},
	                                              {"a.txt", "other"}  // same
	                                              // size,
	                                              // other
	                                              // CRC
        })),
	                                  limits);
	EXPECT_EQ(opened.error, Error::EntryRejected) << opened.detail;
	EXPECT_NE(opened.detail.find("twice"), std::string::npos) << opened.detail;

	auto sizes = ArchiveReader::Open(Built("dup3.tar",
	                                       Kind::Tar,
	                                       {
	                                               {"a.txt",  "xx"},
	                                               {"a.txt", "yyy"}
        }),
	                                 limits);
	EXPECT_EQ(sizes.error, Error::EntryRejected) << sizes.detail;
}

TEST_F(ArchiveReaderTest, AnIdenticalDuplicateKeepsTheFirst)
{
	auto opened = ArchiveReader::Open(
	        Raw("dup4.zip",
	            MakeStoredZip({
	                    {"a.txt", "same"},
                            {"b.txt",    "b"},
                            {"a.txt", "same"}
        })),
	        limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(opened.reader->Entries().size(), 2u);
	EXPECT_EQ(Read(*opened.reader, 0), "same");
	// tar carries no CRC: equal sizes with different bytes cannot be
	// told apart, the first wins (documented limit).
	auto tar = ArchiveReader::Open(Built("dup5.tar",
	                                     Kind::Tar,
	                                     {
	                                             {"a.txt", "xx"},
                                                     {"a.txt", "yy"}
        }),
	                               limits);
	ASSERT_EQ(tar.error, Error::None) << tar.detail;
	EXPECT_EQ(tar.reader->Entries().size(), 1u);
}

TEST_F(ArchiveReaderTest, OpensFromADescriptorAndKeepsReadingAfterTheCallerClosesIt)
{
	const auto path = Built("fd.zip", Kind::ZipStored, Game());
	const auto fd   = ArchiveMount::OpenReadOnlyShared(path);
	ASSERT_GE(fd, 0);
	auto opened = ArchiveReader::Open(fd, path, limits);
	ArchiveMount::CloseDescriptor(fd);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	EXPECT_EQ(opened.reader->Entries().size(), 4u);
	EXPECT_EQ(Read(*opened.reader, 3), "hello");
	// The stat pair comes from the descriptor, so it equals the path's.
	auto by_path = ArchiveReader::Open(path, limits);
	ASSERT_EQ(by_path.error, Error::None);
	EXPECT_EQ(opened.reader->IndexedStat().size,
	          by_path.reader->IndexedStat().size);
	EXPECT_EQ(opened.reader->IdentityHash(), by_path.reader->IdentityHash());
}

#if !defined(WIN32)
TEST_F(ArchiveReaderTest, KeepsServingAnUnlinkedArchive)
{
	const auto path = Built("gone.zip", Kind::ZipStored, Game());
	const auto fd   = ArchiveMount::OpenReadOnlyShared(path);
	ASSERT_GE(fd, 0);
	auto opened = ArchiveReader::Open(fd, path, limits);
	ArchiveMount::CloseDescriptor(fd);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	ASSERT_TRUE(fs::remove(path));
	// D16: the session runs on the inode it started with.
	EXPECT_EQ(Read(*opened.reader, 0), std::string(3000, 'x'));
	std::vector<uint8_t> out = {};
	auto detail              = std::string();
	EXPECT_EQ(opened.reader->Materialize(2, out, &detail), Error::None) << detail;
}
#endif

TEST_F(ArchiveReaderTest, ABadDescriptorIsOpenFailed)
{
	auto opened = ArchiveReader::Open(-1, "nothing.zip", limits);
	EXPECT_EQ(opened.error, Error::OpenFailed);
	EXPECT_FALSE(opened.reader);
}

TEST_F(ArchiveReaderTest, AZipBehindAStubIsJudgedByItsDirectoryBeforeTheOpen)
{
	// ada-qbxb: a stub hides the local header signature; the end record
	// is still found. A spanned set's last part tells the paths apart:
	// libarchive's bid says "unrecognized", the directory names it.
	auto options   = ZipOptions{};
	options.prefix = "MZ" + std::string(198, 'S');
	auto bytes     = MakeStoredZip(
                {
                        {"A.TXT", "aaaa"}
        },
                options);
	const auto eocd = bytes.size() - 22;
	bytes[eocd + 4] = bytes[eocd + 6] = 1; // this disk, directory's disk
	auto opened = ArchiveReader::Open(Raw("span-sfx.zip", bytes), limits);
	EXPECT_EQ(opened.error, Error::MultiVolume) << opened.detail;
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

// Same bytes under both spellings: with different bytes the duplicate
// rule refuses the archive (ADuplicatePathWithDifferentBytesRefusesTheArchive).
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
	                                                     "bung.txt", "u"},
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

// A 1.44 MB floppy's bytes with a pattern per sector, so a misplaced
// block shows.
std::string FloppyPattern(const char seed)
{
	std::string image(1474560, '\0');
	for (size_t i = 0; i < image.size(); ++i) {
		image[i] = static_cast<char>(
		        (i / 512 * 13 + i + static_cast<size_t>(seed)) & 0xff);
	}
	return image;
}

std::string SlurpFile(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in),
	        std::istreambuf_iterator<char>()};
}

// Plan 4b D13.1-D13.3: the name decides, the size is counted, the member
// is named after the file and stamped with its mtime.
TEST_F(ArchiveReaderTest, ACompressedImageNameOpensAsOneMemberNamedAfterTheFile)
{
	const auto image = FloppyPattern('a');
	for (const auto& [filter, ext] : {
	             std::pair{ Filter::Gzip,  "gz"},
	             std::pair{   Filter::Xz,  "xz"},
	             std::pair{ Filter::Zstd, "zst"},
	             std::pair{Filter::Bzip2, "bz2"}
        }) {
		const auto path = tmp_dir / (std::string("disk1.img.") + ext);
		ASSERT_TRUE(WriteCompressed(path, filter, image)) << ext;
		auto opened = ArchiveReader::Open(path, limits);
		ASSERT_EQ(opened.error, Error::None) << ext << ": " << opened.detail;
		EXPECT_EQ(opened.reader->GetFormat(), Format::Raw) << ext;
		EXPECT_EQ(opened.reader->GetAccess(), Access::PerMember) << ext;
		ASSERT_EQ(opened.reader->Entries().size(), 1u) << ext;
		const auto& e = opened.reader->Entries()[0];
		EXPECT_EQ(e.ordinal, 0);
		EXPECT_EQ(e.utf8_path, "disk1.img") << ext;
		EXPECT_EQ(e.dos_path, "DISK1.IMG") << ext;
		EXPECT_EQ(e.size, static_cast<int64_t>(image.size())) << ext;
		EXPECT_EQ(e.mtime, MtimeOf(path)) << ext;
		EXPECT_EQ(opened.reader->TotalBytes(),
		          static_cast<int64_t>(image.size()));
		EXPECT_TRUE(Read(*opened.reader, 0) == image) << ext;
	}
}

// libarchive's tar reader bids on a block of zeros (an empty archive's
// end), which is how an ISO's system area and a blank boot sector begin.
TEST_F(ArchiveReaderTest, ACompressedImageThatBeginsWithZerosIsStillOneImage)
{
	std::string floppy(327680, '\0');
	floppy[512]    = '\xFF';
	const auto iso = MinimalIso();
	for (const auto& [name, bytes] : {
	             std::pair{std::string("blank.img.gz"), floppy},
	             std::pair{ std::string("game.iso.xz"),    iso}
        }) {
		const auto filter = name.ends_with(".gz") ? Filter::Gzip
		                                          : Filter::Xz;
		ASSERT_TRUE(WriteCompressed(tmp_dir / name, filter, bytes));
		auto opened = ArchiveReader::Open(tmp_dir / name, limits);
		ASSERT_EQ(opened.error, Error::None) << name << ": " << opened.detail;
		EXPECT_EQ(opened.reader->GetFormat(), Format::Raw) << name;
		ASSERT_EQ(opened.reader->Entries().size(), 1u) << name;
		EXPECT_EQ(opened.reader->Entries()[0].size,
		          static_cast<int64_t>(bytes.size()));
		EXPECT_TRUE(Read(*opened.reader, 0) == bytes) << name;
	}
}

TEST_F(ArchiveReaderTest, ACompressedStreamWithoutAnImageNameIsStillTarOrNothing)
{
	auto error = std::string();
	ASSERT_TRUE(WriteCompressed(tmp_dir / "notes.txt.gz", Filter::Gzip, "hello", &error))
	        << error;
	auto notes = ArchiveReader::Open(tmp_dir / "notes.txt.gz", limits);
	EXPECT_EQ(notes.error, Error::NotAnArchive) << notes.detail;
	auto tar = ArchiveReader::Open(Built("set.tar.gz", Kind::TarGz, Game()),
	                               limits);
	ASSERT_EQ(tar.error, Error::None) << tar.detail;
	EXPECT_EQ(tar.reader->GetFormat(), Format::Tar);
}

// D13.1: the name decides, in the engine (which never decodes, D15) and here
// alike; a tar under an image name is refused with the reason.
TEST_F(ArchiveReaderTest, ATarNamedLikeACompressedImageIsRefused)
{
	auto opened = ArchiveReader::Open(Built("set.img.gz", Kind::TarGz, Game()),
	                                  limits);
	EXPECT_EQ(opened.error, Error::NotAnArchive) << opened.detail;
	EXPECT_NE(opened.detail.find("tar"), std::string::npos) << opened.detail;
	EXPECT_FALSE(opened.reader);
}

TEST(FilterDecodable, FollowsTheDecodersTheHostHas)
{
	using ZipCentralDirectory::Decoders;
	const auto none = Decoders{false, false, false, false};
	for (const auto kind :
	     {HeadKind::Gzip, HeadKind::Bzip2, HeadKind::Xz, HeadKind::Lzma, HeadKind::Zstd}) {
		EXPECT_TRUE(FilterDecodable(kind, Decoders{})) << HeadKindName(kind);
		EXPECT_FALSE(FilterDecodable(kind, none)) << HeadKindName(kind);
	}
	auto no_xz = Decoders{};
	no_xz.xz   = false;
	EXPECT_FALSE(FilterDecodable(HeadKind::Lzma, no_xz));
	EXPECT_TRUE(FilterDecodable(HeadKind::Zstd, no_xz));
	auto no_gzip = Decoders{};
	no_gzip.gzip = false;
	EXPECT_FALSE(FilterDecodable(HeadKind::Gzip, no_gzip));
	EXPECT_TRUE(FilterDecodable(HeadKind::Bzip2, no_gzip));
	for (const auto kind : {HeadKind::Zip,
	                        HeadKind::Rar4,
	                        HeadKind::Rar5,
	                        HeadKind::SevenZip,
	                        HeadKind::Tar,
	                        HeadKind::Isz,
	                        HeadKind::Unknown}) {
		EXPECT_TRUE(FilterDecodable(kind, none)) << HeadKindName(kind);
	}
}

TEST(FiltersForHead, LzmaBidsOnlyOnAHeadTheSniffCalledLzma)
{
	for (const auto head :
	     {HeadKind::Gzip, HeadKind::Bzip2, HeadKind::Xz, HeadKind::Lzma, HeadKind::Zstd}) {
		EXPECT_EQ(FiltersForHead(head), std::vector<HeadKind>{head})
		        << HeadKindName(head);
	}
	EXPECT_TRUE(FiltersForHead(HeadKind::Isz).empty());
	const auto with_magic = std::vector<HeadKind>{HeadKind::Gzip,
	                                              HeadKind::Bzip2,
	                                              HeadKind::Xz,
	                                              HeadKind::Zstd};
	for (const auto head : {HeadKind::Zip,
	                        HeadKind::Rar4,
	                        HeadKind::Rar5,
	                        HeadKind::SevenZip,
	                        HeadKind::Tar,
	                        HeadKind::Unknown}) {
		EXPECT_EQ(FiltersForHead(head), with_magic) << HeadKindName(head);
	}
}

// Without the decoder the raw reader would serve the compressed bytes as
// the image, and the tar reader would answer with no reason at all.
TEST_F(ArchiveReaderTest, AStreamTheHostCannotDecodeIsUnsupported)
{
	auto no_zstd = ZipCentralDirectory::Decoders{};
	no_zstd.zstd = false;
	ASSERT_TRUE(WriteCompressed(tmp_dir / "disk1.img.zst",
	                            Filter::Zstd,
	                            FloppyPattern('z')));
	const auto tar = Built("game.tar.zst", Kind::TarZst, Game());
	SetDecodersForTests(no_zstd);
	const auto image = ArchiveReader::Open(tmp_dir / "disk1.img.zst", limits);
	const auto as_tar = ArchiveReader::Open(tar, limits);
	SetDecodersForTests(std::nullopt);
	EXPECT_EQ(image.error, Error::Unsupported) << image.detail;
	EXPECT_NE(image.detail.find("zstd"), std::string::npos) << image.detail;
	EXPECT_FALSE(image.reader);
	EXPECT_EQ(as_tar.error, Error::Unsupported) << as_tar.detail;
	EXPECT_NE(as_tar.detail.find("zstd"), std::string::npos) << as_tar.detail;
	EXPECT_EQ(ArchiveReader::Open(tmp_dir / "disk1.img.zst", limits).error,
	          Error::None);
}

TEST_F(ArchiveReaderTest, ACompressedImageOverTheMemberCapIsTooLarge)
{
	auto capped             = limits;
	capped.max_member_bytes = 1024;
	ASSERT_TRUE(WriteCompressed(tmp_dir / "big.img.xz",
	                            Filter::Xz,
	                            std::string(4096, 'x')));
	auto opened = ArchiveReader::Open(tmp_dir / "big.img.xz", capped);
	EXPECT_EQ(opened.error, Error::TooLarge) << opened.detail;
	EXPECT_FALSE(opened.reader);
}

// A CD-sized chunk count, so data_offs lies past the 68-byte trailer.
TEST_F(ArchiveReaderTest, AnIszOfManyChunksOpens)
{
	const auto iso = MinimalIso();
	for (const bool trailer : {true, false}) {
		auto o          = ArchiveFixtures::IszOptions{};
		o.block_size    = 2048;
		o.trailer       = trailer;
		const auto path = tmp_dir / "many.isz";
		ASSERT_TRUE(WriteIsz(path, iso, IszChunk::Zlib, o));
		auto opened = ArchiveReader::Open(path, limits);
		ASSERT_EQ(opened.error, Error::None)
		        << "trailer " << trailer << ": " << opened.detail;
		EXPECT_TRUE(Read(*opened.reader, 0) == iso);
	}
}

TEST_F(ArchiveReaderTest, AnIszOpensAsOneIsoAndMaterializesByteForByte)
{
	const auto iso = MinimalIso() + std::string(64 * 1024, 'q');
	for (const auto kind : {IszChunk::Zlib, IszChunk::Bzip2, IszChunk::Stored}) {
		const auto path = tmp_dir / "Game Disc.isz";
		ASSERT_TRUE(WriteIsz(path, iso, kind));
		auto opened = ArchiveReader::Open(path, limits);
		ASSERT_EQ(opened.error, Error::None) << opened.detail;
		EXPECT_EQ(opened.reader->GetFormat(), Format::Isz);
		EXPECT_EQ(opened.reader->GetAccess(), Access::PerMember);
		ASSERT_EQ(opened.reader->Entries().size(), 1u);
		const auto& e = opened.reader->Entries()[0];
		EXPECT_EQ(e.utf8_path, "Game Disc.iso");
		EXPECT_EQ(e.dos_path, "GAMEDI~1.ISO");
		EXPECT_EQ(e.size, static_cast<int64_t>(iso.size()));
		EXPECT_EQ(e.mtime, MtimeOf(path));
		EXPECT_TRUE(Read(*opened.reader, 0) == iso);
		const auto target = tmp_dir / "out.iso";
		auto detail       = std::string();
		ASSERT_EQ(opened.reader->MaterializeToFile(0, target, &detail),
		          Error::None)
		        << detail;
		EXPECT_TRUE(SlurpFile(target) == iso);
		EXPECT_EQ(MtimeOf(target), MtimeOf(path));
		fs::remove(target);
	}
}

// Without a chunk table (D13.4) the content CRC covers the first and the
// last 64 KiB of the raw image after the header.
TEST_F(ArchiveReaderTest, AnIszWithoutATableTakesItsContentCrcFromTheImagesEnds)
{
	const auto iso = MinimalIso() + std::string(200 * 1024, 'q') +
	                 std::string(2048, 'z');
	auto o          = IszOptions{};
	o.table         = false;
	const auto path = tmp_dir / "raw.isz";
	ASSERT_TRUE(WriteIsz(path, iso, IszChunk::Stored, o));
	auto opened = ArchiveReader::Open(path, limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	ASSERT_EQ(opened.reader->Entries().size(), 1u);
	constexpr size_t Window = 64 * 1024;
	const auto ends = iso.substr(0, Window) + iso.substr(iso.size() - Window);
	const auto expected = crc32(crc32(0L, Z_NULL, 0),
	                            reinterpret_cast<const Bytef*>(ends.data()),
	                            static_cast<uInt>(ends.size()));
	EXPECT_TRUE(opened.reader->Entries()[0].has_crc);
	EXPECT_EQ(opened.reader->Entries()[0].crc32, static_cast<uint32_t>(expected));
	EXPECT_TRUE(Read(*opened.reader, 0) == iso);
}

TEST_F(ArchiveReaderTest, AnEncryptedOrSegmentedIszIsRefusedByReason)
{
	auto enc     = IszOptions{};
	enc.password = 2;
	ASSERT_TRUE(WriteIsz(tmp_dir / "e.isz", MinimalIso(), IszChunk::Stored, enc));
	EXPECT_EQ(ArchiveReader::Open(tmp_dir / "e.isz", limits).error,
	          Error::Encrypted);
	auto seg     = IszOptions{};
	seg.seg_offs = 48;
	ASSERT_TRUE(WriteIsz(tmp_dir / "s.isz", MinimalIso(), IszChunk::Stored, seg));
	EXPECT_EQ(ArchiveReader::Open(tmp_dir / "s.isz", limits).error,
	          Error::MultiVolume);
}

// ada-ciru item 5: the count pass stops at the archive cap, not the 2 GiB
// member cap; its message ("'name': over N bytes") tells it apart from the
// later total check ("declared total over N bytes").
TEST_F(ArchiveReaderTest, ARawCountStopsAtTheArchiveCap)
{
	auto capped            = limits;
	capped.max_total_bytes = 2048;
	ASSERT_TRUE(WriteCompressed(tmp_dir / "big.img.gz",
	                            Filter::Gzip,
	                            std::string(1024 * 1024, 'x')));
	auto opened = ArchiveReader::Open(tmp_dir / "big.img.gz", capped);
	EXPECT_EQ(opened.error, Error::TooLarge) << opened.detail;
	EXPECT_NE(opened.detail.find("'big.img': over 2048 bytes"), std::string::npos)
	        << opened.detail;
}

// ada-ciru item 6: two compressed images of the same name, size, date and
// container length still differ in identity when their content differs.
TEST_F(ArchiveReaderTest, TheIdentityOfACompressedImageFollowsItsContent)
{
	fs::create_directories(tmp_dir / "a");
	fs::create_directories(tmp_dir / "b");
	const auto a = tmp_dir / "a" / "disk1.img.gz";
	const auto b = tmp_dir / "b" / "disk1.img.gz";
	ASSERT_TRUE(WriteCompressed(a, Filter::Gzip, std::string(1474560, 'a')));
	ASSERT_TRUE(WriteCompressed(b, Filter::Gzip, std::string(1474560, 'b')));
	ASSERT_EQ(fs::file_size(a), fs::file_size(b));
	fs::last_write_time(b, fs::last_write_time(a));
	auto open_a = ArchiveReader::Open(a, limits);
	auto open_b = ArchiveReader::Open(b, limits);
	ASSERT_TRUE(open_a.reader && open_b.reader) << open_a.detail << open_b.detail;
	EXPECT_NE(open_a.reader->IdentityHash(), open_b.reader->IdentityHash());
}

// ada-ciru item 6 for ISZ: same name, size, date and container length,
// different bytes in the image, different identity.
TEST_F(ArchiveReaderTest, TheIdentityOfAnIszFollowsItsContent)
{
	fs::create_directories(tmp_dir / "a");
	fs::create_directories(tmp_dir / "b");
	const auto a                  = tmp_dir / "a" / "game.isz";
	const auto b                  = tmp_dir / "b" / "game.isz";
	auto image_a                  = MinimalIso();
	auto image_b                  = image_a;
	image_b[image_b.size() - 100] = 'X';
	ASSERT_TRUE(WriteIsz(a, image_a, IszChunk::Stored));
	ASSERT_TRUE(WriteIsz(b, image_b, IszChunk::Stored));
	ASSERT_EQ(fs::file_size(a), fs::file_size(b));
	fs::last_write_time(b, fs::last_write_time(a));
	auto open_a = ArchiveReader::Open(a, limits);
	auto open_b = ArchiveReader::Open(b, limits);
	ASSERT_TRUE(open_a.reader && open_b.reader) << open_a.detail << open_b.detail;
	EXPECT_NE(open_a.reader->IdentityHash(), open_b.reader->IdentityHash());
}

// D2's identity for a container: the listing (name, size, the container's
// date), the container's length and, since ada-ciru item 6, a content CRC.
TEST_F(ArchiveReaderTest, TheIdentityOfAContainerFollowsItsListingAndLength)
{
	fs::create_directories(tmp_dir / "a");
	fs::create_directories(tmp_dir / "b");
	fs::create_directories(tmp_dir / "c");
	const auto a = tmp_dir / "a" / "disk1.img.xz";
	const auto b = tmp_dir / "b" / "disk1.img.xz";
	const auto c = tmp_dir / "c" / "disk1.img.xz";
	ASSERT_TRUE(WriteCompressed(a, Filter::Xz, FloppyPattern('a')));
	ASSERT_TRUE(WriteCompressed(b, Filter::Xz, FloppyPattern('a')));
	ASSERT_TRUE(WriteCompressed(c, Filter::Xz, std::string(1474560, 'c')));
	for (const auto& p : {a, b, c}) {
		fs::last_write_time(p, fs::last_write_time(a));
	}
	auto open_a = ArchiveReader::Open(a, limits);
	auto open_b = ArchiveReader::Open(b, limits);
	auto open_c = ArchiveReader::Open(c, limits);
	ASSERT_TRUE(open_a.reader && open_b.reader && open_c.reader) << open_a.detail;
	EXPECT_EQ(open_a.reader->IdentityHash(), open_b.reader->IdentityHash());
	EXPECT_NE(open_a.reader->IdentityHash(), open_c.reader->IdentityHash());
}

TEST_F(ArchiveReaderTest, MaterializeAllWritesTheLoneMemberAsOrdinalZero)
{
	const auto floppy = FloppyPattern('m');
	ASSERT_TRUE(WriteCompressed(tmp_dir / "disk.ima.gz", Filter::Gzip, floppy));
	const auto iso = MinimalIso();
	ASSERT_TRUE(WriteIsz(tmp_dir / "cd.isz", iso, IszChunk::Zlib));
	for (const auto& [name, bytes] : {
	             std::pair{std::string("disk.ima.gz"), floppy},
	             std::pair{     std::string("cd.isz"),    iso}
        }) {
		auto opened = ArchiveReader::Open(tmp_dir / name, limits);
		ASSERT_EQ(opened.error, Error::None) << name << ": " << opened.detail;
		const auto out = tmp_dir / ("all-" + name);
		auto detail    = std::string();
		ASSERT_EQ(opened.reader->MaterializeAll(out, &detail), Error::None)
		        << detail;
		EXPECT_TRUE(SlurpFile(out / "0") == bytes) << name;
	}
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

// The cut lands in big.bin's data, past the first header, so the walk
// meets it; a cut inside the first header fails libarchive's open.
TEST_F(ArchiveMaterializeTest, ATruncatedTarLzmaIsCorrupt)
{
	const auto path = Built("cut.tar.lzma", Kind::TarLzma, WithBig());
	const auto full = fs::file_size(path);
	fs::resize_file(path, full / 2);
	auto opened = ArchiveReader::Open(path, limits);
	EXPECT_EQ(opened.error, Error::Corrupt) << opened.detail;
	EXPECT_FALSE(opened.reader);
}

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

// Only tar sits behind a filter (D12): with every reader registered the
// streaming RAR reader would open this. (A gzip of a zip tells nothing,
// the seekable zip reader cannot bid on a stream.) Either error is fine.
TEST_F(RarFixtureTest, AGzipOfARarIsNotAnArchive)
{
	const auto fixture = Fixture("plain-v4.rar");
	if (Missing(fixture)) {
		return;
	}
	auto in    = std::ifstream(fixture, std::ios::binary);
	auto bytes = std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
	                                  std::istreambuf_iterator<char>());
	const auto path = tmp_dir / "rar.gz";
	auto error      = std::string();
	ASSERT_TRUE(WriteCompressed(path,
	                            Filter::Gzip,
	                            std::string(bytes.begin(), bytes.end()),
	                            &error))
	        << error;
	auto opened = ArchiveReader::Open(path, limits);
	EXPECT_TRUE(opened.error == Error::NotAnArchive ||
	            opened.error == Error::Corrupt)
	        << ErrorText(opened.error) << ": " << opened.detail;
	EXPECT_FALSE(opened.reader);
}

// Behind "MZ" libarchive's RAR bidders search from 64 KiB on in 16-byte
// steps (to 128 KiB for RAR4, 512 KiB for RAR5), so a self-extractor
// reaches the walk with the main flags unread: refused, not searched.
TEST_F(RarFixtureTest, RefusesARarBehindAStub)
{
	for (const auto* name : {"plain-v4.rar", "plain-v5.rar"}) {
		const auto fixture = Fixture(name);
		if (Missing(fixture)) {
			return;
		}
		auto bytes = std::vector<uint8_t>(0x10000, 'S');
		bytes[0]   = 'M';
		bytes[1]   = 'Z';
		auto in    = std::ifstream(fixture, std::ios::binary);
		bytes.insert(bytes.end(),
		             std::istreambuf_iterator<char>(in),
		             std::istreambuf_iterator<char>());
		auto opened = ArchiveReader::Open(Raw(std::string("sfx-") + name, bytes),
		                                  limits);
		EXPECT_EQ(opened.error, Error::NotAnArchive)
		        << name << ": " << opened.detail;
		EXPECT_NE(opened.detail.find("self-extract"), std::string::npos)
		        << name << ": " << opened.detail;
	}
}

TEST_F(RarFixtureTest, Rar4IdentityCarriesCrcsAndTheStoredTime)
{
	const auto fixture = Fixture("plain-v4.rar");
	if (Missing(fixture)) {
		return;
	}
	auto opened = ArchiveReader::Open(fixture, limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	auto files = 0;
	for (const auto& e : opened.reader->Entries()) {
		if (!e.is_dir) {
			EXPECT_TRUE(e.has_crc) << e.utf8_path;
			++files;
		}
	}
	EXPECT_GE(files, 2);
	// The walk counts the fixture's symlink, Entries() its implied
	// directories, hence the loose bound; the CRCs above and the
	// reader's count check prove the strict pairing.
	const auto fd = ArchiveMount::OpenReadOnlyShared(fixture);
	ASSERT_GE(fd, 0);
	const auto walked = Rar4Headers::Read(fd, 1000);
	ArchiveMount::CloseDescriptor(fd);
	EXPECT_EQ(walked.error, Rar4Headers::Error::None);
	EXPECT_GE(walked.files.size(), opened.reader->Entries().size() - 2);
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

TEST(ArchiveSource, TheNewErrorValuesHaveText)
{
	EXPECT_STREQ(ErrorText(Error::HelperFailed),
	             "the archive helper failed or spoke out of protocol");
	EXPECT_STREQ(ErrorText(Error::Timeout),
	             "the archive helper did not answer in time");
}

TEST_F(ArchiveReaderTest, ReaderIsAnArchiveSourceAndReportsItsStatPair)
{
	const auto path = Built("src.zip",
	                        Kind::ZipStored,
	                        {
	                                {"A.TXT", "abc"}
        });
	auto opened     = ArchiveReader::Open(path, limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	ArchiveSource& source = *opened.reader;
	EXPECT_EQ(source.Entries().size(), 1u);
	const auto pair = source.IndexedStat();
	EXPECT_EQ(pair.size, static_cast<int64_t>(fs::file_size(path)));
	EXPECT_GT(pair.mtime, 0);
	EXPECT_EQ(source.ArchiveLength(), pair.size);
}

// PKZIP 1.x shrink and implode (ada-unr8): the reader serves these members
// itself; the fixtures and their inputs are described in
// tests/files/archives/README.md.
namespace {

const fs::path LegacyDir = fs::path("tests/files/archives/legacy");

std::vector<uint8_t> SlurpBytes(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in),
	        std::istreambuf_iterator<char>()};
}

class LegacyZipReaderTest : public ArchiveReaderTest {};

int64_t SeekDescriptor(const int fd, const int64_t offset, const int origin)
{
#if defined(WIN32)
	return _lseeki64(fd, offset, origin);
#else
	return lseek(fd, offset, origin);
#endif
}

} // namespace

TEST_F(LegacyZipReaderTest, PkzipArchivesIndexAndEveryMemberMatchesItsInput)
{
	for (const auto* zip : {"SHRUNK.ZIP", "IMPLODED.ZIP"}) {
		auto opened = ArchiveReader::Open(LegacyDir / zip, limits);
		ASSERT_EQ(opened.error, Error::None) << zip << ": " << opened.detail;
		ASSERT_EQ(opened.reader->Entries().size(), 6u) << zip;
		for (const auto& e : opened.reader->Entries()) {
			std::vector<uint8_t> out = {};
			auto detail              = std::string();
			ASSERT_EQ(opened.reader->Materialize(e.ordinal, out, &detail),
			          Error::None)
			        << zip << " " << e.utf8_path << ": " << detail;
			EXPECT_EQ(out, SlurpBytes(LegacyDir / "inputs" / e.utf8_path))
			        << zip << " " << e.utf8_path;
		}
	}
}

TEST_F(LegacyZipReaderTest, MaterializeToFileWritesTheMemberWithItsDate)
{
	auto opened = ArchiveReader::Open(LegacyDir / "IMPLODED.ZIP", limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	for (const auto& e : opened.reader->Entries()) {
		auto detail       = std::string();
		const auto target = tmp_dir / e.utf8_path;
		ASSERT_EQ(opened.reader->MaterializeToFile(e.ordinal, target, &detail),
		          Error::None)
		        << e.utf8_path << ": " << detail;
		EXPECT_EQ(SlurpBytes(target),
		          SlurpBytes(LegacyDir / "inputs" / e.utf8_path));
		EXPECT_EQ(MtimeOf(target), e.mtime) << e.utf8_path;
	}
}

TEST_F(LegacyZipReaderTest, ADamagedImplodedMemberFailsWithoutAFile)
{
	const auto copy = tmp_dir / "DAMAGED.ZIP";
	fs::copy_file(LegacyDir / "IMPLODED.ZIP", copy);
	auto bytes    = SlurpBytes(copy);
	const auto cd = ZipCentralDirectory::Read(copy, 100);
	ASSERT_EQ(cd.error, ZipCentralDirectory::Error::None);
	bool flipped = false;
	for (const auto& rec : cd.records) {
		if (rec.raw_name == "LARGE.TXT") {
			const auto at = static_cast<size_t>(rec.local_header_offset);
			const auto start = at + 30 +
			                   (bytes[at + 26] | (bytes[at + 27] << 8)) +
			                   (bytes[at + 28] | (bytes[at + 29] << 8));
			bytes[start + rec.compressed_size / 2] ^= 0x10;
			flipped = true;
		}
	}
	ASSERT_TRUE(flipped);
	ArchiveFixtures::WriteBytes(copy, bytes);
	auto opened = ArchiveReader::Open(copy, limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	for (const auto& e : opened.reader->Entries()) {
		if (e.utf8_path != "LARGE.TXT") {
			continue;
		}
		std::vector<uint8_t> out = {};
		auto detail              = std::string();
		EXPECT_EQ(opened.reader->Materialize(e.ordinal, out, &detail),
		          Error::Corrupt)
		        << detail;
		const auto target = tmp_dir / "out" / "LARGE.TXT";
		EXPECT_EQ(opened.reader->MaterializeToFile(e.ordinal, target, &detail),
		          Error::Corrupt)
		        << detail;
		EXPECT_FALSE(fs::exists(target));
	}
}

TEST_F(LegacyZipReaderTest, ReadRangeLeavesTheCallersOffsetAlone)
{
	const int fd = OpenReadOnlyShared(LegacyDir / "SHRUNK.ZIP");
	ASSERT_GE(fd, 0);
	ASSERT_EQ(SeekDescriptor(fd, 17, SEEK_SET), 17);
	std::vector<uint8_t> out = {};
	EXPECT_EQ(ZipCentralDirectory::ReadRange(fd, 0, 4, out),
	          ZipCentralDirectory::Error::None);
	EXPECT_EQ(out, (std::vector<uint8_t>{'P', 'K', 3, 4}));
	EXPECT_EQ(SeekDescriptor(fd, 0, SEEK_CUR), 17);
	EXPECT_EQ(ZipCentralDirectory::ReadRange(
	                  fd, fs::file_size(LegacyDir / "SHRUNK.ZIP") - 2, 4, out),
	          ZipCentralDirectory::Error::Truncated);
	CloseDescriptor(fd);
}

TEST_F(LegacyZipReaderTest, ReduceStaysUnsupportedAndNamesTheMethod)
{
	StoredEntry e   = {};
	e.name          = "A.TXT";
	e.data          = "hello";
	e.method        = 2;
	const auto path = Raw("reduced.zip", MakeStoredZip({e}));
	auto opened     = ArchiveReader::Open(path, limits);
	EXPECT_EQ(opened.error, Error::Unsupported);
	EXPECT_NE(opened.detail.find("method 2"), std::string::npos) << opened.detail;
}

TEST_F(LegacyZipReaderTest, AWrongCrcFailsAMemberThatDecodes)
{
	const auto copy = tmp_dir / "BADCRC.ZIP";
	fs::copy_file(LegacyDir / "SHRUNK.ZIP", copy);
	auto bytes    = SlurpBytes(copy);
	const auto cd = ZipCentralDirectory::Read(copy, 100);
	ASSERT_EQ(cd.error, ZipCentralDirectory::Error::None);
	// The CRC sits at offset 14 of the local header and 16 of the central
	// header (APPNOTE 4.3.7, 4.3.12); flip its low bit in both.
	size_t patched = 0;
	for (size_t i = 0; i + 46 <= bytes.size(); ++i) {
		const bool local = bytes[i] == 'P' && bytes[i + 1] == 'K' &&
		                   bytes[i + 2] == 3 && bytes[i + 3] == 4;
		const bool central = bytes[i] == 'P' && bytes[i + 1] == 'K' &&
		                     bytes[i + 2] == 1 && bytes[i + 3] == 2;
		const size_t name_at = local ? 26 : 28;
		const size_t head    = local ? 30 : 46;
		if ((local || central) &&
		    std::string(reinterpret_cast<const char*>(&bytes[i + head]),
		                bytes[i + name_at] | (bytes[i + name_at + 1]
		                                      << 8)) == "LARGE.TXT") {
			bytes[i + (local ? 14 : 16)] ^= 1;
			++patched;
		}
	}
	ASSERT_EQ(patched, 2u);
	ArchiveFixtures::WriteBytes(copy, bytes);
	auto opened = ArchiveReader::Open(copy, limits);
	ASSERT_EQ(opened.error, Error::None) << opened.detail;
	for (const auto& e : opened.reader->Entries()) {
		std::vector<uint8_t> out = {};
		auto detail              = std::string();
		const auto read = opened.reader->Materialize(e.ordinal, out, &detail);
		if (e.utf8_path == "LARGE.TXT") {
			EXPECT_EQ(read, Error::Corrupt);
			EXPECT_NE(detail.find("CRC"), std::string::npos) << detail;
		} else {
			EXPECT_EQ(read, Error::None) << e.utf8_path << ": " << detail;
		}
	}
}
