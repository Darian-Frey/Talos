#include "SyncScrollView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

// -------------------------------------------------------- SyncScrollDiagram
SyncScrollDiagram::SyncScrollDiagram(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(360, 200);
}

void SyncScrollDiagram::setStep(const SyncScroll::Step &s)
{
    m_step = s;
    update();
}

void SyncScrollDiagram::paintEvent(QPaintEvent *)
{
    using namespace SyncScroll;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(20, 20, 24));

    const int mL = 12, mR = 12;
    const int w = width() - mL - mR;

    // --- Top: zoomed line-start timeline, cycles 0..40 ---------------------
    const int tY = 26, tH = 30;
    const int cycMax = 40;
    auto X = [&](double c) { return mL + c * w / cycMax; };
    p.setPen(QColor(150, 150, 160));
    p.drawText(mL, 16, QStringLiteral("Start of one scanline — the three $ffff8260 switches (cycles):"));

    // the ≤4 (hi) and ≤20 (med) windows, then the display-on point.
    p.fillRect(QRectF(X(0), tY, X(kHiMaxCycle) - X(0), tH), QColor(60, 40, 40));
    p.fillRect(QRectF(X(kHiMaxCycle), tY, X(kMedMaxCycle) - X(kHiMaxCycle), tH), QColor(44, 44, 56));
    p.fillRect(QRectF(X(kMedMaxCycle), tY, X(cycMax) - X(kMedMaxCycle), tH), QColor(36, 48, 60));
    p.setPen(QColor(90, 90, 100));
    p.drawRect(QRectF(X(0), tY, X(cycMax) - X(0) - 1, tH));

    auto sw = [&](double c, const QString &lbl, const QColor &col) {
        p.setPen(QPen(col, 2));
        p.drawLine(QPointF(X(c), tY - 4), QPointF(X(c), tY + tH + 4));
        p.setPen(col);
        p.drawText(QRectF(X(c) - 34, tY + tH + 5, 68, 12), Qt::AlignCenter, lbl);
    };
    sw(2, QStringLiteral("hi ≤4"), QColor(230, 120, 110));
    sw(14, QStringLiteral("med ≤20"), QColor(230, 200, 110));
    sw(m_step.loCycle, QStringLiteral("lo =%1").arg(m_step.loCycle), QColor(120, 210, 130));

    // --- Bottom: before / after strip -------------------------------------
    const int sY = tY + tH + 34;
    const int rowH = 22, gap = 30;
    const int cells = 16, cellW = w / (cells + 2);
    auto drawRow = [&](int y, int shiftCells, const QString &tag) {
        p.setPen(QColor(150, 150, 160));
        p.drawText(mL, y - 4, tag);
        for (int i = 0; i < cells; ++i) {
            const bool on = (i % 2) == 0;   // simple striped pattern
            QColor c = on ? QColor(90, 160, 220) : QColor(40, 44, 52);
            p.fillRect(QRectF(mL + (i + shiftCells) * cellW, y, cellW - 1, rowH), c);
        }
    };
    // Scale the shift up so it reads (px → cells is illustrative, not to scale):
    // show the shift as a whole-cell offset proportional to the pixel amount.
    const double shiftCells = m_step.shiftPx / 4.0;
    drawRow(sY, 0, QStringLiteral("normal line"));
    drawRow(sY + rowH + gap, shiftCells,
            QStringLiteral("shifted line (+%1 px right)").arg(m_step.shiftPx));

    // arrow between the two rows
    p.setPen(QPen(QColor(120, 210, 130), 1));
    const int ax = mL + int(shiftCells * cellW) + cells * cellW / 2;
    p.drawLine(ax, sY + rowH + 3, ax, sY + rowH + gap - 3);
    p.drawText(QRectF(mL, sY + rowH + 4, w, gap - 8), Qt::AlignCenter,
               QStringLiteral("→ +%1 px").arg(m_step.shiftPx));
}

// ------------------------------------------------------------ SyncScrollView
SyncScrollView::SyncScrollView(QWidget *parent) : QWidget(parent)
{
    m_steps = SyncScroll::steps();

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    auto *intro = new QLabel(
        QStringLiteral("<b>Sync scroll</b> — the plain STF has no fine-scroll register, so "
                       "ST Connexion shift a scanline with three <b>$ffff8260</b> resolution "
                       "switches at its start. The exact cycle of the low-res switch sets the "
                       "shift:"),
        this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    lay->addWidget(intro);

    auto *sel = new QHBoxLayout;
    sel->addWidget(new QLabel(QStringLiteral("Shift:"), this));
    for (int i = 0; i < m_steps.size(); ++i) {
        auto *b = new QPushButton(QStringLiteral("%1 px").arg(m_steps[i].shiftPx), this);
        b->setCheckable(true);
        b->setToolTip(QStringLiteral("low-res switch at cycle %1").arg(m_steps[i].loCycle));
        connect(b, &QPushButton::clicked, this, [this, i] { selectStep(i); });
        m_btn.append(b);
        sel->addWidget(b);
    }
    sel->addStretch();
    lay->addLayout(sel);

    m_diagram = new SyncScrollDiagram(this);
    lay->addWidget(m_diagram, 1);

    m_facts = new QLabel(this);
    m_facts->setWordWrap(true);
    m_facts->setTextFormat(Qt::RichText);
    m_facts->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    lay->addWidget(m_facts);

    selectStep(m_index);
}

void SyncScrollView::selectStep(int index)
{
    if (index < 0 || index >= m_steps.size())
        return;
    m_index = index;
    for (int i = 0; i < m_btn.size(); ++i)
        m_btn[i]->setChecked(i == index);
    const SyncScroll::Step s = m_steps[index];
    m_diagram->setStep(s);

    // The sourced cycle→shift table, with the selected row emphasised.
    QString table;
    for (const auto &st : m_steps) {
        const bool sel = st.loCycle == s.loCycle;
        table += QStringLiteral("%1cycle %2 → %3 px — %4%5<br>")
                     .arg(sel ? QStringLiteral("<b>") : QString())
                     .arg(st.loCycle).arg(st.shiftPx).arg(st.note)
                     .arg(sel ? QStringLiteral("</b>") : QString());
    }
    m_facts->setText(QStringLiteral(
        "<b>Sequence per line</b> (register $ffff8260): "
        "hi-res ($02) at LineCycles ≤ %1 &nbsp;→&nbsp; med-res ($01) at ≤ %2 "
        "&nbsp;→&nbsp; lo-res ($00) at the <b>exact</b> cycle below.<br><br>"
        "%3<br>"
        "Combine with a byte-granular screen-address change for the coarse (16 px) "
        "step to build a smooth scroll — the STF's answer to the STE hardware scroll "
        "register. From ST Connexion (“Let's Do The Twist”, Punish Your Machine).<br>"
        "<i>All figures sourced from Hatari video.h / video.c (C-007). Teaching view — "
        "the exact per-cycle landing is documented, not reproduced live.</i>")
        .arg(SyncScroll::kHiMaxCycle).arg(SyncScroll::kMedMaxCycle).arg(table));
}
