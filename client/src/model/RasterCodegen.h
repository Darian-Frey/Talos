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

// Animated "copper bars": the same per-line colour table as generate(), but the
// VBL advances a wrapping offset each frame so the bars scroll down `speed` px per
// frame. Reuses generate()'s proven per-line timing (a doubled colour table means
// no per-line wrap); only the a1 base is animated. speed 1 = smoothest.
constexpr int kDefaultCopperSpeed = 1;
QString generateCopper(QVector<Bar> bars, int speed = kDefaultCopperSpeed);

// Palette colour-cycling: fill the screen with a 16-index horizontal stripe ramp,
// set the 16 palette registers to `colours`, and rotate them every VBL so the
// colours flow across the stripes. No cycle-exact timing (palette writes happen
// in the VBL), so it is timing-forgiving. Up to 16 colours (padded/wrapped).
QString generateColourCycle(const QVector<quint16> &colours);

// Intra-line "vertical bands" (Spectrum-512-lite): an HBL-synced handler that
// packs one background-colour write per band, back to back, so each lands ~one
// instruction apart down the scanline -> equal-width vertical bands, steady on
// every line. Bounded by how many writes fit the visible line (~kMaxBands).
constexpr int kMaxBands = 24;
QString generateSplit(const QVector<quint16> &colours);

// Arbitrary-column intra-line split: each Bar's `line` is a target framebuffer
// column where its colour begins. HBL-synced, with a per-gap delay calibrated so
// each write lands at its column (bench-validated, see harness/intraline_split.py).
constexpr int kColBase = 78;      // first colour's start column (write at HBL)
constexpr int kGapBase = 76;      // column advance per gap at zero delay
constexpr int kPxPerDbf = 24;     // column advance per dbf iteration
QString generateColumns(QVector<Bar> bars);

}   // namespace RasterCodegen
