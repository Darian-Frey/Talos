// Spectrum512 — Phase 4 (F-211): import and decode a Spectrum 512 picture.
//
// Spectrum 512 shows up to 512 colours on an ST by rewriting the 16-register
// palette ~48 times per scanline at cycle-exact positions. Talos does NOT
// reproduce the effect on hardware (that is the hardest classic ST timing trick,
// out of scope here) — it *decodes* the file to an exact image and makes the
// per-scanline palette storm visible, which is Talos's mission.
//
// Both variants are supported (decode reverse-engineered and validated against a
// real reference image, 2026-07-16):
//   * .SPC (compressed): "SP" + u16 pad + u32 bitmapLen + u32 paletteLen, then a
//     PackBits-RLE bitmap and a raw map+colours palette (see the .cpp).
//   * .SPU (uncompressed, 51104 bytes): 160-byte top-line pad + 31840-byte
//     bitmap + 19104-byte palette (199 lines x 48 plain ST colour words).
// In both, the bitmap is PLANE-SEPARATED (199 lines of plane 0, then plane 1, …),
// NOT ST-interleaved, and each line has 3 palettes of 16 entries. Which of the 48
// a pixel uses is given by findIndex() below.

#pragma once

#include <array>

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

namespace Spectrum512 {

constexpr int kWidth = 320;
constexpr int kRows = 199;          // displayable rows (screen lines 1..199)
constexpr int kColoursPerLine = 48; // 3 palettes x 16
constexpr int kPlaneStride = 40;    // bytes per scanline in one plane (320/8)
constexpr int kPlaneBytes = kRows * kPlaneStride;   // 7960
constexpr int kBitmapBytes = 4 * kPlaneBytes;       // 31840
constexpr int kSpuBytes = 51104;

// The canonical Spectrum 512 pixel→palette-slot map (0..47) for pixel column x
// (0..319) and 4-bitplane colour index c (0..15). The three 16-colour sets switch
// in at x = 10c±adj and x = 10c±adj + 160 — a staggered per-register storm.
inline int findIndex(int x, int c)
{
    int t = 10 * c;
    if (c & 1)
        t -= 5;
    else
        t += 1;
    if (x < t)
        return c;         // set 1: entries 0..15
    if (x >= t + 160)
        return c + 32;    // set 3: entries 32..47
    return c + 16;        // set 2: entries 16..31
}

// The x column at which register c switches set1→set2 (its set2→set3 is +160).
inline int switchColumn(int c) { return 10 * c + ((c & 1) ? -5 : 1); }

struct Image
{
    bool valid = false;
    QString error;
    QString format;                                   // "SPU" or "SPC"
    QImage rgb;                                       // 320x200, row 0 blank
    QVector<std::array<quint16, kColoursPerLine>> linePalettes;   // per row (0..198)
    QVector<QByteArray> rawIndex;                     // per row: 320 bytes, 0..15

    // For the palette-storm view of one screen line (1..199): the raw 4-bitplane
    // index (0..15) per pixel, and the resolved 48-entry slot (0..47) per pixel.
    QVector<int> rowIndices(int screenLine) const;
    QVector<int> rowSlots(int screenLine) const;
    std::array<quint16, kColoursPerLine> palette(int screenLine) const;
};

// Parse a Spectrum 512 picture, auto-detecting .SPC (compressed, "SP" magic) vs
// .SPU (uncompressed, 51104 bytes). On failure returns {valid=false, error=…}.
Image parse(const QByteArray &bytes);

// Convert an arbitrary image (any format Qt can load) into a Spectrum 512 picture
// by quantising it to the S512 constraint (48 position-dependent colours/line via
// a per-scanline Lloyd's pass + light dithering). Detailed/colourful images come
// out well; smooth gradients band — that is S512's inherent limit, not a bug.
Image convertImage(const QImage &src);

// Encode a decoded/converted Image back to .SPU bytes (51104) — a "Talos-usable"
// file that parse() re-reads. Used by the viewer's Export .SPU.
QByteArray encodeSpu(const Image &img);

// Decode one ST $0rgb palette word to an RGB colour (3 bits/gun), matching the
// client's Palette decode.
QRgb decodeStColour(quint16 word);

}   // namespace Spectrum512
