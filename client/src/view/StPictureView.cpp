#include "StPictureView.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "model/Palette.h"

StPictureView::StPictureView(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    auto *top = new QHBoxLayout;
    auto *import = new QPushButton(QStringLiteral("Import ST picture…"), this);
    import->setToolTip(QStringLiteral(
        "Load an Atari ST picture — DEGAS (.PI1/.PI2/.PI3, .PC1/.PC2/.PC3), "
        "NEOchrome (.NEO) or Tiny (.TNY/.TN1/.TN2/.TN3) — and decode it (Phase 6)."));
    top->addWidget(import);
    m_info = new QLabel(QStringLiteral("No picture loaded."), this);
    top->addWidget(m_info, 1);
    lay->addLayout(top);

    m_picture = new QLabel(this);
    m_picture->setMinimumSize(160, 100);
    m_picture->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_picture->setAlignment(Qt::AlignCenter);
    m_picture->setStyleSheet(QStringLiteral("background:#000;"));
    lay->addWidget(m_picture, 1);

    m_palette = new QLabel(this);
    m_palette->setFixedHeight(22);
    m_palette->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    lay->addWidget(m_palette);

    connect(import, &QPushButton::clicked, this, &StPictureView::importPicture);
}

void StPictureView::importPicture()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import an Atari ST picture"), QString(),
        QStringLiteral("ST pictures (*.pi1 *.pi2 *.pi3 *.pc1 *.pc2 *.pc3 *.neo "
                       "*.tny *.tn1 *.tn2 *.tn3 *.PI1 *.PI2 *.PI3 *.PC1 *.PC2 "
                       "*.PC3 *.NEO *.TNY *.TN1 *.TN2 *.TN3);;All files (*)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        m_info->setText(QStringLiteral("could not open %1").arg(file));
        return;
    }
    m_img = StPicture::parse(f.readAll(), QFileInfo(file).suffix());
    f.close();
    if (!m_img.valid) {
        m_info->setText(m_img.error);
        m_picture->clear();
        m_palette->clear();
        return;
    }

    updatePicture();

    // Palette swatches (the colours actually used at this resolution).
    const int n = m_img.colours;
    const int sw = 20;
    QPixmap pm(n * sw, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    for (int i = 0; i < n && i < m_img.palette.size(); ++i)
        p.fillRect(i * sw, 0, sw - 1, 20, Palette::decode(m_img.palette[i]));
    m_palette->setPixmap(pm);

    m_info->setText(QStringLiteral("%1 — %2 (%3×%4, %5 colours)")
                        .arg(QFileInfo(file).fileName(), m_img.format)
                        .arg(m_img.rgb.width())
                        .arg(m_img.rgb.height())
                        .arg(m_img.colours));
}

void StPictureView::updatePicture()
{
    if (!m_img.valid)
        return;
    m_picture->setPixmap(QPixmap::fromImage(m_img.rgb).scaled(
        m_picture->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

void StPictureView::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    updatePicture();
}
