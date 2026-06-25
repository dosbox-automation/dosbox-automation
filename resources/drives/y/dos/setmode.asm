; setmode.com - set a BIOS video mode given as a hex argument.
; License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
;
; Usage:  setmode <hex>      e.g.  setmode 13   sets mode 13h (320x200x256)
;
; The automation Lua API can read and write memory and inject keys, but it
; cannot invoke INT 10h or do port I/O, so it cannot set a graphics video mode
; on its own. This tiny tool does it from the guest: parse a hex mode number
; off the command line and call INT 10h, AH=0. Bundled on the Y: tools drive,
; which is on the PATH, so the test harness (and any script) can run it by name.
;
; Build (reproducible, no build-system rule by design, like the other Y: tools):
;   nasm -f bin setmode.asm -o setmode.com

cpu 8086
org 0x100

start:
        xor     bx, bx          ; BL accumulates the parsed mode value
        mov     si, 0x81        ; PSP command tail starts at 0x81 (0x80 = length)

.scan:
        mov     al, [si]
        inc     si
        cmp     al, 0x0D        ; CR ends the command tail
        je      .run
        cmp     al, ' '         ; skip spaces (DOS puts one ahead of the args)
        je      .scan

        cmp     al, '9'         ; AL is a hex digit -> fold to a 0..15 nibble
        jbe     .digit
        or      al, 0x20        ; 'A'-'F' -> 'a'-'f'
        sub     al, 'a' - 10    ; 'a' -> 10
        jmp     .accum
.digit:
        sub     al, '0'
.accum:
        and     al, 0x0F
        shl     bl, 1           ; BL = BL*16 + nibble (8086 has no shl by imm)
        shl     bl, 1
        shl     bl, 1
        shl     bl, 1
        or      bl, al
        jmp     .scan

.run:
        mov     al, bl          ; AL = mode number
        xor     ah, ah          ; AH = 0 -> set video mode
        int     0x10
        mov     ax, 0x4C00      ; AH=4Ch terminate with return code, AL=0
        int     0x21
