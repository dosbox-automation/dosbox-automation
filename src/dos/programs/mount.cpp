// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 dosbox-automation contributors

#include "mount.h"
#include "dos/archive/descriptor.h"
#include "dos/archive/head_sniff.h"
#include "dos/archive/member_path.h"
#include "dos/programs/append.h"
#include "dos/programs/archive_images.h"
#include "dos/programs/mount_archive_internal.h"
#include "dos/programs/archive_chooser.h"
#include "dos/programs/mount_run.h"
#include "dos/programs/menu_session.h"
#include "mount_common.h"
#include "mount_policy.h"

#include "dosbox.h"

#include <algorithm>
#include <vector>

#include "config/config.h"
#include "dos/cdrom.h"
#include "dos/drives.h"
#include "dos/floppy_image.h"
#include "gui/mapper.h"
#include "hardware/ide.h"
#include "ints/bios_disk.h"
#include "ints/int10.h"
#include "misc/cross.h"
#include "misc/notifications.h"
#include "misc/unicode.h"
#include "more_output.h"
#include "shell/shell.h"
#include "utils/fs_utils.h"
#include "utils/string_utils.h"
#include "webserver/webserver.h"
#include <sys/stat.h>

#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

void MOUNT::ListMounts()
{
	const std::string header_drive = MSG_Get("PROGRAM_MOUNT_STATUS_DRIVE");
	const std::string header_type  = MSG_Get("PROGRAM_MOUNT_STATUS_TYPE");
	const std::string header_path  = MSG_Get("PROGRAM_MOUNT_STATUS_PATH");
	const std::string header_label = MSG_Get("PROGRAM_MOUNT_STATUS_LABEL");

	const int console_width = real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS);
	const auto width_drive  = static_cast<int>(header_drive.length());
	const auto width_type   = 16;
	const auto width_label  = std::max(minimum_column_length,
                                          static_cast<int>(header_label.size()));
	const auto width_path   = console_width - 4 - width_drive - width_type -
	                        width_label;

	if (width_path < 0) {
		LOG_WARNING("Message is too long.");
		return;
	}

	auto print_row = [&](const std::string& txt_drive,
	                     const std::string& txt_type,
	                     const std::string& txt_path,
	                     const std::string& txt_label) {
		WriteOut("%-*s %-*s %-*s %-*s\n",
		         width_drive,
		         txt_drive.c_str(),
		         width_type,
		         txt_type.c_str(),
		         width_path,
		         txt_path.c_str(),
		         width_label,
		         txt_label.c_str());
	};

	WriteOut("\n");
	print_row(convert_ansi_markup("[color=white]") + header_drive,
	          header_type,
	          header_path,
	          header_label + convert_ansi_markup("[reset]"));

	// Make a horizontal divider that draws "-" underneath the headers,
	// using width_drive, width_type, width_path, and width_label to
	// determine how long each section should be
	const std::string horizontal_divider = std::string(width_drive, '-') + " " +
	                                       std::string(width_type, '-') + " " +
	                                       std::string(width_path, '-') + " " +
	                                       std::string(width_label, '-') + "\n";
	WriteOut_NoParsing(horizontal_divider);

	bool found_drives = false;
	for (uint8_t d = 0; d < DOS_DRIVES; d++) {
		if (Drives[d]) {
			const auto& images = DriveManager::GetFilesystemImages(d);

			// Render rows for drives holding multiple loaded images
			if (images.size() > 1) {
				bool first = true;
				for (const auto& img : images) {
					auto type = img->GetTypeString();
					auto path = img->GetInfo();

					std::string drive_letter_str =
					        first ? convert_ansi_markup(
					                        " [color=light-cyan]" +
					                        std::string{drive_letter(
					                                d)} +
					                        ":[reset]  ")
					              : "";
					std::string label_str =
					        first ? To_Label(Drives[d]->GetLabel())
					              : "";
					std::string type_str = first ? type : "";

					print_row(drive_letter_str,
					          type_str,
					          truncate_path(path, width_path),
					          label_str);
					first = false;
				}
			} else {
				// Render singular drive logic
				auto type = Drives[d]->GetTypeString();
				if (const auto archive = ArchiveDriveOf(Drives[d]);
				    archive && archive->State() !=
				                       ArchiveMountState::Ready) {
					type += std::string(":") +
					        ArchiveDrive::StateName(
					                archive->State());
				}
				auto path = Drives[d]->GetInfo();

				print_row(convert_ansi_markup(
				                  " [color=light-cyan]" +
				                  std::string{drive_letter(d)} +
				                  ":[reset]  "),
				          type,
				          truncate_path(path, width_path),
				          To_Label(Drives[d]->GetLabel()));
			}
			found_drives = true;
		}
	}

	if (!found_drives) {
		WriteOut(MSG_Get("PROGRAM_IMGMOUNT_STATUS_NONE"));
	}
	WriteOut("\n");
}
void MOUNT::ShowUsage()
{
	MoreOutputStrings output(*this);
	// Combined help
	output.AddString(MSG_Get("PROGRAM_MOUNT_HELP_LONG"), PRIMARY_MOD_NAME);
#ifdef WIN32
	output.AddString(MSG_Get("PROGRAM_MOUNT_HELP_LONG_WIN32"));
#elif defined(MACOSX)
	output.AddString(MSG_Get("PROGRAM_MOUNT_HELP_LONG_MACOSX"));
#else
	output.AddString(MSG_Get("PROGRAM_MOUNT_HELP_LONG_OTHER"));
#endif
	output.Display();
}

// A helper function that expands wildcard paths from the given argument and
// adds them to the given paths. Returns true if the expansion succeeded.
bool MOUNT::AddWildcardPaths(const std::string& path_arg,
                             std::vector<std::string>& paths)
{
	// Expand the given path argument
	constexpr auto OnlyExpandFiles          = true;
	constexpr auto SkipNativePath           = true;
	std::vector<std::string> expanded_paths = {};
	if (!get_expanded_files(path_arg, expanded_paths, OnlyExpandFiles, SkipNativePath)) {
		return false;
	}

	// Sort wildcards with natural ordering
	const auto has_wildcards = path_arg.find_first_of('*') != std::string::npos ||
	                           path_arg.find_first_of('?') != std::string::npos;
	if (has_wildcards) {
		std::sort(expanded_paths.begin(), expanded_paths.end(), natural_compare);
	}

	// Move the expanded paths out
	paths.insert(paths.end(), //-V823
	             std::make_move_iterator(expanded_paths.begin()),
	             std::make_move_iterator(expanded_paths.end()));

	return true;
}

// An image taken out of an archive is named as typed, not by its cache copy.
static std::vector<std::string> shown_images(const MountParameters& params)
{
	auto shown = params.paths;
	for (auto& path : shown) {
		if (const auto found = params.member_shown.find(path);
		    found != params.member_shown.end()) {
			path = found->second;
		}
	}
	return shown;
}

void MOUNT::WriteMountStatus(const std::string& image_type,
                             const std::vector<std::string>& images,
                             char drive_letter, bool readonly)
{
	constexpr auto Indent  = "  ";
	const auto indent_size = strlen(Indent);
	// No text mode yet (the tests, a mount before INT 10h ran) reports no
	// columns; truncate_path asserts on a width that is not positive.
	const size_t columns   = INT10_GetTextColumns();
	const auto path_width  = static_cast<int>(
                (columns > indent_size ? columns : size_t{80}) - indent_size);
	std::string images_str = {};

	if (images.size() == 1) {
		// If only one image, don't add newlines and just write in one
		// line:
		images_str = image_type.c_str() + std::string(" ") + images[0];
		WriteOut(MSG_Get("PROGRAM_MOUNT_STATUS_2"),
		         images_str.c_str(),
		         drive_letter);
		if (readonly) {
			WriteOut(MSG_Get("PROGRAM_MOUNT_READONLY"));
		}
	} else {

		for (const auto& image : images) {
			assert(!image.empty());
			images_str = images_str.append(
			        std::string(Indent) +
			        truncate_path(image, path_width) +
			        std::string("\n"));
		}
		WriteOut(MSG_Get("PROGRAM_MOUNT_RESULT"),
		         image_type.c_str(),
		         drive_letter,
		         images_str.c_str());
	}
}

// Sets:
//   params.sizes
//
bool MOUNT::MountImageFat(MountParameters& params)
{
	// Autosize detection
	bool detect_image_size = (params.type == MountType::HardDiskImage) &&
	                         (params.sizes[0] == 0 && params.sizes[1] == 0 &&
	                          params.sizes[2] == 0 && params.sizes[3] == 0);

	if (detect_image_size) {
		FILE* diskfile = fopen_wrap_ro_fallback(params.paths[0],
		                                        params.roflag);
		if (!diskfile) {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_IMGMOUNT_INVALID_IMAGE");
			return false;
		}
		const auto sz = stdio_num_sectors(diskfile);
		if (sz < 0) {
			fclose(diskfile);
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_IMGMOUNT_INVALID_IMAGE");
			return false;
		}

		uint32_t fcsize = check_cast<uint32_t>(sz);
		uint8_t buf[512];

		if (cross_fseeko(diskfile, 0L, SEEK_SET) != 0 ||
		    fread(buf, sizeof(uint8_t), 512, diskfile) < 512) {
			fclose(diskfile);

			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_IMGMOUNT_INVALID_IMAGE");
			return false;
		}

		fclose(diskfile);

		if ((buf[510] != 0x55) || (buf[511] != 0xaa)) {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_IMGMOUNT_INVALID_GEOMETRY");
			return false;
		}

		Bitu sectors = (Bitu)(fcsize / (16 * 63));

		if (sectors * 16 * 63 != fcsize) {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_IMGMOUNT_INVALID_GEOMETRY");
			return false;
		}

		params.sizes[0] = 512;
		params.sizes[1] = 63;
		params.sizes[2] = 16;
		params.sizes[3] = static_cast<uint16_t>(sectors);

		LOG_MSG("DRIVE: Autosized image file: %d:%d:%d:%d",
		        params.sizes[0],
		        params.sizes[1],
		        params.sizes[2],
		        params.sizes[3]);
	}

	if (Drives.at(drive_index(params.drive))) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_IMGMOUNT_ALREADY_MOUNTED");
		return false;
	}

	DriveManager::filesystem_images_t fat_images = {};

	for (const auto& fat_path : params.paths) {
		auto fat_image = std::make_shared<fatDrive>(fat_path.c_str(),
		                                            params.sizes[0],
		                                            params.sizes[1],
		                                            params.sizes[2],
		                                            params.sizes[3],
		                                            params.mediaid,
		                                            params.roflag);
		if (fat_image->created_successfully) {
			fat_images.push_back(fat_image);
		} else {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_IMGMOUNT_CANT_CREATE");
			return false;
		}
	}
	if (fat_images.empty()) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_IMGMOUNT_CANT_CREATE");
		return false;
	}

	// Update DriveManager
	DriveManager::AppendFilesystemImages(drive_index(params.drive), fat_images);
	DriveManager::InitializeDrive(drive_index(params.drive));

	// Set the correct media byte in the table
	// Each entry is 9 bytes, with the media byte at offset 0x00
	constexpr auto DptEntrySize = 9;

	mem_writeb(RealToPhysical(dos.tables.mediaid) +
	                   drive_index(params.drive) * DptEntrySize,
	           params.mediaid);

	// Command uses dta so set it to our internal dta
	RealPt save_dta = dos.dta();
	dos.dta(dos.tables.tempdta);

	for (auto it = fat_images.begin(); it != fat_images.end(); ++it) {
		const bool should_notify = (std::next(it) == fat_images.end());

		DriveManager::CycleDisks(drive_index(params.drive), should_notify);

		// Obtain the drive label, saving it in the dirCache
		char root[7] = {params.drive, ':', '\\', '*', '.', '*', 0};

		AppendCommand::Suspend no_append_search;
		if (!DOS_FindFirst(root, FatAttributeFlags::Volume)) {
			LOG_WARNING("DRIVE: Unable to find %c drive's volume label",
			            params.drive);
		}
	}

	dos.dta(save_dta);

	std::string mount_message = (params.paths.size() > 1)
	                                  ? MSG_Get("MOUNT_TYPE_FAT_PLURAL")
	                                  : MSG_Get("MOUNT_TYPE_FAT");

	WriteMountStatus(mount_message, shown_images(params), params.drive, params.roflag);

	const auto fat_image = std::dynamic_pointer_cast<fatDrive>(
	        fat_images.front());

	assert(fat_image);
	const auto has_hdd = fat_image->loadedDisk && fat_image->loadedDisk->hardDrive;

	const auto is_floppy = (params.drive == 'A' || params.drive == 'B') &&
	                       !has_hdd;

	const auto is_hdd = (params.drive == 'C' || params.drive == 'D') && has_hdd;

	if (is_floppy || is_hdd) {
		imageDiskList.at(drive_index(params.drive)) = fat_image->loadedDisk;
		updateDPT();
	}

	return true;
}

