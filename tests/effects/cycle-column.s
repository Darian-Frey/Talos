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
    lea     bandtab(pc),a2       ; fill: each line = its band's index
    move.l  $44e.w,a0
    move.w  #199,d2
.fl:
    moveq   #0,d0
    move.b  (a2)+,d0
    mulu    #160,d0              ; 80 words per line pattern
    lea     linepat(pc),a1
    adda.l  d0,a1
    moveq   #80-1,d1
.cp:
    move.w  (a1)+,(a0)+
    dbf     d1,.cp
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
linepat:
	dc.w	$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000
	dc.w	$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000
	dc.w	$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000
	dc.w	$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000
	dc.w	$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000
	dc.w	$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000
	dc.w	$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000
	dc.w	$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000
	dc.w	$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000
	dc.w	$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000
	dc.w	$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000
	dc.w	$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000
	dc.w	$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000
	dc.w	$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000
	dc.w	$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000
	dc.w	$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000
	dc.w	$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff
	dc.w	$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000
	dc.w	$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff
	dc.w	$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000
	dc.w	$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff
	dc.w	$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000
	dc.w	$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff
	dc.w	$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000
	dc.w	$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff
	dc.w	$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000
	dc.w	$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff
	dc.w	$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000
	dc.w	$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff
	dc.w	$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000
	dc.w	$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff
	dc.w	$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000
	dc.w	$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000
	dc.w	$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000
	dc.w	$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000
	dc.w	$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000
	dc.w	$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000
	dc.w	$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000
	dc.w	$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000
	dc.w	$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000
	dc.w	$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000
	dc.w	$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000
	dc.w	$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000
	dc.w	$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000
	dc.w	$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000
	dc.w	$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000
	dc.w	$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000
	dc.w	$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000
	dc.w	$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff
	dc.w	$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000
	dc.w	$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff
	dc.w	$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000
	dc.w	$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff
	dc.w	$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000
	dc.w	$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff
	dc.w	$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000
	dc.w	$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff
	dc.w	$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000
	dc.w	$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff
	dc.w	$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000
	dc.w	$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff
	dc.w	$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000
	dc.w	$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff
	dc.w	$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000
	dc.w	$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000
	dc.w	$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff
	dc.w	$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000
	dc.w	$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff
	dc.w	$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000
	dc.w	$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff
	dc.w	$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000
	dc.w	$0000,$ffff,$0000,$0000,$0000,$ffff,$0000,$0000,$0000,$ffff
	dc.w	$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000
	dc.w	$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff
	dc.w	$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000
	dc.w	$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff
	dc.w	$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000
	dc.w	$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff
	dc.w	$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000
	dc.w	$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff
	dc.w	$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff
	dc.w	$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff
	dc.w	$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff
	dc.w	$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff
	dc.w	$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff
	dc.w	$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff
	dc.w	$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff
	dc.w	$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff,$0000,$ffff
	dc.w	$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff
	dc.w	$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff
	dc.w	$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff
	dc.w	$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff
	dc.w	$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff
	dc.w	$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff
	dc.w	$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff
	dc.w	$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff
	dc.w	$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000
	dc.w	$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff
	dc.w	$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000
	dc.w	$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff
	dc.w	$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000
	dc.w	$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff
	dc.w	$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000
	dc.w	$ffff,$ffff,$0000,$0000,$ffff,$ffff,$0000,$0000,$ffff,$ffff
	dc.w	$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000
	dc.w	$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff
	dc.w	$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000
	dc.w	$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff
	dc.w	$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000
	dc.w	$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff
	dc.w	$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000
	dc.w	$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff
	dc.w	$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff
	dc.w	$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff
	dc.w	$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff
	dc.w	$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff
	dc.w	$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff
	dc.w	$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff
	dc.w	$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff
	dc.w	$ffff,$ffff,$0000,$ffff,$ffff,$ffff,$0000,$ffff,$ffff,$ffff
	dc.w	$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff
	dc.w	$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff
	dc.w	$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff
	dc.w	$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff
	dc.w	$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff
	dc.w	$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff
	dc.w	$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff
	dc.w	$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff,$ffff
    even
bandtab:
	dc.b	$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$01,$01,$01
	dc.b	$01,$01,$01,$01,$01,$01,$01,$01,$01,$02,$02,$02,$02,$02,$02,$02
	dc.b	$02,$02,$02,$02,$02,$02,$03,$03,$03,$03,$03,$03,$03,$03,$03,$03
	dc.b	$03,$03,$04,$04,$04,$04,$04,$04,$04,$04,$04,$04,$04,$04,$04,$05
	dc.b	$05,$05,$05,$05,$05,$05,$05,$05,$05,$05,$05,$06,$06,$06,$06,$06
	dc.b	$06,$06,$06,$06,$06,$06,$06,$06,$07,$07,$07,$07,$07,$07,$07,$07
	dc.b	$07,$07,$07,$07,$08,$08,$08,$08,$08,$08,$08,$08,$08,$08,$08,$08
	dc.b	$08,$09,$09,$09,$09,$09,$09,$09,$09,$09,$09,$09,$09,$0a,$0a,$0a
	dc.b	$0a,$0a,$0a,$0a,$0a,$0a,$0a,$0a,$0a,$0a,$0b,$0b,$0b,$0b,$0b,$0b
	dc.b	$0b,$0b,$0b,$0b,$0b,$0b,$0c,$0c,$0c,$0c,$0c,$0c,$0c,$0c,$0c,$0c
	dc.b	$0c,$0c,$0c,$0d,$0d,$0d,$0d,$0d,$0d,$0d,$0d,$0d,$0d,$0d,$0d,$0e
	dc.b	$0e,$0e,$0e,$0e,$0e,$0e,$0e,$0e,$0e,$0e,$0e,$0e,$0f,$0f,$0f,$0f
	dc.b	$0f,$0f,$0f,$0f,$0f,$0f,$0f,$0f
    even
pal:
    dc.w    $0700,$0740,$0770,$0070,$0077,$0007,$0707,$0777,$0700,$0740,$0770,$0070,$0077,$0007,$0707,$0777
