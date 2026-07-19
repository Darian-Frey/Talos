#include "BorderWalkthroughView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

using BorderCodegen::Axis;
using BorderCodegen::Border;

// ------------------------------------------------------------- BorderDiagram
BorderDiagram::BorderDiagram(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(360, 220);
}

void BorderDiagram::setBorder(Border b)
{
    m_border = b;
    update();
}

void BorderDiagram::paintEvent(QPaintEvent *)
{
    using namespace BorderCodegen;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(20, 20, 24));

    // Axes: X = cycles 0..512, Y = lines 0..313. Leave a margin for labels.
    const int mL = 44, mR = 14, mT = 18, mB = 26;
    const QRectF area(mL, mT, width() - mL - mR, height() - mT - mB);
    auto X = [&](double cyc) { return area.left() + cyc * area.width() / kCyclesPerLine; };
    auto Y = [&](double line) { return area.top() + line * area.height() / kLinesPerFrame; };
    auto cell = [&](double c0, double c1, double l0, double l1) {
        return QRectF(X(c0), Y(l0), X(c1) - X(c0), Y(l1) - Y(l0));
    };

    const Facts f = facts(m_border);

    // Whole captured frame (border colour), then the normal display rectangle.
    p.fillRect(cell(0, kCyclesPerLine, 0, kLinesPerFrame), QColor(46, 30, 30));  // border (reddish)
    p.fillRect(cell(kLineStart, kLineEnd, kDispFirst, kDispLast), QColor(40, 58, 82));  // display

    // The region this border opens (green), keyed off the border.
    p.setBrush(QColor(70, 150, 80, 210));
    p.setPen(Qt::NoPen);
    switch (m_border) {
    case Border::Left:
        p.drawRect(cell(0, kLineStart, kDispFirst, kDispLast));
        break;
    case Border::Right:
        p.drawRect(cell(kLineEnd, kLineEndNoRight, kDispFirst, kDispLast));
        break;
    case Border::Top:
        p.drawRect(cell(kLineStart, kLineEnd, kFirstVisible, kDispFirst));
        break;
    case Border::Bottom:
        p.drawRect(cell(kLineStart, kLineEnd, kDispLast, kDispLast + kBottomExtra));
        break;
    }

    // Display outline.
    p.setBrush(Qt::NoBrush);
    p.setPen(QColor(90, 110, 140));
    p.drawRect(cell(kLineStart, kLineEnd, kDispFirst, kDispLast));

    // The switch marker: vertical (a cycle, on every line) for L/R, horizontal
    // (a line) plus the cycle-504 tick for T/B.
    p.setPen(QPen(QColor(240, 210, 90), 2));
    if (f.axis == Axis::Horizontal) {
        const double x = X(f.switchCycle);
        p.drawLine(QPointF(x, Y(kDispFirst)), QPointF(x, Y(kDispLast)));
        p.setPen(QColor(240, 210, 90));
        p.drawText(QRectF(x - 40, Y(kDispFirst) - 14, 80, 12), Qt::AlignCenter,
                   QStringLiteral("cyc %1").arg(f.switchCycle));
    } else {
        const double y = Y(f.trickFirst);
        p.drawLine(QPointF(X(0), y), QPointF(X(kCyclesPerLine), y));
        const double x = X(f.switchCycle);
        p.setPen(QPen(QColor(240, 120, 90), 2));
        p.drawLine(QPointF(x, y - 6), QPointF(x, y + 6));
        p.setPen(QColor(240, 210, 90));
        p.drawText(QRectF(X(0), y - 14, area.width(), 12), Qt::AlignLeft,
                   QStringLiteral(" line %1, switch before cyc %2").arg(f.trickFirst).arg(f.switchCycle));
    }

    // Axis labels.
    p.setPen(QColor(150, 150, 160));
    p.drawText(QRectF(area.left(), height() - mB + 4, area.width(), 14), Qt::AlignLeft,
               QStringLiteral("cycle 0"));
    p.drawText(QRectF(area.left(), height() - mB + 4, area.width(), 14), Qt::AlignRight,
               QStringLiteral("%1").arg(kCyclesPerLine));
    p.drawText(QRectF(area.left(), height() - mB + 4, area.width(), 14), Qt::AlignCenter,
               QStringLiteral("cycles across a scanline →"));
    p.save();
    p.translate(12, area.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-90, -8, 180, 14), Qt::AlignCenter,
               QStringLiteral("lines down the frame ↓"));
    p.restore();
}

