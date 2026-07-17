// StPicture — Phase 6: import common Atari ST picture formats.
//
// Decodes the classic 16-colour ST picture formats to an exact image + palette,
// so Talos can view your own ST art (Phase 6 "more ST image formats"). Distinct
// from the Spectrum 512 tab — these have one palette per screen, not the S512
// per-scanline palette storm.
//
// Supported (byte layouts sourced from the ST format references and validated
// against the canonical RECOIL decoder, 2026-07-16):
//   * DEGAS  .PI1/.PI2/.PI3  — 1 word resolution + 16 palette words + 32000-byte
//     standard interleaved ST screen (32034 bytes total).
//   * DEGAS Elite .PC1/.PC2/.PC3 — resolution word with bit 15 set + 16 palette
//     words + PackBits-RLE screen, per-scanline as four 40-byte planes.
//   * NEOchrome .NEO — 128-byte header (flag, resolution, 16 palette words, …) +
//     32000-byte interleaved screen (32128 bytes total).
//   * Tiny .TNY/.TN1-3 — header (resolution, optional colour-animation, 16 palette
//     words, control/data counts) + a word-level RLE over a 4-set vertical-column
//     layout.
// Resolution: 0 = 320x200 (16 col, 4 planes), 1 = 640x200 (4 col, 2 planes),
// 2 = 640x400 (mono, 1 plane).

#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

namespace StPicture {

struct Image
{
    bool valid = false;
    QString error;
    QString format;                 // "DEGAS .PI1", "DEGAS .PC1", "NEOchrome", …
    int resolution = 0;             // 0 low / 1 med / 2 high
    int colours = 16;               // 16 / 4 / 2
    QImage rgb;                     // decoded, native ST dimensions
    QVector<quint16> palette;       // the (up to 16) ST $0rgb words used
};

// Parse an ST picture, auto-detecting the format from content (+ the file
// extension as a hint for the compressed variants). On failure returns
// {valid=false, error=…}.
Image parse(const QByteArray &bytes, const QString &extension = QString());

}   // namespace StPicture
