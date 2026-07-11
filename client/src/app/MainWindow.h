// MainWindow — the M0 Talos shell.
//
// Assembles the framebuffer view, a register/counter panel and run/stop/step
// controls over an RdbClient + HatariLauncher. This is the M0 exit criterion:
// connect over the socket, read registers + VBL/HBL/cycle counters, run/stop/
// step, and display Hatari's framebuffer.

#pragma once

#include <QMainWindow>
#include <QTemporaryDir>

#include "model/Machine.h"
#include "model/MachineState.h"
#include "model/WriteEvent.h"
#include "session/HatariLauncher.h"
#include "view/BeamGeometry.h"

class RdbClient;
class FramebufferView;
class PaletteView;
class CaptureController;
class QImage;
class QAction;
class QTableWidget;
class QLabel;
class QTimer;
class QSpinBox;
class QLineEdit;
class QComboBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    struct Config
    {
        HatariLauncher::Config hatari;   // launch parameters
        bool attachOnly = false;         // skip launching; connect to a running Hatari
        QString host = "127.0.0.1";
        MachineType machine = MachineType::ST;      // initial machine
        VideoRegion region = VideoRegion::Pal50;    // initial region
        Language language = Language::English;       // initial language
    };

    explicit MainWindow(Config config, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Programmatic entry points (used by the GUI and by --selftest*).
    void startSession();
    void beginCapture(quint32 address, int count);
    const QVector<WriteEvent> &capturedWrites() const { return m_writes; }

signals:
    // Emitted whenever a fresh framebuffer is taken and displayed. beamVisible is
    // true when the beam fell inside the rendered area (so the overlay is drawn).
    void frameReceived(const QImage &frame, bool beamVisible);
    // Emitted when a capture run finishes (a post-capture frame follows).
    void captureCompleted(int nWrites);

private slots:
    void onStartClicked();      // launch (or attach) + connect
    void doStop();              // terminate/disconnect the running machine
    void onMachineChanged(int index);
    void onRegionChanged(int index);
    void onLanguageChanged(int index);
    void onConnected();
    void onConnectionFailed(const QString &reason);
    void onNotification(const QByteArray &name, const QList<QByteArray> &args);
    void refresh();             // pull regs + a fresh framebuffer
    void doBreak();
    void doRun();
    void doStep();
    void runToLine();   // park the beam on a chosen visible scanline (F-203)
    void onCaptureClicked();
    void onCaptureProgress(int count, int target);
    void onCaptureFinished(bool ok, const QString &reason);
    void onTimelineRowChanged(int row);

private:
    void buildUi();
    void relaunch();            // re-launch Hatari with the current machine/region
    void reconcileRegion();     // sync the region combo to the language's actual region
    void updateLaunchStopState();
    void updateCapabilities();  // refresh the capability readout for the machine
    void updateRegisterPanel();
    void updateStatusBar();
    void setRunningControlsEnabled(bool connected);
    void refreshScreen();
    void refreshRegs();
    void refreshPalette();
    bool updateBeamOverlay(QSize frameSize);   // returns whether the beam is on-frame
    void recomputeWriteMarks(QSize frameSize);
    void populateTimeline();
    void setControlsEnabledForCapture(bool capturing);

    Config m_config;
    RdbClient *m_rdb = nullptr;
    HatariLauncher *m_launcher = nullptr;
    FramebufferView *m_fb = nullptr;
    PaletteView *m_palette = nullptr;
    QTableWidget *m_regTable = nullptr;
    QTimer *m_liveTimer = nullptr;
    QTemporaryDir m_shotDir;
    QString m_shotPath;

    QComboBox *m_machineCombo = nullptr;
    QComboBox *m_languageCombo = nullptr;
    QComboBox *m_regionCombo = nullptr;
    QLabel *m_capsLabel = nullptr;

    QAction *m_actStart = nullptr;
    QAction *m_actStop = nullptr;
    QAction *m_actBreak = nullptr;
    QAction *m_actRun = nullptr;
    QAction *m_actStep = nullptr;
    QAction *m_actRefresh = nullptr;
    QAction *m_actLive = nullptr;
    QAction *m_actRunToLine = nullptr;
    QSpinBox *m_lineSpin = nullptr;
    QAction *m_actCapture = nullptr;
    QLineEdit *m_regEdit = nullptr;
    QSpinBox *m_countSpin = nullptr;
    QTableWidget *m_timeline = nullptr;

    QLabel *m_connLabel = nullptr;
    QLabel *m_posLabel = nullptr;
    QLabel *m_captureLabel = nullptr;   // persistent last-capture result

    MachineState m_state;       // latest parsed regs/counters snapshot
    MachineType m_machine = MachineType::ST;      // selected machine
    VideoRegion m_region = VideoRegion::Pal50;    // selected region
    Language m_language = Language::English;       // selected language

    CaptureController *m_capture = nullptr;
    QVector<WriteEvent> m_writes;   // last captured register writes
    int m_highlightRow = -1;        // selected timeline row -> highlighted marker
};
