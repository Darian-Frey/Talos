// Palette — decode the ST/STE colour registers ($ff8240..$ff825e) to RGB.
//
// Matches Hatari's Screen_SetupRGBTable (screen.c): each 4-bit gun nibble is
// reordered — the STE bit-order quirk (C-008), where bit 3 of the nibble is the
// LSB of the intensity and the ST 3 bits are the top — then the 4-bit intensity
// is scaled to 8 bits. The decode is the same for ST and STE; the hardware masks
// ST writes to $0777 (bit 3 = 0 -> 8 levels/gun = 512 colours) while STE allows
// the full nibble (16 levels = 4096 colours).

#pragma once

#include <QByteArray>
#include <QColor>
#include <QVector>

#include "protocol/MemCodec.h"

namespace Palette {

// One gun nibble (0..15) -> 8-bit intensity.
inline int gun(int nibble)
{
    const int i4 = ((nibble & 0x7) << 1) | ((nibble & 0x8) >> 3);   // C-008 reorder
    return i4 | (i4 << 4);                                          // 4-bit -> 8-bit
}

// A colour register word (%0000 RRRR GGGG BBBB) -> RGB.
inline QColor decode(quint16 reg)
{
    return QColor(gun((reg >> 8) & 0xf), gun((reg >> 4) & 0xf), gun(reg & 0xf));
}

// The 16 palette register words from a `mem ff8240 20` data token (32 bytes).
inline QVector<quint16> readRegisters(const QByteArray &memData)
{
    const QByteArray b = MemCodec::decode(memData, 32);
    QVector<quint16> regs;
    for (int i = 0; i + 1 < b.size() && regs.size() < 16; i += 2)
        regs.append((static_cast<quint8>(b[i]) << 8) | static_cast<quint8>(b[i + 1]));
    return regs;
}

} // namespace Palette
