#include "AbCompareView.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QComboBox *machineCombo(QWidget *parent, MachineType initial)
{
    auto *box = new QComboBox(parent);
    for (MachineType t : Machines::all())
        box->addItem(Machines::info(t).name, static_cast<int>(t));
    box->setCurrentIndex(Machines::all().indexOf(initial));
    return box;
}
}   // namespace

AbCompareView::AbCompareView(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    auto *row = new QHBoxLayout;
    m_a = machineCombo(this, MachineType::ST);
    m_b = machineCombo(this, MachineType::STE);
    row->addWidget(new QLabel(QStringLiteral("A:"), this));
    row->addWidget(m_a);
    row->addWidget(new QLabel(QStringLiteral("vs  B:"), this));
    row->addWidget(m_b);
    m_compare = new QPushButton(QStringLiteral("Compare"), this);
    m_compare->setToolTip(QStringLiteral(
        "Run the last-built effect on both machines (headless) and diff the frames"));
    connect(m_compare, &QPushButton::clicked, this,
            [this] { emit compareRequested(machineAt(m_a), machineAt(m_b)); });
    row->addWidget(m_compare);
    row->addStretch();
    lay->addLayout(row);

    // The paint area fills the rest (drawn in paintEvent via a child-free widget).
    lay->addStretch(1);

    m_status = new QLabel(
        QStringLiteral("Build or load an effect, then Compare two machines to see "
                       "where they diverge."),
        this);
    m_status->setWordWrap(true);
    lay->addWidget(m_status);
}

MachineType AbCompareView::machineAt(QComboBox *box) const
{
    return static_cast<MachineType>(box->currentData().toInt());
}

void AbCompareView::setFrames(const QImage &a, const QImage &b, const QString &labelA,
                              const QString &labelB)
{
    // Normalise to a known 32-bit layout so the per-pixel compare is valid
    // regardless of how the PNGs were loaded (indexed, RGB888, …).
    m_frameA = a.isNull() ? a : a.convertToFormat(QImage::Format_RGB32);
    m_frameB = b.isNull() ? b : b.convertToFormat(QImage::Format_RGB32);
    m_labelA = labelA;
    m_labelB = labelB;
    m_rowDiff.clear();
    m_diffRows = 0;

    if (!m_frameA.isNull() && !m_frameB.isNull() && m_frameA.size() == m_frameB.size()) {
        const int h = m_frameA.height(), w = m_frameA.width();
        m_rowDiff.resize(h);
        for (int y = 0; y < h; ++y) {
            const QRgb *ra = reinterpret_cast<const QRgb *>(m_frameA.constScanLine(y));
            const QRgb *rb = reinterpret_cast<const QRgb *>(m_frameB.constScanLine(y));
            bool diff = false;
            for (int x = 0; x < w && !diff; ++x)
                diff = (ra[x] | 0xff000000u) != (rb[x] | 0xff000000u);
            m_rowDiff[y] = diff;
            if (diff)
                ++m_diffRows;
        }
        const int total = h;
        setStatus(m_diffRows == 0
                      ? QStringLiteral("Identical — %1 and %2 render this effect the same.")
                            .arg(labelA, labelB)
                      : QStringLiteral("%1/%2 scanlines differ between %3 and %4 (red strip).")
                            .arg(m_diffRows).arg(total).arg(labelA, labelB),
                  true);
    } else if (!m_frameA.isNull() && !m_frameB.isNull()) {
        setStatus(QStringLiteral("Frames differ in size (%1×%2 vs %3×%4).")
                      .arg(m_frameA.width()).arg(m_frameA.height())
                      .arg(m_frameB.width()).arg(m_frameB.height()),
                  true);
    }
    update();
}

void AbCompareView::setBusy(bool busy)
{
    m_compare->setEnabled(!busy);
    m_a->setEnabled(!busy);
    m_b->setEnabled(!busy);
    if (busy)
        setStatus(QStringLiteral("Capturing both machines (headless, ~30 s)…"), true);
}

void AbCompareView::setStatus(const QString &text, bool ok)
{
    m_status->setStyleSheet(ok ? QString() : QStringLiteral("color:#c62828;"));
    m_status->setText(text);
}

void AbCompareView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 28));
    if (m_frameA.isNull() || m_frameB.isNull())
        return;

    // Layout: [ frame A ] [ frame B ] [ diff strip ], below the controls row.
    const int top = 44, pad = 8, hdr = 14, strip = 14, gap = 8;
    const int availH = height() - top - pad - hdr;
    const int availW = width() - 3 * pad - gap - strip - gap;
    if (availH < 20 || availW < 40)
        return;
    const int colW = availW / 2;

    auto blit = [&](int x, const QImage &img, const QString &label) -> QRect {
        const QImage s = img.scaled(colW, availH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int ix = x + (colW - s.width()) / 2;
        p.setPen(QColor(180, 180, 190));
        p.drawText(QRect(x, top, colW, hdr), Qt::AlignHCenter | Qt::AlignVCenter, label);
        p.drawImage(ix, top + hdr, s);
        return QRect(ix, top + hdr, s.width(), s.height());
    };

    const QRect ra = blit(pad, m_frameA, m_labelA);
    blit(pad + colW + gap, m_frameB, m_labelB);

    // Diff strip: one column, aligned to the frames' drawn height; red where the
    // corresponding source scanline differs.
    const int sx = pad + 2 * colW + 2 * gap;
    const int sy = ra.y(), sh = ra.height();
    p.fillRect(QRect(sx, sy, strip, sh), QColor(34, 34, 40));
    p.setPen(QColor(180, 180, 190));
    p.drawText(QRect(sx - 4, top, strip + 8, hdr), Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("Δ"));
    if (!m_rowDiff.isEmpty() && sh > 0) {
        for (int dy = 0; dy < sh; ++dy) {
            const int srcRow = int(qint64(dy) * m_rowDiff.size() / sh);
            if (srcRow < m_rowDiff.size() && m_rowDiff[srcRow]) {
                p.setPen(QColor(230, 80, 70));
                p.drawLine(sx, sy + dy, sx + strip - 1, sy + dy);
            }
        }
    }
}
