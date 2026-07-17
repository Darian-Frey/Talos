#include "StPicture.h"

#include "model/Palette.h"

namespace StPicture {

namespace {

quint16 be16(const QByteArray &b, int off)
{
    return (static_cast<quint8>(b[off]) << 8) | static_cast<quint8>(b[off + 1]);
}

struct ResInfo { int width, height, planes, colours; };

ResInfo resInfo(int res)
{
    switch (res) {
    case 1: return {640, 200, 2, 4};
    case 2: return {640, 400, 1, 2};
    default: return {320, 200, 4, 16};
    }
}

QRgb stColour(quint16 w) { return Palette::decode(w).rgb(); }

// Standard Mac/TIFF PackBits: 0..127 -> copy (n+1) literals; 128 -> nop;
// 129..255 -> repeat the next byte (257 - n) times.
QByteArray unpackBits(const QByteArray &src, int want)
{
    QByteArray out;
    out.reserve(want);
    int i = 0;
    while (i < src.size() && out.size() < want) {
        const int n = static_cast<quint8>(src[i++]);
        if (n < 128) {
            const int count = n + 1;
            out.append(src.mid(i, count));
            i += count;
        } else if (n > 128 && i < src.size()) {
            out.append(QByteArray(257 - n, src[i]));
            ++i;
        }
    }
    return out;
}

// Build the image from a STANDARD INTERLEAVED ST screen (DEGAS .PIx / NEO):
// per 16px word-group, the planes are stored word-interleaved.
QImage decodeInterleaved(const QByteArray &screen, int screenOff, const ResInfo &r,
                         const QVector<quint16> &pal)
{
    const int bpl = (r.width / 16) * r.planes * 2;   // bytes per scanline
    QImage img(r.width, r.height, QImage::Format_RGB32);
    for (int y = 0; y < r.height; ++y) {
        QRgb *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < r.width; ++x) {
            const int wx = x >> 4, bit = 15 - (x & 15);
            int c = 0;
            for (int p = 0; p < r.planes; ++p) {
                const int off = screenOff + y * bpl + wx * (2 * r.planes) + p * 2;
                c |= ((be16(screen, off) >> bit) & 1) << p;
            }
            scan[x] = stColour(pal[c]);
        }
    }
    return img;
}

// Build the image from a PLANE-CONSECUTIVE-per-line screen (DEGAS .PCx after
// decompression): each scanline holds its planes back to back, `width/8` each.
QImage decodePlanar(const QByteArray &screen, const ResInfo &r, const QVector<quint16> &pal)
{
    const int planeBytes = r.width / 8;              // bytes per plane per line
    const int bpl = planeBytes * r.planes;
    QImage img(r.width, r.height, QImage::Format_RGB32);
    for (int y = 0; y < r.height; ++y) {
        QRgb *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < r.width; ++x) {
            const int byte = x >> 3, bit = 7 - (x & 7);
            int c = 0;
            for (int p = 0; p < r.planes; ++p) {
                const int off = y * bpl + p * planeBytes + byte;
                if (off < screen.size())
                    c |= ((static_cast<quint8>(screen[off]) >> bit) & 1) << p;
            }
            scan[x] = stColour(pal[c]);
        }
    }
    return img;
}

// Med-res ST pixels are 2:1 (640x200 on a 4:3 display), so double the rows for a
// correct-aspect image, matching how ST viewers (and RECOIL) present them.
QImage aspectCorrect(QImage img, int res)
{
    return res == 1
               ? img.scaled(img.width(), img.height() * 2, Qt::IgnoreAspectRatio,
                            Qt::FastTransformation)
               : img;
}

QVector<quint16> readPalette(const QByteArray &b, int off)
{
    QVector<quint16> pal(16);
    for (int i = 0; i < 16; ++i)
        pal[i] = be16(b, off + i * 2);
    return pal;
}

Image parseDegasUncompressed(const QByteArray &b)
{
    Image img;
    const int res = be16(b, 0) & 0x3;
    const ResInfo r = resInfo(res);
    img.format = QStringLiteral("DEGAS .PI%1").arg(res + 1);
    img.resolution = res;
    img.colours = r.colours;
    img.palette = readPalette(b, 2);
    img.rgb = aspectCorrect(decodeInterleaved(b, 34, r, img.palette), res);
    img.valid = true;
    return img;
}

Image parseDegasCompressed(const QByteArray &b)
{
    Image img;
    const int res = be16(b, 0) & 0x3;
    const ResInfo r = resInfo(res);
    img.format = QStringLiteral("DEGAS .PC%1").arg(res + 1);
    img.resolution = res;
    img.colours = r.colours;
    img.palette = readPalette(b, 2);
    const QByteArray screen = unpackBits(b.mid(34), 32000);
    if (screen.size() < (r.width / 8) * r.planes * r.height) {
        img.error = QStringLiteral(".PC%1 decompressed short (%2 bytes)")
                        .arg(res + 1).arg(screen.size());
        return img;
    }
    img.rgb = aspectCorrect(decodePlanar(screen, r, img.palette), res);
    img.valid = true;
    return img;
}

