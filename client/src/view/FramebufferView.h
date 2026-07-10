// FramebufferView — displays Hatari's taken framebuffer (D-007) with a beam-
// position overlay (F-203).
//
// The frame is painted centred and aspect-preserved with crisp pixels; the beam
// overlay is registered to it. The overlay is described in image-pixel space by
// a BeamMarker (the geometry lives in BeamGeometry); this widget only draws it.

#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

// What to draw for the beam, in framebuffer image-pixel coordinates.
struct BeamMarker
{
    bool valid = false;       // anything to draw at all
    bool scanline = false;    // draw the horizontal current-scanline line at y
    double y = 0.0;
    bool crosshair = false;   // draw the beam crosshair at (x, y)
    double x = 0.0;
    bool vblank = false;      // beam is in vertical blanking -> top banner instead
    QString label;
};

class FramebufferView : public QWidget
{
    Q_OBJECT
public:
    explicit FramebufferView(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    bool hasImage() const { return !m_image.isNull(); }
    QSize imageSize() const { return m_image.size(); }

    void setBeam(const BeamMarker &marker);

    // Captured register-write positions (F-203), in image-pixel coordinates.
    struct WriteMark
    {
        QPointF pos;
        QColor color = QColor(0, 200, 255);
        bool highlight = false;
    };
    void setWriteMarks(const QVector<WriteMark> &marks);

    // The current frame with the beam + write overlay burned in, in image-pixel
    // space (no widget scaling). Used for headless verification and, later, export.
    QImage composite() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRectF frameRect() const;
    // Draws the overlays with the given image->target transform.
    void paintBeam(QPainter &painter, const QRectF &dst, double sx, double sy) const;
    void paintMarks(QPainter &painter, const QRectF &dst, double sx, double sy) const;

    QImage m_image;
    BeamMarker m_beam;
    QVector<WriteMark> m_writeMarks;
};