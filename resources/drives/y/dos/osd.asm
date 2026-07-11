; osd.com - show text on the dosbox-automation OSD overlay from DOS.
; License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
;
; Usage:  osd <text>        show <text> on the overlay
;         osd /c            clear the overlay
;
; Talks to the host through the automation OSD port interface:
; text bytes go to port 0x3E1, then command 0x01 (show) to port 0x3E0.
; Command 0x02 clears. Port 0x3E0 reads back 0x4F ('O') while the
; automation webserver is running; anything else means the interface is
; absent (plain DOSBox, real hardware) and we exit with errorlevel 1
; without touching the ports.
;
; Build (reproducible, no build-system rule by design, like the other Y: tools):
;   nasm -f bin osd.asm -o osd.com

cpu 8086
org 0x100

PORT_CMD        equ 0x3E0
PORT_DATA       equ 0x3E1
CMD_SHOW        equ 0x01
CMD_CLEAR       equ 0x02
SIGNATURE       equ 0x4F

start:
        mov     dx, PORT_CMD    ; probe for the interface first
        in      al, dx
        cmp     al, SIGNATURE
        jne     .absent

        mov     si, 0x81        ; PSP command tail starts at 0x81 (0x80 = length)

.skipspace:
        lodsb
        cmp     al, ' '
        je      .skipspace
        cmp     al, 0x0D        ; empty command line -> nothing to show
        je      .done

        cmp     al, '/'         ; "/c" or "/C" clears the overlay
        jne     .text
        lodsb
        or      al, 0x20
        cmp     al, 'c'
        jne     .done           ; unknown switch, do nothing
        mov     al, CMD_CLEAR
        mov     dx, PORT_CMD
        out     dx, al
        jmp     .done

.text:                          ; AL holds the first text byte already
        mov     dx, PORT_DATA
.send:
        out     dx, al
        lodsb
        cmp     al, 0x0D        ; CR ends the command tail
        jne     .send

        mov     al, CMD_SHOW
        mov     dx, PORT_CMD
        out     dx, al

.done:
        mov     ax, 0x4C00      ; AH=4Ch terminate, errorlevel 0
        int     0x21

.absent:
        mov     ax, 0x4C01      ; interface not present, errorlevel 1
        int     0x21
