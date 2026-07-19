#include "MfpView.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString hz(double f)
{
    if (f <= 0)
        return QStringLiteral("—");
    if (f >= 1000)
        return QStringLiteral("%1 kHz").arg(f / 1000.0, 0, 'f', 2);
    return QStringLiteral("%1 Hz").arg(f, 0, 'f', 1);
}

QTableWidgetItem *flag(bool on, const QColor &onCol)
{
    auto *it = new QTableWidgetItem(on ? QStringLiteral("●") : QStringLiteral("·"));
    it->setTextAlignment(Qt::AlignCenter);
    if (on)
        it->setForeground(onCol);
    return it;
}
}   // namespace

MfpView::MfpView(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("MC68901 MFP — timers & interrupts"), this));
    row->addStretch();
    m_read = new QPushButton(QStringLiteral("Read MFP"), this);
    m_read->setToolTip(QStringLiteral("Read $fffa00–$fffa2f from the live machine and decode"));
    connect(m_read, &QPushButton::clicked, this, [this] { emit readRequested(); });
    row->addWidget(m_read);
    lay->addLayout(row);

    m_timers = new QTableWidget(0, 7, this);
    m_timers->setHorizontalHeaderLabels(
        {QStringLiteral("Timer"), QStringLiteral("Mode"), QStringLiteral("Presc"),
         QStringLiteral("Data"), QStringLiteral("Frequency"), QStringLiteral("Int/frame"),
         QStringLiteral("Typical use")});
    m_timers->verticalHeader()->setVisible(false);
    m_timers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_timers->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_timers->setFixedHeight(140);
    lay->addWidget(m_timers);

    m_ints = new QTableWidget(0, 5, this);
    m_ints->setHorizontalHeaderLabels(
        {QStringLiteral("Interrupt source"), QStringLiteral("En"), QStringLiteral("Msk"),
         QStringLiteral("Pnd"), QStringLiteral("Svc")});
    m_ints->verticalHeader()->setVisible(false);
    m_ints->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ints->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    lay->addWidget(m_ints, 1);

    m_note = new QLabel(
        QStringLiteral("Read the MFP to see the four timers and which interrupts are live. "
                       "En=enabled (IER), Msk=unmasked (IMR), Pnd=pending (IPR), Svc=in-service (ISR)."),
        this);
    m_note->setWordWrap(true);
    lay->addWidget(m_note);
}

void MfpView::setState(const Mfp::State &s)
{
    m_timers->setRowCount(s.timers.size());
    for (int i = 0; i < s.timers.size(); ++i) {
        const Mfp::Timer &t = s.timers[i];
        auto set = [&](int c, const QString &txt) {
            auto *it = new QTableWidgetItem(txt);
            if (t.running)
                it->setForeground(QColor(120, 210, 130));
            m_timers->setItem(i, c, it);
        };
        set(0, QStringLiteral("Timer %1").arg(t.name));
        set(1, t.mode);
        set(2, t.prescaler ? QStringLiteral("/%1").arg(t.prescaler) : QStringLiteral("—"));
        set(3, t.running ? QString::number(t.data) : QStringLiteral("—"));
        set(4, t.eventCount ? QStringLiteral("per event") : hz(t.freqHz));
        QString perFrame;
        if (t.eventCount)
            perFrame = QStringLiteral("per counted event");
        else if (t.perFrame <= 0)
            perFrame = QStringLiteral("—");
        else if (t.lineGap >= 1)
            perFrame = QStringLiteral("%1  (~every %2 lines)")
                           .arg(t.perFrame, 0, 'f', t.perFrame < 10 ? 1 : 0).arg(t.lineGap);
        else if (t.perFrame >= 1)
            perFrame = QStringLiteral("%1  (>1 per line)").arg(t.perFrame, 0, 'f', 0);
        else
            perFrame = QStringLiteral("%1").arg(t.perFrame, 0, 'f', 3);
        set(5, perFrame);
        set(6, t.use);
    }
    m_timers->resizeColumnsToContents();
    m_timers->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);

    m_ints->setRowCount(s.sources.size());
    int activeCount = 0;
    for (int i = 0; i < s.sources.size(); ++i) {
        const Mfp::Source &src = s.sources[i];
        auto *name = new QTableWidgetItem(src.name);
        if (src.isTimer)
            name->setBackground(QColor(40, 52, 66));
        if (src.active())
            name->setForeground(QColor(120, 210, 130));
        m_ints->setItem(i, 0, name);
        m_ints->setItem(i, 1, flag(src.enabled, QColor(120, 210, 130)));
        m_ints->setItem(i, 2, flag(src.unmasked, QColor(120, 210, 130)));
        m_ints->setItem(i, 3, flag(src.pending, QColor(230, 200, 110)));
        m_ints->setItem(i, 4, flag(src.inService, QColor(230, 120, 110)));
        if (src.active())
            ++activeCount;
    }
    for (int c = 1; c < 5; ++c)
        m_ints->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);

    m_note->setText(
        QStringLiteral("%1 interrupt source(s) enabled & unmasked. Timer-B in event-count mode "
                       "fires per display line (Spectrum 512); Timer-C is the 200 Hz system tick. "
                       "En=IER · Msk=IMR · Pnd=IPR · Svc=ISR.")
            .arg(activeCount));
}

void MfpView::setBusy(bool busy)
{
    m_read->setEnabled(!busy);
}