// Return string representation of MSCDEX_AddDrive error code
static const char* mscdex_error_to_message_id(const int error, const bool is_image_file)
{
	switch (error) {
	case 0: return "MSCDEX_SUCCESS";
	case 1: return "MSCDEX_ERROR_MULTIPLE_CDROMS";
	case 2: return "MSCDEX_ERROR_NOT_SUPPORTED";
	case 3:
		return is_image_file ? "MSCDEX_ERROR_OPEN" : "MSCDEX_ERROR_PATH";
	case 4: return "MSCDEX_TOO_MANY_DRIVES";
	case 5: return "MSCDEX_LIMITED_SUPPORT";
	case 6: return "MSCDEX_INVALID_FILEFORMAT";
	default: return "MSCDEX_UNKNOWN_ERROR";
	}
}

bool MOUNT::MountImageIso(const MountParameters& params)
{
	if (Drives.at(drive_index(params.drive))) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_IMGMOUNT_ALREADY_MOUNTED");
		return false;
	}

	// create new drives for all images
	DriveManager::filesystem_images_t iso_images = {};
	for (const auto& iso_path : params.paths) {
		int error = -1;
		iso_images.push_back(std::make_shared<isoDrive>(
		        params.drive, iso_path.c_str(), params.mediaid, error));

		// error: report and leave
		if (error) { //-V547
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      mscdex_error_to_message_id(error, true));
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_IMGMOUNT_CANT_CREATE");
			return false;
		}
	}

	// Update DriveManager
	DriveManager::AppendFilesystemImages(drive_index(params.drive), iso_images);
	DriveManager::InitializeDrive(drive_index(params.drive));

	// Set the correct media byte in the table
	mem_writeb(RealToPhysical(dos.tables.mediaid) + drive_index(params.drive) * 9,
	           params.mediaid);

	// If instructed, attach to IDE controller as ATAPI CD-ROM device
	if (params.is_ide) {
		if (params.ide_index >= 0) {
			IDE_CDROM_Attach(params.ide_index,
			                 params.is_second_cable_slot,
			                 drive_index(params.drive));
		} else {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_IMGMOUNT_IDE_CONTROLLERS_UNAVAILABLE");
		}
	}

	// Print status message (success)
	NOTIFY_DisplayInfoMessage(Notification::Source::Console,
	                          "MOUNT",
	                          mscdex_error_to_message_id(0, true));

	std::string mount_message = (params.paths.size() > 1)
	                                  ? MSG_Get("MOUNT_TYPE_CDIMAGE_PLURAL")
	                                  : MSG_Get("MOUNT_TYPE_CDIMAGE");

	WriteMountStatus(mount_message, shown_images(params), params.drive, params.roflag);

	return true;
}

// Sets:
//   params.roflag
//
bool MOUNT::MountImageRaw(MountParameters& params)
{
	auto new_disk = fopen_wrap_ro_fallback(params.paths[0], params.roflag);
	if (!new_disk) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_IMGMOUNT_INVALID_IMAGE");
		return false;
	}

	const auto sz = stdio_size_kb(new_disk);
	if (sz < 0) {
		fclose(new_disk);
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_IMGMOUNT_INVALID_IMAGE");
		return false;
	}

	auto imagesize = check_cast<uint32_t>(sz);

	// 0=A:, 1=B:, 2=C:, 3=D:
	const auto is_hdd = (params.drive >= '2');

	// Seems to make sense to require a valid geometry..
	if (is_hdd && params.sizes[0] == 0 && params.sizes[1] == 0 &&
	    params.sizes[2] == 0 && params.sizes[3] == 0) {

		fclose(new_disk);

		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_IMGMOUNT_SPECIFY_GEOMETRY");
		return false;
	}

	const auto drv_idx = params.drive - '0';

	imageDiskList.at(drv_idx) = std::make_shared<imageDisk>(
	        new_disk, params.paths[0].c_str(), imagesize, is_hdd);

	if (is_hdd) {
		imageDiskList.at(drv_idx)->Set_Geometry(params.sizes[2],
		                                        params.sizes[3],
		                                        params.sizes[1],
		                                        params.sizes[0]);
	}

	if ((params.drive == '2' || params.drive == '3') && is_hdd) {
		updateDPT();
	}

	WriteOut(MSG_Get("PROGRAM_IMGMOUNT_MOUNT_NUMBER"),
	         drv_idx,
	         params.paths[0].c_str());

	return true;
}

bool MOUNT::MountImage(MountParameters& params)
{
	// Determine Media ID if not already set strictly
	params.mediaid = (params.type == MountType::FloppyImage)
	                       ? MediaId::Floppy1_44MB
	                       : MediaId::HardDisk;

	if (params.fstype == MountFileSystemType::Fat16) {
		return MountImageFat(params);

	} else if (params.fstype == MountFileSystemType::Iso) {
		return MountImageIso(params);

	} else if (params.fstype == MountFileSystemType::None) {
		return MountImageRaw(params);
	}
	return true;
}

bool MOUNT::HandleUnmount()
{
	std::string umount = {};

	// Standard order: -u <drive>
	if (cmd->FindString("-u", umount, false)) {
		WriteOut(UnmountHelper(umount[0]), toupper(umount[0]));
		return true;
	}

	// Reverse order: <drive> -u
	if (cmd->FindExist("-u", false)) {
		// Check umount != "-u" to prevent parsing errors if the user
		// just types "mount -u".
		if (cmd->FindCommand(1, umount) && !umount.empty() && umount != "-u") {
			WriteOut(UnmountHelper(umount[0]), toupper(umount[0]));
			return true;
		}
	}

	return false;
}

static std::optional<MountType> parse_mount_type(const std::string s)
{
	if (s == "floppy" || s == "fdd") {
		return MountType::FloppyImage;

	} else if (s == "hdd") {
		return MountType::HardDiskImage;

	} else if (s == "iso" || s == "cdrom") {
		return MountType::CdRomImage;

	} else if (s == "dir") {
		return MountType::Directory;

	} else if (s == "overlay") {
		return MountType::Overlay;

	} else if (s == "archive") {
		return MountType::Archive;

	} else {
		return {};
	}
}

static std::optional<MountFileSystemType> parse_file_system_type(const std::string s)
{
	if (s == "fat") {
		return MountFileSystemType::Fat16;

	} else if (s == "iso") {
		return MountFileSystemType::Iso;

	} else if (s == "none") {
		return MountFileSystemType::None;

	} else {
		return {};
	}
}

std::optional<MOUNT::MountOption> MOUNT::TakeOption(const std::string& name,
                                                    const OptionValue value_kind)
{
	MountOption option = {};
	int occurrences    = 0;

	if (value_kind == OptionValue::None) {
		while (cmd->FindExist(name, true)) {
			++occurrences;
		}
	} else {
		occurrences = cmd->FindRemoveOption(name, option.value);
	}
	option.present = occurrences > 0;

	if (occurrences > 1) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_REPEATED_OPTION",
		                      name.c_str());
		return {};
	}
	if (option.present && value_kind == OptionValue::Required && !option.value) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_OPTION_NEEDS_VALUE",
		                      name.c_str());
		return {};
	}
	return option;
}

// Runs after every known option has been consumed, so a dash token left
// on the line is a typo or an unsupported option. Refusing it here keeps
// it out of the path list, where it would fail much later as an image
// that "isn't a directory or valid image file".
bool MOUNT::HasUnknownOptions()
{
	bool found      = false;
	std::string arg = {};

	for (unsigned int i = 1; cmd->FindCommand(i, arg); ++i) {
		if (arg.starts_with('-')) {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_MOUNT_UNKNOWN_OPTION",
			                      arg.c_str());
			found = true;
		}
	}
	return found;
}

