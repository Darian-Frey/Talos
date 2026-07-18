#include "GifWriter.h"

#include <QFile>
#include <QHash>

void GifWriter::addFrame(const QImage &frame, int delayCs)
{
    m_frames.append({frame.convertToFormat(QImage::Format_RGB32), qMax(2, delayCs)});
}

namespace {

// GIF variable-width LZW (normal, not early-change). Returns the compressed image
// data already split into <=255-byte sub-blocks and terminated.
QByteArray gifLzw(const QVector<uchar> &idx, int minCodeSize)
{
    const int clearCode = 1 << minCodeSize;
    const int eoiCode = clearCode + 1;
    int codeSize = minCodeSize + 1;
    int nextCode = eoiCode + 1;
    QHash<quint32, int> dict;

    QByteArray bits;
    quint32 buf = 0;
    int nbits = 0;
    auto put = [&](int code) {
        buf |= (quint32(code) << nbits);
        nbits += codeSize;
        while (nbits >= 8) {
            bits.append(char(buf & 0xff));
            buf >>= 8;
            nbits -= 8;
        }
    };

    put(clearCode);
    if (!idx.isEmpty()) {
        int prefix = idx[0];
        for (int i = 1; i < idx.size(); ++i) {
            const int px = idx[i];
            const quint32 key = (quint32(prefix) << 8) | px;
            const auto it = dict.constFind(key);
            if (it != dict.constEnd()) {
                prefix = it.value();
            } else {
                put(prefix);
                dict.insert(key, nextCode++);
                // The decoder's table lags this one by a code, so it widens a
                // code later — match it (bump at 2^codeSize + 1, not 2^codeSize)
                // or the widths desync mid-stream.
                if (codeSize < 12) {
                    if (nextCode == (1 << codeSize) + 1)
                        ++codeSize;
                } else if (nextCode == 4096) {   // 12-bit table full → reset
                    put(clearCode);
                    dict.clear();
                    codeSize = minCodeSize + 1;
                    nextCode = eoiCode + 1;
                }
                prefix = px;
            }
        }
        put(prefix);
    }
    put(eoiCode);
    if (nbits > 0)
        bits.append(char(buf & 0xff));

    QByteArray out;
    for (int p = 0; p < bits.size();) {
        const int n = qMin(255, int(bits.size()) - p);
        out.append(char(n));
        out.append(bits.mid(p, n));
        p += n;
    }
    out.append(char(0));   // block terminator
    return out;
}

void appendLe16(QByteArray &b, int v)
{
    b.append(char(v & 0xff));
    b.append(char((v >> 8) & 0xff));
}

}   // namespace

bool GifWriter::write(const QString &path) const
{
    if (m_frames.isEmpty())
        return false;
    const int W = m_frames.first().img.width();
    const int H = m_frames.first().img.height();

    // 1. Global palette: exact for <=256 distinct colours, else the first 256
    //    seen with a nearest-colour fallback for the rest.
    QVector<QRgb> palette;
    QHash<QRgb, int> exact;
    bool overflow = false;
    for (const Frame &f : m_frames) {
        for (int y = 0; y < H && !overflow; ++y) {
            const QRgb *s = reinterpret_cast<const QRgb *>(f.img.constScanLine(y));
            for (int x = 0; x < W; ++x) {
                const QRgb c = s[x] | 0xff000000u;
                if (!exact.contains(c)) {
                    if (palette.size() < 256) {
                        exact.insert(c, palette.size());
                        palette.append(c);
                    } else {
                        overflow = true;
                        break;
                    }
                }
            }
        }
        if (overflow)
            break;
    }

    int palBits = 1;
    while ((1 << palBits) < palette.size())
        ++palBits;
    palBits = qBound(1, palBits, 8);
    const int palSize = 1 << palBits;
    const int minCodeSize = qMax(2, palBits);

    QHash<QRgb, int> nearestCache;
    auto indexOf = [&](QRgb c) -> int {
        c |= 0xff000000u;
        const auto it = exact.constFind(c);
        if (it != exact.constEnd())
            return it.value();
        const auto cit = nearestCache.constFind(c);
        if (cit != nearestCache.constEnd())
            return cit.value();
        int best = 0;
        long bestD = LONG_MAX;
        for (int i = 0; i < palette.size(); ++i) {
            const QRgb p = palette[i];
            const long d = long(qRed(p) - qRed(c)) * (qRed(p) - qRed(c))
                           + long(qGreen(p) - qGreen(c)) * (qGreen(p) - qGreen(c))
                           + long(qBlue(p) - qBlue(c)) * (qBlue(p) - qBlue(c));
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        nearestCache.insert(c, best);
        return best;
    };

    // 2. Assemble the GIF89a byte stream.
    QByteArray g;
    g.append("GIF89a");
    appendLe16(g, W);
    appendLe16(g, H);
    g.append(char(0x80 | ((palBits - 1) << 4) | (palBits - 1)));   // global table + sizes
    g.append(char(0));   // background colour index
    g.append(char(0));   // pixel aspect ratio
    for (int i = 0; i < palSize; ++i) {
        const QRgb c = i < palette.size() ? palette[i] : qRgb(0, 0, 0);
        g.append(char(qRed(c)));
        g.append(char(qGreen(c)));
        g.append(char(qBlue(c)));
    }
    // Netscape looping extension (loop forever).
    g.append(char(0x21));
    g.append(char(0xff));
    g.append(char(0x0b));
    g.append("NETSCAPE2.0");
    g.append(char(0x03));
    g.append(char(0x01));
    appendLe16(g, 0);
    g.append(char(0x00));

    QVector<uchar> idx(W * H);
    for (const Frame &f : m_frames) {
        // Graphic control extension (per-frame delay).
        g.append(char(0x21));
        g.append(char(0xf9));
        g.append(char(0x04));
        g.append(char(0x00));           // no disposal, no transparency
        appendLe16(g, f.delayCs);
        g.append(char(0x00));           // transparent colour index (unused)
        g.append(char(0x00));           // block terminator
        // Image descriptor.
        g.append(char(0x2c));
        appendLe16(g, 0);
        appendLe16(g, 0);
        appendLe16(g, W);
        appendLe16(g, H);
        g.append(char(0x00));           // no local table / interlace
        // Indexed pixels + LZW.
        int k = 0;
        for (int y = 0; y < H; ++y) {
            const QRgb *s = reinterpret_cast<const QRgb *>(f.img.constScanLine(y));
            for (int x = 0; x < W; ++x)
                idx[k++] = static_cast<uchar>(indexOf(s[x]));
        }
        g.append(char(minCodeSize));
        g.append(gifLzw(idx, minCodeSize));
    }
    g.append(char(0x3b));   // trailer

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(g);
    return true;
}
