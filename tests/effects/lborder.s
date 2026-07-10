; lborder.s — Talos Phase 1 test effect: cycle-exact LEFT BORDER REMOVAL.
;
; On a plain STF (WS3) the left border is removed by switching the resolution
; register $ffff8260 to hi-res (2) then back to lo-res (0) at the very start of a
; scanline: Hatari sets BORDERMASK_LEFT_OFF when it sees hi-res at LineCycles<=4
; (video.c). Doing this on a band of scanlines opens the left border there,
; showing 52 extra pixels of screen data where the border colour normally is.
;
; Timing is cycle-exact: sync on VBL via `stop`, mask interrupts, burn a tuned
; delay to the first target line, then a per-scanline loop of exactly one line
; (512 cycles) so every switch lands at the same cycle. MFP interrupts are
; disabled so Timer-C can't jitter the VBL sync frame to frame. Tuned with
; `--trace video_res,video_border_h`.
;
; Runs from the AUTO folder (EmuTOS user mode -> Super(0)).

RES     equ $ffff8260          ; Shifter resolution register (byte)
PAL0    equ $ffff8240          ; palette 0 = border / background colour
PAL15   equ $ffff825e          ; palette 15 = screen content colour
IERA    equ $fffffa07          ; MFP interrupt-enable A
IERB    equ $fffffa09          ; MFP interrupt-enable B
NLINES  equ 120                ; scanlines to open the border on
DELAY_C equ 920                ; dbf count: VBL -> first target line (TUNE)

    text
start:
    clr.l   -(sp)              ; Super(0)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp

    move.b  #0,IERA            ; disable MFP interrupts (Timer-C etc.) -> no jitter
    move.b  #0,IERB

    move.w  #$0700,PAL0        ; border = red
    move.w  #$0070,PAL15       ; screen content = green (contrasts the red border)

    move.l  $44e.w,a0          ; fill the screen with colour 15 (all four planes)
    move.w  #(32000/4)-1,d0
    moveq   #-1,d1
.fill:
    move.l  d1,(a0)+
    dbf     d0,.fill

    move.l  #vbl,$70.w         ; install our VBL handler

main:
    stop    #$2300             ; wait for VBL (jitter-free with MFP off)
    bra.s   main

; --- VBL handler: cycle-counted left-border removal over a band of lines -------
vbl:
    move.w  #$2700,sr          ; mask interrupts: the timing below must be exact
    movem.l d0-d2/a0,-(sp)
    movea.l #RES,a0

    move.w  #DELAY_C,d0        ; coarse delay: reach the first target scanline
.d: dbf     d0,.d

    move.w  #NLINES-1,d2
.line:
    move.b  #2,(a0)            ; hi-res  <- must land at LineCycles <= 4
    move.b  #0,(a0)            ; back to lo-res
    ; pad each iteration to exactly one scanline (512 cyc, no drift):
    move.w  #36,d1
.p: dbf     d1,.p
    nop
    nop
    nop
    nop
    nop
    dbf     d2,.line

    movem.l (sp)+,d0-d2/a0
    rte
