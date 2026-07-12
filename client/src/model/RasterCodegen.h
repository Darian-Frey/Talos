// RasterCodegen — Phase 4 (F-212): a raster-bar prototype -> cycle-exact 68k asm.
//
// Turns a list of (scanline, colour) bars into a VBL-synced, cycle-counted
// raster-bar stub (ST PAL low-res): MFP off to kill jitter, one colour write per
// scanline padded to exactly CYC_PER_LINE. The emitted stub is the export
// artefact — run it in stock Hatari and it reproduces the effect by construction
// (verified by harness/raster_roundtrip.py). Timing constants match that tool.

#pragma once

#include <QPair>
#include <QString>
#include <QVector>

namespace RasterCodegen {

struct Bar {
    int line;         // scanline the colour takes effect from
    quint16 colour;   // ST $0rgb (each gun 0-7)
};

constexpr int kVisibleLines = 200;   // low-res scanlines covered
constexpr int kCycPerLine = 512;     // ST PAL low-res (C-007)
constexpr int kDefaultPad = 36;      // per-line dbf pad (proven in raster_roundtrip.py)
constexpr int kDefaultDelay = 900;   // VBL -> first visible line

// bars need not be sorted; empty lines default to $000. Returns 68k asm text.
QString generate(QVector<Bar> bars, int pad = kDefaultPad, int delay = kDefaultDelay,
                 int total = kVisibleLines);

}   // namespace RasterCodegen
