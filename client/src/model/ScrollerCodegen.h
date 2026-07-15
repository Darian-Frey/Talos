// ScrollerCodegen — Phase 4 (F-212): author an STE hardware fine-scroll message
// scroller, then build/verify/export like the raster workspace.
//
// Talos rasterises the message into a plane0 bitmap strip in C++ (glyph work
// happens here, not in 68k), then emits a stub that scrolls that strip with the
// STE shifter. The scroll recipe was bench-validated against the Hatari fork
// (see the header comment in ScrollerCodegen.cpp for the exact register dance):
//   - $ff8265 (PREFETCH horizontal scroll) advances the fine offset 0..15 so the
//     message flows smoothly through a fixed window (1 px/frame at speed 1);
//   - a fixed screen base + a fast software column-shift of just the text band
//     (not the whole screen) hands off at each 16 px wrap, so there is no seam;
//   - $ff820f (LineWidth) widens the scanline by one 16 px column for the
//     incoming glyph data the prefetch pulls in.
// Running the emitted stub in stock Hatari reproduces the effect by construction.

#pragma once

#include <QString>

namespace ScrollerCodegen {

constexpr int kFontW = 8;         // glyph width in pixels
constexpr int kFontH = 8;         // glyph height in pixels
constexpr int kVisibleCols = 20;  // low-res 16 px columns on screen (320 px)
constexpr int kCycPerLine = 512;  // ST/STE PAL low-res (C-007)

// Vertical scale of the font (1 => 8 px band, 2 => 16 px band, ...).
constexpr int kDefaultVScale = 2;
// Scroll speed in pixels per frame (STE fine scroll advances this much / VBL).
constexpr int kDefaultSpeed = 1;

// True if the character has a glyph (else it renders as a space). Uppercased.
bool isRenderable(QChar c);

// The rendered strip width, in 16 px columns, for a given message (incl. the
// lead-in/lead-out blank screens). Useful for the UI to show/validate length.
int stripColumns(const QString &message);

// Emit the 68k asm stub: a pre-rendered plane0 strip (dc.w) + the STE scroll
// engine. `speed` is px/frame; `vscale` scales the font height.
QString generate(const QString &message, int speed = kDefaultSpeed,
                 int vscale = kDefaultVScale);

}   // namespace ScrollerCodegen
