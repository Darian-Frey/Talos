// BorderCodegen — Phase 6 (Border-removal walkthrough): the sourced facts for
// each of the four ST screen borders, plus the proven runnable left-border stub.
//
// This is a *teaching* feature: Talos does not emulate the border logic (D-002),
// it shows where the sync/resolution write must land and why the border opens.
// Every cycle/line figure here is sourced from Hatari's own video.h / video.c
// (C-007 — never a number from memory), cited in the .cpp. The one runnable
// example is the left border, whose stub reproduces lborder.s — already
// bench-validated by harness/diff_harness.py --border-check.

#pragma once

#include <QString>

namespace BorderCodegen {

enum class Border { Left, Right, Top, Bottom };

// ST 50 Hz geometry, all sourced from Hatari video.h (see BorderCodegen.cpp).
constexpr int kCyclesPerLine  = 512;   // CYCLES_PER_LINE_50HZ
constexpr int kLineStart      = 56;    // LINE_START_CYCLE_50  (display on)
constexpr int kLineEnd        = 376;   // LINE_END_CYCLE_50    (display off / right border)
constexpr int kLineEndNoRight = 460;   // LINE_END_CYCLE_NO_RIGHT (372 + 44*2)
constexpr int kLinesPerFrame  = 313;   // SCANLINES_PER_FRAME_50HZ
constexpr int kFirstVisible   = 34;    // FIRST_VISIBLE_HBL_50HZ (top of the captured frame)
constexpr int kDispFirst      = 63;    // VIDEO_START_HBL_50HZ  (first displayed line)
constexpr int kDispLast       = 263;   // VIDEO_END_HBL_50HZ    (line the display stops at)
constexpr int kTopExtra       = 29;    // kDispFirst - kFirstVisible (top-border lines)
constexpr int kBottomExtra    = 47;    // VIDEO_HEIGHT_BOTTOM_50HZ
constexpr int kRightExtra     = 44;    // (LINE_END_CYCLE_NO_RIGHT - LINE_END_CYCLE_60)/2
constexpr int kRemoveTBCycle  = 504;   // LINE_REMOVE_TOP/BOTTOM_CYCLE_STF (STE: 500)
constexpr int kTopTrickLine   = 33;    // switch to 60 Hz on line 33 removes the top border
constexpr int kBottomTrickLine = 263;  // switch on line 263 removes the bottom border

// Which axis the border extends — a left/right border widens the *scanline*
// (horizontal); a top/bottom border adds *lines* to the frame (vertical).
enum class Axis { Horizontal, Vertical };

struct Facts
{
    Border border;
    QString name;         // "Left border"
    QString reg;          // "$ffff8260"
    QString regName;      // "Shifter resolution"
    QString writeSeq;     // "hi-res ($02) → lo-res ($00)"
    QString cycleWindow;  // where on the line the write must land (sourced)
    QString onLines;      // which scanline(s) the trick runs on
    QString consequence;  // "opens ~52 px of left overscan"
    QString mechanism;    // one-paragraph why-it-works
    Axis axis;            // horizontal (L/R) or vertical (T/B)
    int switchCycle;      // representative cycle on the line for the diagram marker
    int trickFirst;       // first trick scanline (frame diagram)
    int trickLast;        // last trick scanline
    bool runnable;        // true only for Left (the bench-proven stub)
};

Facts facts(Border b);
QString borderName(Border b);

// The runnable left-border stub — a cycle-exact $ffff8260 hi/lo switch at the
// start of a band of scanlines (reproduces the proven tests/effects/lborder.s).
// nLines = how many scanlines to open the border on.
QString generateLeft(int nLines = 120);

}   // namespace BorderCodegen
