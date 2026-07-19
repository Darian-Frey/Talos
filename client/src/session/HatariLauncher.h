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
#include <QTemporaryDir>

class HatariLauncher : public QObject
{
    Q_OBJECT
public:
    struct Config
    {
        QString hatariBinary;         // path to the fork's `hatari` executable
        QString tosImage;             // path to TOS/EmuTOS ROM (C-009, user-supplied)
        QString machine = "st";       // st | ste | megast | megaste | tt | falcon
        int stRamSizeKB = 1024;       // ST RAM in KiB -> --memsize (512/1024/2048/4096);
                                      // 0 = leave Hatari's default
        int cpuClock = 0;             // 0 = default; 8/16 -> --cpuclock (Mega STE dual-speed, F-210)
        bool monoMonitor = false;     // --monitor mono -> high-res 640x400 (BUG-003 geometry)
        bool bootFastForward = false; // launch with --fast-forward on; the client turns it off
                                      // once the effect is running (BUG-007 faster boot)
        QString memStateFile;         // --memstate <file>: restore a saved state at launch (F-217)
        QString country;              // EmuTOS country -> PAL/NTSC (us=NTSC, de=PAL)
        bool headless = false;        // run with dummy SDL video/audio (no window)
        bool hideStatusBar = true;    // hide Hatari's own status-bar overlay
        quint16 remotePort = 56001;   // fixed by the fork (RDB_PORT)
        // Optional GEMDOS drive directory (mounted C:) — used to auto-run an
        // effect (or a loaded .PRG) from its AUTO folder (tests/effects/disk).
        QString gemdosDir;
        // Optional floppy disk images in drive A (--disk-a) and B (--disk-b) — used
        // to boot a real demo/program disk (.st/.msa/.stx/.dim/.ipf/.zip). Boots
        // the disk's bootsector if it is bootable; else TOS comes up with A: mounted.
        // Disks can also be swapped at runtime (patched `floppy` command, F-219).
        QString diskImage;
        QString diskB;
        // Ignore the user's ~/.config/hatari/hatari.cfg by launching with an
        // empty config, so a stray floppy setting can't hijack the boot drive
        // (BUG-001). On by default for reproducible runs.
        bool cleanConfig = true;
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
    QTemporaryDir m_scratch;   // holds the empty config file for cleanConfig
};
