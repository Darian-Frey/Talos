; cycle.s — Talos generated palette colour-cycling (F-212). Do not hand-edit.
IERA    equ $fffffa07
IERB    equ $fffffa09
PALBASE equ $ffff8240
    text
start:
    clr.l   -(sp)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp
    move.b  #0,IERA
    move.b  #0,IERB
    move.l  $44e.w,a0            ; fill 200 lines with the 16-index stripe ramp
    move.w  #199,d2
.fl:
    lea     linedat(pc),a1
    moveq   #80-1,d0
.cp:
    move.w  (a1)+,(a0)+
    dbf     d0,.cp
    dbf     d2,.fl
    lea     pal(pc),a1           ; load the 16 palette registers
    lea     PALBASE,a0
    moveq   #15,d0
.sp:
    move.w  (a1)+,(a0)+
    dbf     d0,.sp
    move.l  #vbl,$70.w
main:
    stop    #$2300
    bra.s   main

vbl:
    movem.l d0-d1/a0,-(sp)       ; rotate the palette one step each frame
    lea     PALBASE,a0
    move.w  (a0),d0              ; save colour 0
    moveq   #14,d1
.rot:
    move.w  2(a0),(a0)+          ; pal[i] = pal[i+1]
    dbf     d1,.rot
    move.w  d0,(a0)              ; pal[15] = old colour 0
    movem.l (sp)+,d0-d1/a0
    rte

    even
linedat:
	dc.w	$0000,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$0000,$ffff
	dc.w	$0000,$0000,$ffff,$ffff,$0000,$0000,$0000,$0000,$ffff,$0000
	dc.w	$ffff,$0000,$ffff,$0000,$0000,$ffff,$ffff,$0000,$ffff,$ffff
	dc.w	$ffff,$0000,$0000,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff
	dc.w	$0000,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$0000,$0000
	dc.w	$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$0000,$ffff,$ffff,$ffff
	dc.w	$ffff,$ffff,$ffff,$ffff,$0000,$0000,$0000,$0000,$ffff,$0000
	dc.w	$0000,$0000,$0000,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000

    even
pal:
    dc.w    $0700,$0740,$0770,$0070,$0077,$0007,$0707,$0777,$0700,$0740,$0770,$0070,$0077,$0007,$0707,$0777
