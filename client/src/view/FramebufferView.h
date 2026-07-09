// FramebufferView — displays Hatari's taken framebuffer (D-007).
//
// It holds the most recent frame (a PNG grabbed via `console screenshot`) and
// paints it centred, scaled to fit while preserving aspect ratio, with nearest-
// neighbour sampling so ST pixels stay crisp. This is the visual base that
// Phase 1's beam/register overlays will later be drawn on top of.

#pragma once

#include <QImage>
#include <QWidget>

class FramebufferView : public QWidget
{
    Q_OBJECT
public:
    explicit FramebufferView(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    bool hasImage() const { return !m_image.isNull(); }
    QSize imageSize() const { return m_image.size(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
};
