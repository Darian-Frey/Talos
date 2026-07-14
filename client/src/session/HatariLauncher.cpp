#include "HatariLauncher.h"

#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>

HatariLauncher::HatariLauncher(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::started, this, &HatariLauncher::started);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit failed(m_process->errorString());
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { emit stopped(code); });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        const QList<QByteArray> lines = m_process->readAllStandardError().split('\n');
        for (const QByteArray &l : lines) {
            const QString s = QString::fromLocal8Bit(l).trimmed();
            if (!s.isEmpty())
                emit logLine(s);
        }
    });
}

HatariLauncher::~HatariLauncher()
{
    terminate();
}

bool HatariLauncher::launch(const Config &cfg)
{
    if (!QFileInfo::exists(cfg.hatariBinary)) {
        emit failed(QStringLiteral("Hatari binary not found: %1").arg(cfg.hatariBinary));
        return false;
    }
    if (!QFileInfo::exists(cfg.tosImage)) {
        emit failed(QStringLiteral("TOS image not found: %1").arg(cfg.tosImage));
        return false;
    }

    QStringList args;

    // Ignore the user config so a stray floppy/boot setting can't hijack the run.
    if (cfg.cleanConfig && m_scratch.isValid()) {
        const QString cfgPath = m_scratch.filePath("empty.cfg");
        QFile f(cfgPath);
        if (f.open(QIODevice::WriteOnly))
            f.close();   // an empty file is a valid, do-nothing Hatari config
        args << "--configfile" << cfgPath;
    }

    args << "--machine" << cfg.machine
         << "--tos" << cfg.tosImage
         << "--sound" << "off";
    if (cfg.cpuClock == 8 || cfg.cpuClock == 16)
        args << "--cpuclock" << QString::number(cfg.cpuClock);   // Mega STE 8/16 MHz (F-210)
    if (cfg.monoMonitor)
        args << "--monitor" << "mono";   // high-res 640x400 (BUG-003)
    if (cfg.bootFastForward)
        args << "--fast-forward" << "on";   // fast boot; client turns it off (BUG-007)
    if (!cfg.memStateFile.isEmpty())
        args << "--memstate" << cfg.memStateFile;   // restore a saved state (F-217)
    if (!cfg.country.isEmpty())
        args << "--country" << cfg.country;   // selects PAL/NTSC via the TOS
    if (cfg.hideStatusBar)
        args << "--statusbar" << "off";
    if (!cfg.gemdosDir.isEmpty())
        args << "-d" << cfg.gemdosDir;   // mounts as C:; AUTO\*.PRG auto-runs

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (cfg.headless) {
        // Off-screen rendering: no Hatari window, but the surface still renders
        // so `console screenshot` works (verified in Phase 0).
        env.insert("SDL_VIDEODRIVER", "dummy");
        env.insert("SDL_AUDIODRIVER", "dummy");
    }
    m_process->setProcessEnvironment(env);

    m_process->start(cfg.hatariBinary, args);
    return true;
}

void HatariLauncher::terminate()
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    // A deliberate terminate makes QProcess emit errorOccurred(Crashed) and
    // finished(); those are not launch failures and must not be forwarded to
    // observers — especially since terminate() runs during MainWindow teardown,
    // where a re-entrant slot touching the half-destroyed window would crash.
    const QSignalBlocker blocker(m_process);
    m_process->terminate();
    if (!m_process->waitForFinished(2000)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool HatariLauncher::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}
