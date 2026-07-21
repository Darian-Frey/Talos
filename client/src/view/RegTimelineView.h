// RegTimelineView — F-220: a whole frame's hardware-register writes on the beam.
//
// Captures one frame via RegTraceController (the patched `regtrace`) and plots
// every write on a 2-D beam grid (X = cycle across a scanline, Y = scanline down
// the frame), colour-coded by register class (palette / sync / resolution / STE
// scroll / video base / …). This is the whole-frame view the single-register
// capture can't give — you see all of an effect's hardware activity at once, and
// exactly where on the beam each write lands. (C-004 measured: it fits the socket.)

#pragma once

#include <QVector>
#include <QWidget>

#include "model/WriteEvent.h"
#include "view/BeamGeometry.h"   // VideoRegion

// Register classes for colouring (by address).
namespace RegClass {
struct Info { const char *name; QColor colour; };
int classify(quint32 addr);        // index into infos()
const QVector<Info> &infos();
}

// The 2-D plot (no Q_OBJECT — it only paints from data the view sets).
class RegTimelinePlot : public QWidget
{
public:
    explicit RegTimelinePlot(QWidget *parent = nullptr);
    void setData(const QVector<WriteEvent> &events, VideoRegion region);
    QSize sizeHint() const override { return {560, 360}; }
    QSize minimumSizeHint() const override { return {360, 240}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<WriteEvent> m_events;
    VideoRegion m_region = VideoRegion::Pal50;
};

class QPushButton;
class QLabel;

class RegTimelineView : public QWidget
{
    Q_OBJECT
public:
    explicit RegTimelineView(QWidget *parent = nullptr);

    void setEvents(const QVector<WriteEvent> &events, VideoRegion region);
    void setBusy(bool busy);
    void setStatus(const QString &text, bool ok);

signals:
    void captureRequested();

private:
    QPushButton *m_capture = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_legend = nullptr;
    RegTimelinePlot *m_plot = nullptr;
};
