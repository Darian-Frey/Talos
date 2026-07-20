#include "RasterCodegen.h"

#include <algorithm>
#include <cmath>

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

QString generateCopper(QVector<Bar> bars, int speed, bool bounce)
{
    if (speed < 1)
        speed = 1;
    std::sort(bars.begin(), bars.end(),
              [](const Bar &a, const Bar &b) { return a.line < b.line; });
    const int total = kVisibleLines;
    QVector<quint16> colours(total, 0x000);
    for (int line = 0; line < total; ++line)
        for (const Bar &b : bars)
            if (line >= b.line)
                colours[line] = b.colour;

    // Doubled table so the animated read window (offset..offset+total-1) never wraps.
    QString tab;
    for (int i = 0; i < total * 2; i += 8) {
        QStringList row;
        for (int j = 0; j < 8 && i + j < total * 2; ++j)
            row << QStringLiteral("$%1").arg(colours[(i + j) % total], 3, 16, QLatin1Char('0'));
        tab += QStringLiteral("\tdc.w\t") + row.join(QLatin1Char(',')) + QLatin1Char('\n');
    }

    // The VBL computes the frame's colour offset (d3) into the doubled table.
    QString vblOffset, extraData;
    if (!bounce) {
        vblOffset = QStringLiteral(
            "    move.w  cofs,d3              ; scroll offset (lines)\n"
            "    add.w   #%1,d3               ; += speed\n"
            "    cmp.w   #%2,d3               ; wrap at total lines\n"
            "    blt.s   .now\n"
            "    sub.w   #%2,d3\n"
            ".now:\n"
            "    move.w  d3,cofs\n")
            .arg(speed).arg(total);
    } else {
        // Bounce: a 256-entry sine LUT (offset 0..total-1), stepped by `speed`.
        vblOffset = QStringLiteral(
            "    move.w  cofs,d0              ; frame counter\n"
            "    add.w   #%1,d0\n"
            "    and.w   #255,d0\n"
            "    move.w  d0,cofs\n"
            "    add.w   d0,d0                ; *2 -> word index\n"
            "    lea     sinetab(pc),a1\n"
            "    move.w  (a1,d0.w),d3         ; d3 = bounce offset\n")
            .arg(speed);
        QStringList sw;
        for (int i = 0; i < 256; ++i) {
            const int v = qRound((total - 1) / 2.0 * (1.0 - std::cos(2.0 * M_PI * i / 256.0)));
            sw << QStringLiteral("$%1").arg(v, 4, 16, QLatin1Char('0'));
        }
        QString sd;
        for (int i = 0; i < 256; i += 8)
            sd += QStringLiteral("\tdc.w\t")
                  + QStringList(sw.mid(i, 8)).join(QLatin1Char(',')) + QLatin1Char('\n');
        extraData = QStringLiteral("    even\nsinetab:\n") + sd;
    }

    return QStringLiteral(R"(; copper.s — Talos generated animated raster bars (F-212). Do not hand-edit.
IERA    equ $fffffa07
IERB    equ $fffffa09
PAL0    equ $ffff8240
    text
start:
    clr.l   -(sp)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp
    move.b  #0,IERA
    move.b  #0,IERB
    move.l  $44e.w,a0
    move.w  #(32000/4)-1,d0
    moveq   #0,d1
.clr:
    move.l  d1,(a0)+
    dbf     d0,.clr
    clr.w   cofs
    move.l  #vbl,$70.w
main:
    stop    #$2300
    bra.s   main

vbl:
    move.w  #$2700,sr
    movem.l d0-d3/a1,-(sp)
%1    move.w  #%2,d0               ; delay: VBL -> first visible line
.d: dbf     d0,.d
    lea     coltab(pc),a1
    add.w   d3,d3                ; offset*2 (word table)
    adda.w  d3,a1                ; a1 = coltab + offset*2
    move.w  #%3,d2               ; total-1
.line:
    move.w  (a1)+,PAL0
    move.w  #%4,d1               ; pad => %5 cyc/line
.p: dbf     d1,.p
    dbf     d2,.line
    movem.l (sp)+,d0-d3/a1
    rte

    even
cofs: dc.w 0
%6    even
coltab:
%7)")
        .arg(vblOffset)       // %1
        .arg(kDefaultDelay)   // %2
        .arg(total - 1)       // %3
        .arg(kDefaultPad)     // %4
        .arg(kCycPerLine)     // %5
        .arg(extraData)       // %6
        .arg(tab);            // %7
}

