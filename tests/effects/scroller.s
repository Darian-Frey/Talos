; scroller.s — Talos Phase 4: STE hardware smooth scroll (F-211 third example).
;
; Demonstrates the STE hardware scroll registers, not a software copy. The screen
; is filled with 16px vertical stripes; each VBL the scroll offset advances one
; pixel, split into the STE fine-scroll register ($ff8265, 0-15 px within a 16px
; unit) and a coarse advance of the video base ($ff8201/03/0d) by one 16px unit
; (8 bytes) when the fine scroll wraps. Total shift = coarse*16 + fine = the
; offset, and the stripes repeat every 32px, so 0..31 wraps seamlessly -> a
; continuous smooth left scroll. Because every scanline is identical, no
; line-width ($ff820f) compensation is needed.
;
; Needs STE hardware scroll: run on STE / Mega STE. Runs from AUTO (Super(0)).

FSCROLL equ $ffff8265           ; STE fine horizontal scroll (0-15 px)
V_HI    equ $ffff8201           ; video base high / mid / low (STE)
V_MID   equ $ffff8203
V_LO    equ $ffff820d
PAL0    equ $ffff8240
PAL1    equ $ffff8242

    text
start:
    clr.l   -(sp)               ; Super(0)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp

    move.w  #$007,PAL0          ; colour 0 = blue
    move.w  #$770,PAL1          ; colour 1 = yellow

    move.l  $44e.w,a0           ; fill the screen with 16px vertical stripes
    move.l  a0,base
    move.w  #(32000/8)-1,d0     ; 8-byte (16px, 4-plane) groups
    moveq   #0,d1
.fill:
    move.w  d1,(a0)             ; plane 0 = stripe on/off
    clr.w   2(a0)               ; planes 1-3 = 0
    clr.w   4(a0)
    clr.w   6(a0)
    not.w   d1                  ; toggle $0000 <-> $ffff each group
    lea     8(a0),a0
    dbf     d0,.fill

    clr.w   scroll
    move.l  #vbl,$70.w
main:
    stop    #$2300
    bra.s   main

vbl:
    movem.l d0-d1/a1,-(sp)
    move.w  scroll,d0
    addq.w  #1,d0
    cmp.w   #32,d0              ; stripe period = 32px -> wrap
    blo.s   .ok
    moveq   #0,d0
.ok:
    move.w  d0,scroll
    move.w  d0,d1
    and.w   #$f,d1
    move.b  d1,FSCROLL          ; fine scroll = offset & 15
    lsr.w   #4,d0               ; coarse unit (0 or 1)
    lsl.w   #3,d0               ; -> 0 or 8 bytes
    move.l  base,a1
    adda.w  d0,a1               ; video base = screen + coarse
    move.l  a1,d0
    move.b  d0,V_LO
    lsr.l   #8,d0
    move.b  d0,V_MID
    lsr.l   #8,d0
    move.b  d0,V_HI
    movem.l (sp)+,d0-d1/a1
    rte

    bss
    even
base:   ds.l    1
scroll: ds.w    1
