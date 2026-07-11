#include "DmaSndTrace.h"

namespace {
constexpr int kFrame = 0, kDrain = 1, kCtrl = 2, kLmc = 3;
quint32 hex32(const QByteArray &t) { return t.toUInt(nullptr, 16); }
}   // namespace

DmaSndTrace DmaSndTrace::parse(const QList<QByteArray> &reply)
{
    DmaSndTrace t;
    if (reply.size() < 2 || reply[0] != "OK")
        return t;
    const int count = static_cast<int>(hex32(reply[1]));
    if (reply.size() < 2 + count * 5)
        return t;

    bool firstCyc = true;
    for (int i = 0; i < count; ++i) {
        const int base = 2 + i * 5;
        const quint64 cyc = (static_cast<quint64>(hex32(reply[base + 0])) << 32)
                            | hex32(reply[base + 1]);
        const int kind = static_cast<int>(hex32(reply[base + 2]));
        const quint32 a = hex32(reply[base + 3]);
        const quint32 b = hex32(reply[base + 4]);

        if (firstCyc) {
            t.cycMin = t.cycMax = cyc;
            firstCyc = false;
        } else {
            t.cycMin = qMin(t.cycMin, cyc);
            t.cycMax = qMax(t.cycMax, cyc);
        }

        switch (kind) {
        case kFrame:
            t.bufStart = a;
            t.bufEnd = b;
            ++t.frames;
            break;
        case kDrain:
            t.drain.append({cyc, a});
            break;
        case kCtrl:
            t.control = static_cast<quint16>(a);
            t.mode = static_cast<quint16>(b);
            t.haveCtrl = true;
            break;
        case kLmc:
            if (a < 6)
                t.lmc[a] = static_cast<int>(b);
            t.lmcSeq.append({static_cast<int>(a), static_cast<int>(b)});
            break;
        default:
            break;
        }
    }
    return t;
}