// Sets:
//   params.type   (from the -t option)
//   params.roflag (from the -ro option)
//   params.fstype (from the -fs option or if -t iso is specified)
//
//   params.is_ide (from the -ide option))
//   params.ide_index (based on DOS internal state)
//   params.is_second_cable_slot (based on DOS internal state)
//
//   params.label  (from the -label option)
//
//   params.freesize_arg, size_arg, chs_arg (raw strings for ParseGeometry)
//
bool MOUNT::ParseArguments(MountParameters& params, bool& explicit_fs,
                           bool& path_relative_to_last_config)
{
	const auto pr = TakeOption("-pr", OptionValue::None);
	if (!pr) {
		return false;
	}
	path_relative_to_last_config = pr->present;

	// Default is "dir" if the -t option is not provided
	const auto type = TakeOption("-t", OptionValue::Required);
	if (!type) {
		return false;
	}
	const auto type_str = type->value.value_or("dir");

	const auto maybe_mount_type = parse_mount_type(type_str);
	if (!maybe_mount_type) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_ILL_TYPE",
		                      type_str.c_str());
		return false;
	}
	params.type = *maybe_mount_type;

	const auto ro = TakeOption("-ro", OptionValue::None);
	if (!ro) {
		return false;
	}
	params.roflag = ro->present;

	// Parse -fs (filesystem type)
	// Default is "fat" if the -fs option is not provided
	const auto fs = TakeOption("-fs", OptionValue::Required);
	if (!fs) {
		return false;
	}
	explicit_fs           = fs->present;
	const auto fstype_str = fs->value.value_or("fat");

	const auto maybe_fs_type = parse_file_system_type(fstype_str);
	if (!maybe_fs_type) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_ILL_TYPE",
		                      fstype_str.c_str());
		return false;
	}
	if (explicit_fs) {
		params.fstype = *maybe_fs_type;

		// A floppy cannot carry an ISO filesystem; resolves the
		// refactor's own TODO in the tests (aug-55a2).
		if (params.type == MountType::FloppyImage &&
		    params.fstype == MountFileSystemType::Iso) {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_MOUNT_ILL_TYPE",
			                      fstype_str.c_str());
			return false;
		}
	} else {
		if (params.type == MountType::CdRomImage) {
			params.fstype = MountFileSystemType::Iso;
		}
	}

	// -ide may carry a value or stand alone
	const auto ide = TakeOption("-ide", OptionValue::Optional);
	if (!ide) {
		return false;
	}
	params.is_ide = ide->present;

	if (params.is_ide && (params.type == MountType::CdRomImage)) {
		IDE_Get_Next_Cable_Slot(params.ide_index, params.is_second_cable_slot);
	}

	const auto label = TakeOption("-label", OptionValue::Required);
	if (!label) {
		return false;
	}
	params.label = label->value.value_or("");

	const auto root = TakeOption("-root", OptionValue::Required);
	if (!root) {
		return false;
	}
	params.archive_root  = root->value.value_or("");
	const auto nosidecar = TakeOption("-nosidecar", OptionValue::None);
	if (!nosidecar) {
		return false;
	}
	params.no_sidecar = nosidecar->present;
	const auto sidecar = TakeOption("-sidecar", OptionValue::Required);
	if (!sidecar) {
		return false;
	}
	params.sidecar_path = sidecar->value.value_or("");
	const auto scratch = TakeOption("-scratch", OptionValue::None);
	if (!scratch) {
		return false;
	}
	params.scratch = scratch->present;
	if (params.scratch &&
	    (params.roflag || params.no_sidecar || !params.sidecar_path.empty())) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_ARCHIVE_SCRATCH_CONFLICT");
		return false;
	}

	const auto run      = TakeOption("-run", OptionValue::Optional);
	const auto run_exit = TakeOption("-exit", OptionValue::None);
	if (!run || !run_exit) {
		return false;
	}
	if (run->present) {
		params.run = run->value.value_or("");
	}
	params.run_exit = run_exit->present;
	if (params.run_exit && !params.run) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_EXIT_NEEDS_RUN");
		return false;
	}

	// The geometry strings are interpreted in ParseGeometry once
	// ProcessPaths has settled the mount type.
	const auto freesize = TakeOption("-freesize", OptionValue::Required);
	const auto size     = TakeOption("-size", OptionValue::Required);
	const auto chs      = TakeOption("-chs", OptionValue::Required);
	if (!freesize || !size || !chs) {
		return false;
	}
	params.freesize_arg = freesize->value.value_or("");
	params.size_arg     = size->value.value_or("");
	params.chs_arg      = chs->value.value_or("");

	return true;
}

// Sets:
//   params.mediaid
//   params.sizes
//
bool MOUNT::ParseGeometry(MountParameters& params)
{
	std::string str_size = "";
	std::string str_chs  = "";

	// Default sizing logic based on type
	switch (params.type) {
	case MountType::FloppyImage:
		str_size = "512,1,2880,2880";

		params.mediaid = MediaId::Floppy1_44MB;
		break;

	case MountType::Directory:
	case MountType::Overlay: {
		// 512*32*32765==~500MB total size
		// 512*32*16000==~250MB total free size
		str_size = "512,32,32765,16000";

		// If drive mounted to A or B, set mediaid to floppy
		// This is preferable to using type because floppies can
		// be auto-mounted as type "dir"
		std::string command_arg;
		cmd->FindCommand(1, command_arg);

		if (!command_arg.empty()) {
			const int i_drive = std::toupper(command_arg[0]);
			if (i_drive == 'A' || i_drive == 'B') {
				params.mediaid = MediaId::Floppy1_44MB;
			}
		}
	} break;

	case MountType::CdRomImage:
		str_size = "2048,1,65535,0";

		// mediaid is used in staging to differentiate between floppy
		// and non-floppy for cache rescan, disk noise and I/O timing.
		// The same value was used in the original dosbox code
		params.mediaid = MediaId::HardDisk;
		break;

	case MountType::HardDiskImage: break;
	case MountType::Archive: params.mediaid = MediaId::HardDisk; break;
	default: assertm(false, "Invalid MountType");
	}

	// An image has its own size; free space is a directory mount's.
	if (params.is_image_mode && !params.freesize_arg.empty()) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_FREESIZE_IMAGE");
		return false;
	}

	// Parse the free space in mb (kb for floppies)
	const std::string& mb_size = params.freesize_arg;

	if (!mb_size.empty()) {

		char teststr[1024];
		uint16_t freesize = static_cast<uint16_t>(atoi(mb_size.c_str()));

		if (params.type == MountType::FloppyImage) {
			// freesize in kb
			safe_sprintf(teststr,
			             "512,1,2880,%d",
			             freesize * 1024 / (512 * 1));
		} else {
			uint32_t total_size_cyl = 32765;

			uint32_t free_size_cyl = (uint32_t)freesize * 1024 *
			                         1024 / (512 * 32);

			if (free_size_cyl > 65534) {
				free_size_cyl = 65534;
			}
			if (total_size_cyl < free_size_cyl) {
				total_size_cyl = free_size_cyl + 10;
			}
			if (total_size_cyl > 65534) {
				total_size_cyl = 65534;
			}
			safe_sprintf(teststr, "512,32,%d,%d", total_size_cyl, free_size_cyl);
		}
		str_size = teststr;
	}

	// Parse -size
	if (!params.size_arg.empty()) {
		str_size = params.size_arg;
	}

	// Apply str_size string to sizes array
	if (!str_size.empty()) {
		char number[21]  = {0};
		const char* scan = str_size.c_str();

		int index = 0;
		int count = 0;

		// Parse the str_size string
		while (*scan && index < 20 && count < 4) {
			if (*scan == ',') {
				number[index]         = 0;
				params.sizes[count++] = atoi(number);
				index                 = 0;
			} else {
				number[index++] = *scan;
			}
			scan++;
		}
		if (count < 4) {
			// always goes correct as index is max 20 at this point.
			number[index] = 0;

			params.sizes[count] = atoi(number);
		}
	}

	// Parse -chs C,H,S
	if (!params.chs_arg.empty()) {
		str_chs           = params.chs_arg;
		int cmd_cylinders = 0;
		int cmd_heads     = 0;
		int cmd_sectors   = 0;

		if (sscanf(str_chs.c_str(), "%d,%d,%d", &cmd_cylinders, &cmd_heads, &cmd_sectors) ==
		    3) {

			params.sizes[0] = 512;
			params.sizes[1] = static_cast<uint16_t>(cmd_sectors);
			params.sizes[2] = static_cast<uint16_t>(cmd_heads);
			params.sizes[3] = static_cast<uint16_t>(cmd_cylinders);

		} else {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_MOUNT_INVALID_CHS");
			return false;
		}
	}

	return true;
}

// Sets:
//   params.drive
//   params.fstype
//   params.is_drive_number
//
bool MOUNT::ParseDrive(MountParameters& params, bool explicit_fs)
{
	// get the drive letter or number
	std::string temp_line;
	cmd->FindCommand(1, temp_line);

	if ((temp_line.size() > 2) ||
	    ((temp_line.size() > 1) && (temp_line[1] != ':'))) {

		ShowUsage();
		return false;
	}

	const char first_char  = toupper(temp_line[0]);
	params.is_drive_number = false;

	if (isdigit(first_char)) {
		// Handle Drive Numbers (0, 1, 2, 3) for bootable images
		if (first_char >= '0' && first_char <= '3') {
			params.drive           = first_char;
			params.is_drive_number = true;

			// If user did not explicit specify filesystem, assume
			// 'none' for raw bootable access
			if (!explicit_fs) {
				params.fstype = MountFileSystemType::None;
			}
		} else {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_IMGMOUNT_SPECIFY2");
			return false;
		}
	} else if (first_char >= 'A' && first_char <= 'Z') {
		params.drive = first_char;

		// Allow A:, B:, C: and D: to be mounted as raw bootable images
		if (explicit_fs && params.fstype == MountFileSystemType::None) {
			switch (params.drive) {
			case 'A':
				params.drive           = '0';
				params.is_drive_number = true;
				break;
			case 'B':
				params.drive           = '1';
				params.is_drive_number = true;
				break;
			case 'C':
				params.drive           = '2';
				params.is_drive_number = true;
				break;
			case 'D':
				params.drive           = '3';
				params.is_drive_number = true;
				break;
			default:
				// Don't allow booting from E:, F:, etc.
				NOTIFY_DisplayWarning(Notification::Source::Console,
				                      "MOUNT",
				                      "PROGRAM_IMGMOUNT_SPECIFY2");
				return false;
			}
		}
	} else {
		ShowUsage();
		return false;
	}

	// Check overlaps
	if (!params.is_drive_number) {
		if (params.type == MountType::Overlay) {
			// Ensure that the base drive exists:
			if (!Drives.at(drive_index(params.drive))) {
				NOTIFY_DisplayWarning(Notification::Source::Console,
				                      "MOUNT",
				                      "PROGRAM_MOUNT_OVERLAY_NO_BASE");
				return false;
			}
		} else if (Drives.at(drive_index(params.drive))) {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_MOUNT_ALREADY_MOUNTED",
			                      params.drive,
			                      Drives.at(drive_index(params.drive))
			                              ->GetInfoString()
			                              .c_str());
			return false;
		}
	}

	return true;
}

std::string MOUNT::ApplyRelativePath(const std::string& path,
                                     bool is_relative_to_last_config) const
{
	if (is_relative_to_last_config && !control->config_files.empty() &&
	    !std_fs::path(path).is_absolute()) {

		auto last_config_dir = control->config_files.back();

		const auto pos = last_config_dir.rfind(CROSS_FILESPLIT);

		last_config_dir.erase(pos == std::string::npos ? 0 : pos);
		if (!last_config_dir.empty()) {
			return last_config_dir + CROSS_FILESPLIT + path;
		}
	}
	return path;
}

// Translates a DOS path to a host path if the DOS path is currently mapped as a
// drive. Returns an empty string if it cannot be translated.
std::string MOUNT::GetDosMappedHostPath(const std::string& dos_path)
{
	// DOS_MakeName requires a writable buffer
	auto fullname_buf = std::array<char, CROSS_LEN>{};
	uint8_t drive_idx = 0;

	if (DOS_MakeName(dos_path.data(), fullname_buf.data(), &drive_idx)) {

		if (Drives.at(drive_idx) &&
		    Drives.at(drive_idx)->GetType() == DosDriveType::Local) {

			if (const auto local_drive = std::dynamic_pointer_cast<localDrive>(
			            Drives.at(drive_idx))) {

				return local_drive->MapDosToHostFilename(
				        fullname_buf.data());
			}
		}
	}

	return "";
}

std::string MOUNT::HostText(const std::string& arg) const
{
	return path_text == PathText::Host ? arg : MountPolicy::HostPathFromDos(arg);
}

// The host path a path argument names: ~ expanded, -pr applied, and a
// mounted DOS path taken as the host path it maps to (ada-ep2a).

