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

#include <QChar>
#include <QHash>
#include <QString>
#include <QVector>

class QImage;

namespace ScrollerCodegen {

constexpr int kFontW = 8;         // built-in glyph width in pixels
constexpr int kFontH = 8;         // built-in glyph height in pixels
constexpr int kVisibleCols = 20;  // low-res 16 px columns on screen (320 px)
constexpr int kCycPerLine = 512;  // ST/STE PAL low-res (C-007)

// Vertical scale of the font (1 => font-height band, 2 => double, ...).
constexpr int kDefaultVScale = 2;
// Scroll speed in pixels per frame (STE fine scroll advances this much / VBL).
constexpr int kDefaultSpeed = 1;

// A bitmap font: glyph size + a 1-bit-per-pixel glyph per character. The built-in
// is uppercase-only 8x8; an imported one can be any cell size and character range.
struct Font
{
    int w = kFontW;
    int h = kFontH;
    bool caseFold = false;                 // fold to upper-case on lookup (built-in)
    QString label = QStringLiteral("built-in 8×8");
    QHash<QChar, QVector<bool>> glyphs;    // per char: h rows x w cols, row-major

    bool renderable(QChar c) const;        // has a glyph (space renders blank)
    bool pixel(QChar c, int gx, int gy) const;   // foreground at (gx,gy)?
};

// The built-in uppercase 8x8 font (A-Z 0-9 and common punctuation).
const Font &builtinFont();

// Build a font from a font-sheet image: a grid of cellW x cellH glyphs, row-major,
// assigned consecutive characters from `firstChar`. Foreground pixels are those
// that differ from the top-left (background) pixel. Any cell size / char range.
Font fontFromImage(const QImage &img, int cellW, int cellH, QChar firstChar);

// True if the character renders (space or a font glyph).
bool isRenderable(QChar c, const Font &font);

// The rendered strip width, in 16 px columns, for a message (incl. the
// lead-in/lead-out blank screens). Useful for the UI to show/validate length.
int stripColumns(const QString &message, const Font &font);

// Emit the 68k asm stub: a pre-rendered plane0 strip (dc.w) + the STE scroll
// engine. `speed` is px/frame; `vscale` scales the font height.
QString generate(const QString &message, int speed, int vscale, const Font &font);

}   // namespace ScrollerCodegen
