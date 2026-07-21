// RegTraceController — F-220: capture one whole frame of hardware-register writes.
//
// Uses the patched `regtrace` command (B2, patches/0004): stop, run to a VBL
// boundary, enable the trace, run exactly one frame, dump. Each write comes back
// with its beam position (scanline + cycle-in-line), so the client can plot a
// whole frame's hardware activity. Answers C-004 in the socket's favour — a
// heavy frame (~6 k writes) is ~100 KB, dumped on demand.

#pragma once

#include <QObject>
#include <QVector>
#include <functional>

#include "model/WriteEvent.h"
#include "protocol/RdbClient.h"

class QTimer;

class RegTraceController : public QObject
{
    Q_OBJECT
public:
    explicit RegTraceController(RdbClient *rdb, QObject *parent = nullptr);

    void captureFrame();           // capture the next full frame's register writes
    bool isRunning() const { return m_running; }
    const QVector<WriteEvent> &events() const { return m_events; }

signals:
    void finished(bool ok, const QString &reason);

private:
    void pollUntilStopped(std::function<void()> then, const QString &failMsg);
    void doPoll();
    void runToVbl(quint32 target, std::function<void()> then);
    void parseDump(const RdbClient::Tokens &t);
    void fail(const QString &reason);

    RdbClient *m_rdb = nullptr;
    QTimer *m_poll = nullptr;
    bool m_running = false;
    std::function<void()> m_pollThen;
    QString m_pollFail;
    int m_pollCount = 0;
    QVector<WriteEvent> m_events;
};