std::string MOUNT::ResolveHostPath(const std::string& arg,
                                   const bool relative_to_last_config) const
{
	auto path = ApplyRelativePath(resolve_home(HostText(arg)).string(),
	                              relative_to_last_config);
#if defined(WIN32)
	// Removing trailing backslash if not root dir so stat will succeed
	if (path.size() > 3 && path.back() == '\\') {
		path.pop_back();
	}
#endif
	struct stat test = {};
	if (path_text == PathText::Dos && stat(path.c_str(), &test) != 0) {
		if (const auto mapped = HostPathOfDosPath(arg); !mapped.empty()) {
			return mapped;
		}
	}
	return path;
}

std::string MOUNT::HostPathOfDosPath(const std::string& dos_path)
{
	for (const auto& reading : {dos_path, MountPolicy::HostPathFromDos(dos_path)}) {
		const auto mapped = GetDosMappedHostPath(reading);
		struct stat test  = {};
		if (!mapped.empty() && stat(mapped.c_str(), &test) == 0) {
			return mapped;
		}
	}
	return {};
}

DirMountPolicy MOUNT::CurrentDirPolicy()
{
	// The same gate as the archive path check: a running webserver sets
	// the injection flag, the tests set the flag alone.
	if (WEBSERVER_IsEnabled() || MountPolicy::IsInjectionPossible()) {
		return DirMountPolicy::WhitelistEnforced;
	}
	return DirMountPolicy::OwnerTrusted;
}

void MOUNT::ReportMountDenied(const std::string& path, const MountVerdict& verdict,
                              const std::string& detail)
{
	if (verdict.reason == DenyReason::None) {
		return;
	}
	const auto reason_text = MountPolicy::DenyReasonText(verdict.reason);
	LOG_WARNING("MOUNT: Blocked '%s' - %s%s%s",
	            path.c_str(),
	            reason_text,
	            detail.empty() ? "" : ": ",
	            detail.c_str());
	const std::string message_id = MountPolicy::DenyMessageId(verdict.reason);
	if (message_id == "PROGRAM_MOUNT_POLICY_REFUSED") {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      message_id,
		                      path.c_str(),
		                      reason_text);
	} else if (message_id == "PROGRAM_MOUNT_ARCHIVE_REFUSED") {
		const auto detail_line = detail.empty() ? std::string()
		                                        : "  " + detail + "\n";
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      message_id,
		                      path.c_str(),
		                      reason_text,
		                      detail_line.c_str());
	} else if (message_id == "PROGRAM_MOUNT_ARCHIVE_UNSUPPORTED") {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      message_id,
		                      path.c_str(),
		                      detail.c_str());
	} else if (message_id == "PROGRAM_MOUNT_ARCHIVE_SEQUENTIAL" ||
	           message_id == "PROGRAM_MOUNT_ARCHIVE_CACHE" ||
	           message_id == "PROGRAM_MOUNT_ARCHIVE_SIDECAR" ||
	           message_id == "PROGRAM_MOUNT_ARCHIVE_ROOT" ||
	           message_id == "PROGRAM_MOUNT_SIDECAR_BUSY") {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      message_id,
		                      path.c_str(),
		                      detail.c_str());
	} else {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      message_id,
		                      path.c_str());
	}
}

// The first image's type from its name's extension and its host file's
// size; a floppy-sized .img, .ima or .dsk is a floppy.
static void detect_image_type(const std::string& name,
                              const std::string& host_file, MountParameters& params)
{
	struct stat t2 = {};
	if (stat(host_file.c_str(), &t2) != 0 || !S_ISREG(t2.st_mode)) {
		return;
	}
	auto ext = name.substr(name.find_last_of('.') + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	if (ext == "iso" || ext == "cue" || ext == "bin" || ext == "mds" ||
	    ext == "ccd") {
		params.type   = MountType::CdRomImage;
		params.fstype = MountFileSystemType::Iso;
	} else if (ext == "vhd") {
		params.type = MountType::HardDiskImage;
	} else if (ext == "vfd" || ext == "flp" || ext == "360" || ext == "720" ||
	           ext == "1200" || ext == "1440") {
		params.type = MountType::FloppyImage;
	} else if (ext == "img" || ext == "ima" || ext == "dsk") {
		params.type = FloppyImage::IsFloppy(std_fs::path(host_file))
		                    ? MountType::FloppyImage
		                    : MountType::HardDiskImage;
	}
}

// Process paths and prepare (mutate) the passed `MountParameters` for
// mounting. This includes resolving host paths, wildcard path arguments,
// auto-detecting mount types, and determining whether we're dealing with
// image or directory/overlay mounts.
//
// Sets:
//   params.fstype
//   params.is_image_mode
//   params.mediaid
//   params.paths
//   params.type
//
void MOUNT::ProcessPaths(const std::string first_path, MountParameters& params,
                         bool path_relative_to_last_config)
{
	auto path_arg_1 = ResolveHostPath(first_path, path_relative_to_last_config);
	params.shown_path = HostText(first_path);

	// Check first path on the host OS
	struct stat test   = {};
	const auto stat_ok = (stat(path_arg_1.c_str(), &test) == 0);

	const auto target_is_dir  = stat_ok && S_ISDIR(test.st_mode);
	const auto target_is_file = stat_ok && S_ISREG(test.st_mode);

	const auto explicit_image_type = (params.type == MountType::HardDiskImage ||
	                                  params.type == MountType::CdRomImage ||
	                                  params.type == MountType::FloppyImage);
	const auto explicit_archive = params.type == MountType::Archive;

	const auto has_wildcards = path_arg_1.find_first_of("*?") !=
	                           std::string::npos;

	// Design D7: a path that does not exist but walks into an archive
	// names an image inside it.
	const auto first_member = (!stat_ok && !has_wildcards)
	                                ? ArchiveMount::SplitMemberPath(path_arg_1)
	                                : std::nullopt;
	// Plan 4b: a single-image container (game.imz, disk1.img.xz, game.isz)
	// is a member path of its own; -sidecar and -nosidecar apply to it.
	const auto first_lone = (target_is_file && !has_wildcards && !explicit_archive)
	                              ? ArchiveMount::LoneImageMember(path_arg_1)
	                              : std::nullopt;
	const auto names_member = first_member.has_value() || first_lone.has_value();

	// An archive mount takes one path; any further ones are kept so
	// MountArchive refuses them instead of the drive silently dropping them.
	auto keep_further_paths = [&] {
		std::string further = {};
		for (unsigned int i = 3; cmd->FindCommand(i, further); ++i) {
			params.paths.push_back(
			        ResolveHostPath(further,
			                        path_relative_to_last_config));
		}
	};

	// Nested design 4.1: a member path whose leaf is an archive name
	// mounts that archive as the drive; MountArchive splits it again.
	if (first_member && !explicit_image_type &&
	    MountArchiveInternal::ArchiveDriveChain(*first_member)) {
		params.type          = MountType::Archive;
		params.is_image_mode = false;
		params.paths.clear();
		params.paths.push_back(path_arg_1);
		keep_further_paths();
		if (!params.sidecar_path.empty()) {
			params.sidecar_path = ResolveHostPath(params.sidecar_path,
			                                      path_relative_to_last_config);
		}
		return;
	}
	// An archive is its own kind of source (archive mount design D12):
	// named by -t archive, or a regular file with a listed extension
	// whose first bytes carry a known signature.
	if (explicit_archive && !target_is_file) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_ERROR_2",
		                      path_arg_1.c_str());
		params.is_image_mode = false;
		return;
	}
	// A drive number is refused in MountPaths: ParseDrive runs after this.
	if (target_is_file && !has_wildcards &&
	    (explicit_archive ||
	     (!explicit_image_type && ArchiveMount::IsArchiveFile(path_arg_1)))) {
		params.type          = MountType::Archive;
		params.is_image_mode = false;
		params.paths.clear();
		params.paths.push_back(simplify_path(path_arg_1).string());
		keep_further_paths();
		if (!params.sidecar_path.empty()) {
			params.sidecar_path = ResolveHostPath(params.sidecar_path,
			                                      path_relative_to_last_config);
		}
		return;
	}
	if (!params.archive_root.empty() ||
	    (!names_member && (params.no_sidecar || params.scratch ||
	                       !params.sidecar_path.empty()))) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_ARCHIVE_ONLY_OPTIONS",
		                      path_arg_1.c_str());
		params.is_image_mode = false;
		return;
	}

	if (names_member && !params.sidecar_path.empty()) {
		params.sidecar_path = ResolveHostPath(params.sidecar_path,
		                                      path_relative_to_last_config);
	}

	params.is_image_mode = (explicit_image_type || params.is_drive_number ||
	                        has_wildcards || first_member.has_value());

	// If the target is a directory, it is a directory mount,
	// even if -t floppy was specified (legacy MOUNT behavior).
	if (target_is_dir) {
		params.is_image_mode = false;
	}

	// Implicit trigger: First argument exists and is a regular file
	if (!params.is_image_mode && target_is_file) {
		params.is_image_mode = true;
	}

	if (params.is_image_mode) {
		// Loop through all remaining arguments
		auto arg_idx        = 2;
		std::string cur_arg = "";

		while (cmd->FindCommand(arg_idx++, cur_arg)) {
			const auto typed = cur_arg;

			// Expand ~, then apply relative path logic
			cur_arg = ApplyRelativePath(
			        resolve_home(HostText(cur_arg)).string(),
			        path_relative_to_last_config);

			if (const auto lone = ArchiveMount::LoneImageMember(cur_arg)) {
				if (!AddArchiveMember(*lone,
				                      cur_arg,
				                      HostText(typed),
				                      params)) {
					params.paths.clear();
					params.member_holds.clear();
					params.is_image_mode = false;
					return;
				}
				continue;
			}
			if (!local_drive_path_exists(cur_arg.c_str())) {
				if (const auto member = ArchiveMount::SplitMemberPath(cur_arg)) {
					if (!AddArchiveMember(*member,
					                      cur_arg,
					                      HostText(typed),
					                      params)) {
						params.paths.clear();
						params.member_holds.clear();
						params.is_image_mode = false;
						return;
					}
					continue;
				}
			}

			// A host image would be written in place on a -scratch drive.
			if (params.scratch) {
				NOTIFY_DisplayWarning(Notification::Source::Console,
				                      "MOUNT",
				                      "PROGRAM_MOUNT_ARCHIVE_SCRATCH_HOST_IMAGE",
				                      cur_arg.c_str());
				params.paths.clear();
				params.member_holds.clear();
				params.is_image_mode = false;
				return;
			}

			// Resolve virtual drive letters to host paths first
			const auto mapped_path = GetDosMappedHostPath(cur_arg);
			const auto path_to_expand = mapped_path.empty()
			                                  ? cur_arg
			                                  : mapped_path;

			// Now try wildcard expansion on the translated host path
			if (path_to_expand.find_first_of("*?") != std::string::npos) {
				if (MOUNT::AddWildcardPaths(path_to_expand,
				                            params.paths)) {
					continue;
				}
			}

			// Fallback for literal files
			const auto real_path = to_native_path(path_to_expand);
			auto loop_final_path = real_path.empty() ? path_to_expand
			                                         : real_path;

			if (real_path.empty() ||
			    !local_drive_path_exists(real_path.c_str())) {
				// Try Virtual DOS Drive mapping
				auto found_on_virtual = false;

				const auto fallback_mapped = HostPathOfDosPath(typed);

				if (!fallback_mapped.empty() &&
				    local_drive_path_exists(fallback_mapped.c_str())) {
					loop_final_path  = fallback_mapped;
					found_on_virtual = true;
				}

				if (!found_on_virtual) {
					// Try wildcards if strictly not found
					if (MOUNT::AddWildcardPaths(cur_arg,
					                            params.paths)) {
						continue;
					}
				}
			}

			// Auto-detect type from FIRST valid file if generic "dir"
			if (params.paths.empty() &&
			    params.type == MountType::Directory) {
				detect_image_type(loop_final_path,
				                  loop_final_path,
				                  params);
			}

			// Shortest equivalent form, which can be RELATIVE;
			// MountPaths replaces it with the validated canonical
			// path before anything opens it (aug-fuay).
			loop_final_path = simplify_path(loop_final_path).string();
			params.paths.push_back(loop_final_path);
		}

		// Ensure consistency between type and fstype if user didn't
		// override -fs
		if (params.type == MountType::FloppyImage &&
		    params.fstype == MountFileSystemType::Fat16) {

			params.mediaid = MediaId::Floppy1_44MB;
		}

	} else {
		// Standard directory or overlay mount
		if (stat_ok && S_ISDIR(test.st_mode)) {
			params.paths.push_back(path_arg_1);
		} else {
			// Upstream #4999 dropped the specific "must be a
			// directory" message here; keep it for our users.
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_MOUNT_ERROR_2",
			                      path_arg_1.c_str());
		}
	}
}

