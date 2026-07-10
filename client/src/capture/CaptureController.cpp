#include "CaptureController.h"

#include "model/MachineState.h"
#include "protocol/MemCodec.h"
#include "protocol/RdbClient.h"

#include <QTimer>

CaptureController::CaptureController(RdbClient *rdb, QObject *parent)
    : QObject(parent)
    , m_rdb(rdb)
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setSingleShot(true);
    m_pollTimer->setInterval(3);
    connect(m_pollTimer, &QTimer::timeout, this, &CaptureController::doPoll);
}

void CaptureController::start(quint32 address, int maxWrites)
{
    if (m_running || !m_rdb->isConnected())
        return;
    m_address = address;
    m_target = qMax(1, maxWrites);
    m_events.clear();
    m_running = true;
    // Stop the emulator, then wait until it has *actually* stopped before arming
    // — a change breakpoint only arms on the following stopped->run transition,
    // and break's OK returns when the stop is requested, not when it lands.
    m_rdb->breakExec([this](const RdbClient::Tokens &) {
        pollUntilStopped([this] { armAndRun(); },
                         QStringLiteral("could not stop the emulator"));
    });
}

void CaptureController::cancel()
{
    if (!m_running)
        return;
    m_running = false;
    m_pollTimer->stop();
    emit finished(false, QStringLiteral("cancelled"));
}

void CaptureController::pollUntilStopped(std::function<void()> then, const QString &failMsg)
{
    m_pollThen = std::move(then);
    m_pollFail = failMsg;
    m_pollCount = 0;
    doPoll();
}

void CaptureController::doPoll()
{
    if (!m_running)
        return;
    m_rdb->reqStatus([this](const RdbClient::Tokens &t) {
        if (!m_running)
            return;
        const bool stopped = t.size() > 1 && t[1] == "0";   // status: OK, run?, PC
        if (stopped) {
            auto then = m_pollThen;
            m_pollThen = nullptr;
            if (then)
                then();
            return;
        }
        if (++m_pollCount > kMaxPolls) {
            fail(m_pollFail);
            return;
        }
        m_pollTimer->start();
    });
}

void CaptureController::armAndRun()
{
    if (!m_running)
        return;
    const QByteArray a = QByteArray::number(m_address, 16);
    const QByteArray cmd = "bp ( $" + a + " ).w ! ( $" + a + " ).w :once";
    m_rdb->sendCommand(cmd, [this](const RdbClient::Tokens &r) {
        if (!m_running)
            return;
        if (r.isEmpty() || r.first() != "OK") {
            fail(QStringLiteral("breakpoint rejected"));
            return;
        }
        m_rdb->run();
        // Now wait for the breakpoint to fire (the emulator stops again).
        pollUntilStopped([this] { onStopped(); },
                         QStringLiteral("no write to $%1 within timeout")
                             .arg(m_address, 0, 16));
    });
}

void CaptureController::onStopped()
{
    m_rdb->reqRegs([this](const RdbClient::Tokens &r) {
        if (!m_running)
            return;
        const MachineState st = MachineState::fromRegsReply(r);
        WriteEvent ev;
        ev.address = m_address;
        ev.scanline = static_cast<int>(st.value("HBL"));
        ev.cycleInLine = static_cast<int>(st.value("LineCycles"));
        ev.frameCycle = st.value("FrameCycles");
        ev.pc = st.value("PC");

        const QByteArray mem = "mem " + QByteArray::number(m_address, 16) + " 2";
        m_rdb->sendCommand(mem, [this, ev](const RdbClient::Tokens &m) mutable {
            if (!m_running)
                return;
            if (m.size() >= 4 && m.first() == "OK")
                ev.value = MemCodec::word(m.last());
            m_events.append(ev);
            emit progress(m_events.size(), m_target);
            if (m_events.size() >= m_target)
                succeed();
            else
                armAndRun();   // emulator is already stopped (bp hit)
        });
    });
}

void CaptureController::succeed()
{
    m_running = false;
    m_pollTimer->stop();
    emit finished(true, QString());
}

void CaptureController::fail(const QString &reason)
{
    m_running = false;
    m_pollTimer->stop();
    emit finished(false, reason);
}
