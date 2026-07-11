; dmasound.s — Talos Phase 3 test effect: DMA sound + LMC1992 EQ sweep (F-209).
;
; Plays a looping sawtooth sample through the STE DMA sound hardware, and
; continuously sweeps the LMC1992 bass/treble via the Microwire interface. So a
; `dmatrace` capture at any moment catches the sample buffer draining (a rising
; play-head sawtooth that resets each loop) and a stream of EQ setting changes.
;
; Requires DMA sound + LMC1992: run on STE / Mega STE.
; Runs from the AUTO folder (EmuTOS user mode -> Super(0)).

CTRL    equ $ffff8900           ; DMA sound control (word): bit0 play, bit1 loop
FSTART  equ $ffff8903           ; frame start: hi $8903 / mid $8905 / lo $8907
FEND    equ $ffff890f           ; frame end:   hi $890f / mid $8911 / lo $8913
MODE    equ $ffff8921           ; sound mode (byte): bits0-1 freq, bit7 mono
MWDATA  equ $ffff8922           ; Microwire data (word)
MWMASK  equ $ffff8924           ; Microwire mask (word)

LEN     equ 4096                ; sample length (bytes)

; Microwire/LMC1992 command = 11 bits: '10' + 3-bit param + 6-bit value, in the
; low 11 bits of the data word, with mask = $7ff. param: 1 bass, 2 treble,
; 3 master. Base words: bass $440, treble $480, master $4c0 (= $400 | param<<6).

    text
start:
    clr.l   -(sp)               ; Super(0)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp

    lea     sample,a0           ; fill the buffer with a sawtooth
    move.w  #LEN-1,d0
    moveq   #0,d1
.fill:
    move.b  d1,(a0)+
    addq.b  #1,d1
    dbf     d0,.fill

    move.l  #sample,d0          ; frame start = sample
    move.b  d0,FSTART+4         ; lo
    lsr.l   #8,d0
    move.b  d0,FSTART+2         ; mid
    lsr.l   #8,d0
    move.b  d0,FSTART           ; hi

    move.l  #sample+LEN,d0      ; frame end = sample + LEN
    move.b  d0,FEND+4
    lsr.l   #8,d0
    move.b  d0,FEND+2
    lsr.l   #8,d0
    move.b  d0,FEND

    move.b  #$81,MODE           ; mono, 12.5 kHz

    move.w  #$7ff,MWMASK        ; master volume = max ($4c0 | $3f)
    move.w  #$4ff,MWDATA
    bsr     mwwait

    move.w  #3,CTRL             ; play + loop

    moveq   #0,d3               ; bass sweep counter (0..15)
main:
    move.w  #$7ff,MWMASK        ; bass = d3
    move.w  d3,d4
    or.w    #$440,d4
    move.w  d4,MWDATA
    bsr     mwwait

    move.w  #15,d5              ; treble = 15 - d3 (sweeps opposite)
    sub.w   d3,d5
    or.w    #$480,d5
    move.w  #$7ff,MWMASK
    move.w  d5,MWDATA
    bsr     mwwait

    addq.w  #1,d3
    and.w   #$f,d3
    bra     main

mwwait:                         ; coarse delay: let the 16-bit Microwire shift finish
    move.l  #$8000,d0
.w: subq.l  #1,d0
    bne.s   .w
    rts

    bss
    even
sample:
    ds.b    LEN
