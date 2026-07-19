#include "MfpState.h"

// All constants sourced from Hatari (verified 2026-07-19), never from memory (C-007):
//   clocks_timings.c: ATARI_MFP_XTAL 2457600 (MFP_Timer_Freq).
//   mfp.c:            MFPDiv[] = { 0, 4, 10, 16, 50, 64, 100, 200 };
//                     MFP_REG_TO_CYCLES(data,ctrl) = data * MFPDiv[ctrl&7];
//                     control 0x08 = event-count mode (Timer A/B only).
//   mfp.h:            68901 register + interrupt-bit map (below).
//   Registers (odd bytes at $fffaXX): IERA $07 IERB $09 IPRA $0b IPRB $0d
//     ISRA $0f ISRB $11 IMRA $13 IMRB $15 TACR $19 TBCR $1b TCDCR $1d
//     TADR $1f TBDR $21 TCDR $23 TDDR $25.

namespace Mfp {

namespace {
constexpr int MFPDiv[8] = {0, 4, 10, 16, 50, 64, 100, 200};

Timer decodeTimer(const QString &name, int ctrl, bool abTimer, int data,
                  double frameHz, int linesPerFrame, const QString &use)
{
    Timer t;
    t.name = name;
    t.data = data;
    t.use = use;
    t.running = (ctrl & (abTimer ? 0xf : 0x7)) != 0;
    if (!t.running) {
        t.mode = QStringLiteral("stopped");
        return t;
    }
    if (abTimer && (ctrl & 0xf) == 0x08) {
        t.mode = QStringLiteral("event count");
        t.eventCount = true;
        return t;   // rate depends on the counted event (e.g. display line)
    }
    // Delay mode (pulse-width is handled as delay, per mfp.c).
    t.mode = (abTimer && (ctrl & 0x8)) ? QStringLiteral("pulse width")
                                       : QStringLiteral("delay");
    t.prescaler = MFPDiv[ctrl & 0x7];
    const int count = data == 0 ? 256 : data;   // 68901: data 0 = 256 counts
    if (t.prescaler > 0) {
        t.freqHz = double(kXtalHz) / (double(t.prescaler) * count);
        t.perFrame = t.freqHz / frameHz;
        if (t.perFrame >= 1.0)
            t.lineGap = int(linesPerFrame / t.perFrame);
    }
    return t;
}
}   // namespace

State decode(const QByteArray &block, VideoRegion region)
{
    State s;
    if (block.size() < 0x26)
        return s;
    auto R = [&](int off) { return static_cast<quint8>(block[off]); };

    const double frameHz = (region == VideoRegion::Ntsc60) ? 60.0 : 50.0;
    const int lines = (region == VideoRegion::Ntsc60) ? 263 : 313;

    s.iera = R(0x07); s.ierb = R(0x09);
    s.ipra = R(0x0b); s.iprb = R(0x0d);
    s.isra = R(0x0f); s.isrb = R(0x11);
    s.imra = R(0x13); s.imrb = R(0x15);
    const quint8 tacr = R(0x19), tbcr = R(0x1b), tcdcr = R(0x1d);

    s.timers.append(decodeTimer(QStringLiteral("A"), tacr & 0xf, true, R(0x1f), frameHz, lines,
                                QStringLiteral("free — effects / sampled sound")));
    s.timers.append(decodeTimer(QStringLiteral("B"), tbcr & 0xf, true, R(0x21), frameHz, lines,
                                QStringLiteral("event count → display (Spectrum 512), or delay")));
    s.timers.append(decodeTimer(QStringLiteral("C"), (tcdcr >> 4) & 0x7, false, R(0x23), frameHz,
                                lines, QStringLiteral("200 Hz system tick (TOS)")));
    s.timers.append(decodeTimer(QStringLiteral("D"), tcdcr & 0x7, false, R(0x25), frameHz, lines,
                                QStringLiteral("RS-232 baud-rate generator")));

    // Interrupt sources, highest priority first. {regB?, bit, name, isTimer}.
    struct Def { bool b; quint8 bit; const char *name; bool timer; };
    static const Def defs[16] = {
        {false, 0x80, "GPIP7 — monochrome monitor", false},
        {false, 0x40, "GPIP6 — RS-232 ring", false},
        {false, 0x20, "Timer A", true},
        {false, 0x10, "Receive buffer full", false},
        {false, 0x08, "Receive error", false},
        {false, 0x04, "Transmit buffer empty", false},
        {false, 0x02, "Transmit error", false},
        {false, 0x01, "Timer B", true},
        {true,  0x80, "GPIP5 — FDC / HDC", false},
        {true,  0x40, "GPIP4 — ACIA (kbd/MIDI)", false},
        {true,  0x20, "Timer C", true},
        {true,  0x10, "Timer D", true},
        {true,  0x08, "GPIP3 — blitter", false},
        {true,  0x04, "GPIP2 — RS-232 CTS", false},
        {true,  0x02, "GPIP1 — RS-232 DCD", false},
        {true,  0x01, "GPIP0 — parallel busy", false},
    };
    for (const Def &d : defs) {
        Source src;
        src.name = QString::fromUtf8(d.name);
        src.isTimer = d.timer;
        src.enabled = (d.b ? s.ierb : s.iera) & d.bit;
        src.unmasked = (d.b ? s.imrb : s.imra) & d.bit;
        src.pending = (d.b ? s.iprb : s.ipra) & d.bit;
        src.inService = (d.b ? s.isrb : s.isra) & d.bit;
        s.sources.append(src);
    }

    s.valid = true;
    return s;
}

}   // namespace Mfp
