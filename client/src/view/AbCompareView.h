// AbCompareView — Phase 6: run the same effect on two machines/regions and show
// the frames side by side with a per-scanline diff — where the two diverge (e.g.
// an STE effect that is blank on a plain ST, or the STE palette/prefetch shift
// changing an ST effect). Extends the F-207 ST<->STE differential to whole frames.
//
// Talos captures via the ab_compare harness (each machine headless) and diffs the
// frames here; it emulates nothing. The compared effect is the last one built/run.

#pragma once

#include <QImage>
#include <QVector>
#include <QWidget>

#include "model/Machine.h"

class QComboBox;
class QPushButton;
class QLabel;

class AbCompareView : public QWidget
{
    Q_OBJECT
public:
    explicit AbCompareView(QWidget *parent = nullptr);

    void setFrames(const QImage &a, const QImage &b, const QString &labelA,
                   const QString &labelB);
    void setBusy(bool busy);
    void setStatus(const QString &text, bool ok);

signals:
    void compareRequested(MachineType a, MachineType b);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    MachineType machineAt(QComboBox *box) const;

    QComboBox *m_a = nullptr;
    QComboBox *m_b = nullptr;
    QPushButton *m_compare = nullptr;
    QLabel *m_status = nullptr;

    // The paint surface draws the two frames + a diff strip.
    QImage m_frameA, m_frameB;
    QString m_labelA, m_labelB;
    QVector<bool> m_rowDiff;   // per full-res scanline: frames differ
    int m_diffRows = 0;
};
