// MemCodec — decode the `mem` command's data payload.
//
// Hatari encodes memory bytes uuencode-style (see RemoteDebug_mem): each group of
// 3 bytes becomes 4 ASCII chars, each char = 32 + a 6-bit group. This reverses it.

#pragma once

#include <QByteArray>

namespace MemCodec {

// Decode `enc` (the last token of a `mem` reply) to at most `count` raw bytes.
inline QByteArray decode(const QByteArray &enc, int count)
{
    QByteArray out;
    for (int i = 0; i + 3 < enc.size(); i += 4) {
        quint32 accum = 0;
        for (int j = 0; j < 4; ++j)
            accum = (accum << 6) | ((static_cast<quint8>(enc[i + j]) - 32u) & 0x3fu);
        out.append(static_cast<char>((accum >> 16) & 0xff));
        out.append(static_cast<char>((accum >> 8) & 0xff));
        out.append(static_cast<char>(accum & 0xff));
    }
    return out.left(count);
}

// Decode a big-endian word (2 bytes) from a `mem <addr> 2` data token.
inline quint32 word(const QByteArray &enc)
{
    const QByteArray b = decode(enc, 2);
    if (b.size() < 2)
        return 0;
    return (static_cast<quint8>(b[0]) << 8) | static_cast<quint8>(b[1]);
}

} // namespace MemCodec