// Design D7: one image inside an archive becomes the host file the image
// code opens; its hold travels in params until the mount registers it.
bool MOUNT::AddArchiveMember(const ArchiveMount::MemberPath& member,
                             const std::string& path_arg,
                             const std::string& shown, MountParameters& params)
{
	// A locked engine opens nothing; MountPaths refuses with its message.
	if (MountPolicy::IsLocked()) {
		params.paths.push_back(path_arg);
		return true;
	}
	ArchiveImages::Request request = {};
	request.member                 = member;
	request.origin                 = MountOrigin::GuestCommand;
	request.read_only              = params.roflag || params.no_sidecar;
	request.sidecar_path           = params.sidecar_path;
	if (params.scratch) {
		auto why = std::string();
		if (!EnsureScratch(params, &why)) {
			ReportMountDenied(shown,
			                  MountArchiveInternal::Denied(
			                          DenyReason::CacheUnavailable),
			                  why);
			return false;
		}
		request.scratch_dir = params.scratch_made->dir;
	}
	request.allowed_image_roots    = MountPolicy::AllowedImageRoots();
	request.conf_anchor            = MountPolicy::ConfAnchor();
	WriteOut(MSG_Get("PROGRAM_MOUNT_ARCHIVE_IDENTIFYING"),
	         member.archive.filename().string().c_str());
	auto result = ArchiveImages::Resolve(request);
	if (!result.resolved) {
		ReportMountDenied(shown, result.verdict, result.detail);
		return false;
	}
	const auto host = result.resolved->host_file.string();
	if (params.paths.empty() && params.type == MountType::Directory) {
		detect_image_type(result.resolved->member_name, host, params);
	}
	if (request.read_only || result.resolved->read_only) {
		params.roflag = true;
	}
	params.paths.push_back(host);
	params.member_holds[host] = std::move(result.resolved->hold);
	// The archive by its full path, as the archive drive's own line names it.
	auto ec            = std::error_code();
	const auto archive = std_fs::weakly_canonical(member.archive, ec);
	auto mounted = ArchiveMount::PathToUtf8(ec ? member.archive : archive);
	if (!member.member.empty()) {
		mounted += "/" + member.member;
	}
	params.member_shown[host] = mounted;
	return true;
}

bool MOUNT::MountPaths(MountParameters& params)
{
	if (params.paths.empty()) {
		// Dir mounts already reported ERROR_2 in ProcessPaths; a
		// second message here would double up.
		if (params.is_image_mode) {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_IMGMOUNT_FILE_NOT_FOUND");
		}
		return false;
	}

	if (MountPolicy::IsLocked()) {
		LOG_WARNING("MOUNT: Blocked mount - locked");
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_CONFIG_SECURE_DISALLOW");
		return false;
	}

	if (params.type == MountType::Archive) {
		// ParseDrive has run: a drive number, or -fs none which becomes
		// one, leaves no letter for the archive drive to register under.
		if (params.is_drive_number) {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_MOUNT_ARCHIVE_NEEDS_LETTER",
			                      params.paths.empty() ? "" : params.paths[0].c_str());
			return false;
		}
		// ProcessPaths refused without a path (reported there).
		return !params.paths.empty() && MountArchive(params);
	}

	if (params.is_image_mode) {
		// Validate every image path before any construction. The
		// validated canonical path replaces the raw string so the
		// object opened is the object validated - a relative path
		// would be re-resolved against the cwd at every later open
		// (aug-fuay; boot.cpp and drive_swap.cpp already do this).
		for (auto& img_path : params.paths) {
			if (params.member_holds.contains(img_path)) {
				// The archive passed the policy in ProcessPaths; this is
				// the engine's own copy, judged by its structure only.
				if (!MountPolicy::ValidateDiskImageStructure(img_path)) {
					ReportMountDenied(img_path,
					                  MountArchiveInternal::Denied(
					                          DenyReason::NotADiskImage));
					return false;
				}
				continue;
			}
			const auto img_verdict = MountPolicy::ValidateImagePath(
			        std_fs::path(img_path),
			        MountOrigin::GuestCommand,
			        MountPolicy::AllowedImageRoots(),
			        MountPolicy::ConfAnchor());
			if (!img_verdict.allowed) {
				ReportMountDenied(img_path, img_verdict);
				return false;
			}
			img_path = img_verdict.resolved.string();
		}

		const auto success = MountImage(params);
		if (success && params.type == MountType::FloppyImage) {
			incrementFDD();
		}
		if (success) {
			// Every image drive records its mode, so a swap keeps -ro
			// for host images as well; the first image typed names the
			// drive's archive.
			std::vector<std::shared_ptr<ArchiveImages::Hold>> holds = {};
			for (const auto& path : params.paths) {
				const auto found = params.member_holds.find(path);
				if (found != params.member_holds.end()) {
					holds.push_back(found->second);
				}
			}
			for (const auto& [path, hold] : params.member_holds) {
				if (std::find(holds.begin(), holds.end(), hold) ==
				    holds.end()) {
					holds.push_back(hold);
				}
			}
			const int index               = params.is_drive_number
			                                      ? params.drive - '0'
			                                      : drive_index(params.drive);
			ArchiveImages::DriveMode mode = {};
			mode.read_only    = params.roflag || params.no_sidecar;
			mode.sidecar_path = params.sidecar_path;
			if (params.scratch_made) {
				mode.scratch_dir = params.scratch_made->dir;
				KeepScratch(params.drive, std::move(*params.scratch_made));
				params.scratch_made.reset();
				WriteOut(MSG_Get("PROGRAM_MOUNT_ARCHIVE_SCRATCH"),
				         params.drive);
			}
			if (!holds.empty()) {
				mode.archive = holds.front()->archive;
				ArchiveImages::Keep(index, std::move(holds));
			}
			ArchiveImages::SetDriveMode(index, mode);
		}

	} else {
		// Standard directory or overlay mount
		const auto dir_verdict = MountPolicy::ValidateDirectoryMount(
		        std_fs::path(params.paths[0]),
		        MountPolicy::ConfAnchor(),
		        MountPolicy::AllowedBases(),
		        CurrentDirPolicy());
		if (!dir_verdict.allowed) {
			ReportMountDenied(params.paths[0], dir_verdict);
			return false;
		}

		MountLocal(params, params.paths[0]);
	}

	return true;
}

void MOUNT::MountLocal(MountParameters& params, const std::string& local_path)
{
	std::string final_path = local_path;
	if (!final_path.empty() && final_path.back() != CROSS_FILESPLIT) {
		final_path += CROSS_FILESPLIT;
	}

	// Give a warning when mount c:\ or the / root directory
#if defined(WIN32)
	if ((final_path == "c:\\") || (final_path == "C:\\") ||
	    (final_path == "c:/") || (final_path == "C:/")) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_WARNING_WIN");
	}
#else
	if (final_path == "/") {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_WARNING_OTHER");
	}
#endif

	std::shared_ptr<DOS_Drive> newdrive = {};
	const uint8_t int8_tize             = (uint8_t)params.sizes[1];

	if (params.type == MountType::Overlay) {
		const auto ldp = std::dynamic_pointer_cast<localDrive>(
		        Drives[drive_index(params.drive)]);

		const auto cdp = std::dynamic_pointer_cast<cdromDrive>(
		        Drives[drive_index(params.drive)]);

		if (!ldp || cdp) {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      "PROGRAM_MOUNT_OVERLAY_INCOMPAT_BASE");
			return;
		}
		std::string base = ldp->GetBasedir();
		uint8_t o_error  = 0;
		newdrive         = std::make_shared<Overlay_Drive>(base.c_str(),
                                                           final_path.c_str(),
                                                           params.sizes[0],
                                                           int8_tize,
                                                           params.sizes[2],
                                                           params.sizes[3],
                                                           params.mediaid,
                                                           o_error);
		// Erase old drive on success
		if (o_error) { //-V547
			if (o_error == 1) {
				NOTIFY_DisplayWarning(Notification::Source::Console,
				                      "MOUNT",
				                      "PROGRAM_MOUNT_OVERLAY_REL_ABS");
			} else if (o_error == 2) {
				NOTIFY_DisplayWarning(Notification::Source::Console,
				                      "MOUNT",
				                      "PROGRAM_MOUNT_OVERLAY_SAME_FS");
			} else {
				NOTIFY_DisplayWarning(Notification::Source::Console,
				                      "MOUNT",
				                      "PROGRAM_MOUNT_OVERLAY_UNKNOWN_ERROR");
			}
			return;
		}
		// Copy current directory if not marked as deleted.
		if (newdrive->TestDir(ldp->curdir)) {
			safe_strcpy(newdrive->curdir, ldp->curdir);
		}
		Drives.at(drive_index(params.drive)) = nullptr;

	} else if (params.type == MountType::CdRomImage) {
		// Mount a host directory as a CD-ROM drive.
		int error = 0;
		newdrive  = std::make_shared<cdromDrive>(params.drive,
                                                        final_path.c_str(),
                                                        params.sizes[0],
                                                        int8_tize,
                                                        params.sizes[2],
                                                        0,
                                                        params.mediaid,
                                                        error);

		const char* msg_id = mscdex_error_to_message_id(error, false);
		if (error == 0) { //-V457 //-V547
			NOTIFY_DisplayInfoMessage(Notification::Source::Console,
			                          "MOUNT",
			                          msg_id);
		} else {
			NOTIFY_DisplayWarning(Notification::Source::Console,
			                      "MOUNT",
			                      msg_id);
		}

		// Errors 1-4 are fatal; 0 (success) and 5 (limited support
		// for subdirectory mounts) allow the drive to be registered.
		if (error && error != 5) { //-V560
			return;
		}
	} else {
		const auto section = get_section("dosbox");
		// Standard directory mount
		newdrive = std::make_shared<localDrive>(
		        final_path.c_str(),
		        params.sizes[0],
		        int8_tize,
		        params.sizes[2],
		        params.sizes[3],
		        params.mediaid,
		        params.roflag,
		        section->GetBool("allow_write_protected_files"));
	}

	DriveManager::RegisterFilesystemImage(drive_index(params.drive), newdrive);
	Drives.at(drive_index(params.drive)) = newdrive;

	// Set the correct media byte in the table
	mem_writeb(RealToPhysical(dos.tables.mediaid) +
	                   (drive_index(params.drive)) * 9,
	           newdrive->GetMediaByte());

	if (params.type != MountType::Overlay) {
		WriteOut(MSG_Get("PROGRAM_MOUNT_STATUS_2"),
		         newdrive->GetInfoString().c_str(),
		         params.drive);

		if (params.roflag) {
			WriteOut(MSG_Get("PROGRAM_MOUNT_READONLY"));
		}
	} else {
		WriteOut(MSG_Get("PROGRAM_MOUNT_OVERLAY_STATUS"),
		         final_path.c_str(),
		         params.drive);
	}

	// check if volume label is given and don't allow it to updated in the
	// future
	if (!params.label.empty()) {
		const auto is_cdrom = (params.type == MountType::CdRomImage);
		newdrive->dirCache.SetLabel(params.label.c_str(), is_cdrom, false);

	} else if (params.type == MountType::Directory ||
	           params.type == MountType::Overlay) {
		// For hard drives set the label to DRIVELETTER_Drive.
		// For floppy drives set the label to DRIVELETTER_Floppy.
		// This way every drive except cdroms should get a label.
		params.label = params.drive;
		params.label += "_DRIVE";

		newdrive->dirCache.SetLabel(params.label.c_str(), false, false);

	} else if (params.type == MountType::FloppyImage) {
		// Floppy labels handled in IMGMOUNT logic usually, but if
		// mounted as dir:
		params.label = params.drive;
		params.label += "_FLOPPY";

		newdrive->dirCache.SetLabel(params.label.c_str(), false, true);
	}

	// We incrementFDD here only if it was a directory mount pretending to
	// be floppy Image mounts return early.
	if (params.type == MountType::FloppyImage) {
		incrementFDD();
	}
}

