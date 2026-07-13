#include "RasterWorkspace.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
// ST $0rgb (each gun 0-7) -> display QColor, matching the client palette decode.
QColor stColour(quint16 v)
{
    auto gun = [](int n) {
        const int i4 = ((n & 7) << 1) | ((n & 8) >> 3);
        return i4 | (i4 << 4);
    };
    return QColor(gun((v >> 8) & 0xf), gun((v >> 4) & 0xf), gun(v & 0xf));
}
}   // namespace

RasterWorkspace::RasterWorkspace(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    m_mode = new QComboBox(this);
    m_mode->addItem(QStringLiteral("Raster bars (horizontal, per-line)"), Bars);
    m_mode->addItem(QStringLiteral("Vertical bands (intra-line, HBL-synced)"), Bands);
    m_mode->setToolTip(QStringLiteral(
        "Bars: colour per scanline (F-212 per-line). "
        "Bands: colours packed across each line -> vertical bands (Spectrum-512-lite)."));
    lay->addWidget(m_mode);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Scanline"), QStringLiteral("Colour $0rgb")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    connect(m_table, &QTableWidget::cellChanged, this, &RasterWorkspace::recolourRow);
    lay->addWidget(m_table);

    m_actions = new QWidget(this);
    auto *btns = new QHBoxLayout(m_actions);
    btns->setContentsMargins(0, 0, 0, 0);
    auto *add = new QPushButton(QStringLiteral("＋ Bar"), m_actions);
    auto *del = new QPushButton(QStringLiteral("－"), m_actions);
    auto *build = new QPushButton(QStringLiteral("Build & Run"), m_actions);
    auto *verify = new QPushButton(QStringLiteral("Verify on Hatari"), m_actions);
    auto *xport = new QPushButton(QStringLiteral("Export…"), m_actions);
    build->setToolTip(QStringLiteral("Codegen -> vasm -> run the effect in Hatari (F-212)"));
    verify->setToolTip(QStringLiteral("Run the exported stub through the round-trip harness"));
    xport->setToolTip(QStringLiteral("Write the .s stub, assembled .PRG and register sequence to a folder"));
    btns->addWidget(add);
    btns->addWidget(del);
    btns->addStretch();
    btns->addWidget(build);
    btns->addWidget(verify);
    btns->addWidget(xport);
    lay->addWidget(m_actions);

    m_result = new QLabel(QString(), this);
    m_result->setWordWrap(true);
    lay->addWidget(m_result);

    connect(add, &QPushButton::clicked, this, [this] {
        const int line = m_table->rowCount() ? 190 : 20;
        addBar(line, 0x070);
    });
    connect(del, &QPushButton::clicked, this, [this] {
        const int r = m_table->currentRow();
        if (r >= 0) m_table->removeRow(r);
    });
    connect(build, &QPushButton::clicked, this,
            [this] { emit buildRequested(bars()); });
    connect(verify, &QPushButton::clicked, this,
            [this] { emit verifyRequested(bars()); });
    connect(xport, &QPushButton::clicked, this,
            [this] { emit exportRequested(bars()); });

    // In Bands mode the scanline column is ignored (row order = left-to-right
    // bands); reflect that in the header and offer a hint.
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const bool bands = mode() == Bands;
        m_table->setHorizontalHeaderLabels(
            {bands ? QStringLiteral("(band #)") : QStringLiteral("Scanline"),
             QStringLiteral("Colour $0rgb")});
        setResult(bands ? QStringLiteral("Bands mode: row order = left→right vertical bands "
                                         "(max %1).").arg(RasterCodegen::kMaxBands)
                        : QString(),
                  true);
    });

    // Seed with a rainbow so the workspace is usable immediately.
    const quint16 rainbow[] = {0x700, 0x070, 0x007, 0x770, 0x707, 0x077, 0x777};
    for (int i = 0; i < 7; ++i)
        addBar(i * (RasterCodegen::kVisibleLines / 7), rainbow[i]);
}

RasterWorkspace::Mode RasterWorkspace::mode() const
{
    return static_cast<Mode>(m_mode->currentData().toInt());
}

QVector<quint16> RasterWorkspace::colours() const
{
    QVector<quint16> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *c = m_table->item(r, 1);
        if (!c)
            continue;
        bool ok = false;
        const quint16 col = static_cast<quint16>(c->text().trimmed().toUInt(&ok, 16) & 0x777);
        if (ok)
            out.append(col);
    }
    return out;
}

void RasterWorkspace::addBar(int line, quint16 colour)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    m_table->setItem(r, 0, new QTableWidgetItem(QString::number(line)));
    m_table->setItem(r, 1, new QTableWidgetItem(
                               QStringLiteral("%1").arg(colour, 3, 16, QLatin1Char('0'))));
    recolourRow(r, 1);
}

void RasterWorkspace::recolourRow(int row, int col)
{
    if (col != 1)
        return;
    QTableWidgetItem *it = m_table->item(row, 1);
    if (!it)
        return;
    bool ok = false;
    const quint16 c = static_cast<quint16>(it->text().trimmed().toUInt(&ok, 16) & 0x777);
    if (!ok)
        return;
    const QColor bg = stColour(c);
    it->setBackground(bg);
    it->setForeground(bg.lightness() < 128 ? Qt::white : Qt::black);
}

QVector<RasterCodegen::Bar> RasterWorkspace::bars() const
{
    QVector<RasterCodegen::Bar> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *l = m_table->item(r, 0);
        QTableWidgetItem *c = m_table->item(r, 1);
        if (!l || !c)
            continue;
        bool okL = false, okC = false;
        const int line = l->text().trimmed().toInt(&okL);
        const quint16 col = static_cast<quint16>(c->text().trimmed().toUInt(&okC, 16) & 0x777);
        if (okL && okC && line >= 0 && line < RasterCodegen::kVisibleLines)
            out.append({line, col});
    }
    return out;
}

void RasterWorkspace::setBusy(bool busy)
{
    m_actions->setEnabled(!busy);
}

void RasterWorkspace::setResult(const QString &text, bool ok)
{
    m_result->setStyleSheet(ok ? QStringLiteral("color:#2e7d32;")
                               : QStringLiteral("color:#c62828;"));
    m_result->setText(text);
}
