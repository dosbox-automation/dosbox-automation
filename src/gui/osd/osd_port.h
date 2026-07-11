// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_GUI_OSD_PORT_H
#define DOSBOX_GUI_OSD_PORT_H

// Guest-side OSD interface on I/O ports 0x3E0/0x3E1, so DOS programs
// and batch files (via osd.com on the Y: drive) can show overlay text.
//
// Protocol, byte writes only:
//   port 0x3E1 (data):    append one text byte to the pending buffer
//   port 0x3E0 (command): 0x01 show pending buffer as overlay
//                         0x02 clear the overlay
//                         0x03 discard the pending buffer
//   port 0x3E0 read:      returns 0x4F ('O') so guest code can detect
//                         the interface before using it
//
// Registered only while the automation webserver is enabled; the ports
// stay unclaimed otherwise.

void OSDPORT_Init();
void OSDPORT_Destroy();

#endif
