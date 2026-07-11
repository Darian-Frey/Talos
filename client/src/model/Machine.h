// Machine — the four ST-family machines and their capabilities (F-205, F-207).
//
// Drives config generation (the Hatari --machine value and the --country used for
// PAL/NTSC) and the capability gating / differential view: which hardware each
// machine has, so the UI can reflect the differences honestly.

#pragma once

#include <QList>
#include <QString>

#include "view/BeamGeometry.h"   // VideoRegion

enum class MachineType { ST, MegaST, STE, MegaSTE };

struct MachineInfo
{
    MachineType type;
    QString name;            // display name
    QString hatariMachine;   // Hatari --machine value

    // Capabilities (what differs across the family):
    bool blitter;            // Blitter (Mega ST onwards)
    bool hardwareScroll;     // STE fine horizontal scroll ($ff8264/65)
    bool dmaSound;           // STE DMA sound + LMC1992
    int paletteColours;      // 512 (ST, 3 bits/gun) or 4096 (STE, 4 bits/gun)
    bool dualSpeed;          // Mega STE 8/16 MHz
};

namespace Machines {

inline const MachineInfo &info(MachineType t)
{
    static const MachineInfo kTable[] = {
        // type              name           --machine   blit  scroll dma   pal   dual
        {MachineType::ST,     "520/1040 ST", "st",      false, false, false, 512,  false},
        {MachineType::MegaST, "Mega ST",     "megast",  true,  false, false, 512,  false},
        {MachineType::STE,    "STE",         "ste",     true,  true,  true,  4096, false},
        {MachineType::MegaSTE,"Mega STE",    "megaste", true,  true,  true,  4096, true},
    };
    for (const MachineInfo &m : kTable)
        if (m.type == t)
            return m;
    return kTable[0];
}

inline QList<MachineType> all()
{
    return {MachineType::ST, MachineType::MegaST, MachineType::STE, MachineType::MegaSTE};
}

} // namespace Machines

// Region <-> Hatari --country (with a multi-language EmuTOS) and display name.
inline const char *regionCountry(VideoRegion r)
{
    return r == VideoRegion::Ntsc60 ? "us" : "de";   // us=60Hz NTSC, de=50Hz PAL
}
inline const char *regionName(VideoRegion r)
{
    return r == VideoRegion::Ntsc60 ? "NTSC" : "PAL";
}
