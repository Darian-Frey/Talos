#include "ReconstructView.h"

#include <algorithm>

#include <QPainter>

#include "model/Palette.h"

ReconstructView::ReconstructView(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(180);
}

void ReconstructView::setFrame(const QImage &frame)
{
    m_frame = frame;
    rebuild();   // reconstruction is sized to the frame
    update();
}

void ReconstructView::setReconstruction(const QVector<WriteEvent> &writes,
                                        VideoRegion region, quint32 address)
{
    m_writes = writes;
    m_region = region;
    m_address = address;
    m_paletteReg = (address >= 0xff8240 && address <= 0xff825e);
    rebuild();
    update();
}

void ReconstructView::rebuild()
{
    // Only palette-register captures reconstruct a colour field, and we need a
    // frame to size the reconstruction to.
    if (!m_paletteReg || m_writes.isEmpty() || m_frame.isNull()) {
        m_recon = QImage();
        m_sig.clear();
        return;
    }

    // Skip redundant rebuilds (setFrame fires every live grab; the field only
    // changes when the writes / region / frame size do).
    const WriteEvent &f0 = m_writes.first();
    const WriteEvent &fn = m_writes.last();
    const QString sig = QStringLiteral("%1x%2|%3|%4|%5|%6|%7")
                            .arg(m_frame.width()).arg(m_frame.height())
                            .arg(int(m_region)).arg(m_address)
                            .arg(m_writes.size()).arg(f0.frameCycle).arg(fn.value);
    if (sig == m_sig && !m_recon.isNull())
        return;
    m_sig = sig;

    const QSize size = m_frame.size();
    const BeamGeometry geo(m_region, size);
    const int cpl = geo.cyclesPerLine();

    // Fold the writes onto one frame, sorted by beam key (scanline, then cycle).
    struct KV { int key; QRgb rgb; };
    QVector<KV> ev;
    ev.reserve(m_writes.size());
    for (const WriteEvent &w : m_writes)
        ev.append({w.scanline * cpl + w.cycleInLine,
                   Palette::decode(static_cast<quint16>(w.value)).rgb()});
    std::sort(ev.begin(), ev.end(), [](const KV &a, const KV &b) { return a.key < b.key; });

    // The colour before the first write wraps to the last write's value (a looping
    // per-frame effect holds its final colour into the top of the next frame).
    const QRgb wrapRgb = ev.last().rgb;

    m_recon = QImage(size, QImage::Format_RGB32);
    const int w = size.width(), h = size.height();
    for (int y = 0; y < h; ++y) {
        const int sl = geo.scanlineAtY(y);
        QRgb *line = reinterpret_cast<QRgb *>(m_recon.scanLine(y));
        // Walk the events across the row (key rises monotonically with x).
        int idx = 0;
        for (int x = 0; x < w; ++x) {
            const int key = sl * cpl + geo.cycleAtX(x);
            while (idx < ev.size() && ev[idx].key <= key)
                ++idx;
            line[x] = (idx == 0) ? wrapRgb : ev[idx - 1].rgb;
        }
    }
}

void ReconstructView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 28));

    const int gap = 12, hdr = 16, pad = 8, cap = 16;
    const int colW = (width() - 3 * pad - gap) / 2;
    const int imgY = pad + hdr;
    const int imgH = height() - imgY - pad - cap;
    const QRect left(pad, imgY, colW, imgH);
    const QRect right(pad * 2 + colW + gap, imgY, colW, imgH);

    p.setPen(QColor(180, 180, 190));
    p.drawText(QRect(left.x(), pad, left.width(), hdr), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Hatari (taken framebuffer)"));
    p.drawText(QRect(right.x(), pad, right.width(), hdr), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Reconstructed from $%1 writes").arg(m_address, 0, 16));

    auto blit = [&](const QRect &cell, const QImage &img) {
        p.setPen(QColor(70, 70, 80));
        p.drawRect(cell.adjusted(0, 0, -1, -1));
        if (img.isNull())
            return false;
        const QImage s = img.scaled(cell.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage(cell.x() + (cell.width() - s.width()) / 2,
                    cell.y() + (cell.height() - s.height()) / 2, s);
        return true;
    };

    blit(left, m_frame);
    if (!blit(right, m_recon)) {
        p.setPen(Qt::gray);
        const QString msg = m_writes.isEmpty()
            ? QStringLiteral("Capture writes to a palette register\n($ff8240 = background) to reconstruct")
            : (!m_paletteReg
                   ? QStringLiteral("Reconstruction is shown for palette-register\ncaptures ($ff8240–$ff825e)")
                   : QStringLiteral("Grab a frame to size the reconstruction"));
        p.drawText(right, Qt::AlignCenter, msg);
    }

    p.setPen(QColor(140, 140, 150));
    p.drawText(QRect(pad, height() - cap, width() - 2 * pad, cap),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Secondary teaching view — the register field, never a replacement "
                              "for the taken frame (D-007)."));
}
