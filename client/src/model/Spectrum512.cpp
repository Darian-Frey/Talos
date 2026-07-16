#include "Spectrum512.h"

#include "model/Palette.h"

namespace Spectrum512 {

namespace {

quint16 be16(const QByteArray &b, int off)
{
    return (static_cast<quint8>(b[off]) << 8) | static_cast<quint8>(b[off + 1]);
}

quint32 be32(const QByteArray &b, int off)
{
    return (static_cast<quint32>(static_cast<quint8>(b[off])) << 24)
           | (static_cast<quint32>(static_cast<quint8>(b[off + 1])) << 16)
           | (static_cast<quint32>(static_cast<quint8>(b[off + 2])) << 8)
           | static_cast<quint32>(static_cast<quint8>(b[off + 3]));
}

// PackBits-style RLE used by .SPC's bitmap: control byte n (signed) — 0..127 copy
// (n+1) literal bytes; negative repeats the next byte (2 - n) times.
QByteArray unpackBits(const QByteArray &src)
{
    QByteArray out;
    int i = 0;
    while (i < src.size()) {
        const int n = static_cast<signed char>(src[i++]);
        if (n >= 0) {
            const int count = n + 1;
            out.append(src.mid(i, count));
            i += count;
        } else if (i < src.size()) {
            out.append(QByteArray(2 - n, src[i]));
            ++i;
        }
    }
    return out;
}

// 4-bitplane colour index (0..15) for pixel (row, x) in PLANE-SEPARATED data:
// plane p's line `row` starts at p*kPlaneBytes + row*kPlaneStride.
int pixelIndex(const QByteArray &planes, int row, int x)
{
    const int byte = x >> 3;
    const int bit = 7 - (x & 7);
    int c = 0;
    for (int p = 0; p < 4; ++p) {
        const int off = p * kPlaneBytes + row * kPlaneStride + byte;
        c |= ((static_cast<quint8>(planes[off]) >> bit) & 1) << p;
    }
    return c;
}

using LinePalettes = QVector<std::array<quint16, kColoursPerLine>>;

// Fill the decoded image + raw index from a plane-separated bitmap and the
// per-line palettes. Screen line 0 is blank; file rows 0..198 -> lines 1..199.
void decode(Image &img, const QByteArray &planes)
{
    img.rawIndex.resize(kRows);
    img.rgb = QImage(kWidth, kRows + 1, QImage::Format_RGB32);
    img.rgb.fill(qRgb(0, 0, 0));
    for (int row = 0; row < kRows; ++row) {
        QRgb *scan = reinterpret_cast<QRgb *>(img.rgb.scanLine(row + 1));
        const auto &pal = img.linePalettes[row];
        QByteArray &idx = img.rawIndex[row];
        idx.resize(kWidth);
        for (int x = 0; x < kWidth; ++x) {
            const int c = pixelIndex(planes, row, x);
            idx[x] = static_cast<char>(c);
            scan[x] = decodeStColour(pal[findIndex(x, c)]);
        }
    }
    img.valid = true;
}

Image parseSpu(const QByteArray &bytes)
{
    Image img;
    img.format = QStringLiteral("SPU");
    constexpr int kPad = 160;
    constexpr int kPalBase = kPad + kBitmapBytes;   // 31840 + 160
    constexpr int kPalLineBytes = kColoursPerLine * 2;

    const QByteArray planes = bytes.mid(kPad, kBitmapBytes);
    img.linePalettes.resize(kRows);
    for (int row = 0; row < kRows; ++row) {
        const int base = kPalBase + row * kPalLineBytes;
        for (int s = 0; s < kColoursPerLine; ++s)
            img.linePalettes[row][s] = be16(bytes, base + s * 2);
    }
    decode(img, planes);
    return img;
}

Image parseSpc(const QByteArray &bytes)
{
    Image img;
    img.format = QStringLiteral("SPC");
    if (bytes.size() < 12) {
        img.error = QStringLiteral("truncated .SPC header");
        return img;
    }
    const quint32 bmpLen = be32(bytes, 4);
    const quint32 palLen = be32(bytes, 8);
    if (12 + bmpLen + palLen != quint32(bytes.size())) {
        img.error = QStringLiteral(".SPC length mismatch (header says %1, file is %2)")
                        .arg(12 + bmpLen + palLen)
                        .arg(bytes.size());
        return img;
    }

    const QByteArray planes = unpackBits(bytes.mid(12, bmpLen));
    if (planes.size() != kBitmapBytes) {
        img.error = QStringLiteral(".SPC bitmap decompressed to %1 bytes (expected %2)")
                        .arg(planes.size())
                        .arg(kBitmapBytes);
        return img;
    }

    // Palette: raw (not RLE'd). Per line, 3 palettes; each is a u16 map (bit i set
    // => entry i present, read the next colour; else black) + the present colours.
    const QByteArray pal = bytes.mid(12 + bmpLen, palLen);
    img.linePalettes.resize(kRows);
    int pos = 0;
    for (int row = 0; row < kRows; ++row) {
        auto &line = img.linePalettes[row];
        line.fill(0);
        for (int set = 0; set < 3; ++set) {
            if (pos + 2 > pal.size()) {
                img.error = QStringLiteral(".SPC palette ran short at line %1").arg(row);
                return img;
            }
            const quint16 map = be16(pal, pos);
            pos += 2;
            for (int i = 0; i < 16; ++i) {
                if (map & (1 << i)) {
                    if (pos + 2 > pal.size()) {
                        img.error = QStringLiteral(".SPC palette ran short");
                        return img;
                    }
                    line[set * 16 + i] = be16(pal, pos);
                    pos += 2;
                }
            }
        }
    }
    decode(img, planes);
    return img;
}

}   // namespace

QRgb decodeStColour(quint16 w)
{
    return Palette::decode(w).rgb();
}

Image parse(const QByteArray &bytes)
{
    if (bytes.size() >= 2 && bytes[0] == 'S' && bytes[1] == 'P')
        return parseSpc(bytes);
    if (bytes.size() == kSpuBytes)
        return parseSpu(bytes);

    Image img;
    img.error = QStringLiteral("not a Spectrum 512 picture (.SPC starts \"SP\"; "
                               ".SPU is %1 bytes, got %2)")
                    .arg(kSpuBytes)
                    .arg(bytes.size());
    return img;
}

std::array<quint16, kColoursPerLine> Image::palette(int screenLine) const
{
    const int row = screenLine - 1;
    if (row < 0 || row >= linePalettes.size())
        return {};
    return linePalettes[row];
}

QVector<int> Image::rowIndices(int screenLine) const
{
    QVector<int> out;
    const int row = screenLine - 1;
    if (!valid || row < 0 || row >= rawIndex.size())
        return out;
    out.reserve(kWidth);
    for (int x = 0; x < kWidth; ++x)
        out.append(static_cast<quint8>(rawIndex[row][x]));
    return out;
}

QVector<int> Image::rowSlots(int screenLine) const
{
    QVector<int> out;
    const int row = screenLine - 1;
    if (!valid || row < 0 || row >= rawIndex.size())
        return out;
    out.reserve(kWidth);
    for (int x = 0; x < kWidth; ++x)
        out.append(findIndex(x, static_cast<quint8>(rawIndex[row][x])));
    return out;
}

}   // namespace Spectrum512
