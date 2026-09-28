// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/exclusive_file.h"

#include <fcntl.h>
#include <sys/stat.h>

#if defined(WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ArchiveMount {

FILE* OpenExclusive(const std::filesystem::path& path)
{
#if defined(WIN32)
	const auto fd = _wopen(path.wstring().c_str(),
	                       _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
	                       _S_IREAD | _S_IWRITE);
	if (fd < 0) {
		return nullptr;
	}
	FILE* f = _fdopen(fd, "wb");
	if (!f) {
		_close(fd);
	}
	return f;
#else
	// O_EXCL refuses a symlink at the path whatever it points to.
	const auto fd = open(path.c_str(),
	                     O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
	                     0600);
	if (fd < 0) {
		return nullptr;
	}
	FILE* f = fdopen(fd, "wb");
	if (!f) {
		close(fd);
	}
	return f;
#endif
}

} // namespace ArchiveMount
