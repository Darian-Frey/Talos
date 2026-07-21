#include "RegTimelineView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace RegClass {

const QVector<Info> &infos()
{
    static const QVector<Info> k = {
        {"Palette $ff8240-5e", QColor(120, 210, 130)},   // 0
        {"Sync/freq $ff820a", QColor(230, 205, 90)},     // 1
        {"Resolution $ff8260", QColor(230, 90, 80)},     // 2
        {"STE scroll $ff8264/5", QColor(90, 200, 220)},  // 3
        {"Line width $ff820f", QColor(210, 120, 210)},   // 4
        {"Video base/counter", QColor(110, 150, 235)},   // 5
        {"Other $ff82xx", QColor(150, 150, 155)},        // 6
    };
    return k;
}

int classify(quint32 addr)
{
    addr &= 0xffffff;
    if (addr >= 0xff8240 && addr <= 0xff825f)
        return 0;
    if (addr == 0xff820a)
        return 1;
    if (addr == 0xff8260)
        return 2;
    if (addr == 0xff8264 || addr == 0xff8265)
        return 3;
    if (addr == 0xff820f)
        return 4;
    if (addr == 0xff8201 || addr == 0xff8203 || addr == 0xff820d
        || (addr >= 0xff8205 && addr <= 0xff8209))
        return 5;
    return 6;
}

}   // namespace RegClass

// -------------------------------------------------------------- RegTimelinePlot
RegTimelinePlot::RegTimelinePlot(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(360, 240);
}

void RegTimelinePlot::setData(const QVector<WriteEvent> &events, VideoRegion region)
{
    m_events = events;
    m_region = region;
    update();
}

void RegTimelinePlot::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(18, 18, 22));

    const int cpl = (m_region == VideoRegion::Ntsc60) ? 508 : 512;
    const int spf = (m_region == VideoRegion::Ntsc60) ? 263 : 313;
    const int mL = 40, mR = 12, mT = 14, mB = 22;
    const QRectF area(mL, mT, width() - mL - mR, height() - mT - mB);
    auto X = [&](double c) { return area.left() + c * area.width() / cpl; };
    auto Y = [&](double l) { return area.top() + l * area.height() / spf; };

    // Display window (PAL: cycles 56–376, lines 63–263) for context.
    p.fillRect(QRectF(X(56), Y(63), X(376) - X(56), Y(263) - Y(63)), QColor(30, 36, 48));
    p.setPen(QColor(70, 70, 80));
    p.drawRect(area);

    // Axis labels.
    p.setPen(QColor(140, 140, 150));
    p.drawText(QRectF(area.left(), height() - mB + 2, area.width(), 14),
               Qt::AlignLeft, QStringLiteral("cycle 0"));
    p.drawText(QRectF(area.left(), height() - mB + 2, area.width(), 14),
               Qt::AlignRight, QString::number(cpl));
    p.drawText(QRectF(area.left(), height() - mB + 2, area.width(), 14),
               Qt::AlignCenter, QStringLiteral("cycle across scanline →"));
    p.save();
    p.translate(11, area.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-80, -8, 160, 14), Qt::AlignCenter, QStringLiteral("scanline ↓"));
    p.restore();

    // Every write as a mark at its beam position, coloured by register class.
    const auto &infos = RegClass::infos();
    for (const WriteEvent &e : m_events) {
        if (e.scanline < 0 || e.scanline >= spf)
            continue;
        const int ci = RegClass::classify(e.address);
        p.fillRect(QRectF(X(e.cycleInLine), Y(e.scanline), 2.0, 2.0), infos[ci].colour);
    }
}

// -------------------------------------------------------------- RegTimelineView
RegTimelineView::RegTimelineView(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    auto *row = new QHBoxLayout;
    m_capture = new QPushButton(QStringLiteral("Capture frame"), this);
    m_capture->setToolTip(QStringLiteral(
        "Record every video-register write in the next whole frame and plot it on "
        "the beam (patched `regtrace`, F-220)"));
    connect(m_capture, &QPushButton::clicked, this, [this] { emit captureRequested(); });
    row->addWidget(m_capture);
    m_status = new QLabel(QStringLiteral("Run an effect, then Capture a frame."), this);
    m_status->setWordWrap(true);
    row->addWidget(m_status, 1);
    lay->addLayout(row);

    m_plot = new RegTimelinePlot(this);
    lay->addWidget(m_plot, 1);

    m_legend = new QLabel(this);
    m_legend->setTextFormat(Qt::RichText);
    m_legend->setWordWrap(true);
    lay->addWidget(m_legend);
}

void RegTimelineView::setEvents(const QVector<WriteEvent> &events, VideoRegion region)
{
    m_plot->setData(events, region);

    const auto &infos = RegClass::infos();
    QVector<int> counts(infos.size(), 0);
    for (const WriteEvent &e : events)
        ++counts[RegClass::classify(e.address)];

    QStringList parts;
    for (int i = 0; i < infos.size(); ++i) {
        if (counts[i] == 0)
            continue;
        const QColor c = infos[i].colour;
        parts << QStringLiteral("<span style='color:%1'>●</span> %2 (%3)")
                     .arg(c.name(), infos[i].name).arg(counts[i]);
    }
    m_legend->setText(parts.join(QStringLiteral(" &nbsp; ")));
    setStatus(QStringLiteral("%1 register writes this frame — each dot is a write at its "
                             "beam position.").arg(events.size()),
              true);
}

void RegTimelineView::setBusy(bool busy)
{
    m_capture->setEnabled(!busy);
    if (busy)
        setStatus(QStringLiteral("Capturing a frame…"), true);
}

void RegTimelineView::setStatus(const QString &text, bool ok)
{
    m_status->setStyleSheet(ok ? QString() : QStringLiteral("color:#c62828;"));
    m_status->setText(text);
}
