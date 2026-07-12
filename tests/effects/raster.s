; raster.s — Talos Phase 4 generated raster bars (F-212). Do not hand-edit.
IERA    equ $fffffa07
IERB    equ $fffffa09
PAL0    equ $ffff8240
    text
start:
    clr.l   -(sp)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp
    move.b  #0,IERA              ; MFP off -> Timer-C can't jitter the VBL sync
    move.b  #0,IERB
    move.l  $44e.w,a0            ; clear the screen to palette 0 (whole screen = bg)
    move.w  #(32000/4)-1,d0
    moveq   #0,d1
.clr:
    move.l  d1,(a0)+
    dbf     d0,.clr
    move.l  #vbl,$70.w           ; VBL handler runs the cycle-counted bar loop
main:
    stop    #$2300               ; wait for VBL (jitter-free with MFP off)
    bra.s   main

vbl:
    move.w  #$2700,sr            ; mask: the timing below must be exact
    movem.l d0-d2/a1,-(sp)
    move.w  #900,d0               ; delay: VBL -> first visible bar line
.d: dbf     d0,.d
    lea     coltab(pc),a1
    move.w  #199,d2
.line:
    move.w  (a1)+,PAL0           ; this scanline's colour
    move.w  #36,d1               ; pad the rest of the scanline (=> 512 cyc/line)
.p: dbf     d1,.p
    dbf     d2,.line
    movem.l (sp)+,d0-d2/a1
    rte

    even
coltab:
	dc.w	$700,$700,$700,$700,$700,$700,$700,$700
	dc.w	$700,$700,$700,$700,$700,$700,$700,$700
	dc.w	$700,$700,$700,$700,$700,$700,$700,$700
	dc.w	$700,$700,$700,$700,$070,$070,$070,$070
	dc.w	$070,$070,$070,$070,$070,$070,$070,$070
	dc.w	$070,$070,$070,$070,$070,$070,$070,$070
	dc.w	$070,$070,$070,$070,$070,$070,$070,$070
	dc.w	$007,$007,$007,$007,$007,$007,$007,$007
	dc.w	$007,$007,$007,$007,$007,$007,$007,$007
	dc.w	$007,$007,$007,$007,$007,$007,$007,$007
	dc.w	$007,$007,$007,$007,$770,$770,$770,$770
	dc.w	$770,$770,$770,$770,$770,$770,$770,$770
	dc.w	$770,$770,$770,$770,$770,$770,$770,$770
	dc.w	$770,$770,$770,$770,$770,$770,$770,$770
	dc.w	$707,$707,$707,$707,$707,$707,$707,$707
	dc.w	$707,$707,$707,$707,$707,$707,$707,$707
	dc.w	$707,$707,$707,$707,$707,$707,$707,$707
	dc.w	$707,$707,$707,$707,$077,$077,$077,$077
	dc.w	$077,$077,$077,$077,$077,$077,$077,$077
	dc.w	$077,$077,$077,$077,$077,$077,$077,$077
	dc.w	$077,$077,$077,$077,$077,$077,$077,$077
	dc.w	$777,$777,$777,$777,$777,$777,$777,$777
	dc.w	$777,$777,$777,$777,$777,$777,$777,$777
	dc.w	$777,$777,$777,$777,$777,$777,$777,$777
	dc.w	$777,$777,$777,$777,$777,$777,$777,$777