// -------------------------------------------------------- BorderWalkthroughView
BorderWalkthroughView::BorderWalkthroughView(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    // Border selector.
    auto *sel = new QHBoxLayout;
    sel->addWidget(new QLabel(QStringLiteral("Border:"), this));
    const Border borders[4] = {Border::Left, Border::Right, Border::Top, Border::Bottom};
    for (int i = 0; i < 4; ++i) {
        m_btn[i] = new QPushButton(BorderCodegen::borderName(borders[i]).section(' ', 0, 0), this);
        m_btn[i]->setCheckable(true);
        const Border b = borders[i];
        connect(m_btn[i], &QPushButton::clicked, this, [this, b] { selectBorder(b); });
        sel->addWidget(m_btn[i]);
    }
    sel->addStretch();
    lay->addLayout(sel);

    m_diagram = new BorderDiagram(this);
    lay->addWidget(m_diagram, 1);

    m_facts = new QLabel(this);
    m_facts->setWordWrap(true);
    m_facts->setTextFormat(Qt::RichText);
    m_facts->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    lay->addWidget(m_facts);

    m_actions = new QWidget(this);
    auto *btns = new QHBoxLayout(m_actions);
    btns->setContentsMargins(0, 0, 0, 0);
    m_build = new QPushButton(QStringLiteral("Build & Run"), m_actions);
    m_verify = new QPushButton(QStringLiteral("Verify on Hatari"), m_actions);
    m_build->setToolTip(QStringLiteral("Codegen → vasm → run the border effect on ST/PAL"));
    m_verify->setToolTip(QStringLiteral("Run headless and confirm the border opens (border-check harness)"));
    connect(m_build, &QPushButton::clicked, this, [this] { emit buildRequested(m_border); });
    connect(m_verify, &QPushButton::clicked, this, [this] { emit verifyRequested(m_border); });
    btns->addWidget(m_build);
    btns->addWidget(m_verify);
    btns->addStretch();
    lay->addWidget(m_actions);

    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    lay->addWidget(m_result);

    selectBorder(Border::Left);
}

void BorderWalkthroughView::selectBorder(Border b)
{
    m_border = b;
    const Border borders[4] = {Border::Left, Border::Right, Border::Top, Border::Bottom};
    for (int i = 0; i < 4; ++i)
        m_btn[i]->setChecked(borders[i] == b);

    m_diagram->setBorder(b);
    const BorderCodegen::Facts f = BorderCodegen::facts(b);
    m_facts->setText(QStringLiteral(
        "<b>%1</b> &mdash; %2<br>"
        "<b>Register:</b> %3 (%4) &nbsp; <b>write:</b> %5<br>"
        "<b>Cycle window:</b> %6<br>"
        "<b>On lines:</b> %7<br>"
        "<b>Consequence:</b> %8"
        "<br><i>All figures sourced from Hatari video.h / video.c (C-007).</i>")
        .arg(f.name, f.mechanism, f.reg, f.regName, f.writeSeq, f.cycleWindow, f.onLines,
             f.consequence));

    // Only the left border is runnable (bench-proven); the rest are teaching-only.
    m_build->setEnabled(f.runnable);
    m_verify->setEnabled(f.runnable);
    if (f.runnable)
        setResult(QStringLiteral("Left border is runnable — Build & Run to open it live."), true);
    else
        setResult(QStringLiteral("Teaching view. The runnable flagship is the Left border; "
                                 "the others show the exact write, window and consequence."),
                  true);
}

void BorderWalkthroughView::setBusy(bool busy)
{
    m_actions->setEnabled(!busy);
}

void BorderWalkthroughView::setResult(const QString &text, bool ok)
{
    m_result->setStyleSheet(ok ? QStringLiteral("color:#2e7d32;")
                               : QStringLiteral("color:#c62828;"));
    m_result->setText(text);
}
