// HatariLauncher — starts (or attaches to) the patched Hatari fork (F-214, M0 part).
//
// Two-process architecture (D-004): this owns the Hatari child process. For M0
// it launches Hatari with a TOS, machine type and the remote-debug listener
// (always on, port 56001). "Attach" mode skips launching and connects to an
// already-running instance. The remote socket itself is driven by RdbClient.

#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class HatariLauncher : public QObject
{
    Q_OBJECT
public:
    struct Config
    {
        QString hatariBinary;         // path to the fork's `hatari` executable
        QString tosImage;             // path to TOS/EmuTOS ROM (C-009, user-supplied)
        QString machine = "st";       // st | ste | megast | megaste | tt | falcon
        bool headless = false;        // run with dummy SDL video/audio (no window)
        bool hideStatusBar = true;    // hide Hatari's own status-bar overlay
        quint16 remotePort = 56001;   // fixed by the fork (RDB_PORT)
    };

    explicit HatariLauncher(QObject *parent = nullptr);
    ~HatariLauncher() override;

    // Returns false (and emits failed) if the binary/TOS are missing.
    bool launch(const Config &cfg);
    void terminate();
    bool isRunning() const;

signals:
    void started();
    void failed(const QString &reason);
    void stopped(int exitCode);
    void logLine(const QString &line);   // forwarded Hatari stderr, for diagnostics

private:
    QProcess *m_process = nullptr;
};