void MOUNT::Run(void)
{
	if (std::string{cmd->GetFileName()} == "Z:\\IMGMOUNT.COM") {
		// Print deprecation notice
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "IMGMOUNT",
		                      "PROGRAM_IMGMOUNT_DEPRECATED");
	}

	// Hack to allow long commandlines
	ChangeToLongCmd();

	if (!cmd->GetCount()) {
		ListMounts();
		return;
	}
	if (HelpRequested()) {
		ShowUsage();
		return;
	}
	if (control->SecureMode()) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_CONFIG_SECURE_DISALLOW");
		return;
	}

	// Check for unmounting
	if (HandleUnmount()) {
		return;
	}
	if (HandleCache()) {
		return;
	}
	if (HandleReconnect()) {
		return;
	}

	ProcessArguments(cmd);
}

void MOUNT::RunLine(std::unique_ptr<CommandLine> line, const PathText path_text)
{
	assert(line);
	if (control->SecureMode()) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_CONFIG_SECURE_DISALLOW");
		return;
	}
	ProcessArguments(line.release(), path_text);
}

std::optional<MountParameters> MOUNT::ProcessArguments(CommandLine* cmd,
                                                       const PathText path_text)
{
	this->path_text = path_text;

	// To make it easier injecting arbitrary command lines in the unit tests
	if (this->cmd != cmd) {
		delete this->cmd;
		this->cmd = cmd;
	}

	MountParameters params;
	// Phase 6.4: a scratch made for this line and never handed to a drive
	// goes with the line, whichever return ends it.
	struct DiscardUnkeptScratch {
		MountParameters& params;
		~DiscardUnkeptScratch()
		{
			if (params.scratch_made) {
				// The holds keep the copies open; Windows deletes none.
				params.member_holds.clear();
				ArchiveMount::ArchiveCache::DiscardScratch(*params.scratch_made);
				params.scratch_made.reset();
			}
		}
	} discard_unkept{params};

	bool explicit_fs                  = false;
	bool path_relative_to_last_config = false;

	// Parse command line arguments
	if (!ParseArguments(params, explicit_fs, path_relative_to_last_config)) {
		return {};
	}

	if (HasUnknownOptions()) {
		return {};
	}

	// Get the first path argument
	std::string first_path = {};
	if (!cmd->FindCommand(2, first_path) || first_path.empty()) {
		ShowUsage();
		return {};
	}

	// Resolve host paths, wildcard path arguments, auto-detect mount types,
	// and determine whether we're dealing with image or directory/overlay
	// mounts.
	ProcessPaths(first_path, params, path_relative_to_last_config);

	// Check drive geometry and types, abort if not valid
	if (!ParseGeometry(params)) {
		return {};
	}

	// Check drive letter/number and overlaps, abort if not valid
	if (!ParseDrive(params, explicit_fs)) {
		return {};
	}

	// -fs none leaves a drive number: no DOS drive to run anything from.
	if (params.run && params.is_drive_number) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_RUN_NEEDS_LETTER");
		return {};
	}
	// A raw slot has no DOS drive whose unmount discards the scratch.
	if (params.scratch && params.is_drive_number) {
		NOTIFY_DisplayWarning(Notification::Source::Console,
		                      "MOUNT",
		                      "PROGRAM_MOUNT_ARCHIVE_SCRATCH_NEEDS_LETTER");
		return {};
	}

	if (!MountPaths(params)) {
		return {};
	}

	if (params.run) {
		QueueRun(params);
	}

	return params;
}

// Decision 4 (Stefan 2026-10-05): a name that does not resolve leaves the
// mount standing and runs nothing.
void MOUNT::QueueRun(const MountParameters& params)
{
	// The EXIT of -exit follows the chosen program, so a cancelled menu
	// leaves the engine standing
	// Without the bundled UniMenu a menu falls back to -run's own refusal
	const auto queue_menu = [&](ArchiveChooser::Menu menu) {
		const auto autostart = menu.autostart;
		if (!autostart && !MenuSession::MenuProgramAvailable()) {
			return false;
		}
		MenuSession::Open(params.drive,
		                  std::move(menu),
		                  params.run_exit);
		if (autostart && MenuSession::Choose(*autostart)) {
			return true;
		}
		if (!MenuSession::MenuProgramAvailable()) {
			MenuSession::Close();
			return false;
		}
		std::vector<std::string> lines = {};
		lines.emplace_back(std::string(1, params.drive) + ":");
		lines.emplace_back(MenuSession::MenuProgram);
		if (!SHELL_QueuePendingCommands(std::move(lines))) {
			WriteOut(MSG_Get("PROGRAM_MOUNT_RUN_BUSY"));
		}
		return true;
	};
	// Without a name: a curated CHOOSER.TML, then the archive's own
	// DOSBOX.BAT, then the one program at the root, then the scan
	// (UniMenu design, 2026-10-19, steps 3 to 6)
	std::string name = *params.run;
	MountRun::Target target = {};
	if (name.empty()) {
		auto chooser = ArchiveChooser::ReadChooser(params.drive);
		if (chooser && queue_menu(std::move(*chooser))) {
			return;
		}
		auto scan = ArchiveChooser::ScanPrograms(params.drive);
		const auto is_root = [](const ArchiveChooser::Entry& e) {
			return e.exe.find('\\') == std::string::npos;
		};
		const auto at_root = std::count_if(scan.entries.begin(),
		                                   scan.entries.end(),
		                                   is_root);
		if (scan.has_dosbox_bat) {
			name = "DOSBOX.BAT";
		} else if (at_root == 1) {
			name = std::find_if(scan.entries.begin(),
			                    scan.entries.end(),
			                    is_root)
			               ->exe;
		} else if (scan.entries.size() == 1) {
			name = scan.entries.front().exe;
		} else if (scan.entries.size() > 1) {
			std::vector<std::string> candidates = {};
			for (const auto& e : scan.entries) {
				candidates.push_back(e.exe);
			}
			auto menu = ArchiveChooser::ProgramMenu(
			        params.drive, std::move(scan.entries));
			if (queue_menu(std::move(menu))) {
				return;
			}
			target.refusal    = MountRun::Refusal::SeveralPrograms;
			target.candidates = std::move(candidates);
		}
	}
	if (target.refusal == MountRun::Refusal::None) {
		target = MountRun::Resolve(params.drive, name);
	}
	if (target.lines.empty()) {
		const MountRun::Texts texts = {
		        MSG_Get("PROGRAM_MOUNT_RUN_BAD_NAME"),
		        MSG_Get("PROGRAM_MOUNT_RUN_NOT_FOUND"),
		        MSG_Get("PROGRAM_MOUNT_RUN_NOT_A_PROGRAM"),
		        MSG_Get("PROGRAM_MOUNT_RUN_NO_PROGRAM"),
		        MSG_Get("PROGRAM_MOUNT_RUN_SEVERAL_PROGRAMS"),
		        MSG_Get("PROGRAM_MOUNT_RUN_NO_DRIVE"),
		        MSG_Get("PROGRAM_MOUNT_RUN_CANDIDATES"),
		};
		WriteOut_NoParsing(
		        MountRun::Describe(target, name, params.drive, texts));
		return;
	}
	if (params.run_exit) {
		target.lines.emplace_back("EXIT");
	}
	if (!SHELL_QueuePendingCommands(std::move(target.lines))) {
		WriteOut(MSG_Get("PROGRAM_MOUNT_RUN_BUSY"));
	}
}

const char* const MOUNT::ArchiveUnsupportedMessage =
        "%s\n"
        "cannot be mounted: it uses a compression method that is not implemented\n"
        "yet (%s).\n"
        "Please open an issue at\n"
        "https://github.com/dosbox-automation/dosbox-automation/issues\n"
        "and attach the archive, or a link to where it can be downloaded.\n";

