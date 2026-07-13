; multisplit.s — Talos Phase 4: HBL-synced multi-split (Spectrum-512-lite).
;
; Extends split.s from one mid-line colour change to MANY: the HBL handler writes
; a run of background colours back-to-back (packed, ~no delay), so each write
; lands ~one instruction apart down the scanline -> a row of narrow vertical
; colour bands. Re-synced every line by the HBL interrupt (from stop), so the
; whole grid is steady. This is how Spectrum 512 works (it packs 44+ writes/line
; for 512 on-screen colours); here 20 writes give 20 bands, bounded by how many
; fit the visible line (~416 cycles).
;
; MFP off so only HBL/VBL fire. Runs from the AUTO folder (Super(0)).

IERA    equ $fffffa07
IERB    equ $fffffa09
PAL0    equ $ffff8240

    text
start:
    clr.l   -(sp)               ; Super(0)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp

    move.b  #0,IERA             ; MFP off -> only HBL/VBL fire
    move.b  #0,IERB
    move.l  $44e.w,a0           ; clear the screen to palette 0
    move.w  #(32000/4)-1,d0
    moveq   #0,d1
.clr:
    move.l  d1,(a0)+
    dbf     d0,.clr
    move.l  #hbl,$68.w          ; HBL autovector -> the packed colour run
    move.l  #vbl,$70.w

main:
    stop    #$2100              ; IPL1: HBL wakes us with fixed latency
    bra.s   main

; 20 packed writes cycling an 8-colour palette -> 20 vertical bands per line.
hbl:
    move.w  #$700,PAL0
    move.w  #$070,PAL0
    move.w  #$007,PAL0
    move.w  #$770,PAL0
    move.w  #$707,PAL0
    move.w  #$077,PAL0
    move.w  #$777,PAL0
    move.w  #$330,PAL0
    move.w  #$700,PAL0
    move.w  #$070,PAL0
    move.w  #$007,PAL0
    move.w  #$770,PAL0
    move.w  #$707,PAL0
    move.w  #$077,PAL0
    move.w  #$777,PAL0
    move.w  #$330,PAL0
    move.w  #$700,PAL0
    move.w  #$070,PAL0
    move.w  #$007,PAL0
    move.w  #$770,PAL0
    rte

vbl:
    rte
