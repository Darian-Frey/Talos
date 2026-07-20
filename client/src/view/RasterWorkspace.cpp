#include "RasterWorkspace.h"

#include <algorithm>

#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QMap>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
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

    auto *modeRow = new QHBoxLayout;
    m_mode = new QComboBox(this);
    m_mode->addItem(QStringLiteral("Raster bars (horizontal, per-line)"), Bars);
    m_mode->addItem(QStringLiteral("Vertical bands (intra-line, HBL-synced)"), Bands);
    m_mode->addItem(QStringLiteral("Copper bars (animated, scrolling)"), Copper);
    m_mode->addItem(QStringLiteral("Colour cycle (palette rotation)"), Cycle);
    m_mode->setToolTip(QStringLiteral(
        "Bars: colour per scanline. Bands: colours packed across each line. "
        "Copper: the bars scroll down each frame. Colour cycle: rotate the palette."));
    modeRow->addWidget(m_mode, 1);
    // Fill the bar table with a ready-made pattern (Bars / Copper modes).
    auto *patterns = new QPushButton(QStringLiteral("Fill ▾"), this);
    auto *pmenu = new QMenu(patterns);
    pmenu->addAction(QStringLiteral("Gradient"), this, [this] { fillPattern(0); });
    pmenu->addAction(QStringLiteral("Rainbow"), this, [this] { fillPattern(1); });
    pmenu->addAction(QStringLiteral("Mirror current"), this, [this] { fillPattern(2); });
    patterns->setMenu(pmenu);
    patterns->setToolTip(QStringLiteral("Fill the bar table with a gradient / rainbow, "
                                        "or mirror the current bars about the centre"));
    modeRow->addWidget(patterns);
    m_speed = new QSpinBox(this);
    m_speed->setRange(1, 8);
    m_speed->setValue(RasterCodegen::kDefaultCopperSpeed);
    m_speed->setPrefix(QStringLiteral("scroll "));
    m_speed->setSuffix(QStringLiteral(" px/f"));
    m_speed->setToolTip(QStringLiteral("Copper-bar scroll speed (pixels per frame)"));
    m_speed->setVisible(false);   // shown only in Copper mode
    modeRow->addWidget(m_speed);
    lay->addLayout(modeRow);

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
    auto *import = new QPushButton(QStringLiteral("Import…"), m_actions);
    auto *xport = new QPushButton(QStringLiteral("Export…"), m_actions);
    build->setToolTip(QStringLiteral("Codegen -> vasm -> run the effect in Hatari (F-212)"));
    verify->setToolTip(QStringLiteral("Run the exported stub through the round-trip harness"));
    import->setToolTip(QStringLiteral("Load a register sequence (raster.json) back into the workspace"));
    xport->setToolTip(QStringLiteral("Write the .s stub, assembled .PRG and register sequence to a folder"));
    btns->addWidget(add);
    btns->addWidget(del);
    btns->addStretch();
    btns->addWidget(build);
    btns->addWidget(verify);
    btns->addWidget(import);
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
        if (r >= 0) {
            m_table->removeRow(r);
            emit contentChanged();
        }
    });
    // Any cell edit / added row changes the effect; mode changes below too.
    connect(m_table, &QTableWidget::cellChanged, this, &RasterWorkspace::contentChanged);
    connect(build, &QPushButton::clicked, this,
            [this] { emit buildRequested(bars()); });
    connect(verify, &QPushButton::clicked, this,
            [this] { emit verifyRequested(bars()); });
    connect(import, &QPushButton::clicked, this, [this] { emit importRequested(); });
    connect(xport, &QPushButton::clicked, this,
            [this] { emit exportRequested(bars()); });

    // In Bands mode the scanline column is ignored (row order = left-to-right
    // bands); reflect that in the header and offer a hint.
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const Mode m = mode();
        const bool bands = m == Bands;
        m_table->setHorizontalHeaderLabels(
            {bands ? QStringLiteral("Column (0–831)")
                   : (m == Cycle ? QStringLiteral("Index (order)") : QStringLiteral("Scanline")),
             QStringLiteral("Colour $0rgb")});
        m_speed->setVisible(m == Copper);
        QString hint;
        if (bands)
            hint = QStringLiteral("Bands mode: colour begins at its column; the lowest-column "
                                  "colour fills from the left. Click the frame to place bands "
                                  "(max %1).").arg(RasterCodegen::kMaxBands);
        else if (m == Copper)
            hint = QStringLiteral("Copper mode: the bars (as in Bars) scroll down every frame — "
                                  "set the scroll speed, then Build.");
        else if (m == Cycle)
            hint = QStringLiteral("Colour-cycle mode: the colour column (up to 16, top-to-bottom) "
                                  "becomes the palette; it rotates every frame over a stripe ramp.");
        setResult(hint, true);
        emit contentChanged();
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

