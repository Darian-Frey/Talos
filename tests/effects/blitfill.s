; blitfill.s — Talos Phase 3 test effect: continuous BLITTER copies (F-208).
;
; Repeatedly blits a 16x16-word source buffer to the top-left of the screen with
; the blitter (HOP = source, LOP = replace), so a `blittrace` capture at any
; moment catches a steady stream of memory traffic: 256 source reads + 256 dest
; writes per blit. Unlike a boot-time screen-clear (writes only), this exercises
; the read path too. The dest Y-increment steps a whole screen line per row, so
; it draws a clean 64x16 rectangle rather than a contiguous smear.
;
; Requires a machine with a blitter: run on STE / Mega ST / Mega STE.
; Runs from the AUTO folder (EmuTOS user mode -> Super(0)).

SRC_XI  equ $ffff8a20           ; blitter source X increment (word)
SRC_YI  equ $ffff8a22           ; source Y increment (word)
SRC_AD  equ $ffff8a24           ; source address (long)
EMASK1  equ $ffff8a28           ; endmask 1/2/3 (word each)
EMASK2  equ $ffff8a2a
EMASK3  equ $ffff8a2c
DST_XI  equ $ffff8a2e           ; dest X increment (word)
DST_YI  equ $ffff8a30           ; dest Y increment (word)
DST_AD  equ $ffff8a32           ; dest address (long)
XCNT    equ $ffff8a36           ; words per line (word)
YCNT    equ $ffff8a38           ; lines (word)
HOP     equ $ffff8a3a           ; halftone/source op (byte)
LOP     equ $ffff8a3b           ; logic op (byte)
CTRL    equ $ffff8a3c           ; control: bit7 BUSY(start), bit6 HOG (byte)
SKEW    equ $ffff8a3d           ; skew (byte)

WORDS   equ 16                  ; words per line
LINES   equ 16                  ; lines per blit  -> 256 words copied
LINEB   equ 160                 ; screen bytes per line (low-res: 320px x 4 planes)

    text
start:
    clr.l   -(sp)               ; Super(0)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp

    lea     src,a0              ; fill the source buffer with a visible pattern
    move.w  #(WORDS*LINES)-1,d0
    move.w  #$0f0f,d1
.sfill:
    move.w  d1,(a0)+
    add.w   #$1111,d1
    dbf     d0,.sfill

    move.w  #2,SRC_XI           ; one-time setup: source read stays contiguous
    move.w  #2,SRC_YI
    move.w  #2,DST_XI
    move.w  #LINEB-(WORDS-1)*2,DST_YI  ; dest row-end jump -> next screen line (=130)
    move.w  #$ffff,EMASK1       ; full words, no edge masking
    move.w  #$ffff,EMASK2
    move.w  #$ffff,EMASK3
    move.b  #2,HOP              ; HOP = source
    move.b  #3,LOP              ; LOP = replace (D = S)
    move.b  #0,SKEW

loop:
    move.l  #src,SRC_AD         ; blitter advances addr/count during a blit -> reset
    move.l  $44e.w,DST_AD       ; dest = current screen base
    move.w  #WORDS,XCNT
    move.w  #LINES,YCNT
    move.b  #$c0,CTRL           ; BUSY | HOG -> run the blit
.wait:
    btst    #7,CTRL             ; spin until BUSY clears
    bne.s   .wait
    bra.s   loop

    bss
    even
src:
    ds.w    WORDS*LINES