QString generateColourCycle(const QVector<quint16> &coloursIn, bool perColumn)
{
    // 16 palette entries (pad/wrap the authored colours; default to a rainbow).
    static const quint16 fallback[16] = {0x700, 0x740, 0x770, 0x470, 0x070, 0x074,
                                          0x077, 0x047, 0x007, 0x407, 0x707, 0x704,
                                          0x777, 0x333, 0x555, 0x000};
    quint16 pal[16];
    for (int i = 0; i < 16; ++i)
        pal[i] = coloursIn.isEmpty() ? fallback[i]
                                     : quint16(coloursIn[i % coloursIn.size()] & 0x777);
    QStringList palWords;
    for (int i = 0; i < 16; ++i)
        palWords << QStringLiteral("$%1").arg(pal[i], 4, 16, QLatin1Char('0'));

    // 4 interleaved plane words for a 16 px group of palette index `idx`.
    auto planeWords = [](int idx) {
        QStringList w;
        for (int p = 0; p < 4; ++p)
            w << QStringLiteral("$%1").arg((idx >> p) & 1 ? 0xffff : 0x0000, 4, 16,
                                           QLatin1Char('0'));
        return w;
    };
    auto dcw = [](const QStringList &words) {
        QString s;
        for (int i = 0; i < words.size(); i += 10)
            s += QStringLiteral("\tdc.w\t")
                 + QStringList(words.mid(i, 10)).join(QLatin1Char(',')) + QLatin1Char('\n');
        return s;
    };

    QString fillCode, fillData;
    if (!perColumn) {
        // Vertical stripes: one line, group g uses index g&15; copied to all 200.
        QStringList lw;
        for (int g = 0; g < 20; ++g)
            lw += planeWords(g & 15);
        fillData = QStringLiteral("    even\nlinedat:\n") + dcw(lw);
        fillCode = QStringLiteral(
            "    move.l  $44e.w,a0            ; fill 200 lines with the stripe ramp\n"
            "    move.w  #199,d2\n"
            ".fl:\n"
            "    lea     linedat(pc),a1\n"
            "    moveq   #80-1,d0\n"
            ".cp:\n"
            "    move.w  (a1)+,(a0)+\n"
            "    dbf     d0,.cp\n"
            "    dbf     d2,.fl\n");
    } else {
        // Horizontal bands: each scanline is a uniform index (line*16/total); 16
        // line patterns + a per-scanline band table, so every column cycles.
        const int total = kVisibleLines;
        QStringList allPat;
        for (int c = 0; c < 16; ++c)
            for (int g = 0; g < 20; ++g)
                allPat += planeWords(c);
        QStringList band;
        for (int line = 0; line < total; ++line)
            band << QStringLiteral("$%1").arg(line * 16 / total, 2, 16, QLatin1Char('0'));
        QString bandData;
        for (int i = 0; i < total; i += 16)
            bandData += QStringLiteral("\tdc.b\t")
                        + QStringList(band.mid(i, 16)).join(QLatin1Char(',')) + QLatin1Char('\n');
        fillData = QStringLiteral("    even\nlinepat:\n") + dcw(allPat)
                   + QStringLiteral("    even\nbandtab:\n") + bandData;
        fillCode = QStringLiteral(
            "    lea     bandtab(pc),a2       ; fill: each line = its band's index\n"
            "    move.l  $44e.w,a0\n"
            "    move.w  #%1,d2\n"
            ".fl:\n"
            "    moveq   #0,d0\n"
            "    move.b  (a2)+,d0\n"
            "    mulu    #160,d0              ; 80 words per line pattern\n"
            "    lea     linepat(pc),a1\n"
            "    adda.l  d0,a1\n"
            "    moveq   #80-1,d1\n"
            ".cp:\n"
            "    move.w  (a1)+,(a0)+\n"
            "    dbf     d1,.cp\n"
            "    dbf     d2,.fl\n")
            .arg(total - 1);
    }

    return QStringLiteral(R"(; cycle.s — Talos generated palette colour-cycling (F-212). Do not hand-edit.
IERA    equ $fffffa07
IERB    equ $fffffa09
PALBASE equ $ffff8240
    text
start:
    clr.l   -(sp)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp
    move.b  #0,IERA
    move.b  #0,IERB
%1    lea     pal(pc),a1           ; load the 16 palette registers
    lea     PALBASE,a0
    moveq   #15,d0
.sp:
    move.w  (a1)+,(a0)+
    dbf     d0,.sp
    move.l  #vbl,$70.w
main:
    stop    #$2300
    bra.s   main

vbl:
    movem.l d0-d1/a0,-(sp)       ; rotate the palette one step each frame
    lea     PALBASE,a0
    move.w  (a0),d0              ; save colour 0
    moveq   #14,d1
.rot:
    move.w  2(a0),(a0)+          ; pal[i] = pal[i+1]
    dbf     d1,.rot
    move.w  d0,(a0)              ; pal[15] = old colour 0
    movem.l (sp)+,d0-d1/a0
    rte

%2    even
pal:
    dc.w    %3
)")
        .arg(fillCode)                          // %1
        .arg(fillData)                          // %2
        .arg(palWords.join(QLatin1Char(',')));  // %3
}

// Wrap an HBL-handler body in the MFP-off, screen-cleared, HBL-synced prologue.
static QString hblWrap(const QString &body)
{
    return QStringLiteral(
        "; split.s — Talos Phase 4 generated intra-line effect (F-212). Do not hand-edit.\n"
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
        "    move.l  #hbl,$68.w           ; HBL autovector -> the colour run\n"
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

QString generateSplit(const QVector<quint16> &colours)
{
    QString body;
    for (int i = 0; i < colours.size() && i < kMaxBands; ++i)
        body += QStringLiteral("    move.w  #$%1,PAL0\n")
                    .arg(colours[i] & 0x777, 3, 16, QLatin1Char('0'));
    return hblWrap(body);
}

QString generateColumns(QVector<Bar> bars)
{
    std::sort(bars.begin(), bars.end(),
              [](const Bar &a, const Bar &b) { return a.line < b.line; });
    if (bars.isEmpty())
        return hblWrap(QString());

    auto write = [](quint16 c) {
        return QStringLiteral("    move.w  #$%1,PAL0\n").arg(c & 0x777, 3, 16, QLatin1Char('0'));
    };
    QString body = write(bars[0].colour);   // leftmost colour, from the left edge
    double prev = kColBase;
    for (int i = 1; i < bars.size() && i < kMaxBands; ++i) {
        int L = qMax(0, qRound((bars[i].line - prev - kGapBase) / double(kPxPerDbf)));
        body += QStringLiteral("    move.w  #%1,d0\n.b%2: dbf     d0,.b%2\n").arg(L).arg(i);
        body += write(bars[i].colour);
        prev = prev + kGapBase + kPxPerDbf * L;
    }
    return hblWrap(body);
}

}   // namespace RasterCodegen
