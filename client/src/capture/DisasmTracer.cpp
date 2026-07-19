#include "DisasmTracer.h"

#include <QFile>
#include <QRegularExpression>
#include <QTimer>

#include "model/MachineState.h"
#include "protocol/RdbClient.h"

DisasmTracer::DisasmTracer(RdbClient *rdb, QObject *parent)
    : QObject(parent), m_rdb(rdb)
{
}

void DisasmTracer::start(int count, const QString &outFile)
{
    if (m_running || count < 1)
        return;
    m_running = true;
    m_count = count;
    m_i = 0;
    m_outFile = outFile;
    m_entries.clear();
    m_entries.reserve(count);

    // Truncate the redirect file (Hatari re-opens it in append mode).
    QFile f(m_outFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.close();

    m_rdb->breakExec([this](const RdbClient::Tokens &) {
        m_rdb->sendCommand("setstd " + m_outFile.toUtf8(),
                           [this](const RdbClient::Tokens &) { iter(); });
    });
}

void DisasmTracer::iter()
{
    if (m_i >= m_count) {
        finalize();
        return;
    }
    m_rdb->reqRegs([this](const RdbClient::Tokens &tokens) {
        const MachineState st = MachineState::fromRegsReply(tokens);
        DisasmEntry e;
        e.pc = st.pc().value_or(0);
        e.scanline = static_cast<int>(st.hbl().value_or(0));
        e.cycleInLine = static_cast<int>(st.lineCycles().value_or(0));
        e.frameCycle = st.frameCycles().value_or(0);
        m_entries.append(e);
        // Disassemble from PC (output goes to the redirect file), then step.
        m_rdb->sendCommand("console disasm", [this](const RdbClient::Tokens &) {
            m_rdb->step([this](const RdbClient::Tokens &) {
                ++m_i;
                iter();
            });
        });
    });
}

void DisasmTracer::finalize()
{
    // Close the redirect (flushes the file), then read + parse it.
    m_rdb->sendCommand("setstd", [this](const RdbClient::Tokens &) {
        QTimer::singleShot(120, this, [this] {
            QHash<quint32, QString> dis;
            QFile f(m_outFile);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                // "00E0086E 52B8 0466        addq.l #$01,$00000466.w"
                static const QRegularExpression re(
                    QStringLiteral("^([0-9A-Fa-f]{6,8})\\s+(?:[0-9A-Fa-f]{4}\\s+)+(\\S.*)$"));
                const QString all = QString::fromUtf8(f.readAll());
                for (const QString &line : all.split('\n')) {
                    const auto m = re.match(line);
                    if (m.hasMatch()) {
                        const quint32 addr = m.captured(1).toUInt(nullptr, 16);
                        if (!dis.contains(addr))
                            dis.insert(addr, m.captured(2).trimmed());
                    }
                }
            }
            for (int i = 0; i < m_entries.size(); ++i) {
                m_entries[i].text = dis.value(m_entries[i].pc, QStringLiteral("?"));
                // Instruction cost = how far the beam advanced before the next one.
                if (i + 1 < m_entries.size()) {
                    const qint64 d = qint64(m_entries[i + 1].frameCycle) - m_entries[i].frameCycle;
                    m_entries[i].cost = (d >= 0 && d < 100000) ? int(d) : 0;   // 0 across a VBL wrap
                }
            }
            m_running = false;
            emit finished(true, QString());
        });
    });
}
