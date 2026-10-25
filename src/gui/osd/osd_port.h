// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_GUI_OSD_PORT_H
#define DOSBOX_GUI_OSD_PORT_H

// Guest-side OSD and menu interface on I/O ports 0x3E0/0x3E1, byte access.
//
// OSD protocol (osd.com on Y:), while the automation webserver runs:
//   0x3E1 write:  append one text byte to the pending buffer
//   0x3E0 write:  0x01 show the buffer, 0x02 clear the overlay,
//                 0x03 discard the buffer
//
// Menu protocol (UniMenu on Y:), inside a MenuSession, webserver or not:
//   0x3E0 write 0x13: start sending the menu; 0x3E0 then reads 0x4F while
//                     bytes remain and 0x14 at the end (at once without a
//                     session), 0x3E1 reads the next byte
//   0x3E0 write 0x10, one byte on 0x3E1 (the entry number), then 0x11:
//                     run that entry; 0x15 is taken as 0x11
//   0x3E0 write 0x12: cancel, the session closes
//
// 0x3E0 reads 0x4F while either side would answer, otherwise 0xFF like an
// unclaimed port.

void OSDPORT_Init();
void OSDPORT_Destroy();

// The overlay commands answer only while the automation webserver runs;
// the menu commands answer inside a MenuSession either way.
void OSDPORT_EnableOverlay(bool enabled);

#endif
