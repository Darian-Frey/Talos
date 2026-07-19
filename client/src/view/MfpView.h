// MfpView — Phase 6: the MC68901 MFP timers + interrupt controller, made legible.
// A timer table (mode / prescaler / data / frequency / interrupts-per-frame) and
// an interrupt matrix (enabled / unmasked / pending / in-service) with the timer
// rows highlighted. Read from the live machine; Talos decodes, never emulates.

#pragma once

#include <QWidget>

#include "model/MfpState.h"

class QTableWidget;
class QPushButton;
class QLabel;

class MfpView : public QWidget
{
    Q_OBJECT
public:
    explicit MfpView(QWidget *parent = nullptr);

    void setState(const Mfp::State &state);
    void setBusy(bool busy);

signals:
    void readRequested();

private:
    QPushButton *m_read = nullptr;
    QTableWidget *m_timers = nullptr;
    QTableWidget *m_ints = nullptr;
    QLabel *m_note = nullptr;
};
