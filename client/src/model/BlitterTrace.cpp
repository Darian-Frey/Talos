#include "BlitterTrace.h"

namespace {
constexpr quint16 kFlagWrite = 1 << 0;
constexpr quint16 kFlagMarker = 1 << 1;

quint32 hex32(const QByteArray &t) { return t.toUInt(nullptr, 16); }
}   // namespace

QVector<BlitOp> BlitterTrace::parse(const QList<QByteArray> &reply)
{
    QVector<BlitOp> ops;
    if (reply.size() < 2 || reply[0] != "OK")
        return ops;

    const int count = static_cast<int>(hex32(reply[1]));
    // Each entry is 5 tokens after the "OK" and count header.
    if (reply.size() < 2 + count * 5)
        return ops;

    BlitOp current;
    for (int i = 0; i < count; ++i) {
        const int base = 2 + i * 5;
        const quint32 addr = hex32(reply[base + 0]);
        const quint64 cyc = (static_cast<quint64>(hex32(reply[base + 1])) << 32)
                            | hex32(reply[base + 2]);
        const quint16 value = static_cast<quint16>(hex32(reply[base + 3]));
        const quint16 flags = static_cast<quint16>(hex32(reply[base + 4]));

        if (flags & kFlagMarker) {
            current.endCycle = cyc;
            ops.append(current);
            current = BlitOp{};
            continue;
        }
        BlitAccess a;
        a.addr = addr;
        a.cycle = cyc;
        a.value = value;
        a.isWrite = (flags & kFlagWrite) != 0;
        current.accesses.append(a);
    }
    // A trailing, unterminated blit (captured mid-flight): keep it, don't drop.
    if (!current.accesses.isEmpty())
        ops.append(current);
    return ops;
}
