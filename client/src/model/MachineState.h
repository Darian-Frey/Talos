// MachineState — the parsed result of a B1 `regs` reply (F-215, minimal M0 form).
//
// The full F-215 design is a per-frame ring of cycle/scanline-indexed events.
// For M0 we only need the machine's current register + video-variable snapshot,
// so this is a flat name->value map with typed accessors for the beam counters
// that Phase 1 will build on. It deliberately does no networking.

#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <optional>

class MachineState
{
public:
    // All name->value pairs from `regs` (CPU registers + Hatari video variables).
    // Values are stored as parsed 32-bit words; the wire form is hex.
    QMap<QString, quint32> values;

    bool has(const QString &key) const { return values.contains(key); }
    quint32 value(const QString &key, quint32 fallback = 0) const
    {
        return values.value(key, fallback);
    }
    std::optional<quint32> opt(const QString &key) const
    {
        auto it = values.constFind(key);
        return it == values.constEnd() ? std::nullopt : std::optional<quint32>(*it);
    }

    // Beam / cycle position (verified available over B1, see protocol/b1-protocol.md).
    std::optional<quint32> hbl() const { return opt("HBL"); }           // scanline (Y)
    std::optional<quint32> vbl() const { return opt("VBL"); }           // frame number
    std::optional<quint32> lineCycles() const { return opt("LineCycles"); }   // cycle-in-line (X)
    std::optional<quint32> frameCycles() const { return opt("FrameCycles"); }
    std::optional<quint32> cycleCounter() const { return opt("CycleCounter"); }
    std::optional<quint32> pc() const { return opt("PC"); }

    // Names Talos treats as video/beam counters — highlighted in the UI and
    // sorted to the top of the register panel.
    static bool isBeamCounter(const QString &key)
    {
        static const QStringList kBeam = {
            "HBL", "VBL", "LineCycles", "FrameCycles", "CycleCounter"};
        return kBeam.contains(key);
    }

    // Parse a `regs` reply. Tokens are the framed message split on 0x1:
    //   [0] == "OK", then alternating <name> <hexvalue> pairs.
    // Returns an empty state if the reply is not an OK regs payload.
    static MachineState fromRegsReply(const QList<QByteArray> &tokens);
};
