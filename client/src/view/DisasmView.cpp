#include "DisasmView.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

DisasmView::DisasmView(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("Trace"), this));
    m_count = new QSpinBox(this);
    m_count->setRange(4, 128);
    m_count->setValue(32);
    m_count->setSuffix(QStringLiteral(" instrs"));
    m_count->setToolTip(QStringLiteral("How many instructions to single-step from PC"));
    row->addWidget(m_count);
    m_trace = new QPushButton(QStringLiteral("Trace from PC"), this);
    m_trace->setToolTip(QStringLiteral(
        "Break, then single-step this many instructions, recording where each "
        "lands on the beam. Break in an effect's per-scanline loop first."));
    connect(m_trace, &QPushButton::clicked, this,
            [this] { emit traceRequested(m_count->value()); });
    row->addWidget(m_trace);
    row->addStretch();
    lay->addLayout(row);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("PC"), QStringLiteral("Instruction"), QStringLiteral("Line"),
         QStringLiteral("Cycle"), QStringLiteral("Cost")});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_table, &QTableWidget::currentCellChanged, this,
            [this](int r, int, int, int) {
                if (r >= 0 && r < m_entries.size())
                    emit rowActivated(m_entries[r].scanline, m_entries[r].cycleInLine);
            });
    lay->addWidget(m_table, 1);

    m_status = new QLabel(
        QStringLiteral("Break in an effect's loop, then Trace to see each "
                       "instruction's beam position."),
        this);
    m_status->setWordWrap(true);
    lay->addWidget(m_status);
}

void DisasmView::setEntries(const QVector<DisasmEntry> &entries)
{
    m_entries = entries;
    m_table->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const DisasmEntry &e = entries[i];
        // A write to the video/palette hardware ($ff8200–$ff82ff) — the payoff row.
        const bool videoWrite = e.text.contains(QStringLiteral("ff82"), Qt::CaseInsensitive);
        auto cell = [&](int col, const QString &t, bool mono = false) {
            auto *it = new QTableWidgetItem(t);
            if (mono)
                it->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            if (videoWrite)
                it->setBackground(QColor(60, 78, 48));
            m_table->setItem(i, col, it);
        };
        cell(0, QStringLiteral("%1").arg(e.pc, 6, 16, QLatin1Char('0')).toUpper(), true);
        cell(1, e.text, true);
        cell(2, QString::number(e.scanline));
        cell(3, QString::number(e.cycleInLine));
        cell(4, e.cost ? QString::number(e.cost) : QString());
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    int videoWrites = 0;
    for (const auto &e : entries)
        if (e.text.contains(QStringLiteral("ff82"), Qt::CaseInsensitive))
            ++videoWrites;
    setStatus(QStringLiteral("%1 instructions traced%2 — select a row to park the beam.")
                  .arg(entries.size())
                  .arg(videoWrites ? QStringLiteral(", %1 video-register write(s) highlighted")
                                         .arg(videoWrites)
                                   : QString()),
              true);
}

void DisasmView::setBusy(bool busy)
{
    m_trace->setEnabled(!busy);
    m_count->setEnabled(!busy);
    if (busy)
        setStatus(QStringLiteral("Tracing…"), true);
}

void DisasmView::setStatus(const QString &text, bool ok)
{
    m_status->setStyleSheet(ok ? QString() : QStringLiteral("color:#c62828;"));
    m_status->setText(text);
}
