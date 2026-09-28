// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/descriptor.h"

#include <fcntl.h>

#if defined(WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ArchiveMount {

int DuplicateNoInherit(const int fd)
{
#if defined(WIN32)
	// _dup would make the copy inheritable until a later flag clear;
	// DuplicateHandle never does.
	const auto source = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
	if (source == INVALID_HANDLE_VALUE) {
		return -1;
	}
	HANDLE copy = INVALID_HANDLE_VALUE;
	if (!DuplicateHandle(GetCurrentProcess(),
	                     source,
	                     GetCurrentProcess(),
	                     &copy,
	                     0,
	                     FALSE,
	                     DUPLICATE_SAME_ACCESS)) {
		return -1;
	}
	const auto copy_fd = _open_osfhandle(reinterpret_cast<intptr_t>(copy),
	                                     _O_RDONLY | _O_BINARY | _O_NOINHERIT);
	if (copy_fd < 0) {
		CloseHandle(copy);
	}
	return copy_fd;
#else
	return fcntl(fd, F_DUPFD_CLOEXEC, 0);
#endif
}

} // namespace ArchiveMount
