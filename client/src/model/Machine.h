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

inline const char *regionName(VideoRegion r)
{
    return r == VideoRegion::Ntsc60 ? "NTSC" : "PAL";
}

// Language / country. EmuTOS's --country sets both the language and the video
// region; most countries are PAL, only a few (us) are NTSC. So a language maps to
// a PAL country and, where one exists, an NTSC country.
enum class Language { English, German, French, Spanish, Italian, Swedish };

struct LanguageInfo
{
    Language lang;
    QString name;
    QString palCountry;    // EmuTOS --country for 50 Hz PAL
    QString ntscCountry;   // for 60 Hz NTSC, or empty if the language has none
};

namespace Languages {

inline const LanguageInfo &info(Language l)
{
    static const LanguageInfo kTable[] = {
        {Language::English, "English", "uk", "us"},
        {Language::German,  "German",  "de", ""},
        {Language::French,  "French",  "fr", ""},
        {Language::Spanish, "Spanish", "es", ""},
        {Language::Italian, "Italian", "it", ""},
        {Language::Swedish, "Swedish", "se", ""},
    };
    for (const LanguageInfo &e : kTable)
        if (e.lang == l)
            return e;
    return kTable[0];
}

inline QList<Language> all()
{
    return {Language::English, Language::German, Language::French,
            Language::Spanish, Language::Italian, Language::Swedish};
}

// The --country for a language + desired region (falls back to PAL when the
// language has no NTSC variant).
inline QString country(Language l, VideoRegion r)
{
    const LanguageInfo &e = info(l);
    if (r == VideoRegion::Ntsc60 && !e.ntscCountry.isEmpty())
        return e.ntscCountry;
    return e.palCountry;
}

// The region a country actually boots in (only us/ca are NTSC).
inline VideoRegion regionOf(const QString &country)
{
    return (country == "us" || country == "ca") ? VideoRegion::Ntsc60
                                                 : VideoRegion::Pal50;
}

} // namespace Languages
