// MfpState — Phase 6: decode the MC68901 MFP timer + interrupt registers, so the
// four timers and the interrupt controller are legible (Timer-B drives Spectrum
// 512, Timer-C is the 200 Hz system tick, HBL/Timer effects, …).
//
// Talos does not emulate the MFP (D-002) — it reads the register block over B1
// (`mem fffa00 30`) and decodes it. Every constant is sourced from Hatari
// (mfp.c / mfp.h / clocks_timings.c), cited in the .cpp (C-007): XTAL 2 457 600 Hz,
// MFPDiv[] = {0,4,10,16,50,64,100,200}, and the 68901 register/interrupt map.

#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "view/BeamGeometry.h"   // VideoRegion

namespace Mfp {

constexpr int kXtalHz = 2457600;   // ATARI_MFP_XTAL

struct Timer
{
    QString name;        // "A".."D"
    QString mode;        // "stopped" / "delay" / "event count" / "pulse width"
    int prescaler = 0;   // MFPDiv (delay/pulse); 0 if N/A
    int data = 0;        // data register (count)
    double freqHz = 0;   // delay/pulse interrupt frequency (0 if not computable)
    double perFrame = 0; // interrupts per video frame (delay mode)
    int lineGap = 0;     // ~scanlines between interrupts (delay mode, if perFrame≥1)
    QString use;         // typical role
    bool running = false;
    bool eventCount = false;
};

struct Source
{
    QString name;      // "Timer C", "GPIP4 (ACIA kbd/MIDI)", …
    bool isTimer = false;
    bool enabled = false;     // IERA/B
    bool unmasked = false;    // IMRA/B (1 = allowed to interrupt)
    bool pending = false;     // IPRA/B
    bool inService = false;   // ISRA/B
    bool active() const { return enabled && unmasked; }
};

struct State
{
    bool valid = false;
    QVector<Timer> timers;     // A, B, C, D
    QVector<Source> sources;   // 16, highest priority first
    // Raw control/data bytes for the header line.
    quint8 iera = 0, ierb = 0, imra = 0, imrb = 0, ipra = 0, iprb = 0, isra = 0, isrb = 0;
};

// Decode a 48-byte block read from $fffa00 (so register $fffaNN = block[NN]).
State decode(const QByteArray &block, VideoRegion region);

}   // namespace Mfp
