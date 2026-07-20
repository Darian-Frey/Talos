// MainWindow — the M0 Talos shell.
//
// Assembles the framebuffer view, a register/counter panel and run/stop/step
// controls over an RdbClient + HatariLauncher. This is the M0 exit criterion:
// connect over the socket, read registers + VBL/HBL/cycle counters, run/stop/
// step, and display Hatari's framebuffer.

#pragma once

#include <QMainWindow>
#include <QStringList>
#include <QTemporaryDir>

#include "model/Machine.h"
#include "model/MachineState.h"
#include "model/BorderCodegen.h"
#include "model/GifWriter.h"
#include "model/RasterCodegen.h"
#include "model/WriteEvent.h"
#include "session/HatariLauncher.h"
#include "view/BeamGeometry.h"

class RdbClient;
class FramebufferView;
class PaletteView;
class BlitterTrafficView;
class DmaSoundView;
class RasterWorkspace;
class ScrollerWorkspace;
class BorderWalkthroughView;
class SyncScrollView;
class ReconstructView;
class DisasmView;
class DisasmTracer;
class MfpView;
class AbCompareView;
class Spectrum512View;
class StPictureView;
class ScanlineBudgetView;
class LedToolButton;
class CaptureController;
class QProcess;
class QImage;
class QAction;
class QTableWidget;
class QLabel;
class QTimer;
class QSlider;
class QToolButton;
class QSpinBox;
class QLineEdit;
class QComboBox;
class QMenu;

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
        // Whether each was set explicitly on the command line — an explicit arg
        // overrides the persisted (last-used) selector; otherwise we restore it.
        bool machineExplicit = false;
        bool regionExplicit = false;
        bool languageExplicit = false;
        bool fastBoot = true;                        // fast-forward the effect boot (BUG-007)
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
    void saveState();           // F-217: snapshot the whole machine to a file
    void loadState();           // F-217: relaunch restoring a saved snapshot
    void onMachineChanged(int index);
    void onRegionChanged(int index);
    void onMemoryChanged(int index);
    void persistBootSelectors();   // save machine/RAM/region/language to QSettings
    void onLanguageChanged(int index);
    void onClockChanged(int index);   // Mega STE 8/16 MHz (F-210) -> relaunch
    void onConnected();
    void onConnectionFailed(const QString &reason);
    void onNotification(const QByteArray &name, const QList<QByteArray> &args);
    void refresh();             // pull regs + a fresh framebuffer (one-off, machine stopped)
    void liveTick();            // live-timer tick: coherent frame grab while running
    void grabCoherentFrame();   // tear-free grab: fast-forward to a VBL, shot, resume
    void toggleRecording(bool on);   // F-Phase6: record live frames -> animated GIF
    void recordFrame(const QImage &raw);   // append a coherent frame while recording
    void doBreak();
    void doRun();
    void doStep();
    void runToLine();   // park the beam on a chosen visible scanline (F-203)
    void onCaptureClicked();
    void captureBlitTraffic();   // F-208: enable trace, run a window, dump + show
    void captureDmaSound();      // F-209: enable trace, run a window, dump + show
    void buildRasterEffect(const QVector<RasterCodegen::Bar> &bars);   // F-212 codegen+run
    void verifyRasterEffect(const QVector<RasterCodegen::Bar> &bars);  // F-212 round-trip
    void exportRasterEffect(const QVector<RasterCodegen::Bar> &bars);  // F-212 asm + sequence
    void importRasterEffect();                                        // F-212 load a sequence
    void buildScrollerEffect(const QString &message, int speed);      // F-212 STE scroller
    void verifyScrollerEffect(const QString &message, int speed);
    void exportScrollerEffect(const QString &message, int speed);
    void importScrollerEffect();
    void buildBorderEffect(BorderCodegen::Border border);   // Phase 6: left-border removal
    void verifyBorderEffect(BorderCodegen::Border border);  // Phase 6: border-check harness
    void onFramebufferClicked(const QPointF &imagePixel);   // click-to-place authoring
    void onCaptureProgress(int count, int target);
    void onCaptureFinished(bool ok, const QString &reason);
    void onTimelineRowChanged(int row);
    void onScrub(int frameCycle);          // move the cursor through the captured frame
    void toggleScrubPlay(bool on);         // auto-sweep the beam through the frame

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
    void readRegionFromCore();   // BUG-002: read the actual region from $ff820a
    void checkBootFastForward(); // BUG-007: turn off boot fast-forward once the effect runs
    void endBootFastForward(const QString &why);
    bool updateBeamOverlay(QSize frameSize);   // returns whether the beam is on-frame
    bool showBeamAt(int scanline, int cycleInLine, QSize frameSize, const QString &prefix = {});
    void recomputeWriteMarks(QSize frameSize);
    void updateReconstruct();   // F-218: rebuild the register-reconstruction panel
    void readMfp();             // Phase 6: read + decode the MFP register block
    void openProgram();         // load + run a real ST program / disk image (file dialog)
    void loadFile(const QString &file);   // dispatch + load + boot a program/disk path
    void addRecent(const QString &file);  // push a path onto the recent-files list
    void removeRecent(const QString &file);
    void rebuildRecentMenu();             // repopulate the Open… dropdown from m_recentFiles
    void manageDisks();         // drive A/B disk manager: hot-swap, eject, desktop
    void compareMachines(MachineType a, MachineType b);   // Phase 6: A/B comparison
    void populateTimeline();
    void updateBudget();        // recompute the per-scanline cycle-budget gauge
    void setupScrub();          // arm the scrubber for the just-captured frame
    void setControlsEnabledForCapture(bool capturing);
    int cyclesPerLine() const;  // 512 PAL / 508 NTSC

    Config m_config;
    RdbClient *m_rdb = nullptr;
    HatariLauncher *m_launcher = nullptr;
    FramebufferView *m_fb = nullptr;
    PaletteView *m_palette = nullptr;
    BlitterTrafficView *m_blitView = nullptr;
    DmaSoundView *m_dmaView = nullptr;
    RasterWorkspace *m_raster = nullptr;
    ScrollerWorkspace *m_scroller = nullptr;
    BorderWalkthroughView *m_borderView = nullptr;
    SyncScrollView *m_syncScroll = nullptr;
    ReconstructView *m_reconstruct = nullptr;
    DisasmView *m_disasm = nullptr;
    DisasmTracer *m_tracer = nullptr;
    MfpView *m_mfp = nullptr;
    AbCompareView *m_ab = nullptr;
    QString m_disasmPath;   // scratch file for redirected disasm output
    ScanlineBudgetView *m_budget = nullptr;
    Spectrum512View *m_spectrum = nullptr;
    StPictureView *m_stPicture = nullptr;
    QTemporaryDir m_rasterDir;            // holds the generated .s + AUTO/RASTER.PRG
    QTemporaryDir m_scrollerDir;          // holds the generated scroller .s + AUTO/SCROLLER.PRG
    QTemporaryDir m_borderDir;            // holds the generated border .s + AUTO/BORDER.PRG
    QTemporaryDir m_loadDir;              // GEMDOS drive for a loaded .PRG/.TOS (AUTO/PROG.*)
    QProcess *m_rasterProc = nullptr;     // vasm / verify subprocess (one at a time)
    QTableWidget *m_regTable = nullptr;
    QTimer *m_liveTimer = nullptr;
    QTemporaryDir m_shotDir;
    QString m_shotPath;

    QComboBox *m_machineCombo = nullptr;
    QComboBox *m_memCombo = nullptr;   // ST RAM size (--memsize)
    QComboBox *m_languageCombo = nullptr;
    QComboBox *m_regionCombo = nullptr;
    QComboBox *m_clockCombo = nullptr;   // CPU clock 8/16 MHz (dual-speed machines only)
    QLabel *m_capsLabel = nullptr;

    QAction *m_actStart = nullptr;
    QAction *m_actStop = nullptr;
    QAction *m_actSaveState = nullptr;
    QAction *m_actLoadState = nullptr;
    QAction *m_actOpen = nullptr;
    QAction *m_actDisks = nullptr;
    QMenu *m_recentMenu = nullptr;        // recent-files dropdown on the Open… button
    QStringList m_recentFiles;            // persisted via QSettings ("recentFiles")
    static constexpr int kMaxRecent = 12;
    LedToolButton *m_fastBootBtn = nullptr;   // checkable + retro LED: fast-forward the boot
    QAction *m_actBreak = nullptr;
    QAction *m_actRun = nullptr;
    QAction *m_actStep = nullptr;
    QAction *m_actRefresh = nullptr;
    QAction *m_actLive = nullptr;
    QAction *m_actRecord = nullptr;
    QAction *m_actRunToLine = nullptr;
    QSpinBox *m_lineSpin = nullptr;
    QAction *m_actCapture = nullptr;
    QAction *m_actBlitCapture = nullptr;
    QAction *m_actDmaCapture = nullptr;
    QComboBox *m_depthCombo = nullptr;   // trace buffer entry cap (shared)
    QSpinBox *m_windowSpin = nullptr;    // trace run window in ms (shared)
    QLineEdit *m_regEdit = nullptr;
    QSpinBox *m_countSpin = nullptr;
    QTableWidget *m_timeline = nullptr;

    QLabel *m_connLabel = nullptr;
    QLabel *m_posLabel = nullptr;
    QLabel *m_captureLabel = nullptr;   // persistent last-capture result

    int m_refreshTick = 0;      // paces the rarely-changing reads (palette/region)
    bool m_grabbing = false;    // a coherent live grab is in flight (skip overlapping ticks)
    bool m_recording = false;   // capturing live frames into m_gif
    GifWriter m_gif;            // accumulates recorded frames; encoded on stop
    bool m_bootWatching = false;   // fast-forwarding boot, watching for the effect (BUG-007)
    int m_bootRamHits = 0;         // consecutive polls with PC stable in the effect's loop
    int m_bootPolls = 0;           // total watch polls (safety timeout)
    quint32 m_bootLastPc = 0;
    MachineState m_state;       // latest parsed regs/counters snapshot
    MachineType m_machine = MachineType::ST;      // selected machine
    VideoRegion m_region = VideoRegion::Pal50;    // selected region
    Language m_language = Language::English;       // selected language

    CaptureController *m_capture = nullptr;
    QVector<WriteEvent> m_writes;   // last captured register writes
    int m_highlightRow = -1;        // selected timeline row -> highlighted marker

    QSlider *m_scrub = nullptr;       // scrub the beam through the captured frame
    QToolButton *m_scrubPlay = nullptr;
    QLabel *m_scrubInfo = nullptr;
    QTimer *m_scrubTimer = nullptr;
    int m_scrubCycle = -1;            // cursor frame-cycle; -1 = not scrubbing (show all)
};
