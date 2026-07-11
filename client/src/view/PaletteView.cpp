#include "PaletteView.h"

#include "model/Palette.h"

#include <QPainter>
#include <QPaintEvent>

PaletteView::PaletteView(QWidget *parent)
    : QWidget(parent)
{
}

void PaletteView::setPalette(const QVector<QColor> &colours,
                             const QVector<quint16> &regs, const QString &machineName,
                             int colourCount, int bitsPerGun)
{
    m_colours = colours;
    m_regs = regs;
    m_machine = machineName;
    m_colourCount = colourCount;
    m_bits = bitsPerGun;
    update();
}

void PaletteView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const int pad = 8;
    int y = pad;

    // Header: what colour capability this machine has.
    p.setPen(palette().color(QPalette::WindowText));
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    p.drawText(pad, y + 12,
               QStringLiteral("%1 · %2 colours · %3 bits/gun")
                   .arg(m_machine).arg(m_colourCount).arg(m_bits));
    p.setFont(font());
    y += 24;

    // 16 palette swatches (8 x 2), the live colour registers.
    if (!m_colours.isEmpty()) {
        const int cols = 8;
        const double sw = double(width() - 2 * pad) / cols;
        const double sh = 22;
        QFont small = font();
        small.setPointSizeF(font().pointSizeF() - 2.0);
        for (int i = 0; i < m_colours.size() && i < 16; ++i) {
            const int cx = i % cols, cy = i / cols;
            const QRectF r(pad + cx * sw, y + cy * (sh + 14), sw - 2, sh);
            p.fillRect(r, m_colours[i]);
            p.setPen(QColor(0, 0, 0, 60));
            p.drawRect(r);
            p.setFont(small);
            p.setPen(palette().color(QPalette::WindowText));
            const QString hex = i < m_regs.size()
                                    ? QStringLiteral("%1").arg(m_regs[i], 3, 16, QChar('0'))
                                    : QString();
            p.drawText(QRectF(r.left(), r.bottom(), r.width(), 13),
                       Qt::AlignHCenter, hex);
            p.setFont(font());
        }
        y += 2 * (int(sh) + 14) + 6;
    }

    // Per-gun intensity ramp at the machine's resolution: the differential.
    const int levels = (m_bits >= 4) ? 16 : 8;
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(pad, y + 11,
               QStringLiteral("intensity ramp (%1 levels/gun):").arg(levels));
    y += 18;
    const char *labels[] = {"R", "G", "B"};
    const double bw = double(width() - 2 * pad - 14) / levels;
    for (int gun = 0; gun < 3; ++gun) {
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(pad, y + 12, QString::fromLatin1(labels[gun]));
        for (int i = 0; i < levels; ++i) {
            const int nibble = (m_bits >= 4) ? i : (i << 1);   // ST: even nibbles only
            const int v = Palette::gun(nibble);
            const QColor c = gun == 0 ? QColor(v, 0, 0)
                             : gun == 1 ? QColor(0, v, 0)
                                        : QColor(0, 0, v);
            p.fillRect(QRectF(pad + 14 + i * bw, y, bw, 14), c);
        }
        y += 16;
    }
}