Image parseNeo(const QByteArray &b)
{
    Image img;
    const int res = be16(b, 2) & 0x3;
    const ResInfo r = resInfo(res);
    img.format = QStringLiteral("NEOchrome");
    img.resolution = res;
    img.colours = r.colours;
    img.palette = readPalette(b, 4);
    img.rgb = aspectCorrect(decodeInterleaved(b, 128, r, img.palette), res);
    img.valid = true;
    return img;
}

// Tiny .TNY/.TN1-3: header + word-level RLE + a 4-set vertical-column layout.
Image parseTiny(const QByteArray &b)
{
    Image img;
    img.format = QStringLiteral("Tiny");
    int pos = 0;
    int res = static_cast<quint8>(b[pos++]);
    if (res >= 3) {            // 3..5: colour-animation info follows (skip it)
        res -= 3;
        pos += 4;              // 1 byte limits + 1 byte dir/speed + 1 word duration
    }
    const ResInfo r = resInfo(res);
    img.format = QStringLiteral("Tiny (.TN%1)").arg(res + 1);
    img.resolution = res;
    img.colours = r.colours;
    img.palette = readPalette(b, pos);
    pos += 32;
    const int nControl = be16(b, pos); pos += 2;
    const int nData = be16(b, pos); pos += 2;
    int cp = pos;                 // control bytes
    int dp = pos + nControl;      // data words
    const int dataEnd = dp + nData * 2;

    // Decompress to 16000 words. Control byte x (signed): x<0 copy -x literals;
    // x==0 word-count repeat of next data word; x==1 word-count literal run;
    // x>1 repeat next data word x times.
    QVector<quint16> words;
    words.reserve(16000);
    auto dataWord = [&] { const quint16 w = be16(b, dp); dp += 2; return w; };
    while (words.size() < 16000 && cp < pos + nControl && dp + 1 < dataEnd + 2) {
        const int x = static_cast<signed char>(b[cp++]);
        if (x < 0) {
            for (int i = 0; i < -x; ++i) words.append(dataWord());
        } else if (x == 0) {
            const int n = be16(b, cp); cp += 2;
            const quint16 w = dataWord();
            for (int i = 0; i < n; ++i) words.append(w);
        } else if (x == 1) {
            const int n = be16(b, cp); cp += 2;
            for (int i = 0; i < n; ++i) words.append(dataWord());
        } else {
            const quint16 w = dataWord();
            for (int i = 0; i < x; ++i) words.append(w);
        }
    }

    // Un-shuffle: columns ordered in 4 sets (col % 4), each column = one word per
    // scanline top-to-bottom, back into the standard interleaved screen.
    const int nCols = (r.width / 16) * r.planes;
    const int nLines = r.height;
    const int bpl = nCols * 2;
    QByteArray screen(bpl * nLines, '\0');
    int idx = 0;
    for (int setbase = 0; setbase < 4; ++setbase)
        for (int col = setbase; col < nCols; col += 4)
            for (int line = 0; line < nLines; ++line) {
                if (idx >= words.size())
                    break;
                const quint16 w = words[idx++];
                const int off = line * bpl + col * 2;
                screen[off] = static_cast<char>(w >> 8);
                screen[off + 1] = static_cast<char>(w & 0xff);
            }

    img.rgb = aspectCorrect(decodeInterleaved(screen, 0, r, img.palette), res);
    img.valid = true;
    return img;
}

}   // namespace

Image parse(const QByteArray &b, const QString &ext)
{
    const QString e = ext.toLower();
    // DEGAS compressed: resolution word has bit 15 set.
    if (b.size() >= 34 && (be16(b, 0) & 0x8000))
        return parseDegasCompressed(b);
    // DEGAS uncompressed: exactly 32034 bytes, resolution 0..2.
    if (b.size() == 32034 && (be16(b, 0) & ~0x3) == 0)
        return parseDegasUncompressed(b);
    // NEOchrome: 32128 bytes, flag word 0.
    if (b.size() == 32128 && be16(b, 0) == 0)
        return parseNeo(b);
    // Fall back on the extension for the compressed/edge cases.
    if (e.startsWith(QStringLiteral("pc")) && b.size() >= 34)
        return parseDegasCompressed(b);
    if (e.startsWith(QStringLiteral("pi")) && b.size() >= 34)
        return parseDegasUncompressed(b);
    if (e == QStringLiteral("neo") && b.size() >= 128)
        return parseNeo(b);
    if (e.startsWith(QStringLiteral("tn")) && b.size() >= 38)
        return parseTiny(b);

    Image img;
    img.error = QStringLiteral("unrecognised ST picture (%1 bytes)").arg(b.size());
    return img;
}

}   // namespace StPicture
