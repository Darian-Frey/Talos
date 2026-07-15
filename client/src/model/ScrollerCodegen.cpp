#include "ScrollerCodegen.h"

#include <QChar>
#include <QStringList>
#include <QVector>

// ---------------------------------------------------------------------------
// STE hardware fine-scroll recipe (bench-validated against the Hatari fork,
// 2026-07-15; sourced from Hatari's src/video.c Video_HorScroll_Write, per
// C-007 "read it from Hatari, don't trust memory").
//
//   * Two scroll registers exist. $ff8264 is "no prefetch" (shifts the whole
//     window — wrong for a scroller); $ff8265 is "prefetch": the shifter reads
//     8 extra bytes (one 16 px column) at line start, so the extra column flows
//     IN from the right while the window stays put. That is what a scroller
//     wants. Write $ff8265 only — following it with $ff8264=0 triggers Hatari's
//     left-border +16 detection instead of scrolling.
//   * $ff820f (LineWidth, in words) widens each scanline so the incoming glyph
//     column has somewhere to live: screen is 21 cols/plane (LineWidth = 4).
//   * The screen base stays fixed. Fine scroll 0..15 gives the sub-column
//     smoothness (1 px/frame at speed 1). At each 16 px wrap we shift only the
//     narrow text BAND left by one column and draw the next glyph column at the
//     off-screen 21st column — cheap enough to finish inside vblank, so there is
//     no visible seam. (Moving the screen base while scroll is active is ignored
//     by the STE for small steps, so we scroll content, not the base.)
//   * Measurement traps for anyone re-validating: Hatari fast-forward frame-skips
//     rendering (use --frameskips 0) and its VBL breakpoint count races ahead of
//     boot (target a VBL relative to the live count, not an absolute low number).
// ---------------------------------------------------------------------------

