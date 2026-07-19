#include "BorderCodegen.h"

// All figures below are taken from Hatari's src/includes/video.h and the border
// documentation in src/video.c (verified 2026-07-19), never from memory (C-007):
//
//   CYCLES_PER_LINE_50HZ 512   LINE_START_CYCLE_50 56   LINE_END_CYCLE_50 376
//   LINE_END_CYCLE_60 372      LINE_END_CYCLE_NO_RIGHT 460 (= 372 + 44*2)
//   VIDEO_START_HBL_50HZ 63    VIDEO_END_HBL_50HZ 263   FIRST_VISIBLE_HBL_50HZ 34
//   VIDEO_HEIGHT_BOTTOM_50HZ 47 (60 Hz: 26)
//   LINE_REMOVE_TOP_CYCLE_STF 504  LINE_REMOVE_BOTTOM_CYCLE_STF 504 (STE: 500)
//   Top border removed by a 50->60 Hz switch on line 33 before cycle 504; bottom
//   by a switch on line 263 before cycle 504 (switch back to 50 Hz after 504).
//   Left border removed by a hi/lo resolution switch at the very start of a line
//   (LineCycles <= 4 — video.c BORDERMASK_LEFT); right border by a 60->50 switch
//   near LINE_END_CYCLE_60 so the display runs on to LINE_END_CYCLE_NO_RIGHT.

