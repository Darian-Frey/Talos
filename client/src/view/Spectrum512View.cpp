#include "Spectrum512View.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>

using Spectrum512::Image;
using Spectrum512::kWidth;
using Spectrum512::switchColumn;

// ------------------------------------------------------------- StormStrip
StormStrip::StormStrip(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(132);
}

void StormStrip::setLine(const Image *img, int screenLine)
{
    m_img = img;
    m_line = screenLine;
    update();
}

void StormStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 28));
    if (!m_img || !m_img->valid) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Import a .SPU to see its palette storm"));
        return;
    }
    const int margin = 6;
    const int w = width() - 2 * margin;
    auto mapX = [&](double x) { return margin + x * w / double(kWidth); };

    // 1. The resolved scanline (what this line actually looks like), 0..319.
    const int stripTop = 4, stripH = 34;
    for (int X = 0; X < w; ++X) {
        const int x = qBound(0, int(double(X) * kWidth / w), kWidth - 1);
        p.fillRect(margin + X, stripTop, 1, stripH, QColor(m_img->rgb.pixel(x, m_line)));
    }
    p.setPen(QColor(70, 70, 78));
    p.drawRect(margin, stripTop, w - 1, stripH);

    // 2. Switch ruler: where each of the 16 registers flips set1->set2 (tick up)
    //    and set2->set3 (+160, tick down). This is the "storm".
    const int rulerY = stripTop + stripH + 10;
    const auto pal = m_img->palette(m_line);
    p.setPen(QColor(90, 90, 98));
    p.drawLine(margin, rulerY, margin + w, rulerY);
    for (int c = 0; c < 16; ++c) {
        const int x1 = switchColumn(c);          // set1 -> set2
        const int x2 = x1 + 160;                 // set2 -> set3
        p.setPen(QPen(QColor(Spectrum512::decodeStColour(pal[c + 16])), 1));
        p.drawLine(int(mapX(x1)), rulerY - 8, int(mapX(x1)), rulerY);
        if (x2 < kWidth) {
            p.setPen(QPen(QColor(Spectrum512::decodeStColour(pal[c + 32])), 1));
            p.drawLine(int(mapX(x2)), rulerY, int(mapX(x2)), rulerY + 8);
        }
    }
    p.setPen(Qt::gray);
    p.drawText(margin, rulerY - 12, QStringLiteral("↑ reg→set2"));
    p.drawText(margin, rulerY + 22, QStringLiteral("↓ reg→set3 (+160px)"));

    // 3. The three 16-colour palette sets for this line.
    const int swTop = rulerY + 30;
    const int swH = 12;
    const double swW = w / 16.0;
    const char *labels[] = {"set1", "set2", "set3"};
    for (int set = 0; set < 3; ++set) {
        const int y = swTop + set * (swH + 2);
        for (int i = 0; i < 16; ++i) {
            p.fillRect(QRectF(margin + i * swW, y, swW - 1, swH),
                       QColor(Spectrum512::decodeStColour(pal[set * 16 + i])));
        }
        p.setPen(Qt::lightGray);
        p.drawText(margin + w + 2, y + swH - 2, labels[set]);
    }
}

