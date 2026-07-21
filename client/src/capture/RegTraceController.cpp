#include "RegTraceController.h"

#include <QTimer>

#include "model/MachineState.h"

namespace {
constexpr int kPollIntervalMs = 3;
constexpr int kMaxPolls = 3000;   // ~9 s: generous, in case the effect boots slowly
}

RegTraceController::RegTraceController(RdbClient *rdb, QObject *parent)
    : QObject(parent), m_rdb(rdb), m_poll(new QTimer(this))
{
    m_poll->setSingleShot(true);
    m_poll->setInterval(kPollIntervalMs);
    connect(m_poll, &QTimer::timeout, this, &RegTraceController::doPoll);
}

void RegTraceController::captureFrame()
{
    if (m_running || !m_rdb->isConnected())
        return;
    m_running = true;
    m_events.clear();

    // Stop, wait until actually stopped, read the current VBL, then: run to the
    // next VBL (a frame boundary), enable the trace, run one full frame, dump.
    m_rdb->breakExec([this](const RdbClient::Tokens &) {
        pollUntilStopped(
            [this] {
                m_rdb->reqRegs([this](const RdbClient::Tokens &t) {
                    const MachineState st = MachineState::fromRegsReply(t);
                    const quint32 v = st.vbl().value_or(0);
                    runToVbl(v + 1, [this, v] {
                        m_rdb->sendCommand("regtrace on 0", [this, v](const RdbClient::Tokens &) {
                            runToVbl(v + 2, [this] {
                                m_rdb->sendCommand("regtrace", [this](const RdbClient::Tokens &d) {
                                    parseDump(d);
                                    m_rdb->sendCommand("regtrace off",
                                                       [this](const RdbClient::Tokens &) {
                                                           m_running = false;
                                                           emit finished(true, QString());
                                                       });
                                });
                            });
                        });
                    });
                });
            },
            QStringLiteral("could not stop the emulator"));
    });
}

void RegTraceController::runToVbl(quint32 target, std::function<void()> then)
{
    const QByteArray cmd = "bp VBL = " + QByteArray::number(target) + " :once";
    m_rdb->sendCommand(cmd, [this, then](const RdbClient::Tokens &r) {
        if (!m_running)
            return;
        if (r.isEmpty() || r.first() != "OK") {
            fail(QStringLiteral("VBL breakpoint rejected"));
            return;
        }
        m_rdb->run();
        pollUntilStopped(then, QStringLiteral("never reached the target VBL"));
    });
}

void RegTraceController::pollUntilStopped(std::function<void()> then, const QString &failMsg)
{
    m_pollThen = std::move(then);
    m_pollFail = failMsg;
    m_pollCount = 0;
    doPoll();
}

void RegTraceController::doPoll()
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
        m_poll->start();
    });
}

void RegTraceController::parseDump(const RdbClient::Tokens &t)
{
    m_events.clear();
    if (t.size() < 2 || t.first() != "OK")
        return;
    bool ok = false;
    const int count = t[1].toInt(&ok, 16);
    if (!ok)
        return;
    m_events.reserve(count);
    for (int i = 0; i < count; ++i) {
        const int o = 2 + i * 4;   // per entry: addr, scanline, lineCycle, value
        if (o + 3 >= t.size())
            break;
        WriteEvent e;
        e.address = t[o].toUInt(nullptr, 16);
        e.scanline = static_cast<qint16>(t[o + 1].toUShort(nullptr, 16));
        e.cycleInLine = static_cast<qint16>(t[o + 2].toUShort(nullptr, 16));
        e.value = t[o + 3].toUShort(nullptr, 16);
        e.frameCycle = quint32(qMax(0, e.scanline)) * 512u + quint32(qMax(0, e.cycleInLine));
        m_events.append(e);
    }
}

void RegTraceController::fail(const QString &reason)
{
    if (!m_running)
        return;
    m_running = false;
    m_poll->stop();
    // Best-effort: leave the trace disabled so it can't perturb later runs.
    m_rdb->sendCommand("regtrace off");
    emit finished(false, reason);
}
