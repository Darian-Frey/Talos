// DmaSoundView — visualise B2 DMA sound + LMC1992 EQ (F-209).
//
// Top panel: the sample buffer draining — play-head position (Y, within
// [bufStart,bufEnd]) over cycle (X), a sawtooth that resets each loop. Bottom
// panel: the LMC1992 EQ curve, tilting with the latest bass/treble, plus the
// current control/mode and volume readout.

#pragma once

#include "model/DmaSndTrace.h"

#include <QWidget>

class DmaSoundView : public QWidget
{
    Q_OBJECT
public:
    explicit DmaSoundView(QWidget *parent = nullptr);

    void setTrace(const DmaSndTrace &trace);
    void clear() { setTrace(DmaSndTrace{}); }

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize minimumSizeHint() const override { return {360, 220}; }

private:
    void paintDrain(QPainter &p, const QRect &r);
    void paintEq(QPainter &p, const QRect &r);

    DmaSndTrace m_t;
};