int RasterWorkspace::copperSpeed() const
{
    return m_speed->value();
}

void RasterWorkspace::fillPattern(int which)
{
    auto clamp = [](int n) { return qBound(0, n, 7); };
    if (which == 2) {   // mirror the current bars about the centre
        const auto cur = bars();
        if (cur.isEmpty()) {
            setResult(QStringLiteral("Add some bars first to mirror."), false);
            return;
        }
        QMap<int, quint16> byLine;
        for (const auto &b : cur)
            byLine[b.line] = b.colour;
        for (const auto &b : cur) {
            const int m = RasterCodegen::kVisibleLines - 1 - b.line;
            if (m >= 0 && !byLine.contains(m))
                byLine[m] = b.colour;
        }
        m_table->setRowCount(0);
        for (auto it = byLine.constBegin(); it != byLine.constEnd(); ++it)
            addBar(it.key(), it.value());
        setResult(QStringLiteral("Mirrored to %1 bars.").arg(byLine.size()), true);
        emit contentChanged();
        return;
    }
    const int N = 16;
    m_table->setRowCount(0);
    for (int i = 0; i < N; ++i) {
        const int line = i * RasterCodegen::kVisibleLines / N;
        quint16 c;
        if (which == 0) {   // gradient: deep blue -> warm white
            const double t = double(i) / (N - 1);
            auto lerp = [&](int a, int b) { return clamp(qRound(a + (b - a) * t)); };
            c = quint16((lerp(0, 7) << 8) | (lerp(0, 7) << 4) | lerp(6, 7));
        } else {            // rainbow: hue sweep
            const QColor h = QColor::fromHsvF(double(i) / N, 1.0, 1.0);
            c = quint16(((h.red() * 7 / 255) << 8) | ((h.green() * 7 / 255) << 4)
                        | (h.blue() * 7 / 255));
        }
        addBar(line, c);
    }
    setResult(QStringLiteral("Filled a %1 — Build to preview.")
                  .arg(which == 0 ? QStringLiteral("gradient") : QStringLiteral("rainbow")),
              true);
    emit contentChanged();
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

QVector<RasterCodegen::Bar> RasterWorkspace::columnBars() const
{
    // Bands mode: column-0 cell is a framebuffer column (0–831), not a scanline.
    QVector<RasterCodegen::Bar> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *l = m_table->item(r, 0);
        QTableWidgetItem *c = m_table->item(r, 1);
        if (!l || !c)
            continue;
        bool okL = false, okC = false;
        const int column = l->text().trimmed().toInt(&okL);
        const quint16 col = static_cast<quint16>(c->text().trimmed().toUInt(&okC, 16) & 0x777);
        if (okL && okC && column >= 0 && column <= 831)
            out.append({column, col});
    }
    std::sort(out.begin(), out.end(),
              [](const auto &a, const auto &b) { return a.line < b.line; });
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

void RasterWorkspace::loadEntries(Mode mode, const QVector<QPair<int, quint16>> &entries)
{
    m_mode->setCurrentIndex(mode);   // updates the header + hint via its signal
    m_table->setRowCount(0);
    for (const auto &e : entries)
        addBar(e.first, e.second);   // col-0 holds a scanline (Bars) or column (Bands)
    setResult(QStringLiteral("Imported %1 %2.").arg(entries.size())
                  .arg(mode == Bands ? QStringLiteral("bands") : QStringLiteral("bars")),
              true);
}

void RasterWorkspace::placeFromClick(int line, int column)
{
    // Cycle a default colour so successive clicks are visually distinct.
    static const quint16 pal[] = {0x700, 0x070, 0x007, 0x770, 0x707, 0x077, 0x777};
    const quint16 c = pal[m_table->rowCount() % 7];
    if (mode() == Bars) {
        addBar(qBound(0, line, RasterCodegen::kVisibleLines - 1), c);
    } else {
        // Bands: place a colour boundary at the clicked column (arbitrary-column
        // codegen). Column is stored in the Scanline cell as the target.
        addBar(qBound(0, column, 831), c);
    }
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