namespace ScrollerCodegen {

namespace {

// 8x8 glyphs as ASCII art ('X' = set pixel, ' ' = clear), rows top->bottom,
// left pixel first. Legible by eye so the bitmaps are hard to get wrong.
struct Glyph { QChar ch; const char *rows[8]; };

const Glyph kFont[] = {
{'A', {"  XX  ", " X  X ", "X    X", "X    X", "XXXXXX", "X    X", "X    X", "      "}},
{'B', {"XXXXX ", "X    X", "X    X", "XXXXX ", "X    X", "X    X", "XXXXX ", "      "}},
{'C', {" XXXX ", "X    X", "X     ", "X     ", "X     ", "X    X", " XXXX ", "      "}},
{'D', {"XXXX  ", "X   X ", "X    X", "X    X", "X    X", "X   X ", "XXXX  ", "      "}},
{'E', {"XXXXXX", "X     ", "X     ", "XXXX  ", "X     ", "X     ", "XXXXXX", "      "}},
{'F', {"XXXXXX", "X     ", "X     ", "XXXX  ", "X     ", "X     ", "X     ", "      "}},
{'G', {" XXXX ", "X    X", "X     ", "X  XXX", "X    X", "X    X", " XXXX ", "      "}},
{'H', {"X    X", "X    X", "X    X", "XXXXXX", "X    X", "X    X", "X    X", "      "}},
{'I', {" XXX  ", "  X   ", "  X   ", "  X   ", "  X   ", "  X   ", " XXX  ", "      "}},
{'J', {"   XXX", "    X ", "    X ", "    X ", "X   X ", "X   X ", " XXX  ", "      "}},
{'K', {"X   X ", "X  X  ", "X X   ", "XX    ", "X X   ", "X  X  ", "X   X ", "      "}},
{'L', {"X     ", "X     ", "X     ", "X     ", "X     ", "X     ", "XXXXXX", "      "}},
{'M', {"X    X", "XX  XX", "X XX X", "X    X", "X    X", "X    X", "X    X", "      "}},
{'N', {"X    X", "XX   X", "X X  X", "X  X X", "X   XX", "X    X", "X    X", "      "}},
{'O', {" XXXX ", "X    X", "X    X", "X    X", "X    X", "X    X", " XXXX ", "      "}},
{'P', {"XXXXX ", "X    X", "X    X", "XXXXX ", "X     ", "X     ", "X     ", "      "}},
{'Q', {" XXXX ", "X    X", "X    X", "X    X", "X  X X", "X   X ", " XXX X", "      "}},
{'R', {"XXXXX ", "X    X", "X    X", "XXXXX ", "X X   ", "X  X  ", "X   X ", "      "}},
{'S', {" XXXX ", "X    X", "X     ", " XXXX ", "     X", "X    X", " XXXX ", "      "}},
{'T', {"XXXXXX", "  X   ", "  X   ", "  X   ", "  X   ", "  X   ", "  X   ", "      "}},
{'U', {"X    X", "X    X", "X    X", "X    X", "X    X", "X    X", " XXXX ", "      "}},
{'V', {"X    X", "X    X", "X    X", "X    X", " X  X ", " X  X ", "  XX  ", "      "}},
{'W', {"X    X", "X    X", "X    X", "X    X", "X XX X", "XX  XX", "X    X", "      "}},
{'X', {"X    X", " X  X ", "  XX  ", "  XX  ", "  XX  ", " X  X ", "X    X", "      "}},
{'Y', {"X    X", " X  X ", "  XX  ", "  X   ", "  X   ", "  X   ", "  X   ", "      "}},
{'Z', {"XXXXXX", "    X ", "   X  ", "  X   ", " X    ", "X     ", "XXXXXX", "      "}},
{'0', {" XXXX ", "X    X", "X   XX", "X X  X", "XX   X", "X    X", " XXXX ", "      "}},
{'1', {"  X   ", " XX   ", "  X   ", "  X   ", "  X   ", "  X   ", " XXX  ", "      "}},
{'2', {" XXXX ", "X    X", "     X", "   XX ", " XX   ", "X     ", "XXXXXX", "      "}},
{'3', {"XXXXX ", "     X", "     X", " XXXX ", "     X", "     X", "XXXXX ", "      "}},
{'4', {"   XX ", "  X X ", " X  X ", "X   X ", "XXXXXX", "    X ", "    X ", "      "}},
{'5', {"XXXXXX", "X     ", "XXXXX ", "     X", "     X", "X    X", " XXXX ", "      "}},
{'6', {" XXXX ", "X     ", "X     ", "XXXXX ", "X    X", "X    X", " XXXX ", "      "}},
{'7', {"XXXXXX", "     X", "    X ", "   X  ", "  X   ", "  X   ", "  X   ", "      "}},
{'8', {" XXXX ", "X    X", "X    X", " XXXX ", "X    X", "X    X", " XXXX ", "      "}},
{'9', {" XXXX ", "X    X", "X    X", " XXXXX", "     X", "     X", " XXXX ", "      "}},
{'.', {"      ", "      ", "      ", "      ", "      ", "  XX  ", "  XX  ", "      "}},
{',', {"      ", "      ", "      ", "      ", "  XX  ", "  XX  ", "  X   ", " X    "}},
{'!', {"  XX  ", "  XX  ", "  XX  ", "  XX  ", "  XX  ", "      ", "  XX  ", "      "}},
{'?', {" XXXX ", "X    X", "    X ", "   X  ", "  X   ", "      ", "  X   ", "      "}},
{'\'', {"  XX  ", "  XX  ", "  X   ", " X    ", "      ", "      ", "      ", "      "}},
{'-', {"      ", "      ", "      ", "XXXXXX", "      ", "      ", "      ", "      "}},
{':', {"      ", "  XX  ", "  XX  ", "      ", "  XX  ", "  XX  ", "      ", "      "}},
{'+', {"      ", "  X   ", "  X   ", "XXXXX ", "  X   ", "  X   ", "      ", "      "}},
{'/', {"     X", "    X ", "   X  ", "  X   ", " X    ", "X     ", "      ", "      "}},
};

const Glyph *findGlyph(QChar c)
{
    const QChar up = c.toUpper();
    for (const Glyph &g : kFont)
        if (g.ch == up)
            return &g;
    return nullptr;   // space / unknown -> blank
}

// Blank columns before and after the message so it enters and leaves cleanly.
constexpr int kLeadCols = kVisibleCols;    // one screen-width of blank lead-in
constexpr int kTailCols = kVisibleCols;

}   // namespace

bool isRenderable(QChar c)
{
    return c == ' ' || findGlyph(c) != nullptr;
}

int stripColumns(const QString &message)
{
    const int textPx = message.length() * kFontW;
    const int totalPx = (kLeadCols + kTailCols) * 16 + textPx;
    return (totalPx + 15) / 16;   // round up to whole 16 px columns
}

QString generate(const QString &message, int speed, int vscale)
{
    if (speed < 1) speed = 1;
    if (speed > 8) speed = 8;      // addq #n limit; keeps the wrap logic simple
    if (vscale < 1) vscale = 1;
    const int bandH = kFontH * vscale;

    // 1. Rasterise the message into a plane0 pixel bitmap, `bandH` rows tall.
    const int textPx = message.length() * kFontW;
    const int leadPx = kLeadCols * 16;
    const int totalPx = leadPx + textPx + kTailCols * 16;
    const int cols = (totalPx + 15) / 16;         // 16 px columns
    const int wpx = cols * 16;                    // padded pixel width

    // bit[y][x] set == foreground pixel.
    QVector<QVector<bool>> bits(bandH, QVector<bool>(wpx, false));
    for (int i = 0; i < message.length(); ++i) {
        const Glyph *g = findGlyph(message[i]);
        if (!g)
            continue;   // space / unknown
        const int x0 = leadPx + i * kFontW;
        for (int gy = 0; gy < kFontH; ++gy) {
            const char *row = g->rows[gy];
            for (int gx = 0; gx < kFontW && row[gx]; ++gx) {
                if (row[gx] != 'X')
                    continue;
                for (int sy = 0; sy < vscale; ++sy)
                    bits[gy * vscale + sy][x0 + gx] = true;
            }
        }
    }

    // 2. Slice into 16 px columns, column-major (all band rows of col 0, then
    //    col 1, ...) so the scroll engine reads strip[(srccol+c)*bandH + r].
    QStringList data;
    for (int c = 0; c < cols; ++c) {
        QStringList rowWords;
        for (int r = 0; r < bandH; ++r) {
            quint16 w = 0;
            for (int px = 0; px < 16; ++px)
                if (bits[r][c * 16 + px])
                    w |= (0x8000 >> px);
            rowWords << QStringLiteral("$%1").arg(w, 4, 16, QLatin1Char('0'));
        }
        // one dc.w line per column (bandH words) keeps the source readable
        for (int i = 0; i < rowWords.size(); i += 8) {
            QStringList chunk = rowWords.mid(i, 8);
            data << QStringLiteral("\tdc.w\t") + chunk.join(QLatin1Char(','));
        }
    }
    const QString strip = data.join(QLatin1Char('\n'));

    // 3. Emit the scroll engine. SCRW = 21 cols (20 visible + 1 incoming);
    //    stride = 168 bytes; band top centres the text vertically.
    const int bandTop = (200 - bandH) / 2;
    return QStringLiteral(
        "; scroller.s — Talos Phase 4 generated STE hardware scroller (F-212).\n"
        "; Do not hand-edit. STE/Mega STE only ($ff8265 prefetch fine scroll).\n"
        "VIDBASEH equ $ffff8201\n"
        "VIDBASEM equ $ffff8203\n"
        "VIDBASEL equ $ffff820d\n"
        "LINEWID  equ $ffff820f\n"
        "HSCROLL5 equ $ffff8265\n"
        "PAL0     equ $ffff8240\n"
        "IERA     equ $fffffa07\n"
        "IERB     equ $fffffa09\n"
        "SCRW     equ 21\n"
        "; Display stride: 160 visible + 8 prefetch (shifter reads a column early\n"
        "; when $ff8265 scroll is active) + LineWidth*2. This is what the shifter\n"
        "; actually advances per scanline, so the band must be drawn at this stride.\n"
        "STRIDE   equ 160+8+(SCRW-20)*8\n"
        "BANDH    equ %1\n"
        "BANDTOP  equ %2\n"
        "SRCCOLS  equ %3\n"
        "    text\n"
        "start:\n"
        "    clr.l   -(sp)\n"
        "    move.w  #$20,-(sp)\n"
        "    trap    #1\n"
        "    addq.l  #6,sp\n"
        "    move.b  #0,IERA              ; MFP off -> only VBL fires, jitter-free\n"
        "    move.b  #0,IERB\n"
        "    move.w  #$004,PAL0           ; colour 0 = background (blue)\n"
        "    move.w  #$770,PAL0+2         ; colour 1 = text (yellow)\n"
        "; Own screen buffer: the widened stride needs 200*STRIDE bytes (> the 32000\n"
        "; TOS screen), so allocate our own and 256-align it, then point the base at it.\n"
        "    lea     screenbuf,a0\n"
        "    move.l  a0,d0\n"
        "    add.l   #255,d0\n"
        "    and.l   #$ffffff00,d0        ; 256-byte align (STE base is byte-granular)\n"
        "    move.l  d0,screen\n"
        "    move.l  d0,$44e.w            ; logbase -> our buffer (drawband uses $44e)\n"
        "    move.b  d0,VIDBASEL          ; program the shifter base (fixed all run)\n"
        "    lsr.l   #8,d0\n"
        "    move.b  d0,VIDBASEM\n"
        "    lsr.l   #8,d0\n"
        "    move.b  d0,VIDBASEH\n"
        "    move.l  screen,a0            ; clear the buffer to background\n"
        "    move.w  #STRIDE/2*200-1,d0\n"
        "    moveq   #0,d1\n"
        ".clr:\n"
        "    move.w  d1,(a0)+\n"
        "    dbf     d0,.clr\n"
        "    move.b  #(SCRW-20)*4,LINEWID ; widen scanline by one 16px column\n"
        "    move.w  #0,srccol\n"
        "    move.w  #0,fine\n"
        "    bsr     drawband             ; prime the band with the first SCRW cols\n"
        "    move.l  #vbl,$70.w\n"
        "main:\n"
        "    stop    #$2300               ; wait for VBL\n"
        "    bra.s   main\n"
        "\n"
        "; Draw SCRW columns of the band from strip[srccol..], column by column so\n"
        "; the strip offset is computed once per column (not per cell) -> the whole\n"
        "; redraw fits inside vblank (no tear at the wrap). strip is column-major:\n"
        "; the BANDH words of column k are contiguous at strip + k*BANDH*2.\n"
        "drawband:\n"
        "    move.l  screen,a1\n"
        "    lea     STRIDE*BANDTOP(a1),a1  ; a1 -> band top-left\n"
        "    move.w  srccol,d4\n"
        "    move.w  #SCRW-1,d3\n"
        ".dc:\n"
        "    move.w  d4,d5                ; strip column (srccol + c)\n"
        "    mulu    #BANDH*2,d5          ; -> byte offset of the column\n"
        "    lea     strip,a2\n"
        "    add.l   d5,a2                ; a2 -> column's first word\n"
        "    move.l  a1,a0                ; a0 -> band top of this screen column\n"
        "    move.w  #BANDH-1,d2\n"
        ".dr:\n"
        "    move.w  (a2)+,(a0)          ; plane0 glyph bits for this scanline\n"
        "    clr.w   2(a0)               ; planes 1-3 clear (2-colour)\n"
        "    clr.w   4(a0)\n"
        "    clr.w   6(a0)\n"
        "    lea     STRIDE(a0),a0       ; next scanline\n"
        "    dbf     d2,.dr\n"
        "    lea     8(a1),a1            ; next screen column (+16px)\n"
        "    addq.w  #1,d4\n"
        "    dbf     d3,.dc\n"
        "    rts\n"
        "\n"
        "vbl:\n"
        "    move.w  #$2700,sr\n"
        "    movem.l d0-d7/a0-a2,-(sp)\n"
        "    move.w  fine,d0\n"
        "    move.b  d0,HSCROLL5          ; STE prefetch fine scroll (0..15)\n"
        "    add.w   #%4,d0               ; advance by speed px\n"
        "    cmp.w   #16,d0\n"
        "    blt.s   .keep\n"
        "    sub.w   #16,d0               ; wrapped a 16px column\n"
        "    addq.w  #1,srccol            ; advance the source strip one column\n"
        "    cmp.w   #SRCCOLS-SCRW,srccol\n"
        "    blt.s   .redraw\n"
        "    move.w  #0,srccol            ; loop the message\n"
        ".redraw:\n"
        "    bsr     drawband             ; hand off: band shifted one column\n"
        ".keep:\n"
        "    move.w  d0,fine\n"
        "    movem.l (sp)+,d0-d7/a0-a2\n"
        "    rte\n"
        "\n"
        "    even\n"
        "fine:   ds.w 1\n"
        "srccol: ds.w 1\n"
        "screen: ds.l 1                   ; 256-aligned screen base\n"
        "    even\n"
        "strip:\n"
        "%5\n"
        "    even\n"
        "screenbuf: ds.b 200*STRIDE+256   ; +256 for the alignment slack\n")
        .arg(bandH)
        .arg(bandTop)
        .arg(cols)
        .arg(speed)
        .arg(strip);
}

}   // namespace ScrollerCodegen
