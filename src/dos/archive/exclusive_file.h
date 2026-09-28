// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_ARCHIVE_EXCLUSIVE_FILE_H
#define DOSBOX_ARCHIVE_EXCLUSIVE_FILE_H

#include <cstdio>
#include <filesystem>

namespace ArchiveMount {

// Creates the file for writing, or returns null when anything already
// sits at the path: a planted link is never followed, an existing file
// never truncated. The descriptor is not inherited by child processes.
FILE* OpenExclusive(const std::filesystem::path& path);

} // namespace ArchiveMount

#endif // DOSBOX_ARCHIVE_EXCLUSIVE_FILE_H
