#include "Spectrum512.h"

#include <algorithm>
#include <array>
#include <climits>

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

namespace {
// Quantise a channel (0..255) to the nearest ST 3-bit gun level, returning 0..7.
int nearestGun(int v)
{
    int best = 0, bestDist = INT_MAX;
    for (int n = 0; n < 8; ++n) {
        const int d = std::abs(Palette::gun(n) - v);
        if (d < bestDist) {
            bestDist = d;
            best = n;
        }
    }
    return best;
}

quint16 encStColour(int r, int g, int b)
{
    return (nearestGun(r) << 8) | (nearestGun(g) << 4) | nearestGun(b);
}
}   // namespace

Image convertImage(const QImage &src)
{
    Image img;
    img.format = QStringLiteral("converted");
    const QImage s = src.convertToFormat(QImage::Format_RGB32)
                         .scaled(kWidth, kRows, Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
    img.linePalettes.resize(kRows);
    img.rawIndex.resize(kRows);

    for (int row = 0; row < kRows; ++row) {
        const QRgb *tgt = reinterpret_cast<const QRgb *>(s.constScanLine(row));
        auto &pal = img.linePalettes[row];

        // Init each of the 48 slots from the mean target colour over the x-range
        // where that (set, register) is actually shown.
        for (int set = 0; set < 3; ++set) {
            for (int c = 0; c < 16; ++c) {
                const int t = switchColumn(c);
                int x0 = 0, x1 = 0;
                if (set == 0) { x0 = 0; x1 = std::max(1, t); }
                else if (set == 1) { x0 = t; x1 = t + 160; }
                else { x0 = t + 160; x1 = kWidth; }
                x0 = std::clamp(x0, 0, kWidth);
                x1 = std::clamp(x1, 0, kWidth);
                long rr = 0, gg = 0, bb = 0, n = 0;
                for (int x = x0; x < x1; ++x) {
                    rr += qRed(tgt[x]); gg += qGreen(tgt[x]); bb += qBlue(tgt[x]); ++n;
                }
                pal[set * 16 + c] = n ? encStColour(rr / n, gg / n, bb / n) : 0;
            }
        }

        std::array<QRgb, kColoursPerLine> dpal;
        auto refresh = [&] { for (int i = 0; i < kColoursPerLine; ++i) dpal[i] = decodeStColour(pal[i]); };
        refresh();

        // Lloyd's: alternate index assignment and slot-colour update.
        std::array<int, kWidth> idx{};
        for (int it = 0; it < 6; ++it) {
            for (int x = 0; x < kWidth; ++x) {
                const int tr = qRed(tgt[x]), tg = qGreen(tgt[x]), tb = qBlue(tgt[x]);
                int best = 0; long bestD = LONG_MAX;
                for (int c = 0; c < 16; ++c) {
                    const QRgb dp = dpal[findIndex(x, c)];
                    const long d = long(qRed(dp) - tr) * (qRed(dp) - tr)
                                   + long(qGreen(dp) - tg) * (qGreen(dp) - tg)
                                   + long(qBlue(dp) - tb) * (qBlue(dp) - tb);
                    if (d < bestD) { bestD = d; best = c; }
                }
                idx[x] = best;
            }
            std::array<long, kColoursPerLine> ar{}, ag{}, ab{}, an{};
            for (int x = 0; x < kWidth; ++x) {
                const int sl = findIndex(x, idx[x]);
                ar[sl] += qRed(tgt[x]); ag[sl] += qGreen(tgt[x]); ab[sl] += qBlue(tgt[x]); ++an[sl];
            }
            for (int sl = 0; sl < kColoursPerLine; ++sl)
                if (an[sl]) pal[sl] = encStColour(ar[sl] / an[sl], ag[sl] / an[sl], ab[sl] / an[sl]);
            refresh();
        }

        // Final assignment with light error diffusion along the row (smooths gradients).
        QByteArray &ri = img.rawIndex[row];
        ri.resize(kWidth);
        double er = 0, eg = 0, eb = 0;
        for (int x = 0; x < kWidth; ++x) {
            const double wr = qRed(tgt[x]) + er, wg = qGreen(tgt[x]) + eg, wb = qBlue(tgt[x]) + eb;
            int best = 0; double bestD = 1e18;
            for (int c = 0; c < 16; ++c) {
                const QRgb dp = dpal[findIndex(x, c)];
                const double d = (qRed(dp) - wr) * (qRed(dp) - wr)
                                 + (qGreen(dp) - wg) * (qGreen(dp) - wg)
                                 + (qBlue(dp) - wb) * (qBlue(dp) - wb);
                if (d < bestD) { bestD = d; best = c; }
            }
            ri[x] = static_cast<char>(best);
            const QRgb dp = dpal[findIndex(x, best)];
            er = (wr - qRed(dp)) * 0.5;
            eg = (wg - qGreen(dp)) * 0.5;
            eb = (wb - qBlue(dp)) * 0.5;
        }
    }

    // Build the display image from the chosen palettes + indices.
    img.rgb = QImage(kWidth, kRows + 1, QImage::Format_RGB32);
    img.rgb.fill(qRgb(0, 0, 0));
    for (int row = 0; row < kRows; ++row) {
        QRgb *scan = reinterpret_cast<QRgb *>(img.rgb.scanLine(row + 1));
        for (int x = 0; x < kWidth; ++x)
            scan[x] = decodeStColour(
                img.linePalettes[row][findIndex(x, static_cast<quint8>(img.rawIndex[row][x]))]);
    }
    img.valid = true;
    return img;
}

QByteArray encodeSpu(const Image &img)
{
    QByteArray out(kSpuBytes, '\0');
    if (!img.valid || img.rawIndex.size() != kRows || img.linePalettes.size() != kRows)
        return out;
    constexpr int kPad = 160;
    for (int row = 0; row < kRows; ++row) {
        for (int x = 0; x < kWidth; ++x) {
            const int c = static_cast<quint8>(img.rawIndex[row][x]);
            const int byte = x >> 3, bit = 7 - (x & 7);
            for (int p = 0; p < 4; ++p)
                if ((c >> p) & 1) {
                    const int off = kPad + p * kPlaneBytes + row * kPlaneStride + byte;
                    out[off] = out[off] | static_cast<char>(1 << bit);
                }
        }
    }
    const int palBase = kPad + kBitmapBytes;
    for (int row = 0; row < kRows; ++row) {
        for (int sl = 0; sl < kColoursPerLine; ++sl) {
            const quint16 w = img.linePalettes[row][sl];
            const int o = palBase + (row * kColoursPerLine + sl) * 2;
            out[o] = static_cast<char>(w >> 8);
            out[o + 1] = static_cast<char>(w & 0xff);
        }
    }
    return out;
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
