// DisasmView — Phase 6: live disassembly synced to the beam. A "Trace" runs the
// next N instructions from PC and tabulates each with where it lands on the beam
// (scanline + cycle) and how many cycles it took — re-aiming hrdb's raw pieces at
// "this write happens here". Video-register writes ($ff82xx) are highlighted, and
// selecting a row parks the beam overlay at that instruction.

#pragma once

#include <QVector>
#include <QWidget>

#include "capture/DisasmTracer.h"

class QSpinBox;
class QPushButton;
class QTableWidget;
class QLabel;

class DisasmView : public QWidget
{
    Q_OBJECT
public:
    explicit DisasmView(QWidget *parent = nullptr);

    void setEntries(const QVector<DisasmEntry> &entries);
    void setBusy(bool busy);
    void setStatus(const QString &text, bool ok);

signals:
    void traceRequested(int count);
    void rowActivated(int scanline, int cycleInLine);   // park the beam here

private:
    QSpinBox *m_count = nullptr;
    QPushButton *m_trace = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
    QVector<DisasmEntry> m_entries;
};