// The name stands on a line of its own and the rest fits 80 columns: a
// long path pushed the reason past the edge, where DOS wraps mid-word.
const std::vector<std::pair<std::string, std::string>>& MOUNT::RefusalTexts()
{
	static const std::vector<std::pair<std::string, std::string>> texts = {
	        {"PROGRAM_MOUNT_POLICY_REFUSED",
	         "%s\n"
	         "was refused by the mount policy: %s.\n"
	         "Put a dosbox-automation.conf next to the game and start with -conf,\n"
	         "or add the path to mount_allowed_bases in the primary config.\n"
	         "The security page of the manual explains the policy.\n"},
	        {"PROGRAM_MOUNT_POLICY_SYMLINK",
	         "%s\n"
	         "was refused by the mount policy: a link is in the path.\n"
	         "Mount the real directory the link points to instead.\n"},
	        {"PROGRAM_MOUNT_ARCHIVE_UNSUPPORTED", ArchiveUnsupportedMessage},
	        {"PROGRAM_MOUNT_ARCHIVE_REFUSED", "%s\ncannot be mounted: %s.\n%s"},
	        {"PROGRAM_MOUNT_ARCHIVE_SEQUENTIAL",
	         "%s\n"
	         "is a sequential archive (%s): reaching any member unpacks\n"
	         "everything before it. To unpack it into the archive cache once at\n"
	         "mount time, set archive_sequential = cache in the [dos] section, or\n"
	         "repack it as a zip.\n"},
	        {"PROGRAM_MOUNT_ARCHIVE_CACHE",
	         "%s\n"
	         "cannot be mounted: the archive cache is unavailable.\n"
	         "  %s\n"
	         "Check archive_cache_path, the free space and the cache caps in [dos].\n"},
	        {"PROGRAM_MOUNT_ARCHIVE_SIDECAR",
	         "%s\n"
	         "cannot be mounted: its sidecar is unusable.\n"
	         "  %s\n"
	         "Check archive_sidecar_path in [dos] and the sidecar's .source-archive.toml.\n"},
	        {"PROGRAM_MOUNT_ARCHIVE_ROOT", "%s\nhas no directory %s to use as the root.\n"},
	        {"PROGRAM_MOUNT_SIDECAR_BUSY",
	         "%s\n"
	         "cannot be mounted: its sidecar is in use by another mount.\n"
	         "  %s\n"
	         "Unmount that drive first, or mount with -nosidecar to read only.\n"},
	};
	return texts;
}

