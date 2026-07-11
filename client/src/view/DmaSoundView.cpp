#include "DmaSoundView.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>

namespace {
const char *kFreq[4] = {"6.25 kHz", "12.5 kHz", "25 kHz", "50 kHz"};
}   // namespace

DmaSoundView::DmaSoundView(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void DmaSoundView::setTrace(const DmaSndTrace &trace)
{
    m_t = trace;
    update();
}

void DmaSoundView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().base());

    const bool haveDrain = !m_t.drain.isEmpty() && m_t.bufEnd > m_t.bufStart;
    if (!haveDrain && !m_t.haveCtrl && m_t.lmcSeq.isEmpty()) {
        p.setPen(palette().text().color());
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("no DMA sound captured\n"
                                  "run a DMA-sound effect, then capture"));
        return;
    }

    // Split: drain plot on top (~60%), EQ curve below.
    const int pad = 8;
    const int gap = 10;
    const int drainH = (height() - 2 * pad - gap) * 3 / 5;
    const QRect drainR(pad, pad, width() - 2 * pad, drainH);
    const QRect eqR(pad, drainR.bottom() + gap, width() - 2 * pad,
                    height() - drainR.bottom() - gap - pad);
    paintDrain(p, drainR);
    paintEq(p, eqR);
}

void DmaSoundView::paintDrain(QPainter &p, const QRect &r)
{
    const QColor ink = palette().text().color();
    const QColor faint = palette().mid().color();
    const QColor headCol(90, 200, 130);

    // Header line: control state + buffer bounds.
    QString hdr;
    if (m_t.haveCtrl) {
        hdr = m_t.playing() ? QStringLiteral("playing") : QStringLiteral("stopped");
        if (m_t.looping()) hdr += QStringLiteral(" · loop");
        hdr += QStringLiteral(" · %1 · %2")
                   .arg(kFreq[m_t.freqIndex()], m_t.mono() ? QStringLiteral("mono")
                                                           : QStringLiteral("stereo"));
    }
    if (m_t.bufEnd > m_t.bufStart) {
        hdr += QStringLiteral("   buffer $%1–$%2 (%3 bytes)")
                   .arg(m_t.bufStart, 0, 16).arg(m_t.bufEnd, 0, 16)
                   .arg(m_t.bufEnd - m_t.bufStart);
    }
    p.setPen(ink);
    p.drawText(r.left(), r.top(), r.width(), 16, Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("DMA drain — ") + hdr);

    const QRect plot(r.left(), r.top() + 20, r.width(), r.height() - 20);
    p.setPen(faint);
    p.drawRect(plot.adjusted(0, 0, -1, -1));

    if (m_t.drain.isEmpty() || m_t.bufEnd <= m_t.bufStart)
        return;

    const double cycSpan = double(m_t.cycMax - m_t.cycMin);
    const double addrSpan = double(m_t.bufEnd - m_t.bufStart);
    auto xOf = [&](quint64 c) {
        if (cycSpan <= 0) return double(plot.left());
        return plot.left() + (double(c - m_t.cycMin) / cycSpan) * (plot.width() - 1);
    };
    auto yOf = [&](quint32 addr) {
        // start at the bottom, end at the top: the head rises as it plays.
        const double f = qBound(0.0, double(addr - m_t.bufStart) / addrSpan, 1.0);
        return plot.bottom() - f * (plot.height() - 1);
    };

    p.setClipRect(plot);
    for (const auto &d : m_t.drain)
        p.fillRect(QRectF(xOf(d.cycle) - 1.0, yOf(d.addr) - 1.0, 2.2, 2.2), headCol);
    p.setClipping(false);
}

void DmaSoundView::paintEq(QPainter &p, const QRect &r)
{
    const QColor ink = palette().text().color();
    const QColor faint = palette().mid().color();
    const QColor curveCol(230, 170, 70);

    const int bass = m_t.lmc[1];
    const int treble = m_t.lmc[2];
    const int master = m_t.lmc[3];

    QString label = QStringLiteral("LMC1992 EQ — ");
    label += (bass >= 0) ? QStringLiteral("bass %1").arg(bass) : QStringLiteral("bass –");
    label += (treble >= 0) ? QStringLiteral(" · treble %1").arg(treble)
                           : QStringLiteral(" · treble –");
    label += (master >= 0) ? QStringLiteral(" · master %1/63").arg(master)
                           : QStringLiteral(" · master –");
    p.setPen(ink);
    p.drawText(r.left(), r.top(), r.width(), 16, Qt::AlignLeft | Qt::AlignVCenter, label);

    const QRect plot(r.left(), r.top() + 20, r.width(), r.height() - 20);
    p.setPen(faint);
    p.drawRect(plot.adjusted(0, 0, -1, -1));

    // Zero-gain centre line + low/high axis labels.
    const double midY = plot.center().y();
    p.setPen(QPen(faint, 1, Qt::DashLine));
    p.drawLine(plot.left(), int(midY), plot.right(), int(midY));
    p.setPen(faint);
    p.drawText(plot.left() + 4, plot.bottom() - 14, 60, 12, Qt::AlignLeft, QStringLiteral("low"));
    p.drawText(plot.right() - 44, plot.bottom() - 14, 40, 12, Qt::AlignRight,
               QStringLiteral("high"));

    if (bass < 0 && treble < 0)
        return;

    // Tone curve: bass sets the low-frequency end, treble the high-frequency end,
    // centre stays flat. Index 6 ≈ flat (0), each step is a tone gain change.
    auto gainY = [&](int idx) {
        const double g = (idx < 0) ? 0.0 : (idx - 6) / 9.0;   // ~[-0.67, +1.0]
        return midY - g * (plot.height() * 0.42);
    };
    const double xL = plot.left() + 2, xM = plot.center().x(), xR = plot.right() - 2;
    QPainterPath path;
    path.moveTo(xL, gainY(bass));
    path.quadTo(xM, midY, xR, gainY(treble));
    p.setRenderHint(QPainter::Antialiasing, true);   // smooth the shallow diagonal
    p.setPen(QPen(curveCol, 2));
    p.drawPath(path);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Master volume as a thin fill bar along the bottom.
    if (master >= 0) {
        const double frac = master / 63.0;
        QRect vol(plot.left(), plot.bottom() - 4, int(plot.width() * frac), 3);
        p.fillRect(vol, curveCol);
    }
}
