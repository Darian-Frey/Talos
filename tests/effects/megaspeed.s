; megaspeed.s — Talos Phase 3 test effect: Mega STE dual-speed demonstrator (F-210).
;
; A VBL-locked colour hammer. Each frame the CPU writes the background colour
; register ($ff8240) as fast as it can, cycling an 8-colour table indexed by a
; free-running counter that the VBL handler resets every frame (so the pattern is
; frame-locked and steady). The video beam scans the same 313 lines/frame
; regardless of CPU clock, so at 16 MHz the CPU lands ~2x as many colour changes
; down the screen as at 8 MHz — the horizontal bands are ~half as tall.
;
; This makes the Mega STE bimodal cycle budget visible (C-005, F-210): launch at
; --cpuclock 8 vs 16 and watch the band density halve/double. Per BUG-008 this is
; the two global speed settings, not intra-setting cache/bus bimodality.
;
; Runs from the AUTO folder (EmuTOS user mode -> Super(0)).

PAL0    equ $ffff8240           ; background / border colour

    text
start:
    clr.l   -(sp)               ; Super(0)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp

    clr.w   count
    move.l  #vbl,$70.w          ; VBL autovector -> reset the counter each frame

main:
    move.w  count,d0
    lsr.w   #6,d0               ; band thickness (tune); >>6 = new colour every 64 writes
    and.w   #7,d0               ; cycle the 8-colour table
    add.w   d0,d0               ; word index
    move.w  coltab(pc,d0.w),PAL0
    addq.w  #1,count
    bra.s   main

vbl:
    clr.w   count               ; frame-lock: restart the colour ramp each VBL
    rte

    even
coltab:
    dc.w    $700,$070,$007,$770,$707,$077,$777,$333

    bss
    even
count:  ds.w    1