// ------------------------------------------------------------- Spectrum512View
Spectrum512View::Spectrum512View(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    auto *top = new QHBoxLayout;
    auto *import = new QPushButton(QStringLiteral("Import picture…"), this);
    import->setToolTip(QStringLiteral(
        "Load a Spectrum 512 picture (.SPU uncompressed or .SPC compressed) and decode it (F-211)"));
    top->addWidget(import);
    top->addWidget(new QLabel(QStringLiteral("Scanline"), this));
    m_lineSpin = new QSpinBox(this);
    m_lineSpin->setRange(1, Spectrum512::kRows);
    m_lineSpin->setValue(100);
    m_lineSpin->setEnabled(false);
    top->addWidget(m_lineSpin);
    m_info = new QLabel(QStringLiteral("No picture loaded."), this);
    top->addWidget(m_info, 1);
    lay->addLayout(top);

    m_picture = new QLabel(this);
    // Scale to the space available (see updatePicture/resizeEvent) with a small
    // minimum so the picture never forces the window taller than the screen.
    m_picture->setMinimumSize(160, 100);
    m_picture->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_picture->setAlignment(Qt::AlignCenter);
    m_picture->setStyleSheet(QStringLiteral("background:#000;"));
    m_picture->setCursor(Qt::CrossCursor);
    m_picture->setToolTip(QStringLiteral("Click a row to inspect its palette storm"));
    m_picture->installEventFilter(this);
    lay->addWidget(m_picture, 1);

    m_lineInfo = new QLabel(this);
    lay->addWidget(m_lineInfo);

    m_storm = new StormStrip(this);
    lay->addWidget(m_storm);

    connect(import, &QPushButton::clicked, this, &Spectrum512View::importSpu);
    connect(m_lineSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &Spectrum512View::setLine);
}

void Spectrum512View::importSpu()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import Spectrum 512 picture"), QString(),
        QStringLiteral("Spectrum 512 (*.spu *.spc *.SPU *.SPC);;All files (*)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        m_info->setText(QStringLiteral("could not open %1").arg(file));
        return;
    }
    m_img = Spectrum512::parse(f.readAll());
    f.close();
    if (!m_img.valid) {
        m_info->setText(m_img.error);
        m_picture->clear();
        m_lineSpin->setEnabled(false);
        m_storm->setLine(nullptr, 0);
        return;
    }

    updatePicture();

    QSet<QRgb> uniq;
    for (int y = 1; y <= Spectrum512::kRows; ++y) {
        const QRgb *scan = reinterpret_cast<const QRgb *>(m_img.rgb.constScanLine(y));
        for (int x = 0; x < kWidth; ++x)
            uniq.insert(scan[x]);
    }
    m_info->setText(QStringLiteral("%1 (%2) — 320×%3, %4 unique colours")
                        .arg(QFileInfo(file).fileName(), m_img.format)
                        .arg(Spectrum512::kRows)
                        .arg(uniq.size()));
    m_lineSpin->setEnabled(true);
    setLine(100);
}

void Spectrum512View::setLine(int line)
{
    if (!m_img.valid)
        return;
    line = qBound(1, line, Spectrum512::kRows);
    {
        const QSignalBlocker b(m_lineSpin);
        m_lineSpin->setValue(line);
    }
    m_storm->setLine(&m_img, line);

    QSet<int> usedSlots;
    for (int s : m_img.rowSlots(line))
        usedSlots.insert(s);
    m_lineInfo->setText(
        QStringLiteral("Line %1: uses %2 of the 48 palette slots; the 16 registers "
                       "flip to set 2 at x = 1,5,21,25,… and to set 3 160 px later.")
            .arg(line)
            .arg(usedSlots.size()));
}

void Spectrum512View::updatePicture()
{
    if (!m_img.valid)
        return;
    // Fit the picture into the label, preserving aspect (nearest-neighbour keeps
    // the pixels crisp). Centred; the label can be any size >= its small minimum.
    m_picture->setPixmap(QPixmap::fromImage(m_img.rgb).scaled(
        m_picture->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

void Spectrum512View::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    updatePicture();
}

bool Spectrum512View::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_picture && ev->type() == QEvent::MouseButtonPress && m_img.valid) {
        const QPixmap pm = m_picture->pixmap();
        if (!pm.isNull()) {
            const auto *me = static_cast<QMouseEvent *>(ev);
            // The scaled pixmap is centred in the label; map the click back to a
            // source row (image is kRows+1 tall, row 0 blank).
            const int offY = (m_picture->height() - pm.height()) / 2;
            const double yInPix = me->position().y() - offY;
            if (yInPix >= 0 && yInPix < pm.height())
                setLine(int(yInPix * (Spectrum512::kRows + 1) / pm.height()));
        }
        return true;
    }
    return QWidget::eventFilter(obj, ev);
}
