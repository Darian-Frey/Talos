// CaptureController — captures a sequence of writes to a watched register, with
// each write's beam position (the register-write-to-cycle mapping, F-203).
//
// Pure B1 (no fork patch, D-005): it drives Hatari over the remote protocol as an
// async state machine — break once, then repeatedly arm a one-shot change
// breakpoint on the register, run to the next write, and read the beam position
// and value. A breakpoint only arms on the stopped->run transition, so the
// initial break matters. See docs/phase-1/register-writes.md.

#pragma once

#include <QObject>
#include <QVector>
#include <functional>

#include "model/WriteEvent.h"

class RdbClient;
class QTimer;

class CaptureController : public QObject
{
    Q_OBJECT
public:
    explicit CaptureController(RdbClient *rdb, QObject *parent = nullptr);

    // Capture up to maxWrites writes to the word register at `address`.
    void start(quint32 address, int maxWrites);
    void cancel();
    bool isRunning() const { return m_running; }

    const QVector<WriteEvent> &events() const { return m_events; }
    quint32 address() const { return m_address; }

signals:
    void progress(int count, int target);
    void finished(bool ok, const QString &reason);

private:
    // Poll `status` until the emulator is stopped, then invoke `then`.
    void pollUntilStopped(std::function<void()> then, const QString &failMsg);
    void doPoll();
    void armAndRun();       // emulator must be stopped on entry
    void onStopped();       // a write breakpoint has fired
    void succeed();
    void fail(const QString &reason);

    RdbClient *m_rdb = nullptr;
    QTimer *m_pollTimer = nullptr;
    QVector<WriteEvent> m_events;
    std::function<void()> m_pollThen;   // continuation once stopped
    QString m_pollFail;                 // message if the poll times out
    quint32 m_address = 0;
    int m_target = 0;
    int m_pollCount = 0;
    bool m_running = false;

    static constexpr int kMaxPolls = 400;   // ~a few seconds before giving up
};
