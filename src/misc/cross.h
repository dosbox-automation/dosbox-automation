// SPDX-FileCopyrightText:  2021-2025 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 dosbox-automation contributors

#ifndef DOSBOX_CROSS_H
#define DOSBOX_CROSS_H

#include "dosbox.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#if defined (_MSC_VER)						/* MS Visual C++ */
#include <direct.h>
#include <io.h>
#define LONGTYPE(a) a##i64
#else										/* LINUX / GCC */
#include <dirent.h>
#include <unistd.h>
#define LONGTYPE(a) a##LL
#endif

#include "misc/std_filesystem.h"

#define CROSS_LEN 512						/* Maximum filename size */


#if defined (WIN32)
#define CROSS_FILENAME(blah)
#define CROSS_FILESPLIT '\\'
#else
#define	CROSS_FILENAME(blah) strreplace(blah,'\\','/')
#define CROSS_FILESPLIT '/'
#endif

#define CROSS_NONE	0
#define CROSS_FILE	1
#define CROSS_DIR	2

#if defined (WIN32) && !defined (__MINGW32__)
#define ftruncate(blah,blah2) chsize(blah,blah2)
#endif

/* Large file support */
#if defined(_MSC_VER)
	// MSVC doesn't support the posix fstream functions,
	// typedef their equivalents
	#define cross_ftello _ftelli64
	#define cross_fseeko _fseeki64
	#define cross_off_t __int64
#else
	// All other platforms should have POSIX fstream 'o' support

	// Check that off_t is 64 bits
	static_assert(sizeof(off_t) == sizeof(int64_t), "off_t not 64 bits");
	#define cross_ftello ftello
	#define cross_fseeko fseeko
	#define cross_off_t off_t
#endif

// fileno is a POSIX function (not mentioned in ISO/C++), which means it might
// be missing when when using C++11 with strict ANSI compatibility.
// New MSVC issues "deprecation" warning when fileno is used and recommends
// using (platform-specific) _fileno. On other platforms we can use fileno
// because it's either a POSIX-compliant system, or the function is available
// when compiling with GNU extensions.
#if defined (_MSC_VER)
#define cross_fileno(s) _fileno(s)
#else
#define cross_fileno(s) fileno(s)
#endif

namespace cross {

#if defined(WIN32)

struct tm *localtime_r(const time_t *timep, struct tm *result);

#else

constexpr auto localtime_r = ::localtime_r;

#endif

} // namespace cross

// Create or determine the location of the config directory (e.g., in portable
// mode, the config directory is the executable dir). Must be called before
// calling get_config_dir().
void init_config_dir();

std_fs::path get_config_dir();
std::string get_primary_config_name();
std_fs::path get_primary_config_path();

std_fs::path resolve_home(const std::string &str) noexcept;

// Get the list of standard directories with fonts
std::deque<std_fs::path> get_standard_font_dirs();

#if defined (WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef struct dir_struct {
	HANDLE          handle;
	char            base_path[MAX_PATH+4];
	WIN32_FIND_DATA search_data;
} DirInformation;

#else

//#include <sys/types.h> //Included above
#include <dirent.h>

typedef struct dir_struct {
	DIR*  dir;
	char base_path[CROSS_LEN];
} DirInformation;

#endif

DirInformation* open_directory(const char* dirname);
bool read_directory_first(DirInformation* dirp, char* entry_name, bool& is_directory);
bool read_directory_next(DirInformation* dirp, char* entry_name, bool& is_directory);
void close_directory(DirInformation* dirp);

FILE *fopen_wrap_ro_fallback(const std::string &filename, bool &is_readonly);

bool wild_match(const char *haystack, const char *needle);
bool wild_file_cmp(const char* file, const char* wild, bool long_compare = false);

bool get_expanded_files(const std::string &path,
                        std::vector<std::string> &files,
                        bool files_only,
                        bool skip_native_path = false) noexcept;

// std::aligned_alloc wants a size that is a multiple of the alignment; glibc
// tolerates any size, macOS returns nullptr (render out_buf, 2026-09-27).
// 0 means the rounding overflowed, which malloc_aligned turns into nullptr.
constexpr size_t aligned_alloc_size(const size_t size, const size_t alignment)
{
	if (alignment == 0) {
		return size;
	}
	if (size > SIZE_MAX - (alignment - 1)) {
		return 0;
	}
	return (size + alignment - 1) / alignment * alignment;
}

// Aligned memory allocate and free, supports Microsoft Vicual C
inline void* malloc_aligned(const size_t size, const size_t alignment)
{
	const auto rounded = aligned_alloc_size(size, alignment);
	if (size != 0 && rounded == 0) {
		return nullptr;
	}
#ifdef _MSC_VER
	// Microsoft Visual C does not support 'std::aligned_alloc'
	return _aligned_malloc(rounded, alignment);
#else
	return std::aligned_alloc(alignment, rounded);
#endif
}

inline void free_aligned(void* pointer)
{
#ifdef _MSC_VER
	// Microsoft Visual C requires a special version of 'free' to be used
	// to deallocate memory alocated with '_aligned_malloc'
	_aligned_free(pointer);
#else
	std::free(pointer);
#endif
}

#endif
