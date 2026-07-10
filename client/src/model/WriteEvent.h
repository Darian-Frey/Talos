// WriteEvent — one captured hardware-register write with its beam position.
//
// The core datum of the register-write-to-cycle mapping (F-203/F-215): where on
// the beam a write to a watched register landed, and what value it wrote.

#pragma once

#include <QtGlobal>

struct WriteEvent
{
    quint32 frameCycle = 0;   // FrameCycles at the hit (cycles since VBL)
    int scanline = 0;         // HBL (absolute scanline from top of frame)
    int cycleInLine = 0;      // LineCycles (cycle within the line)
    quint32 address = 0;      // register address written
    quint32 value = 0;        // value read back after the write
    quint32 pc = 0;           // PC at the stop (just after the writing instruction)
};
