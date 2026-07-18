#include "ScanlineBudgetView.h"

#include <algorithm>

#include <QPainter>

ScanlineBudgetView::ScanlineBudgetView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(88);
}

void ScanlineBudgetView::setModel(const Model &m)
{
    m_model = m;
    update();
}

void ScanlineBudgetView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 28));
    if (!m_model.valid) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("Author a raster / bands effect to see its scanline budget"));
        return;
    }

    const int margin = 10;
    const int w = width() - 2 * margin;
    const int cpl = m_model.cyclesPerLine;
    // The axis spans one line at 8 MHz, or two (the 16 MHz budget) on a Mega STE.
    const int axisMax = m_model.dualSpeed ? cpl * 2 : cpl;
    auto cx = [&](int cyc) { return margin + double(cyc) * w / axisMax; };

    const int barY = 30, barH = 26;

    // 16 MHz extra budget (the second half of the axis), lightly shaded.
    if (m_model.dualSpeed) {
        p.fillRect(QRectF(cx(cpl), barY, cx(axisMax) - cx(cpl), barH), QColor(40, 46, 60));
    }
    // The 8 MHz line: border/blanking vs the visible display window.
    p.fillRect(QRectF(cx(0), barY, cx(cpl) - cx(0), barH), QColor(38, 38, 46));
    p.fillRect(QRectF(cx(m_model.visibleStart), barY,
                      cx(m_model.visibleEnd) - cx(m_model.visibleStart), barH),
               QColor(48, 60, 80));
    p.setPen(QColor(80, 80, 90));
    p.drawRect(QRectF(cx(0), barY, cx(axisMax) - cx(0) - 1, barH));

    // Budget boundary lines: 512 (8 MHz), and 1024 (16 MHz) on a Mega STE.
    auto boundary = [&](int cyc, const QString &label, const QColor &col) {
        p.setPen(QPen(col, 1, Qt::DashLine));
        p.drawLine(int(cx(cyc)), barY - 6, int(cx(cyc)), barY + barH + 6);
        p.setPen(col);
        p.drawText(int(cx(cyc)) - 40, barY - 8, 80, 12, Qt::AlignCenter, label);
    };
    boundary(cpl, QStringLiteral("%1 · 8 MHz").arg(cpl), QColor(210, 170, 90));
    if (m_model.dualSpeed)
        boundary(axisMax, QStringLiteral("%1 · 16 MHz").arg(axisMax), QColor(120, 200, 120));

    // The effect's per-line writes on the line (green in budget, red past visible).
    for (int c : m_model.writeCycles) {
        const bool over = c > m_model.visibleEnd;
        p.setPen(QPen(over ? QColor(230, 90, 80) : QColor(120, 210, 130), 2));
        p.drawLine(int(cx(c)), barY - 3, int(cx(c)), barY + barH + 3);
    }

    // "visible" label under the window.
    p.setPen(Qt::gray);
    p.drawText(QRectF(cx(m_model.visibleStart), barY + barH + 6,
                      cx(m_model.visibleEnd) - cx(m_model.visibleStart), 12),
               Qt::AlignCenter, QStringLiteral("visible display"));

    p.setPen(Qt::lightGray);
    p.drawText(margin, height() - 8, m_model.note);
}
