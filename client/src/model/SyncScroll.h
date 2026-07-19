// SyncScroll — Phase 6 (Sync-scroll walkthrough): the sourced facts of the STF
// "sync scroll" trick, the plain ST's answer to the STE hardware-scroll register.
//
// A plain STF has no fine-scroll register. ST Connexion's trick ("Let's Do The
// Twist", Punish Your Machine) is three resolution ($ffff8260) switches at the
// start of a line — hi → med → lo — where the *exact* cycle of the final low-res
// switch shifts that scanline right by a set number of pixels. Talos does not
// emulate this (D-002); it shows the switch sequence and the cycle→pixel table.
// Every figure is sourced from Hatari video.h / video.c (C-007), cited in the .cpp.

#pragma once

#include <QString>
#include <QVector>

namespace SyncScroll {

// $ffff8260 resolution values used by the trick.
constexpr int kResHi  = 0x02;   // hi-res  — opens the left border (BORDERMASK_LEFT_OFF)
constexpr int kResMed = 0x01;   // med-res — BORDERMASK_LEFT_OFF_MED (must be at LineCycles ≤ 20)
constexpr int kResLo  = 0x00;   // lo-res  — the switch whose exact cycle sets the shift

constexpr int kHiMaxCycle  = 4;    // hi-res must land at LineCycles ≤ 4  (left border off)
constexpr int kMedMaxCycle = 20;   // med-res must land at LineCycles ≤ 20

// The low-res switch cycle → right-shift, from video.h (LINE_LEFT_STAB_LOW 16,
// LINE_SCROLL_13/9/5/1_CYCLE_50 = 20/24/28/32). Matched on an *exact* LineCycles.
struct Step
{
    int loCycle;    // the exact cycle the low-res write must land on
    int shiftPx;    // resulting right shift, in low-res pixels
    QString note;
};

QVector<Step> steps();

}   // namespace SyncScroll
