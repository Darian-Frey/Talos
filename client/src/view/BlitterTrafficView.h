// BlitterTrafficView — visualise B2 blitter memory traffic (F-208).
//
// Plots every captured bus access as a mark: X = cycle, Y = address, colour =
// read vs write. Blit-operation boundaries (the end-of-blit markers) are drawn
// as faint vertical rules, so a fill sweeping through screen memory over time is
// legible at a glance, and bus occupancy (density of marks) shows directly.

#pragma once

#include "model/BlitterTrace.h"

#include <QVector>
#include <QWidget>

class BlitterTrafficView : public QWidget
{
    Q_OBJECT
public:
    explicit BlitterTrafficView(QWidget *parent = nullptr);

    void setOps(const QVector<BlitOp> &ops);
    void clear() { setOps({}); }

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize minimumSizeHint() const override { return {320, 200}; }

private:
    QVector<BlitOp> m_ops;
    // Cached extents across all ops (recomputed in setOps). Read and write
    // addresses are tracked separately because they occupy far-apart memory
    // (source buffer vs screen) and each gets its own scaled lane.
    quint64 m_cycMin = 0, m_cycMax = 0;
    quint32 m_rMin = 0, m_rMax = 0;   // read (source) address extent
    quint32 m_wMin = 0, m_wMax = 0;   // write (dest) address extent
    int m_reads = 0, m_writes = 0, m_blits = 0;
};
