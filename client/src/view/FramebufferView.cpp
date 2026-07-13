#include "FramebufferView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

namespace {
const QColor kMarkerCol(255, 220, 0);          // yellow beam crosshair
const QColor kVblankCol(140, 170, 255);        // blue-ish "beam in blanking"
constexpr double kCross = 5.0;

// Rec. 601 luma of an image pixel (0..255); mid-grey fallback off-image.
int lumaAt(const QImage &img, int x, int y)
{
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height())
        return 128;
    const QRgb p = img.pixel(x, y);
    return (299 * qRed(p) + 587 * qGreen(p) + 114 * qBlue(p)) / 1000;
}

// Average luma over image columns [x0, x1) at row y (sparsely sampled).
int avgLuma(const QImage &img, int x0, int x1, int y)
{
    x0 = qBound(0, x0, img.width());
    x1 = qBound(0, x1, img.width());
    if (x1 <= x0)
        return 128;
    const int step = qMax(1, (x1 - x0) / 16);
    long sum = 0;
    int n = 0;
    for (int x = x0; x < x1; x += step) {
        sum += lumaAt(img, x, y);
        ++n;
    }
    return n ? static_cast<int>(sum / n) : 128;
}

// A colour that contrasts with the given luma: light on dark, dark on light.
QColor contrastColour(int luma, int alpha = 200)
{
    return luma < 128 ? QColor(255, 255, 255, alpha) : QColor(0, 0, 0, alpha);
}
}

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

void FramebufferView::mousePressEvent(QMouseEvent *event)
{
    const QRectF dst = frameRect();
    if (m_image.isNull() || !dst.contains(event->position())) {
        QWidget::mousePressEvent(event);
        return;
    }
    // Widget point -> framebuffer image-pixel coordinates.
    const double ix = (event->position().x() - dst.left()) / dst.width() * m_image.width();
    const double iy = (event->position().y() - dst.top()) / dst.height() * m_image.height();
    emit imageClicked(QPointF(ix, iy));
}

void FramebufferView::setBeam(const BeamMarker &marker)
{
    m_beam = marker;
    update();
}

void FramebufferView::setWriteMarks(const QVector<WriteMark> &marks)
{
    m_writeMarks = marks;
    update();
}

void FramebufferView::paintMarks(QPainter &painter, const QRectF &dst,
                                 double sx, double sy) const
{
    if (m_writeMarks.isEmpty())
        return;
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const WriteMark &m : m_writeMarks) {
        const double x = dst.left() + m.pos.x() * sx;
        const double y = dst.top() + m.pos.y() * sy;
        const double r = m.highlight ? 4.5 : 2.5;
        // Adaptive outline: the value-coloured fill can match the background it
        // set, so contrast the outline against the frame behind the marker.
        const int luma = lumaAt(m_image, static_cast<int>(m.pos.x() + 0.5),
                                static_cast<int>(m.pos.y() + 0.5));
        painter.setBrush(m.color);
        painter.setPen(QPen(contrastColour(luma, 255), 1.0));
        painter.drawEllipse(QPointF(x, y), r, r);
        if (m.highlight) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(contrastColour(luma, 255), 1.6));
            painter.drawEllipse(QPointF(x, y), r + 2.5, r + 2.5);
        }
    }
}

QRectF FramebufferView::frameRect() const
{
    if (m_image.isNull())
        return QRectF();
    const QSize target = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - target.width()) / 2.0,
                          (height() - target.height()) / 2.0),
                  QSizeF(target));
}

void FramebufferView::paintBeam(QPainter &painter, const QRectF &dst,
                                double sx, double sy) const
{
    if (!m_beam.valid)
        return;
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_beam.vblank) {
        // The beam is between rendered rows (e.g. after a break, at VBL). Show a
        // banner at the top edge with the position, so it never looks broken.
        painter.setPen(QPen(kVblankCol, 1.0, Qt::DashLine));
        painter.drawLine(QPointF(dst.left(), dst.top() + 1), QPointF(dst.right(), dst.top() + 1));
        if (!m_beam.label.isEmpty()) {
            painter.setPen(kVblankCol);
            painter.drawText(QPointF(dst.left() + 6, dst.top() + 15), m_beam.label);
        }
        return;
    }

    const double wy = dst.top() + m_beam.y * sy;
    if (m_beam.scanline && !m_image.isNull()) {
        // Adaptive colour: sample the frame behind the line per segment and pick
        // light-on-dark / dark-on-light, so the scanline stays legible over both
        // borders and content. Per-segment (not per-pixel) avoids dither noise.
        const int iy = qBound(0, static_cast<int>(m_beam.y + 0.5), m_image.height() - 1);
        const int w = m_image.width();
        constexpr int kSegments = 32;
        for (int s = 0; s < kSegments; ++s) {
            const int ix0 = s * w / kSegments;
            const int ix1 = (s + 1) * w / kSegments;
            painter.setPen(QPen(contrastColour(avgLuma(m_image, ix0, ix1, iy)), 1.0));
            painter.drawLine(QPointF(dst.left() + ix0 * sx, wy),
                             QPointF(dst.left() + ix1 * sx, wy));
        }
    }

    if (m_beam.crosshair) {
        const double wx = dst.left() + m_beam.x * sx;
        painter.setPen(QPen(kMarkerCol, 1.5));
        painter.drawLine(QPointF(wx - kCross, wy), QPointF(wx + kCross, wy));
        painter.drawLine(QPointF(wx, wy - kCross), QPointF(wx, wy + kCross));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(wx, wy), kCross, kCross);
        if (!m_beam.label.isEmpty()) {
            painter.setPen(kMarkerCol);
            QPointF tp(wx + kCross + 4, wy - kCross - 2);
            if (tp.x() > dst.right() - 120)
                tp.setX(wx - kCross - 4 - 110);
            if (tp.y() < dst.top() + 12)
                tp.setY(wy + kCross + 12);
            painter.drawText(tp, m_beam.label);
        }
    } else if (m_beam.scanline && !m_beam.label.isEmpty()) {
        // Scanline visible but cycle in horizontal blanking: label the line.
        painter.setPen(kMarkerCol);
        painter.drawText(QPointF(dst.left() + 6, wy - 4), m_beam.label);
    }
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

    const QRectF dst = frameRect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(dst, m_image);

    const double sx = dst.width() / m_image.width();
    const double sy = dst.height() / m_image.height();
    paintMarks(painter, dst, sx, sy);
    paintBeam(painter, dst, sx, sy);
}

QImage FramebufferView::composite() const
{
    if (m_image.isNull())
        return QImage();
    QImage out = m_image.convertToFormat(QImage::Format_ARGB32);
    QPainter p(&out);
    // Draw directly in image-pixel space (transform is identity).
    const QRectF dst(QPointF(0, 0), QSizeF(out.size()));
    paintMarks(p, dst, 1.0, 1.0);
    paintBeam(p, dst, 1.0, 1.0);
    return out;
}