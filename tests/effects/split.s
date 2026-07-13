; split.s — Talos Phase 4 intra-line pilot: an HBL-synced vertical raster split.
;
; Demonstrates cycle-*within-line* control (the Spectrum-512 direction), which is
; harder than per-line raster bars: a raster bar tolerates loose timing (one
; colour fills the whole line wherever the write lands), but changing colour
; *mid-line* needs the second write at a precise beam position.
;
; A free-running cycle-counted loop drifts (the write precesses line to line,
; giving a diagonal split). The fix is to re-sync every scanline to the HBL
; interrupt: the CPU sits in STOP (fixed interrupt latency => low jitter), the
; HBL handler writes colour A, burns a tuned delay, then writes colour B. Because
; each line is re-synced, the A->B boundary is a clean vertical line whose column
; is set by the delay (see harness/intraline_split.py for the calibration).
;
; MFP is off so only HBL/VBL fire. Runs from the AUTO folder (Super(0)).

IERA    equ $fffffa07
IERB    equ $fffffa09
PAL0    equ $ffff8240
LEFT    equ 11                  ; HBL-delay dbf count -> split near mid-screen

    text
start:
    clr.l   -(sp)               ; Super(0)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp

    move.b  #0,IERA             ; MFP off -> only HBL/VBL interrupts fire
    move.b  #0,IERB
    move.l  $44e.w,a0           ; clear the screen to palette 0 (whole line = bg)
    move.w  #(32000/4)-1,d0
    moveq   #0,d1
.clr:
    move.l  d1,(a0)+
    dbf     d0,.clr
    move.l  #hbl,$68.w          ; HBL autovector -> the per-line split
    move.l  #vbl,$70.w          ; VBL autovector -> just wake/return

main:
    stop    #$2100              ; IPL1: level-2 HBL wakes us with fixed latency
    bra.s   main

hbl:
    move.w  #$007,PAL0          ; left colour (blue), persists into the line
    move.w  #LEFT,d0
.l: dbf     d0,.l               ; delay -> split column
    move.w  #$700,PAL0          ; right colour (red) at the split
    rte

vbl:
    rte
