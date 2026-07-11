#include "BlitterTrafficView.h"

#include <QPainter>
#include <QPaintEvent>

BlitterTrafficView::BlitterTrafficView(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void BlitterTrafficView::setOps(const QVector<BlitOp> &ops)
{
    m_ops = ops;
    m_reads = m_writes = m_blits = 0;
    m_cycMin = m_cycMax = 0;
    m_rMin = m_wMin = 0xffffffffu;
    m_rMax = m_wMax = 0;

    bool firstCyc = true;
    for (const auto &op : m_ops) {
        if (op.endCycle != 0)
            ++m_blits;
        for (const auto &a : op.accesses) {
            if (firstCyc) {
                m_cycMin = m_cycMax = a.cycle;
                firstCyc = false;
            } else {
                m_cycMin = qMin(m_cycMin, a.cycle);
                m_cycMax = qMax(m_cycMax, a.cycle);
            }
            // Reads and writes land in far-apart memory (source vs screen), so
            // track their address extents separately and give each its own lane.
            if (a.isWrite) {
                ++m_writes;
                m_wMin = qMin(m_wMin, a.addr);
                m_wMax = qMax(m_wMax, a.addr);
            } else {
                ++m_reads;
                m_rMin = qMin(m_rMin, a.addr);
                m_rMax = qMax(m_rMax, a.addr);
            }
        }
    }
    update();
}

void BlitterTrafficView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().base());

    const QColor ink = palette().text().color();
    const QColor faint = palette().mid().color();
    const QColor readCol(80, 150, 240);
    const QColor writeCol(240, 150, 60);

    const int pad = 8;
    const int headerH = 22;

    const int totalAcc = m_reads + m_writes;
    if (m_ops.isEmpty() || totalAcc == 0) {
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("no blitter traffic captured\n"
                                  "enable the trace, run an effect, then capture"));
        return;
    }

    // Header: counts + legend.
    p.setPen(ink);
    const QString hdr = QStringLiteral("%1 blits · %2 reads · %3 writes · %4 accesses")
                            .arg(m_blits).arg(m_reads).arg(m_writes).arg(totalAcc);
    p.drawText(pad, pad, width() - 2 * pad, headerH, Qt::AlignLeft | Qt::AlignVCenter, hdr);
    int lx = width() - pad - 140;
    p.fillRect(lx, pad + 4, 10, 10, readCol);
    p.drawText(lx + 14, pad, 40, headerH, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("read"));
    p.fillRect(lx + 60, pad + 4, 10, 10, writeCol);
    p.drawText(lx + 74, pad, 50, headerH, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("write"));

    // Plot area, split into a read lane (top) and a write lane (bottom): reads
    // hit the source buffer and writes hit screen memory, hundreds of KB apart,
    // so a single shared address axis would collapse each into a sliver. Each
    // lane is scaled to its own address extent.
    const QRect plot(pad, pad + headerH + 4,
                     width() - 2 * pad, height() - (pad + headerH + 4) - pad);
    const int midGap = 12;
    const int laneH = (plot.height() - midGap) / 2;
    const QRect rLane(plot.left(), plot.top(), plot.width(), laneH);
    const QRect wLane(plot.left(), plot.top() + laneH + midGap, plot.width(),
                      plot.height() - laneH - midGap);

    p.setPen(faint);
    p.drawRect(rLane.adjusted(0, 0, -1, -1));
    p.drawRect(wLane.adjusted(0, 0, -1, -1));

    const double cycSpan = double(m_cycMax - m_cycMin);
    auto xOf = [&](quint64 cyc, int idx, int n) -> double {
        if (cycSpan > 0) return plot.left() + (double(cyc - m_cycMin) / cycSpan) * (plot.width() - 1);
        return plot.left() + (n > 1 ? double(idx) / (n - 1) : 0.0) * (plot.width() - 1);
    };
    auto yIn = [](const QRect &lane, quint32 addr, quint32 lo, quint32 hi) -> double {
        if (hi > lo) return lane.top() + (double(addr - lo) / double(hi - lo)) * (lane.height() - 1);
        return lane.top() + lane.height() / 2.0;
    };

    // Blit boundaries: faint vertical rules spanning both lanes.
    p.setPen(QPen(faint, 1, Qt::DotLine));
    for (const auto &op : m_ops) {
        if (op.endCycle == 0) continue;
        const int x = int(xOf(op.endCycle, 0, 1));
        p.drawLine(x, plot.top(), x, plot.bottom());
    }

    // Access marks: each in its lane, coloured by kind.
    p.setClipRect(plot);
    int idx = 0;
    for (const auto &op : m_ops) {
        for (const auto &a : op.accesses) {
            const double x = xOf(a.cycle, idx, totalAcc);
            const double y = a.isWrite ? yIn(wLane, a.addr, m_wMin, m_wMax)
                                       : yIn(rLane, a.addr, m_rMin, m_rMax);
            p.fillRect(QRectF(x - 1.0, y - 1.0, 2.4, 2.4), a.isWrite ? writeCol : readCol);
            ++idx;
        }
    }
    p.setClipping(false);

    // Lane labels: kind + address extent.
    p.setPen(faint);
    if (m_reads) {
        p.drawText(rLane.left() + 4, rLane.top() + 2, rLane.width() - 8, 14,
                   Qt::AlignLeft | Qt::AlignTop,
                   QStringLiteral("read  $%1–$%2")
                       .arg(m_rMin, 0, 16).arg(m_rMax, 0, 16));
    }
    if (m_writes) {
        p.drawText(wLane.left() + 4, wLane.top() + 2, wLane.width() - 8, 14,
                   Qt::AlignLeft | Qt::AlignTop,
                   QStringLiteral("write $%1–$%2")
                       .arg(m_wMin, 0, 16).arg(m_wMax, 0, 16));
    }
}