namespace BorderCodegen {

QString borderName(Border b)
{
    switch (b) {
    case Border::Left:   return QStringLiteral("Left border");
    case Border::Right:  return QStringLiteral("Right border");
    case Border::Top:    return QStringLiteral("Top border");
    case Border::Bottom: return QStringLiteral("Bottom border");
    }
    return {};
}

Facts facts(Border b)
{
    Facts f;
    f.border = b;
    f.name = borderName(b);
    switch (b) {
    case Border::Left:
        f.reg = QStringLiteral("$ffff8260");
        f.regName = QStringLiteral("Shifter resolution");
        f.writeSeq = QStringLiteral("hi-res ($02) → lo-res ($00)");
        f.cycleWindow = QStringLiteral("at LineCycles ≤ 4 — the very start of the line");
        f.onLines = QStringLiteral("every visible line you want opened (%1–%2)")
                        .arg(kDispFirst).arg(kDispLast - 1);
        f.consequence = QStringLiteral("opens ~52 px of left overscan on each line");
        f.mechanism = QStringLiteral(
            "A brief switch to hi-res and back at the start of a line makes the shifter "
            "start fetching before the normal display window (cycle %1), so the left "
            "border is drawn as screen content. Repeat it every line to open a band. "
            "Hatari sets BORDERMASK_LEFT when it sees hi-res at LineCycles ≤ 4.")
            .arg(kLineStart);
        f.axis = Axis::Horizontal;
        f.switchCycle = 2;
        f.trickFirst = kDispFirst;
        f.trickLast = kDispLast - 1;
        f.runnable = true;
        break;
    case Border::Right:
        f.reg = QStringLiteral("$ffff820a");
        f.regName = QStringLiteral("Sync mode (50/60 Hz)");
        f.writeSeq = QStringLiteral("60 Hz ($00) → 50 Hz ($02)");
        f.cycleWindow = QStringLiteral("straddling cycle %1 (LINE_END_CYCLE_60)").arg(kLineEnd - 4);
        f.onLines = QStringLiteral("every visible line you want opened (%1–%2)")
                        .arg(kDispFirst).arg(kDispLast - 1);
        f.consequence = QStringLiteral("+%1 px on the right (display runs on to cycle %2)")
                            .arg(kRightExtra).arg(kLineEndNoRight);
        f.mechanism = QStringLiteral(
            "In 60 Hz the display-off comparison is at cycle %1 instead of %2. Being in "
            "60 Hz as the line reaches that point makes the shifter miss the 50 Hz "
            "right-border start, so it keeps drawing to cycle %3 — an extra %4 px.")
            .arg(kLineEnd - 4).arg(kLineEnd).arg(kLineEndNoRight).arg(kRightExtra);
        f.axis = Axis::Horizontal;
        f.switchCycle = kLineEnd - 4;
        f.trickFirst = kDispFirst;
        f.trickLast = kDispLast - 1;
        f.runnable = false;
        break;
    case Border::Top:
        f.reg = QStringLiteral("$ffff820a");
        f.regName = QStringLiteral("Sync mode (50/60 Hz)");
        f.writeSeq = QStringLiteral("60 Hz ($00) → 50 Hz ($02)");
        f.cycleWindow = QStringLiteral("switch to 60 Hz BEFORE cycle %1 on line %2, back after")
                            .arg(kRemoveTBCycle).arg(kTopTrickLine);
        f.onLines = QStringLiteral("once per frame, on line %1").arg(kTopTrickLine);
        f.consequence = QStringLiteral("+%1 lines at the top (display starts at line %2, not %3)")
                            .arg(kTopExtra).arg(kFirstVisible).arg(kDispFirst);
        f.mechanism = QStringLiteral(
            "The vertical display-start comparison happens near the end of line %1. A "
            "50→60 Hz switch before cycle %2 there moves the first-displayed line up by "
            "%3 lines, so the top border becomes screen content. Switch back to 50 Hz "
            "after cycle %2 to keep the rest of the frame normal (STE: cycle 500).")
            .arg(kTopTrickLine).arg(kRemoveTBCycle).arg(kTopExtra);
        f.axis = Axis::Vertical;
        f.switchCycle = kRemoveTBCycle;
        f.trickFirst = kTopTrickLine;
        f.trickLast = kTopTrickLine;
        f.runnable = false;
        break;
    case Border::Bottom:
        f.reg = QStringLiteral("$ffff820a");
        f.regName = QStringLiteral("Sync mode (50/60 Hz)");
        f.writeSeq = QStringLiteral("60 Hz ($00) → 50 Hz ($02)");
        f.cycleWindow = QStringLiteral("switch to 60 Hz BEFORE cycle %1 on line %2, back after")
                            .arg(kRemoveTBCycle).arg(kBottomTrickLine);
        f.onLines = QStringLiteral("once per frame, on line %1").arg(kBottomTrickLine);
        f.consequence = QStringLiteral("+%1 lines at the bottom (50 Hz; 60 Hz: +26)")
                            .arg(kBottomExtra);
        f.mechanism = QStringLiteral(
            "The display-off comparison for the last line (%1) happens near cycle %2. "
            "Being in 60 Hz there — whose last line is 234 — makes the machine miss the "
            "50 Hz stop, so it keeps drawing %3 more lines into the bottom border. "
            "Switch back to 50 Hz after cycle %2 (STE: 500).")
            .arg(kBottomTrickLine).arg(kRemoveTBCycle).arg(kBottomExtra);
        f.axis = Axis::Vertical;
        f.switchCycle = kRemoveTBCycle;
        f.trickFirst = kBottomTrickLine;
        f.trickLast = kBottomTrickLine;
        f.runnable = false;
        break;
    }
    return f;
}

QString generateLeft(int nLines)
{
    if (nLines < 1)
        nLines = 1;
    // Reproduces the bench-proven tests/effects/lborder.s: VBL-synced, MFP off,
    // a tuned delay to the first display line, then a per-line loop of exactly
    // one scanline (512 cyc) so the hi/lo switch lands at LineCycles <= 4 every
    // line. Runs from an AUTO folder (EmuTOS user mode -> Super(0)).
    return QStringLiteral(R"(; border-left.s — Talos Border walkthrough: LEFT BORDER REMOVAL.
; Cycle-exact $ffff8260 hi/lo switch at the start of a band of scanlines opens
; the left border there (Hatari sets BORDERMASK_LEFT at LineCycles <= 4).

RES     equ $ffff8260          ; Shifter resolution register (byte)
PAL0    equ $ffff8240          ; palette 0 = border / background colour
PAL15   equ $ffff825e          ; palette 15 = screen content colour
IERA    equ $fffffa07          ; MFP interrupt-enable A
IERB    equ $fffffa09          ; MFP interrupt-enable B
NLINES  equ %1                 ; scanlines to open the border on
DELAY_C equ 920                ; dbf count: VBL -> first target line

    text
start:
    clr.l   -(sp)              ; Super(0)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp

    move.b  #0,IERA            ; disable MFP interrupts -> no jitter
    move.b  #0,IERB

    move.w  #$0700,PAL0        ; border = red
    move.w  #$0070,PAL15       ; screen content = green

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

vbl:
    move.w  #$2700,sr          ; mask interrupts: timing below must be exact
    movem.l d0-d2/a0,-(sp)
    movea.l #RES,a0

    move.w  #DELAY_C,d0        ; coarse delay to the first target scanline
.d: dbf     d0,.d

    move.w  #NLINES-1,d2
.line:
    move.b  #2,(a0)            ; hi-res  <- lands at LineCycles <= 4
    move.b  #0,(a0)            ; back to lo-res
    move.w  #36,d1             ; pad each iteration to exactly one scanline
.p: dbf     d1,.p
    nop
    nop
    nop
    nop
    nop
    dbf     d2,.line

    movem.l (sp)+,d0-d2/a0
    rte
)").arg(nLines);
}

}   // namespace BorderCodegen
