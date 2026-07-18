// ScanlineBudgetView — Phase 6: the per-scanline CPU cycle budget, as a gauge.
//
// One ST scanline is a fixed number of CPU cycles (512 PAL / 508 NTSC at 8 MHz,
// sourced from BeamGeometry / Hatari — never hand-counted, C-007). That is the
// budget for all of an effect's per-line work. This gauge draws the line, the
// visible-display window, and where the authored effect's per-line register
// writes land on it — so you can see how much budget the effect uses and whether
// it fits. On a dual-speed Mega STE the 16 MHz budget doubles to 1024, which is
// why an effect that overflows at 8 MHz can still hold at 16 MHz (ties to F-210).
//
// No Q_OBJECT — it only paints from a model MainWindow feeds it.

#pragma once

#include <QVector>
#include <QWidget>

class ScanlineBudgetView : public QWidget
{
public:
    struct Model
    {
        bool valid = false;
        int cyclesPerLine = 512;      // the 8 MHz per-line budget (508 NTSC)
        bool dualSpeed = false;       // Mega STE — also show the 16 MHz (2x) budget
        int visibleStart = 0;         // display window start, in cycles
        int visibleEnd = 512;         // display window end, in cycles
        QVector<int> writeCycles;     // per-line register-write positions, in cycles
        QString note;                 // one-line summary for the mode
    };

    explicit ScanlineBudgetView(QWidget *parent = nullptr);
    void setModel(const Model &m);
    QSize sizeHint() const override { return {640, 96}; }
    QSize minimumSizeHint() const override { return {320, 88}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Model m_model;
};
