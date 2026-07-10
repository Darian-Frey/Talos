; rasterbars.s — Talos Phase 1 test effect (assemble with vasm -Ftos).
;
; Purpose: generate a stream of hardware-register writes at beam positions spread
; across the whole frame, so the register-write-to-cycle capture (F-203) has
; something to visualise. It continuously writes the Shifter background-colour
; register with an incrementing value; with a delay between writes the colour
; changes partway down/across the frame, giving rolling colour bands and writes
; scattered over scanlines and cycles.
;
; Not a cycle-exact stable raster — deliberately simple and robust. A cycle-exact
; border-removal routine comes later as a harder harness case.
;
; Runs on a plain ST in low resolution (matches BeamGeometry's assumption).
; Installed as an AUTO-folder program. EmuTOS runs AUTO programs in USER mode, so
; we must Super(0) into supervisor before touching the (protected) hardware
; registers. It never returns: it takes over and loops forever.

COLOR   equ $ffff8240      ; Shifter palette entry 0 = background colour
DELAY   equ 400            ; spacing between writes (~40 writes/frame at 8 MHz)

    text

start:
    clr.l   -(sp)          ; Super(0): enter supervisor mode
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp

    moveq   #0,d0          ; colour accumulator
.loop:
    move.w  d0,COLOR       ; <-- the watched write: background colour
    addq.w  #1,d0
    and.w   #$777,d0       ; keep within the ST 3-bit-per-gun palette range

    move.w  #DELAY,d1
.wait:
    dbf     d1,.wait       ; spread the next write further along the beam

    bra.s   .loop
