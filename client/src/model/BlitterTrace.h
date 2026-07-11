// BlitterTrace — decode the B2 `blittrace` dump into per-operation memory traffic.
//
// Wire form (protocol/b1-protocol.md, F-208 tap): the `blittrace` reply is
//   OK <count> then, per entry: addr cycle_hi cycle_lo value flags
// where flags bit0 = write (else read), bit1 = blit-complete marker. A marker
// closes the current operation, so a non-hog blit spanning several bus bursts
// still reads back as one BlitOp (see blitter.c:907, the true end-of-blit seam).

#pragma once

#include <QByteArray>
#include <QList>
#include <QVector>

struct BlitAccess {
    quint32 addr = 0;
    quint64 cycle = 0;
    quint16 value = 0;
    bool isWrite = false;
};

struct BlitOp {
    QVector<BlitAccess> accesses;   // in order; between the previous marker and this one
    quint64 endCycle = 0;           // cycle of the closing marker

    int reads() const {
        int n = 0;
        for (const auto &a : accesses) if (!a.isWrite) ++n;
        return n;
    }
    int writes() const { return accesses.size() - reads(); }

    quint64 spanCycles() const {
        if (accesses.isEmpty()) return 0;
        return accesses.last().cycle - accesses.first().cycle;
    }
    quint32 addrLo() const {
        quint32 lo = accesses.isEmpty() ? 0 : accesses.first().addr;
        for (const auto &a : accesses) lo = qMin(lo, a.addr);
        return lo;
    }
    quint32 addrHi() const {
        quint32 hi = 0;
        for (const auto &a : accesses) hi = qMax(hi, a.addr);
        return hi;
    }
};

namespace BlitterTrace {

// Parse a `blittrace` reply (tokens: "OK", count, then 5 per entry). A trailing
// run of accesses with no closing marker (an in-flight blit) is returned as a
// final open BlitOp so nothing is silently dropped. Returns empty on malformed.
QVector<BlitOp> parse(const QList<QByteArray> &reply);

}   // namespace BlitterTrace
