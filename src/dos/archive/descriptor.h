// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_ARCHIVE_DESCRIPTOR_H
#define DOSBOX_ARCHIVE_DESCRIPTOR_H

namespace ArchiveMount {

// Duplicates a descriptor so that no child process the engine spawns
// inherits it, with no window in which it could: F_DUPFD_CLOEXEC on
// POSIX, DuplicateHandle without inheritance on Windows. -1 on failure.
int DuplicateNoInherit(int fd);

} // namespace ArchiveMount

#endif // DOSBOX_ARCHIVE_DESCRIPTOR_H
