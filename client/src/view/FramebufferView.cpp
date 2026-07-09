#include "FramebufferView.h"

#include <QPainter>
#include <QPaintEvent>

FramebufferView::FramebufferView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 200);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
}

void FramebufferView::setImage(const QImage &image)
{
    m_image = image;
    update();
}

void FramebufferView::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), Qt::black);

    if (m_image.isNull()) {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("no frame yet — connect and refresh"));
        return;
    }

    // Fit-to-widget, aspect-preserving, crisp pixels.
    const QSize target = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect dst(QPoint((width() - target.width()) / 2,
                           (height() - target.height()) / 2),
                    target);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(dst, m_image);
}