void MOUNT::AddMessages()
{
	AddCommonMountMessages();
	if (MSG_Exists("PROGRAM_MOUNT_HELP")) {
		return;
	}
	MSG_Add("PROGRAM_MOUNT_HELP",
	        "Mount a directory or an image file to a drive letter.\n");

	MSG_Add("PROGRAM_MOUNT_HELP_LONG",
	        "Mount a directory or an image file to a drive letter.\n"
	        "\n"
	        "Usage:\n"
	        "  [color=light-green]mount[reset] [color=white]DRIVE[reset] [color=light-cyan]PATH[reset] [PARAMETERS]\n"
	        "  [color=light-green]mount[reset] [color=white]DRIVE[reset] [color=light-cyan]IMAGEFILE[reset] [IMAGEFILE2...] [PARAMETERS]\n"
	        "  [color=light-green]mount[reset] [color=white]DRIVE[reset] [color=light-cyan]IMAGE-SET[reset] [PARAMETERS]\n"
	        "  [color=light-green]mount[reset] -u [color=white]DRIVE[reset]  (unmounts [color=white]DRIVE[reset])\n"
	        "\n"
	        "Common parameters:\n"
	        "  [color=white]DRIVE[reset]           drive letter (A-Z) to mount to\n"
	        "  [color=light-cyan]PATH[reset]            directory on the host OS (absolute or relative path)\n"
	        "  [color=light-cyan]IMAGEFILE[reset]       image file on the host OS (absolute or relative path) or on a\n"
	        "                  mounted DOS drive (e.g. C:\\GAME.ISO)\n"
	        "  [color=light-cyan]IMAGE-SET[reset]       ISO, CUE+BIN, CUE+ISO, or CUE+ISO+FLAC/OPUS/OGG/MP3/WAV\n"
	        "\n"
	        "  -t [color=white]TYPE[reset]         type of mount: [color=light-cyan]dir[reset], [color=light-cyan]overlay[reset], [color=light-cyan]floppy[reset], [color=light-cyan]hdd[reset], [color=light-cyan]iso[reset] (or [color=light-cyan]cdrom[reset])\n"
	        "  -fs [color=white]FS[reset]          filesystem: [color=light-cyan]fat[reset], [color=light-cyan]iso[reset], or [color=light-cyan]none[reset] (for bootable images)\n"
	        "  -label [color=white]LABEL[reset]    volume label to assign to the mounted drive\n"
	        "  -ro             mount as read-only\n"
	        "  -t [color=light-cyan]archive[reset]      mounts a zip, rar, 7z or tar archive (also detected by\n"
	        "                  extension); the archive stays as it is, writes go to a sidecar\n"
	        "                  An image inside an archive is named through it:\n"
	        "                  game.zip/disks/disk1.img\n"
	        "                  A compressed single image mounts as that image:\n"
	        "                  disk1.img.xz, game.imz, game.isz\n"
	        "  -root [color=white]DIR[reset]       directory inside the archive to use as the drive's root\n"
	        "  -sidecar [color=white]PATH[reset]  archive mounts: directory for the game's writes (default: saves)\n"
	        "  -nosidecar      archive mounts: no sidecar, the drive is read-only\n"
	        "  -scratch        archive mounts: keep the writes for this mount only,\n"
	        "                  discarded at unmount (installer media)\n"
	        "  -run [color=white]NAME[reset]       run NAME from the new drive once MOUNT is done; without\n"
	        "                  NAME, its one program, or a menu when there are several\n"
	        "  -exit           with -run: leave DOSBox when the program ends\n"
	        "  -reconnect [color=white]DRIVE[reset] reopen an archive mount that reports lost or offline\n"
	        "  -cache          show the archive cache: size, cap, free space and entries\n"
	        "  -cache trim     remove expired entries and trim the cache to its cap\n"
	        "\n"
	        "Directory mount parameters:\n"
	        "  -freesize [color=white]SIZE[reset]  size (in KB for floppies, in MB for hard disks); sets the\n"
	        "                  amount of free space available on the drive (~250 MB by\n"
	        "                  default for HDD directory mounts)\n"
	        "  -t [color=light-cyan]overlay[reset]      mounts the directory as an overlay on top of an existing drive\n"
	        "\n"
	        "Image parameters:\n"
	        "  -chs [color=white]C,H,S[reset]      specify geometry ([color=white]C[reset]ylinders,[color=white]H[reset]eads,[color=white]S[reset]ectors) for HDD images\n"
	        "  -size [color=white]B,S,H,C[reset]   specify geometry ([color=white]B[reset]ytesPerSector,[color=white]S[reset]ectors,[color=white]H[reset]eads,[color=white]C[reset]ylinders);\n"
	        "                  alternative to -chs for HDD images\n"
	        "  -ide            attach as IDE device (for CD-ROM and HDD images)\n"
	        "  -pr             path is relative to the configuration file location\n"
	        "\n"
	        "Notes:\n"
	        "  - Use wildcards or multiple image files to mount them at the same drive\n"
	        "    letter, then press [color=yellow]%s+F4[reset] to cycle between them. This is useful for\n"
	        "    programs that require swapping disks while running.\n"
	        "\n"
	        "  - The -ide flag emulates an IDE controller for an attached HDD or CD drive\n"
	        "    for CD-based games that need a real DOS environment via a bootable HDD\n"
	        "    image.\n"
	        "\n"
	        "  - Type [color=light-cyan]overlay[reset] requires [color=white]DRIVE[reset] to be already mounted. It mounts [color=light-cyan]PATH[reset] on the\n"
	        "    host OS as a write-layer over the drive. Modified files are stored in [color=light-cyan]PATH[reset],\n"
	        "    leaving the original drive data unchanged.\n"
	        "\n"
	        "  - An archive mount never changes the archive. Writes land in a sidecar\n"
	        "    directory under the saves directory; delete it to reset the game.\n"
	        "    Formats that cannot serve one member without unpacking everything\n"
	        "    (7z, RAR 5, tar) are refused unless archive_sequential = cache is set.\n"
	        "\n"
	        "  - A floppy or hard-disk image taken from an archive is copied once into\n"
	        "    the game's sidecar and written there; -ro or -nosidecar use the cache.\n"
	        "\n"
	        "Examples:\n");

	MSG_Add("PROGRAM_MOUNT_HELP_LONG_WIN32",
	        "  [color=light-green]mount[reset] [color=white]C[reset] [color=light-cyan]C:\\dosgames[reset]\n"
	        "  [color=light-green]mount[reset] [color=white]C[reset] [color=light-cyan]C:\\dosgamesoverlay[reset] -t overlay\n"
	        "  [color=light-green]mount[reset] [color=white]D[reset] [color=light-cyan]D:\\Games\\doom.iso[reset] -t iso\n"
	        "  [color=light-green]mount[reset] [color=white]2[reset] [color=light-cyan]Win95.img[reset] -t hdd -fs none -chs 304,64,63\n"
	        "  [color=light-green]mount[reset] [color=white]0[reset] [color=light-cyan]floppy.img[reset] -t floppy -fs none\n"
	        "  [color=light-green]mount[reset] [color=white]A[reset] [color=light-cyan]floppy*.img[reset] -t floppy\n"
	        "  [color=light-green]mount[reset] [color=white]A[reset] [color=light-cyan]disk01.img disk02.img[reset] -t floppy\n"
	        "  [color=light-green]mount[reset] [color=white]C[reset] [color=light-cyan]D:\\Games\\doom.zip[reset]\n");

	MSG_Add("PROGRAM_MOUNT_HELP_LONG_MACOSX",
	        "  (~ is expanded to your home directory)\n"
	        "  [color=light-green]mount[reset] [color=white]C[reset] [color=light-cyan]~/dosgames[reset]\n"
	        "  [color=light-green]mount[reset] [color=white]C[reset] [color=light-cyan]~/dosgamesoverlay[reset] -t overlay\n"
	        "  [color=light-green]mount[reset] [color=white]D[reset] [color=light-cyan]~/Games/doom.iso[reset] -t iso\n"
	        "  [color=light-green]mount[reset] [color=white]2[reset] [color=light-cyan]Win95.img[reset] -t hdd -fs none -chs 304,64,63\n"
	        "  [color=light-green]mount[reset] [color=white]0[reset] [color=light-cyan]floppy.img[reset] -t floppy -fs none\n"
	        "  [color=light-green]mount[reset] [color=white]A[reset] [color=light-cyan]floppy*.img[reset] -t floppy -ro\n"
	        "  [color=light-green]mount[reset] [color=white]A[reset] [color=light-cyan]disk01.img disk02.img[reset] -t floppy\n"
	        "  [color=light-green]mount[reset] [color=white]C[reset] [color=light-cyan]~/Games/doom.zip[reset]\n");

	MSG_Add("PROGRAM_MOUNT_HELP_LONG_OTHER",
	        "  (~ is expanded to your home directory)\n"
	        "  [color=light-green]mount[reset] [color=white]C[reset] [color=light-cyan]~/dosgames[reset]\n"
	        "  [color=light-green]mount[reset] [color=white]C[reset] [color=light-cyan]~/dosgamesoverlay[reset] -t overlay\n"
	        "  [color=light-green]mount[reset] [color=white]D[reset] [color=light-cyan]~/Games/doom.iso[reset] -t iso\n"
	        "  [color=light-green]mount[reset] [color=white]2[reset] [color=light-cyan]Win95.img[reset] -t hdd -fs none -chs 304,64,63\n"
	        "  [color=light-green]mount[reset] [color=white]0[reset] [color=light-cyan]floppy.img[reset] -t floppy -fs none\n"
	        "  [color=light-green]mount[reset] [color=white]A[reset] [color=light-cyan]floppy*.img[reset] -t floppy -ro\n"
	        "  [color=light-green]mount[reset] [color=white]A[reset] [color=light-cyan]disk01.img disk02.img[reset] -t floppy\n"
	        "  [color=light-green]mount[reset] [color=white]C[reset] [color=light-cyan]~/Games/doom.zip[reset]\n");

	MSG_Add("PROGRAM_MOUNT_CDROMS_FOUND", "CD-ROMs found: %d\n");
	MSG_Add("PROGRAM_MOUNT_ERROR_1", "Directory or file %s doesn't exist.\n");

	MSG_Add("PROGRAM_MOUNT_ERROR_2",
	        "%s isn't a directory or valid image file.\n");

	for (const auto& [id, text] : RefusalTexts()) {
		MSG_Add(id.c_str(), text.c_str());
	}

	MSG_Add("PROGRAM_MOUNT_ARCHIVE_IDENTIFYING", "MOUNT: identifying %s ...\n");
	MSG_Add("PROGRAM_MOUNT_ARCHIVE_ROOT_USED",
	        "Using %s as the root of the archive.\n");
	MSG_Add("PROGRAM_MOUNT_ARCHIVE_ENTERED",
	        "Entering %s inside the archive.\n");
	MSG_Add("PROGRAM_MOUNT_ARCHIVE_RECONNECTED", "Drive %c: reconnected.\n");
	MSG_Add("PROGRAM_MOUNT_CACHE_TRIMMED",
	        "Archive cache trimmed: %d expired, %d over the cap and %d damaged "
	        "entries removed, %d in use kept, %lld MiB left.\n");
	MSG_Add("PROGRAM_MOUNT_CACHE_UNAVAILABLE", "Archive cache unavailable: %s.\n");
	MSG_Add("PROGRAM_MOUNT_CACHE_BAD_ARGUMENT",
	        "MOUNT -cache takes no word or 'trim', not '%s'.\n");
	MSG_Add("PROGRAM_MOUNT_ARCHIVE_RECONNECT_FAILED",
	        "Drive %c: reconnect failed: %s.\n");
	MSG_Add("PROGRAM_MOUNT_ARCHIVE_NOT_ARCHIVE",
	        "Drive %c is not an archive mount.\n");
	MSG_Add("PROGRAM_MOUNT_SIDECAR_REPLACED",
	        "The archive changed since %s was written; a new sidecar was created.\n"
	        "Mount with -sidecar to reuse the old one.\n");
	MSG_Add("PROGRAM_MOUNT_SIDECAR_SKIPPED",
	        "A sidecar beside the archive (%s) was not used: its directory is not\n"
	        "allowed for this mount's origin.\n");

	MSG_Add("PROGRAM_MOUNT_ILL_TYPE", "Illegal type %s");
	MSG_Add("PROGRAM_MOUNT_UNKNOWN_OPTION", "Unknown option: %s\n");
	MSG_Add("PROGRAM_MOUNT_ARCHIVE_NEEDS_LETTER",
	        "%s: an archive mounts on a drive letter, not a drive number.\n");
	MSG_Add("PROGRAM_MOUNT_ARCHIVE_ONLY_OPTIONS",
	        "%s: -root, -sidecar, -nosidecar and -scratch apply to archive mounts only.\n");
	MSG_Add("PROGRAM_MOUNT_ARCHIVE_SCRATCH_CONFLICT",
	        "-scratch cannot be combined with -ro, -nosidecar or -sidecar.\n");
	MSG_Add("PROGRAM_MOUNT_ARCHIVE_SCRATCH_HOST_IMAGE",
	        "%s: a -scratch mount takes images from archives only.\n");
	MSG_Add("PROGRAM_MOUNT_ARCHIVE_SCRATCH_NEEDS_LETTER",
	        "-scratch needs a drive letter, not a drive number or -fs none.\n");
	MSG_Add("PROGRAM_MOUNT_ARCHIVE_SCRATCH",
	        "Writes to %c: are kept for this mount only and discarded at unmount.\n");
	MSG_Add("PROGRAM_MOUNT_REPEATED_OPTION", "Option %s given more than once\n");
	const auto& run_texts = MountRun::EnglishTexts();
	MSG_Add("PROGRAM_MOUNT_RUN_BAD_NAME", run_texts.bad_name);
	MSG_Add("PROGRAM_MOUNT_RUN_NOT_FOUND", run_texts.not_found);
	MSG_Add("PROGRAM_MOUNT_RUN_NOT_A_PROGRAM", run_texts.not_a_program);
	MSG_Add("PROGRAM_MOUNT_RUN_NO_PROGRAM", run_texts.no_program);
	MSG_Add("PROGRAM_MOUNT_RUN_SEVERAL_PROGRAMS", run_texts.several_programs);
	MSG_Add("PROGRAM_MOUNT_RUN_NO_DRIVE", run_texts.no_drive);
	MSG_Add("PROGRAM_MOUNT_RUN_CANDIDATES", run_texts.candidates);
	MSG_Add("PROGRAM_MOUNT_RUN_BUSY",
	        "Another program is already waiting to run; this MOUNT queued nothing.\n");
	MSG_Add("PROGRAM_MOUNT_RUN_NEEDS_LETTER",
	        "-run needs a drive letter; it cannot be used with -fs none.\n");
	MSG_Add("PROGRAM_MOUNT_EXIT_NEEDS_RUN",
	        "-exit only works together with -run.\n");
	MSG_Add("PROGRAM_MOUNT_OPTION_NEEDS_VALUE", "Option %s needs a value\n");
	MSG_Add("PROGRAM_MOUNT_ALREADY_MOUNTED", "Drive %c already mounted with %s\n");
	MSG_Add("PROGRAM_MOUNT_UMOUNT_NOT_MOUNTED", "Drive %c isn't mounted.\n");

	MSG_Add("PROGRAM_MOUNT_UMOUNT_SUCCESS",
	        "Drive %c has successfully been removed.\n");

	MSG_Add("PROGRAM_MOUNT_UMOUNT_NO_VIRTUAL",
	        "Virtual Drives can not be unMOUNTed.\n");

	MSG_Add("PROGRAM_MOUNT_UMOUNT_FILES_OPEN",
	        "Drive %c has open files and stays mounted; close them first.\n");

	MSG_Add("PROGRAM_MOUNT_DRIVEID_ERROR",
	        "'%c' is not a valid drive identifier.\n");

	MSG_Add("PROGRAM_MOUNT_WARNING_WIN",
	        "[color=light-red]Mounting c:\\ is NOT recommended. Please mount a (sub)directory next time.[reset]\n");

	MSG_Add("PROGRAM_MOUNT_WARNING_OTHER",
	        "[color=light-red]Mounting / is NOT recommended. Please mount a (sub)directory next time.[reset]\n");

	MSG_Add("PROGRAM_MOUNT_NO_OPTION",
	        "Warning: Ignoring unsupported option '%s'.\n");

	MSG_Add("PROGRAM_MOUNT_OVERLAY_NO_BASE",
	        "A normal directory needs to be MOUNTed first before an overlay can be added on\n"
	        "top.\n");

	MSG_Add("PROGRAM_MOUNT_OVERLAY_INCOMPAT_BASE",
	        "The overlay is NOT compatible with the drive that is specified.\n");

	MSG_Add("PROGRAM_MOUNT_OVERLAY_MIXED_BASE",
	        "The overlay needs to be specified using the same addressing as the underlying\n"
	        "drive. No mixing of relative and absolute paths.\n");

	MSG_Add("PROGRAM_MOUNT_OVERLAY_SAME_AS_BASE",
	        "The overlay directory can not be the same as underlying drive.\n");

	MSG_Add("PROGRAM_MOUNT_OVERLAY_GENERIC_ERROR", "Something went wrong.\n");
	MSG_Add("PROGRAM_MOUNT_OVERLAY_STATUS", "Overlay %s on drive %c mounted.\n");

	MSG_Add("PROGRAM_MOUNT_INVALID_CHS",
	        "Invalid CHS format. Use -chs cylinders,heads,sectors\n");
	MSG_Add("PROGRAM_MOUNT_FREESIZE_IMAGE",
	        "-freesize sets the free space of a directory mount; an image has its\n"
	        "own size. Give an image's geometry with -size or -chs.\n");

	MSG_Add("PROGRAM_MOUNT_OVERLAY_REL_ABS",
	        "The overlay needs to be specified using the same addressing as the underlying\n"
	        "drive. No mixing of relative and absolute paths.\n");

	MSG_Add("PROGRAM_MOUNT_OVERLAY_SAME_FS",
	        "The overlay needs to be on the same filesystem as the underlying drive.\n");

	MSG_Add("PROGRAM_MOUNT_OVERLAY_UNKNOWN_ERROR", "Something went wrong.\n");

	// IMGMOUNT merged messages
	MSG_Add("PROGRAM_IMGMOUNT_SPECIFY2",
	        "Must specify a drive letter A/B/C/D or drive number 0/1/2/3 to mount image at.\n");

	MSG_Add("PROGRAM_IMGMOUNT_SPECIFY_GEOMETRY",
	        "For hard drive images, drive geometry must be specified:\n"
	        "  [color=light-green]imgmount[reset] [color=white]DRIVE[reset] [color=light-cyan]IMAGEFILE[reset] -chs Cylinders,Heads,Sectors\n"
	        "Alternatively:\n"
	        "  [color=light-green]imgmount[reset] [color=white]DRIVE[reset] [color=light-cyan]IMAGEFILE[reset] -size BytesPerSector,Sectors,Heads,Cylinders\n"
	        "For CD-ROM images:\n"
	        "  [color=light-green]imgmount[reset] [color=white]DRIVE[reset] [color=light-cyan]IMAGEFILE[reset] -t iso\n");

	MSG_Add("PROGRAM_IMGMOUNT_STATUS_NONE", "No drive available.\n");

	MSG_Add("PROGRAM_IMGMOUNT_IDE_CONTROLLERS_UNAVAILABLE",
	        "No available IDE controllers. Drive will not have IDE emulation.\n");

	MSG_Add("PROGRAM_IMGMOUNT_INVALID_IMAGE",
	        "Could not load image file.\n"
	        "Check that the path is correct and the image is accessible.\n");

	MSG_Add("PROGRAM_IMGMOUNT_INVALID_GEOMETRY",
	        "Could not extract drive geometry from image.\n"
	        "Use parameter -chs Cylinders,Heads,Sectors to specify the geometry.\n"
	        "Alternatively: -size BytesPerSector,Sectors,Heads,Cylinders\n");

	MSG_Add("PROGRAM_IMGMOUNT_FILE_NOT_FOUND", "Image file not found.\n");

	MSG_Add("PROGRAM_IMGMOUNT_ALREADY_MOUNTED",
	        "Drive already mounted at that letter.\n");

	MSG_Add("PROGRAM_IMGMOUNT_CANT_CREATE", "Can't create drive from file.\n");
	MSG_Add("PROGRAM_IMGMOUNT_MOUNT_NUMBER", "Drive number %d mounted as %s.\n");

	MSG_Add("PROGRAM_IMGMOUNT_DEPRECATED",
	        "The [color=light-green]IMGMOUNT[reset] command is deprecated; "
	        "please use [color=light-green]MOUNT[reset] instead.");
}
