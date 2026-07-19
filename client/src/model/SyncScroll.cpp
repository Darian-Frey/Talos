#include "SyncScroll.h"

// Sourced from Hatari src/includes/video.h and the hi/med/lo detection in
// src/video.c (verified 2026-07-19), never from memory (C-007):
//
//   LINE_LEFT_STAB_LOW      16   -> DisplayPixelShift 0  (remove-left + med stabiliser)
//   LINE_SCROLL_13_CYCLE_50 20   -> DisplayPixelShift 13
//   LINE_SCROLL_9_CYCLE_50  24   -> DisplayPixelShift 9
//   LINE_SCROLL_5_CYCLE_50  28   -> DisplayPixelShift 5
//   LINE_SCROLL_1_CYCLE_50  32   -> DisplayPixelShift 1
//
// video.c requires, in order on the same line: a hi-res write with the left
// border already off, then a hi→med switch at LineCycles ≤ 20 (sets
// BORDERMASK_LEFT_OFF_MED), then a med→lo switch at LineCycles ≤ 32 whose *exact*
// cycle selects the shift (Video_Res_WriteByte). Later cycle = smaller shift.

namespace SyncScroll {

QVector<Step> steps()
{
    return {
        {16, 0,  QStringLiteral("remove-left + med stabiliser (no shift)")},
        {20, 13, QStringLiteral("13 px right")},
        {24, 9,  QStringLiteral("9 px right")},
        {28, 5,  QStringLiteral("5 px right")},
        {32, 1,  QStringLiteral("1 px right")},
    };
}

}   // namespace SyncScroll
