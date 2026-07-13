#include "RasterCodegen.h"

#include <algorithm>

namespace RasterCodegen {

QString generate(QVector<Bar> bars, int pad, int delay, int total)
{
    std::sort(bars.begin(), bars.end(),
              [](const Bar &a, const Bar &b) { return a.line < b.line; });

    // Expand the sparse bars to a per-line colour table (piecewise constant).
    QVector<quint16> colours(total, 0x000);
    for (int line = 0; line < total; ++line)
        for (const Bar &b : bars)
            if (line >= b.line)
                colours[line] = b.colour;

    QString tab;
    for (int i = 0; i < total; i += 8) {
        QStringList row;
        for (int j = 0; j < 8 && i + j < total; ++j)
            row << QStringLiteral("$%1").arg(colours[i + j], 3, 16, QLatin1Char('0'));
        tab += QStringLiteral("\tdc.w\t") + row.join(QLatin1Char(',')) + QLatin1Char('\n');
    }

    return QStringLiteral(
        "; raster.s — Talos Phase 4 generated raster bars (F-212). Do not hand-edit.\n"
        "IERA    equ $fffffa07\n"
        "IERB    equ $fffffa09\n"
        "PAL0    equ $ffff8240\n"
        "    text\n"
        "start:\n"
        "    clr.l   -(sp)\n"
        "    move.w  #$20,-(sp)\n"
        "    trap    #1\n"
        "    addq.l  #6,sp\n"
        "    move.b  #0,IERA              ; MFP off -> Timer-C can't jitter the VBL sync\n"
        "    move.b  #0,IERB\n"
        "    move.l  $44e.w,a0            ; clear the screen to palette 0 (whole screen = bg)\n"
        "    move.w  #(32000/4)-1,d0\n"
        "    moveq   #0,d1\n"
        ".clr:\n"
        "    move.l  d1,(a0)+\n"
        "    dbf     d0,.clr\n"
        "    move.l  #vbl,$70.w           ; VBL handler runs the cycle-counted bar loop\n"
        "main:\n"
        "    stop    #$2300               ; wait for VBL (jitter-free with MFP off)\n"
        "    bra.s   main\n"
        "\n"
        "vbl:\n"
        "    move.w  #$2700,sr            ; mask: the timing below must be exact\n"
        "    movem.l d0-d2/a1,-(sp)\n"
        "    move.w  #%1,d0               ; delay: VBL -> first visible bar line\n"
        ".d: dbf     d0,.d\n"
        "    lea     coltab(pc),a1\n"
        "    move.w  #%2,d2\n"
        ".line:\n"
        "    move.w  (a1)+,PAL0           ; this scanline's colour\n"
        "    move.w  #%3,d1               ; pad the rest of the scanline (=> %4 cyc/line)\n"
        ".p: dbf     d1,.p\n"
        "    dbf     d2,.line\n"
        "    movem.l (sp)+,d0-d2/a1\n"
        "    rte\n"
        "\n"
        "    even\n"
        "coltab:\n"
        "%5")
        .arg(delay)
        .arg(total - 1)
        .arg(pad)
        .arg(kCycPerLine)
        .arg(tab);
}

QString generateSplit(const QVector<quint16> &colours)
{
    QString body;
    for (int i = 0; i < colours.size() && i < kMaxBands; ++i)
        body += QStringLiteral("    move.w  #$%1,PAL0\n")
                    .arg(colours[i] & 0x777, 3, 16, QLatin1Char('0'));

    return QStringLiteral(
        "; split.s — Talos Phase 4 generated intra-line bands (F-212). Do not hand-edit.\n"
        "IERA    equ $fffffa07\n"
        "IERB    equ $fffffa09\n"
        "PAL0    equ $ffff8240\n"
        "    text\n"
        "start:\n"
        "    clr.l   -(sp)\n"
        "    move.w  #$20,-(sp)\n"
        "    trap    #1\n"
        "    addq.l  #6,sp\n"
        "    move.b  #0,IERA              ; MFP off -> only HBL/VBL fire\n"
        "    move.b  #0,IERB\n"
        "    move.l  $44e.w,a0            ; clear the screen to palette 0\n"
        "    move.w  #(32000/4)-1,d0\n"
        "    moveq   #0,d1\n"
        ".clr:\n"
        "    move.l  d1,(a0)+\n"
        "    dbf     d0,.clr\n"
        "    move.l  #hbl,$68.w           ; HBL autovector -> the packed colour run\n"
        "    move.l  #vbl,$70.w\n"
        "main:\n"
        "    stop    #$2100               ; IPL1: HBL wakes us with fixed latency\n"
        "    bra.s   main\n"
        "\n"
        "hbl:\n"
        "%1"
        "    rte\n"
        "\n"
        "vbl:\n"
        "    rte\n")
        .arg(body);
}

}   // namespace RasterCodegen
