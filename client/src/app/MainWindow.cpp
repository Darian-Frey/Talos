#include "MainWindow.h"

#include "capture/CaptureController.h"
#include "model/Palette.h"
#include "protocol/RdbClient.h"
#include "model/BlitterTrace.h"
#include "model/DmaSndTrace.h"
#include "model/RasterCodegen.h"
#include "view/BlitterTrafficView.h"
#include "view/DmaSoundView.h"
#include "view/RasterWorkspace.h"
#include "view/ScrollerWorkspace.h"
#include "view/Spectrum512View.h"
#include "view/StPictureView.h"
#include "view/ScanlineBudgetView.h"
#include "view/BorderWalkthroughView.h"
#include "view/SyncScrollView.h"
#include "view/ReconstructView.h"
#include "view/DisasmView.h"
#include "capture/DisasmTracer.h"
#include "view/MfpView.h"
#include "model/MfpState.h"
#include "protocol/MemCodec.h"
#include "view/AbCompareView.h"
#include "model/ScrollerCodegen.h"
#include "view/LedToolButton.h"
#include "view/CollapsibleDock.h"

#include <QDir>
#include <QFile>
#include <QDialog>
#include <QFileDialog>
#include <QGridLayout>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include "view/FramebufferView.h"
#include "view/PaletteView.h"

#include <QAction>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QHeaderView>
#include <algorithm>

#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kLiveIntervalMs = 70;    // ~14 Hz live refresh — each tick does a
                                       // coherent (fast-forward + VBL-synced) grab
                                       // so animated effects don't tear (see liveTick)
constexpr int kMaxRecordFrames = 300;  // ~21 s at the live rate — bounds GIF memory
constexpr int kRecordDelayCs = 7;      // per-frame GIF delay (1/100 s) ~= the live cadence
constexpr int kCoherentGrabMs = 30;    // settle time for the fast-forward run-to-VBL
                                       // before grabbing (2 frames complete in <1 ms
                                       // under fast-forward; this covers socket latency)

// Decode an ST palette value (%0000 0RRR 0GGG 0BBB, 3 bits/gun) to an RGB colour,
// so a captured colour-register write is drawn in the colour it set.
QColor stColour(quint32 value)
{
    const int r = (value >> 8) & 7;
    const int g = (value >> 4) & 7;
    const int b = value & 7;
    return QColor(r * 36, g * 36, b * 36);
}
}

MainWindow::MainWindow(Config config, QWidget *parent)
    : QMainWindow(parent)
    , m_config(std::move(config))
    , m_rdb(new RdbClient(this))
    , m_launcher(new HatariLauncher(this))
    , m_liveTimer(new QTimer(this))
{
    m_machine = m_config.machine;
    m_region = m_config.region;
    m_language = m_config.language;
    if (m_shotDir.isValid()) {
        m_shotPath = QDir(m_shotDir.path()).filePath("frame.png");
        m_disasmPath = QDir(m_shotDir.path()).filePath("disasm.txt");
    }
    m_tracer = new DisasmTracer(m_rdb, this);

    buildUi();

    connect(m_rdb, &RdbClient::connected, this, &MainWindow::onConnected);
    connect(m_rdb, &RdbClient::connectionFailed, this, &MainWindow::onConnectionFailed);
    connect(m_rdb, &RdbClient::notification, this, &MainWindow::onNotification);
    connect(m_launcher, &HatariLauncher::failed, this, [this](const QString &r) {
        statusBar()->showMessage(QStringLiteral("Hatari launch failed: %1").arg(r), 8000);
        updateLaunchStopState();
    });
    // Unexpected Hatari exit (an intentional terminate blocks this signal).
    connect(m_launcher, &HatariLauncher::stopped, this, [this](int) {
        m_liveTimer->stop();
        m_rdb->disconnectFromHatari();
        setRunningControlsEnabled(false);
        m_connLabel->setText(QStringLiteral("hatari exited"));
        updateLaunchStopState();
    });

    m_capture = new CaptureController(m_rdb, this);
    connect(m_capture, &CaptureController::progress, this, &MainWindow::onCaptureProgress);
    connect(m_capture, &CaptureController::finished, this, &MainWindow::onCaptureFinished);

    m_liveTimer->setInterval(kLiveIntervalMs);
    connect(m_liveTimer, &QTimer::timeout, this, &MainWindow::liveTick);

    setRunningControlsEnabled(false);
    statusBar()->showMessage(QStringLiteral("Ready. Press %1.")
                                 .arg(m_config.attachOnly ? "Connect" : "Launch"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Talos — M0"));
    resize(900, 640);

    // Make the dock separators (the drag handles between panels) clearly visible,
    // so panel boundaries read and re-docking targets are obvious.
    setStyleSheet(QStringLiteral(
        "QMainWindow::separator { background: palette(mid); width: 5px; height: 5px; }"
        "QMainWindow::separator:hover { background: palette(highlight); }"));

    auto *tb = addToolBar(QStringLiteral("Controls"));
    tb->setMovable(false);

    // Machine + region selectors (F-205/F-206): changing them (re)configures the
    // machine — a relaunch when connected.
    m_machineCombo = new QComboBox(this);
    for (MachineType t : Machines::all())
        m_machineCombo->addItem(Machines::info(t).name);
    m_machineCombo->setToolTip(QStringLiteral("Machine type"));
    tb->addWidget(m_machineCombo);
    m_memCombo = new QComboBox(this);
    m_memCombo->addItem(QStringLiteral("512 KB"), 512);
    m_memCombo->addItem(QStringLiteral("1 MB"), 1024);
    m_memCombo->addItem(QStringLiteral("2 MB"), 2048);
    m_memCombo->addItem(QStringLiteral("4 MB"), 4096);
    m_memCombo->setCurrentIndex(1);   // 1 MB — runs most demos; 512 KB = an authentic 520
    m_memCombo->setToolTip(QStringLiteral("ST RAM size (--memsize)"));
    tb->addWidget(m_memCombo);
    m_languageCombo = new QComboBox(this);
    for (Language l : Languages::all())
        m_languageCombo->addItem(Languages::info(l).name);
    m_languageCombo->setToolTip(QStringLiteral("EmuTOS language"));
    tb->addWidget(m_languageCombo);
    m_regionCombo = new QComboBox(this);
    m_regionCombo->addItems({QStringLiteral("PAL"), QStringLiteral("NTSC")});
    m_regionCombo->setToolTip(QStringLiteral("Video region (50/60 Hz)"));
    tb->addWidget(m_regionCombo);
    m_clockCombo = new QComboBox(this);
    m_clockCombo->addItem(QStringLiteral("8 MHz"), 8);
    m_clockCombo->addItem(QStringLiteral("16 MHz"), 16);
    m_clockCombo->setCurrentIndex(1);   // Mega STE native speed
    m_clockCombo->setToolTip(
        QStringLiteral("Mega STE CPU clock (F-210): 16 MHz doubles the per-scanline cycle budget"));
    tb->addWidget(m_clockCombo);
    tb->addSeparator();

    m_actStart = tb->addAction(m_config.attachOnly ? QStringLiteral("Connect")
                                                    : QStringLiteral("Launch"));
    connect(m_actStart, &QAction::triggered, this, &MainWindow::onStartClicked);
    m_actStop = tb->addAction(QStringLiteral("Stop"));
    m_actStop->setToolTip(QStringLiteral("Stop the running machine"));
    m_actStop->setEnabled(false);
    connect(m_actStop, &QAction::triggered, this, &MainWindow::doStop);

    m_actSaveState = tb->addAction(QStringLiteral("Save state"));
    m_actSaveState->setToolTip(QStringLiteral(
        "F-217: snapshot the whole machine to a file (park a prototype / seed a run)"));
    m_actSaveState->setEnabled(false);
    connect(m_actSaveState, &QAction::triggered, this, &MainWindow::saveState);
    m_actLoadState = tb->addAction(QStringLiteral("Load state"));
    m_actLoadState->setToolTip(
        QStringLiteral("F-217: relaunch restoring a saved snapshot (skips boot)"));
    connect(m_actLoadState, &QAction::triggered, this, &MainWindow::loadState);

    m_actOpen = tb->addAction(QStringLiteral("Open…"));
    m_actOpen->setToolTip(QStringLiteral(
        "Load a real ST program or disk image and run it, then instrument it: "
        ".PRG/.TOS auto-run from a GEMDOS drive; .ST/.MSA/.STX/.DIM/.IPF boot as a floppy"));
    connect(m_actOpen, &QAction::triggered, this, &MainWindow::openProgram);
    // Recent-files dropdown on the Open… button (the button still opens the dialog;
    // its arrow lists recently-loaded programs/disks). Persisted across sessions.
    m_recentMenu = new QMenu(this);
    m_recentMenu->setToolTipsVisible(true);
    m_actOpen->setMenu(m_recentMenu);
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(m_actOpen)))
        btn->setPopupMode(QToolButton::MenuButtonPopup);
    m_recentFiles = QSettings().value(QStringLiteral("recentFiles")).toStringList();
    rebuildRecentMenu();

    m_actDisks = tb->addAction(QStringLiteral("Disks…"));
    m_actDisks->setToolTip(QStringLiteral(
        "Drive A/B disk manager: insert / eject / hot-swap a floppy on the running "
        "machine (multi-disk demos), or boot back to a clean desktop"));
    connect(m_actDisks, &QAction::triggered, this, &MainWindow::manageDisks);

    m_fastBootBtn = new LedToolButton(this);
    m_fastBootBtn->setText(QStringLiteral("Fast boot"));
    m_fastBootBtn->setChecked(m_config.fastBoot);
    m_fastBootBtn->setToolTip(QStringLiteral(
        "Fast-forward an effect's ~14 s boot, then run at normal speed (BUG-007). "
        "The LED lights red while fast boot is on; uncheck to watch the boot at real speed."));
    tb->addWidget(m_fastBootBtn);
    tb->addSeparator();

    m_actBreak = tb->addAction(QStringLiteral("Break"));
    connect(m_actBreak, &QAction::triggered, this, &MainWindow::doBreak);
    m_actRun = tb->addAction(QStringLiteral("Run"));
    connect(m_actRun, &QAction::triggered, this, &MainWindow::doRun);
    m_actStep = tb->addAction(QStringLiteral("Step"));
    connect(m_actStep, &QAction::triggered, this, &MainWindow::doStep);
    m_actRefresh = tb->addAction(QStringLiteral("Refresh"));
    connect(m_actRefresh, &QAction::triggered, this, &MainWindow::refresh);
    tb->addSeparator();

    m_actLive = tb->addAction(QStringLiteral("Live"));
    m_actLive->setCheckable(true);
    m_actLive->setChecked(true);
    connect(m_actLive, &QAction::toggled, this, [this](bool on) {
        if (on && m_rdb->isConnected())
            m_liveTimer->start();
        else
            m_liveTimer->stop();
    });

    m_actRecord = tb->addAction(QStringLiteral("● Rec"));
    m_actRecord->setCheckable(true);
    m_actRecord->setToolTip(QStringLiteral(
        "Record the live view to an animated GIF. Toggle on to start, off to save. "
        "Grabs the tear-free coherent frames, so the clip matches what you see."));
    connect(m_actRecord, &QAction::toggled, this, &MainWindow::toggleRecording);
    tb->addSeparator();

    tb->addWidget(new QLabel(QStringLiteral(" Line "), this));
    m_lineSpin = new QSpinBox(this);
    m_lineSpin->setRange(0, 312);          // scanlines per PAL frame
    m_lineSpin->setValue(150);
    m_lineSpin->setToolTip(QStringLiteral("Target scanline for Run→Line"));
    tb->addWidget(m_lineSpin);
    m_actRunToLine = tb->addAction(QStringLiteral("Run→Line"));
    m_actRunToLine->setToolTip(
        QStringLiteral("Run until the beam reaches this scanline, then stop"));
    connect(m_actRunToLine, &QAction::triggered, this, &MainWindow::runToLine);
    tb->addSeparator();

    tb->addWidget(new QLabel(QStringLiteral(" Reg $"), this));
    m_regEdit = new QLineEdit(QStringLiteral("ffff8240"), this);
    m_regEdit->setFixedWidth(80);
    m_regEdit->setToolTip(QStringLiteral("Hardware register (hex) to watch for writes"));
    tb->addWidget(m_regEdit);
    tb->addWidget(new QLabel(QStringLiteral(" × "), this));
    m_countSpin = new QSpinBox(this);
    m_countSpin->setRange(1, 512);
    m_countSpin->setValue(48);
    m_countSpin->setToolTip(QStringLiteral("How many writes to capture"));
    tb->addWidget(m_countSpin);
    m_actCapture = tb->addAction(QStringLiteral("Capture"));
    m_actCapture->setToolTip(
        QStringLiteral("Capture writes to the register and map each to the beam"));
    connect(m_actCapture, &QAction::triggered, this, &MainWindow::onCaptureClicked);

    tb->addSeparator();
    tb->addWidget(new QLabel(QStringLiteral(" Depth "), this));
    m_depthCombo = new QComboBox(this);
    m_depthCombo->addItem(QStringLiteral("1K"), 1024);
    m_depthCombo->addItem(QStringLiteral("4K"), 4096);
    m_depthCombo->addItem(QStringLiteral("16K"), 16384);
    m_depthCombo->addItem(QStringLiteral("64K"), 65536);
    m_depthCombo->setCurrentIndex(2);   // 16K default
    m_depthCombo->setToolTip(
        QStringLiteral("Max trace entries per B2 capture (Blit / DMA). Hitting it truncates."));
    tb->addWidget(m_depthCombo);
    m_windowSpin = new QSpinBox(this);
    m_windowSpin->setRange(100, 2000);
    m_windowSpin->setSingleStep(100);
    m_windowSpin->setValue(500);
    m_windowSpin->setSuffix(QStringLiteral(" ms"));
    m_windowSpin->setToolTip(QStringLiteral("How long to run while tracing, before dumping"));
    tb->addWidget(m_windowSpin);

    m_actBlitCapture = tb->addAction(QStringLiteral("Blit capture"));
    m_actBlitCapture->setToolTip(
        QStringLiteral("F-208 (B2): trace blitter memory traffic over the run window"));
    connect(m_actBlitCapture, &QAction::triggered, this, &MainWindow::captureBlitTraffic);

    m_actDmaCapture = tb->addAction(QStringLiteral("DMA capture"));
    m_actDmaCapture->setToolTip(
        QStringLiteral("F-209 (B2): trace DMA sound drain + LMC1992 EQ over a short run window"));
    connect(m_actDmaCapture, &QAction::triggered, this, &MainWindow::captureDmaSound);

    m_fb = new FramebufferView(this);
    setCentralWidget(m_fb);
    connect(m_fb, &FramebufferView::imageClicked, this, &MainWindow::onFramebufferClicked);

    m_regTable = new QTableWidget(0, 2, this);
    m_regTable->setHorizontalHeaderLabels({QStringLiteral("Reg"), QStringLiteral("Value")});
    m_regTable->horizontalHeader()->setStretchLastSection(true);
    m_regTable->verticalHeader()->setVisible(false);
    m_regTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_regTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_regTable->setMinimumWidth(220);
    // The register table is the stretchy panel: it refills whatever the
    // content-sized Machine/Palette panels leave, and scrolls past that.
    m_regTable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto *dock = new CollapsibleDock(QStringLiteral("Registers / counters"), m_regTable, this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // Register-write timeline (F-203): captured writes ordered by frame cycle.
    m_timeline = new QTableWidget(0, 6, this);
    m_timeline->setHorizontalHeaderLabels(
        {QStringLiteral("frame-cyc"), QStringLiteral("HBL"), QStringLiteral("cyc"),
         QStringLiteral("addr"), QStringLiteral("value"), QStringLiteral("PC")});
    m_timeline->horizontalHeader()->setStretchLastSection(true);
    m_timeline->verticalHeader()->setVisible(false);
    m_timeline->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_timeline->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_timeline->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_timeline, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { onTimelineRowChanged(row); });

    // Per-frame scrubber (Phase 6): drag the cursor through the captured frame and
    // watch the beam sweep and the register writes light up as it passes them.
    auto *scrubBar = new QWidget(this);
    auto *sl = new QHBoxLayout(scrubBar);
    sl->setContentsMargins(4, 2, 4, 2);
    m_scrubPlay = new QToolButton(scrubBar);
    m_scrubPlay->setText(QStringLiteral("▶"));
    m_scrubPlay->setCheckable(true);
    m_scrubPlay->setEnabled(false);
    m_scrubPlay->setToolTip(QStringLiteral("Play — sweep the beam through the frame"));
    sl->addWidget(m_scrubPlay);
    m_scrub = new QSlider(Qt::Horizontal, scrubBar);
    m_scrub->setEnabled(false);
    m_scrub->setToolTip(QStringLiteral(
        "Scrub the beam through the captured frame — writes light up as it passes them"));
    sl->addWidget(m_scrub, 1);
    m_scrubInfo = new QLabel(scrubBar);
    m_scrubInfo->setMinimumWidth(240);
    sl->addWidget(m_scrubInfo);

    m_scrubTimer = new QTimer(this);
    m_scrubTimer->setInterval(30);
    connect(m_scrubTimer, &QTimer::timeout, this, [this] {
        const int step = std::max(1, m_scrub->maximum() / 60);   // ~1.8 s frame sweep
        const int v = m_scrub->value() + step;
        if (v >= m_scrub->maximum()) {
            m_scrub->setValue(m_scrub->maximum());
            m_scrubPlay->setChecked(false);   // toggled() stops the timer
        } else {
            m_scrub->setValue(v);
        }
    });
    connect(m_scrub, &QSlider::valueChanged, this, &MainWindow::onScrub);
    connect(m_scrubPlay, &QToolButton::toggled, this, &MainWindow::toggleScrubPlay);

    auto *tlContainer = new QWidget(this);
    auto *tl = new QVBoxLayout(tlContainer);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(2);
    tl->addWidget(scrubBar);
    tl->addWidget(m_timeline, 1);

    auto *tdock =
        new CollapsibleDock(QStringLiteral("Register-write timeline"), tlContainer, this);
    addDockWidget(Qt::BottomDockWidgetArea, tdock);

    // Blitter memory-traffic view (F-208, B2): tabbed alongside the timeline.
    m_blitView = new BlitterTrafficView(this);
    auto *blitDock =
        new CollapsibleDock(QStringLiteral("Blitter traffic"), m_blitView, this);
    addDockWidget(Qt::BottomDockWidgetArea, blitDock);
    tabifyDockWidget(tdock, blitDock);

    // DMA sound + LMC1992 EQ view (F-209, B2): tabbed alongside the others.
    m_dmaView = new DmaSoundView(this);
    auto *dmaDock = new CollapsibleDock(QStringLiteral("DMA sound / EQ"), m_dmaView, this);
    addDockWidget(Qt::BottomDockWidgetArea, dmaDock);
    tabifyDockWidget(tdock, dmaDock);

    // Raster-bar prototyping workspace (F-211/F-212, Phase 4).
    m_raster = new RasterWorkspace(this);
    auto *rasterDock = new CollapsibleDock(QStringLiteral("Raster workspace"), m_raster, this);
    addDockWidget(Qt::BottomDockWidgetArea, rasterDock);
    tabifyDockWidget(tdock, rasterDock);
    connect(m_raster, &RasterWorkspace::buildRequested, this, &MainWindow::buildRasterEffect);
    connect(m_raster, &RasterWorkspace::verifyRequested, this, &MainWindow::verifyRasterEffect);
    connect(m_raster, &RasterWorkspace::exportRequested, this, &MainWindow::exportRasterEffect);
    connect(m_raster, &RasterWorkspace::importRequested, this, &MainWindow::importRasterEffect);
    connect(m_raster, &RasterWorkspace::contentChanged, this, &MainWindow::updateBudget);

    // Per-scanline cycle-budget gauge (Phase 6): shows how much of a line's cycle
    // budget the authored effect uses, and the 8/16 MHz Mega STE budgets (F-210).
    m_budget = new ScanlineBudgetView(this);
    auto *budgetDock = new CollapsibleDock(QStringLiteral("Cycle budget"), m_budget, this);
    addDockWidget(Qt::BottomDockWidgetArea, budgetDock);
    tabifyDockWidget(tdock, budgetDock);
    updateBudget();   // seed from the workspace's initial (rainbow) bars

    // STE hardware fine-scroll message scroller (F-212, Phase 4).
    m_scroller = new ScrollerWorkspace(this);
    auto *scrollerDock = new CollapsibleDock(QStringLiteral("Scroller workspace"), m_scroller, this);
    addDockWidget(Qt::BottomDockWidgetArea, scrollerDock);
    tabifyDockWidget(tdock, scrollerDock);
    connect(m_scroller, &ScrollerWorkspace::buildRequested, this, &MainWindow::buildScrollerEffect);
    connect(m_scroller, &ScrollerWorkspace::verifyRequested, this, &MainWindow::verifyScrollerEffect);
    connect(m_scroller, &ScrollerWorkspace::exportRequested, this, &MainWindow::exportScrollerEffect);
    connect(m_scroller, &ScrollerWorkspace::importRequested, this, &MainWindow::importScrollerEffect);

    // Spectrum 512 picture viewer (F-211): import a .SPU, decode it, and visualise
    // the per-scanline palette storm. A local file analyser — no running machine
    // needed (Talos shows the picture and its timing, not a hardware reproduction).
    m_spectrum = new Spectrum512View(this);
    auto *spectrumDock = new CollapsibleDock(QStringLiteral("Spectrum 512"), m_spectrum, this);
    addDockWidget(Qt::BottomDockWidgetArea, spectrumDock);
    tabifyDockWidget(tdock, spectrumDock);

    // ST picture viewer (F-211 / Phase 6): import DEGAS / NEOchrome pictures.
    m_stPicture = new StPictureView(this);
    auto *stPicDock = new CollapsibleDock(QStringLiteral("ST picture"), m_stPicture, this);
    addDockWidget(Qt::BottomDockWidgetArea, stPicDock);
    tabifyDockWidget(tdock, stPicDock);

    // Border-removal walkthrough (Phase 6): guided tour of the four ST borders,
    // with the left border runnable (finishes M1's story).
    m_borderView = new BorderWalkthroughView(this);
    auto *borderDock = new CollapsibleDock(QStringLiteral("Border walkthrough"), m_borderView, this);
    addDockWidget(Qt::BottomDockWidgetArea, borderDock);
    tabifyDockWidget(tdock, borderDock);
    connect(m_borderView, &BorderWalkthroughView::buildRequested, this,
            &MainWindow::buildBorderEffect);
    connect(m_borderView, &BorderWalkthroughView::verifyRequested, this,
            &MainWindow::verifyBorderEffect);

    // Sync-scroll walkthrough (Phase 6): the STF's res-switch fine-scroll trick.
    m_syncScroll = new SyncScrollView(this);
    auto *syncDock = new CollapsibleDock(QStringLiteral("Sync scroll"), m_syncScroll, this);
    addDockWidget(Qt::BottomDockWidgetArea, syncDock);
    tabifyDockWidget(tdock, syncDock);

    // Reconstruct-from-registers (F-218): the taken frame beside a screen rebuilt
    // purely from the captured palette writes — secondary, never a replacement.
    m_reconstruct = new ReconstructView(this);
    auto *reconDock = new CollapsibleDock(QStringLiteral("Reconstruct"), m_reconstruct, this);
    addDockWidget(Qt::BottomDockWidgetArea, reconDock);
    tabifyDockWidget(tdock, reconDock);

    // Live disassembly synced to the beam (Phase 6): step-and-trace the code
    // around PC, showing where each instruction lands on the beam.
    m_disasm = new DisasmView(this);
    auto *disasmDock = new CollapsibleDock(QStringLiteral("Disassembly"), m_disasm, this);
    addDockWidget(Qt::BottomDockWidgetArea, disasmDock);
    tabifyDockWidget(tdock, disasmDock);
    connect(m_disasm, &DisasmView::traceRequested, this, [this](int n) {
        if (!m_rdb->isConnected()) {
            m_disasm->setStatus(QStringLiteral("Connect to a machine first."), false);
            return;
        }
        if (m_tracer->isRunning())
            return;
        m_actLive->setChecked(false);   // stepping and the live grab must not race
        m_disasm->setBusy(true);
        m_tracer->start(n, m_disasmPath);
    });
    connect(m_tracer, &DisasmTracer::finished, this, [this](bool ok, const QString &reason) {
        m_disasm->setBusy(false);
        if (ok)
            m_disasm->setEntries(m_tracer->entries());
        else
            m_disasm->setStatus(QStringLiteral("Trace failed — %1").arg(reason), false);
        refreshRegs();   // the machine stepped forward; resync panels + overlay
    });
    connect(m_disasm, &DisasmView::rowActivated, this, [this](int scanline, int cyc) {
        showBeamAt(scanline, cyc, m_fb->imageSize(), QStringLiteral("trace: "));
    });

    // MFP timer / interrupt visualisation (Phase 6): the four 68901 timers and
    // the interrupt controller, decoded from the live register block.
    m_mfp = new MfpView(this);
    auto *mfpDock = new CollapsibleDock(QStringLiteral("MFP"), m_mfp, this);
    addDockWidget(Qt::BottomDockWidgetArea, mfpDock);
    tabifyDockWidget(tdock, mfpDock);
    connect(m_mfp, &MfpView::readRequested, this, &MainWindow::readMfp);

    // A/B machine comparison (Phase 6): the last-built effect on two machines,
    // side by side with a per-scanline diff (extends the F-207 differential).
    m_ab = new AbCompareView(this);
    auto *abDock = new CollapsibleDock(QStringLiteral("A/B compare"), m_ab, this);
    addDockWidget(Qt::BottomDockWidgetArea, abDock);
    tabifyDockWidget(tdock, abDock);
    connect(m_ab, &AbCompareView::compareRequested, this, &MainWindow::compareMachines);

    tdock->raise();   // timeline shown first

    // Machine capabilities / differential view (F-207).
    m_capsLabel = new QLabel(this);
    m_capsLabel->setMargin(8);
    m_capsLabel->setTextFormat(Qt::RichText);
    m_capsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    // Content-sized (capped at its text height) so it doesn't compete with the
    // register table for the leftover space.
    m_capsLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *capsDock = new CollapsibleDock(QStringLiteral("Machine"), m_capsLabel, this);
    capsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, capsDock);

    // Palette panel (F-204/F-207): the ST 512 <-> STE 4096 differential.
    m_palette = new PaletteView(this);
    auto *palDock = new CollapsibleDock(QStringLiteral("Palette"), m_palette, this);
    palDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, palDock);

    // Initial split: give the register table the bulk; Machine/Palette settle at
    // their content height (their Maximum vertical policy caps them there).
    resizeDocks({dock, capsDock, palDock}, {600, 140, 200}, Qt::Vertical);

    m_captureLabel = new QLabel(QString(), this);
    statusBar()->addWidget(m_captureLabel);   // left, persistent (not a timed message)

    m_connLabel = new QLabel(QStringLiteral("disconnected"), this);
    m_posLabel = new QLabel(QString(), this);
    statusBar()->addPermanentWidget(m_posLabel);
    statusBar()->addPermanentWidget(m_connLabel);

    // Restore the last-used boot selectors (unless a CLI arg pinned one). RAM has
    // no CLI option, so it always restores. Done before the combos are set + wired
    // so no relaunch fires during construction.
    {
        QSettings s;
        if (!m_config.machineExplicit && s.contains(QStringLiteral("boot/machine"))) {
            const QString hm = s.value(QStringLiteral("boot/machine")).toString();
            for (MachineType t : Machines::all())
                if (Machines::info(t).hatariMachine == hm) { m_machine = t; break; }
        }
        if (!m_config.regionExplicit && s.contains(QStringLiteral("boot/region")))
            m_region = s.value(QStringLiteral("boot/region")).toString() == QLatin1String("ntsc")
                           ? VideoRegion::Ntsc60 : VideoRegion::Pal50;
        if (!m_config.languageExplicit && s.contains(QStringLiteral("boot/language"))) {
            const QString ln = s.value(QStringLiteral("boot/language")).toString();
            for (Language l : Languages::all())
                if (Languages::info(l).name == ln) { m_language = l; break; }
        }
        const int ram = s.value(QStringLiteral("boot/ram"),
                                m_memCombo->currentData().toInt()).toInt();
        const int ri = m_memCombo->findData(ram);
        if (ri >= 0)
            m_memCombo->setCurrentIndex(ri);
    }

    // Set the initial selection, then wire the change signals (so no spurious
    // relaunch fires during construction).
    m_machineCombo->setCurrentIndex(Machines::all().indexOf(m_machine));
    m_languageCombo->setCurrentIndex(Languages::all().indexOf(m_language));
    m_regionCombo->setCurrentIndex(m_region == VideoRegion::Ntsc60 ? 1 : 0);
    connect(m_machineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onMachineChanged);
    connect(m_languageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onLanguageChanged);
    connect(m_regionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onRegionChanged);
    connect(m_memCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onMemoryChanged);
    connect(m_clockCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onClockChanged);
    reconcileRegion();       // also calls updateCapabilities()
    updateLaunchStopState();
}

void MainWindow::startSession()
{
    onStartClicked();
}

void MainWindow::onStartClicked()
{
    if (!m_config.attachOnly) {
        m_config.hatari.machine = Machines::info(m_machine).hatariMachine;
        // Only dual-speed machines get an explicit clock; others use the default.
        m_config.hatari.cpuClock =
            Machines::info(m_machine).dualSpeed ? m_clockCombo->currentData().toInt() : 0;
        m_config.hatari.stRamSizeKB = m_memCombo->currentData().toInt();
        m_config.hatari.country = Languages::country(m_language, m_region);
        // Fast-forward the ~14 s boot when running an AUTO effect; turned off once
        // the effect is detected running (BUG-007). No boot to skip when restoring
        // a snapshot (F-217) — the state is loaded at launch.
        const bool restoring = !m_config.hatari.memStateFile.isEmpty();
        m_config.hatari.bootFastForward = m_fastBootBtn->isChecked()
                                          && !restoring && !m_config.hatari.gemdosDir.isEmpty();
        if (!m_launcher->launch(m_config.hatari))
            return;
        m_config.hatari.memStateFile.clear();   // one-shot restore
        statusBar()->showMessage(
            restoring ? QStringLiteral("Restoring saved state…")
                      : QStringLiteral("Launching %1 · %2 · %3…")
                            .arg(Machines::info(m_machine).name,
                                 Languages::info(m_language).name, regionName(m_region)));
    }
    m_bootWatching = !m_config.attachOnly && m_config.hatari.bootFastForward;
    m_bootRamHits = m_bootPolls = 0;
    m_bootLastPc = 0;
    m_connLabel->setText(QStringLiteral("connecting…"));
    m_rdb->connectToHatari(m_config.host, m_config.hatari.remotePort);
    updateLaunchStopState();
}

void MainWindow::doStop()
{
    m_liveTimer->stop();
    m_grabbing = false;
    m_rdb->disconnectFromHatari();
    if (!m_config.attachOnly)
        m_launcher->terminate();
    m_fb->setImage(QImage());
    m_fb->setWriteMarks({});
    m_writes.clear();
    m_timeline->setRowCount(0);
    setupScrub();
    setRunningControlsEnabled(false);
    m_connLabel->setText(QStringLiteral("stopped"));
    updateLaunchStopState();
}

void MainWindow::saveState()
{
    if (!m_rdb->isConnected())
        return;
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save machine state"), QStringLiteral("talos.sav"),
        QStringLiteral("Machine state (*.sav)"));
    if (file.isEmpty())
        return;
    // Whole-system snapshot via the debugui (B1). Works while running or stopped.
    m_rdb->sendCommand("console statesave " + file.toLocal8Bit(),
                       [this, file](const RdbClient::Tokens &r) {
                           const bool ok = !r.isEmpty() && r.first() == "OK";
                           statusBar()->showMessage(
                               ok ? QStringLiteral("Machine state saved to %1").arg(file)
                                  : QStringLiteral("Save state failed"),
                               4000);
                       });
}

void MainWindow::loadState()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Restore machine state"), QString(),
        QStringLiteral("Machine state (*.sav)"));
    if (file.isEmpty())
        return;
    if (m_config.attachOnly) {
        statusBar()->showMessage(
            QStringLiteral("Can't restore a snapshot into an attached Hatari."), 5000);
        return;
    }
    // Restore by relaunching with --memstate (the in-place stateload is deferred
    // and unreliable; a relaunch cleanly reloads the whole state, skipping boot).
    m_config.hatari.memStateFile = file;
    if (m_launcher->isRunning() || m_rdb->isConnected())
        relaunch();
    else
        onStartClicked();
}

void MainWindow::onMachineChanged(int index)
{
    const auto machines = Machines::all();
    if (index < 0 || index >= machines.size())
        return;
    m_machine = machines[index];
    updateCapabilities();
    updateBudget();      // dual-speed machines get the 16 MHz budget line
    persistBootSelectors();
    if (m_launcher->isRunning() || m_rdb->isConnected())
        relaunch();
}

void MainWindow::onRegionChanged(int index)
{
    m_region = (index == 1) ? VideoRegion::Ntsc60 : VideoRegion::Pal50;
    reconcileRegion();   // a language with no NTSC variant snaps back to PAL
    updateBudget();      // 512 (PAL) vs 508 (NTSC) cycles/line
    updateReconstruct(); // F-218 geometry follows the region
    persistBootSelectors();
    if (m_launcher->isRunning() || m_rdb->isConnected())
        relaunch();
}

void MainWindow::onMemoryChanged(int)
{
    persistBootSelectors();
    // RAM size only takes effect at boot, so a change relaunches the machine.
    if (m_launcher->isRunning() || m_rdb->isConnected())
        relaunch();
}

void MainWindow::persistBootSelectors()
{
    QSettings s;
    s.setValue(QStringLiteral("boot/machine"), Machines::info(m_machine).hatariMachine);
    s.setValue(QStringLiteral("boot/region"),
               m_region == VideoRegion::Ntsc60 ? QStringLiteral("ntsc") : QStringLiteral("pal"));
    s.setValue(QStringLiteral("boot/language"), Languages::info(m_language).name);
    s.setValue(QStringLiteral("boot/ram"), m_memCombo->currentData().toInt());
}

void MainWindow::onLanguageChanged(int index)
{
    const auto langs = Languages::all();
    if (index < 0 || index >= langs.size())
        return;
    m_language = langs[index];
    reconcileRegion();
    persistBootSelectors();
    if (m_launcher->isRunning() || m_rdb->isConnected())
        relaunch();
}

void MainWindow::onClockChanged(int)
{
    if (m_launcher->isRunning() || m_rdb->isConnected())
        relaunch();
}

void MainWindow::reconcileRegion()
{
    // The chosen language+region resolves to a --country; if that country boots a
    // different region (e.g. a PAL-only language while NTSC was picked), snap the
    // region selector to what will actually boot, so the UI stays honest.
    const VideoRegion actual =
        Languages::regionOf(Languages::country(m_language, m_region));
    if (actual != m_region) {
        m_region = actual;
        const QSignalBlocker blocker(m_regionCombo);
        m_regionCombo->setCurrentIndex(m_region == VideoRegion::Ntsc60 ? 1 : 0);
    }
    updateCapabilities();
}

void MainWindow::relaunch()
{
    if (m_config.attachOnly) {
        statusBar()->showMessage(
            QStringLiteral("Can't change machine/region of an attached Hatari."), 5000);
        return;
    }
    m_liveTimer->stop();
    m_rdb->disconnectFromHatari();
    m_launcher->terminate();
    // Drop the previous machine's state so nothing stale is shown.
    m_fb->setImage(QImage());
    m_fb->setWriteMarks({});
    m_writes.clear();
    m_timeline->setRowCount(0);
    setupScrub();
    setRunningControlsEnabled(false);
    onStartClicked();
}

void MainWindow::updateLaunchStopState()
{
    const bool running = m_launcher->isRunning() || m_rdb->isConnected();
    m_actStart->setEnabled(!running);
    m_actStop->setEnabled(running);
}

void MainWindow::updateCapabilities()
{
    const MachineInfo &m = Machines::info(m_machine);
    auto row = [](const QString &name, bool on, const QString &detail = QString()) {
        const QString mark = on ? QStringLiteral("<b style='color:#2e7d32'>✓</b>")
                                : QStringLiteral("<span style='color:#999'>✗</span>");
        return QStringLiteral("%1&nbsp; %2 %3<br>").arg(mark, name, detail);
    };
    m_capsLabel->setText(
        QStringLiteral("<b>%1</b> &middot; %2 &middot; %3<br><br>")
            .arg(m.name, Languages::info(m_language).name, regionName(m_region))
        + row(QStringLiteral("Blitter"), m.blitter)
        + row(QStringLiteral("Palette"), true,
              QStringLiteral("<b>%1</b> colours").arg(m.paletteColours))
        + row(QStringLiteral("Hardware scroll"), m.hardwareScroll)
        + row(QStringLiteral("DMA sound"), m.dmaSound)
        + row(QStringLiteral("Dual-speed 16&nbsp;MHz"), m.dualSpeed,
              m.dualSpeed ? QStringLiteral("&mdash; running at <b>%1&nbsp;MHz</b>")
                                .arg(m_clockCombo->currentData().toInt())
                          : QString()));

    // The clock selector only applies to dual-speed machines (Mega STE).
    m_clockCombo->setEnabled(m.dualSpeed);
}

void MainWindow::onConnected()
{
    setRunningControlsEnabled(true);
    updateLaunchStopState();
    m_connLabel->setText(QStringLiteral("connected · proto 0x%1")
                             .arg(m_rdb->protocolId(), 0, 16));
    statusBar()->showMessage(QStringLiteral("Connected."), 3000);
    refresh();
    if (m_actLive->isChecked())
        m_liveTimer->start();
}

void MainWindow::onConnectionFailed(const QString &reason)
{
    m_connLabel->setText(QStringLiteral("connect failed"));
    statusBar()->showMessage(QStringLiteral("Connection failed: %1").arg(reason), 8000);
    updateLaunchStopState();
}

void MainWindow::onNotification(const QByteArray &name, const QList<QByteArray> &args)
{
    Q_UNUSED(args);
    // M0: surface state-change notifications; a later phase parses them fully.
    if (name == "status")
        statusBar()->showMessage(QStringLiteral("· status update"), 1000);
}

void MainWindow::refresh()
{
    if (!m_rdb->isConnected())
        return;
    // Chain regs -> screenshot so the beam overlay is computed from the register
    // snapshot that matches the frame we then grab. The screenshot path sustains
    // ~60 fps (BUG-004), so the frame + beam refresh runs every tick (~20 Hz);
    // the palette and region change rarely, so poll them at ~4 Hz to keep the
    // per-frame socket load light and avoid queueing.
    refreshRegs();
    if (m_refreshTick++ % 5 == 0) {
        refreshPalette();
        readRegionFromCore();
    }
}

void MainWindow::liveTick()
{
    if (!m_rdb->isConnected() || m_grabbing)
        return;   // skip a tick while a coherent grab is still in flight
    if (m_refreshTick++ % 5 == 0) {
        refreshPalette();
        readRegionFromCore();
    }
    // During boot we fast-forward and the screen is static (desktop), so a plain
    // running grab is fine and keeps the boot-detection poll (checkBootFastForward)
    // ticking. Once the effect runs (real-time), grab tear-free instead.
    if (m_bootWatching) {
        refreshRegs();
        return;
    }
    grabCoherentFrame();
}

void MainWindow::grabCoherentFrame()
{
    // Under real-time, Hatari's remote screenshot grabs a mid-render surface, which
    // tears animated effects (a scroller shows top/bottom at different scroll
    // offsets). The render is only complete at a frame boundary, and only reliably
    // so under fast-forward. So: read the current frame, momentarily fast-forward to
    // the next VBL (a complete frame), grab there, then resume real-time. Validated
    // against the Hatari fork — the plain running grab tears, this does not.
    if (m_shotPath.isEmpty())
        return;
    m_grabbing = true;
    // Stop first so the frame number is read from a settled state (no race), then
    // fast-forward exactly one frame to the next VBL and grab the complete frame.
    m_rdb->breakExec([this](const RdbClient::Tokens &) {
    m_rdb->reqRegs([this](const RdbClient::Tokens &tokens) {
        m_state = MachineState::fromRegsReply(tokens);
        updateRegisterPanel();
        updateStatusBar();
        const quint32 target = m_state.vbl().value_or(0) + 1;
        m_rdb->sendCommand("ffwd 1");
        m_rdb->sendCommand("bp VBL = " + QByteArray::number(target) + " :once");
        m_rdb->run();
        QTimer::singleShot(kCoherentGrabMs, this, [this] {
            m_rdb->screenshot(m_shotPath, [this](const RdbClient::Tokens &reply) {
                if (!reply.isEmpty() && reply.first() == "OK") {
                    QImage img(m_shotPath);
                    if (!img.isNull()) {
                        m_fb->setImage(img);
                        const bool beamVisible = updateBeamOverlay(img.size());
                        recomputeWriteMarks(img.size());
                        emit frameReceived(m_fb->composite(), beamVisible);
                        recordFrame(img);   // raw, tear-free frame -> GIF (if recording)
                        if (m_reconstruct)
                            m_reconstruct->setFrame(img);   // F-218 reality panel
                    }
                }
                m_rdb->sendCommand("ffwd 0");   // restore real-time speed
                if (m_actLive->isChecked())
                    m_rdb->run();               // resume only if still live (Break may have paused)
                m_grabbing = false;
            });
        });
    });
    });   // close reqRegs + breakExec
}

void MainWindow::toggleRecording(bool on)
{
    if (on) {
        m_gif.clear();
        m_recording = true;
        // Recording only yields frames while the live view is feeding them.
        if (!m_actLive->isChecked())
            m_actLive->setChecked(true);
        statusBar()->showMessage(QStringLiteral("Recording… 0 frames"));
        return;
    }

    m_recording = false;
    if (m_gif.frameCount() == 0) {
        statusBar()->showMessage(QStringLiteral("Recording cancelled — no frames captured."),
                                 4000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save recording"),
        QDir::homePath() + QStringLiteral("/talos-clip.gif"),
        QStringLiteral("Animated GIF (*.gif)"));
    if (path.isEmpty()) {
        statusBar()->showMessage(
            QStringLiteral("Save cancelled — %1 recorded frames discarded.")
                .arg(m_gif.frameCount()),
            4000);
        m_gif.clear();
        return;
    }
    QString out = path;
    if (!out.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive))
        out += QStringLiteral(".gif");
    const int n = m_gif.frameCount();
    const QSize sz = m_gif.size();
    const bool ok = m_gif.write(out);
    m_gif.clear();
    statusBar()->showMessage(
        ok ? QStringLiteral("Saved %1 (%2 frames, %3×%4).")
                 .arg(out).arg(n).arg(sz.width()).arg(sz.height())
           : QStringLiteral("Failed to write %1.").arg(out),
        6000);
}

void MainWindow::recordFrame(const QImage &raw)
{
    if (!m_recording || raw.isNull())
        return;
    // Half-size keeps the clip (and its in-memory frames) small; ST content stays
    // legible and its few-colour palette survives intact.
    m_gif.addFrame(raw.scaled(raw.size() / 2, Qt::IgnoreAspectRatio,
                              Qt::FastTransformation),
                   kRecordDelayCs);
    if (m_gif.frameCount() >= kMaxRecordFrames) {
        statusBar()->showMessage(
            QStringLiteral("Reached %1 frames — stopping. Choose where to save…")
                .arg(kMaxRecordFrames),
            4000);
        m_actRecord->setChecked(false);   // triggers toggleRecording(false) -> save
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("Recording… %1 frames").arg(m_gif.frameCount()));
}

void MainWindow::readRegionFromCore()
{
    // BUG-002 follow-up: rather than assume the selected region, read the sync-mode
    // register $ff820a and follow it — bit 1 = 1 -> 50 Hz (PAL), 0 -> 60 Hz (NTSC).
    // (A sync-mode border trick could flip this transiently; the common case is the
    // stable boot region, and reading it keeps the overlay geometry honest.)
    m_rdb->sendCommand("mem ff820a 1", [this](const RdbClient::Tokens &r) {
        if (r.size() < 4 || r.first() != "OK")
            return;
        const QByteArray b = MemCodec::decode(r.last(), 1);
        if (b.isEmpty())
            return;
        const VideoRegion actual = (static_cast<quint8>(b[0]) & 0x02)
                                       ? VideoRegion::Pal50 : VideoRegion::Ntsc60;
        if (actual == m_region)
            return;
        m_region = actual;   // geometry (all BeamGeometry(m_region, ...)) now follows reality
        const QSignalBlocker blocker(m_regionCombo);   // reflect it without a relaunch
        m_regionCombo->setCurrentIndex(m_region == VideoRegion::Ntsc60 ? 1 : 0);
        updateCapabilities();
    });
}

void MainWindow::refreshPalette()
{
    m_rdb->sendCommand("mem ff8240 20", [this](const RdbClient::Tokens &r) {
        if (r.size() < 4 || r.first() != "OK")
            return;
        const QVector<quint16> regs = Palette::readRegisters(r.last());
        QVector<QColor> colours;
        colours.reserve(regs.size());
        for (quint16 v : regs)
            colours.append(Palette::decode(v));
        const MachineInfo &mi = Machines::info(m_machine);
        const int bits = (mi.paletteColours >= 4096) ? 4 : 3;
        m_palette->setPalette(colours, regs, mi.name, mi.paletteColours, bits);
    });
}

void MainWindow::refreshRegs()
{
    m_rdb->reqRegs([this](const RdbClient::Tokens &tokens) {
        m_state = MachineState::fromRegsReply(tokens);
        updateRegisterPanel();
        updateStatusBar();
        checkBootFastForward();
        refreshScreen();
        readMfp();   // keep the MFP panel current on a manual refresh
    });
}

void MainWindow::checkBootFastForward()
{
    if (!m_bootWatching)
        return;
    const auto pc = m_state.pc();
    // The AUTO effect runs from the TPA (low RAM) and loops tightly; EmuTOS boots
    // from ROM ($E00000+). Detect the effect by PC staying inside a small window in
    // RAM across a few polls (a tight loop / a `stop`), which boot code never does.
    const bool inRam = pc && *pc >= 0x2000 && *pc < 0x400000;
    const bool stable = inRam && m_bootLastPc
                        && qAbs(qint64(*pc) - qint64(m_bootLastPc)) < 0x200;
    if (stable) {
        if (++m_bootRamHits >= 5) {
            endBootFastForward(QStringLiteral("effect running"));
            return;
        }
    } else {
        m_bootRamHits = 0;
    }
    if (inRam)
        m_bootLastPc = *pc;
    if (++m_bootPolls > 120)   // safety: ~6 s of watching, then run at normal speed
        endBootFastForward(QStringLiteral("timeout"));
}

void MainWindow::endBootFastForward(const QString &why)
{
    m_bootWatching = false;
    m_rdb->sendCommand("ffwd 0");
    statusBar()->showMessage(QStringLiteral("Boot fast-forward off (%1).").arg(why), 3000);
}

void MainWindow::refreshScreen()
{
    if (m_shotPath.isEmpty())
        return;
    m_rdb->screenshot(m_shotPath, [this](const RdbClient::Tokens &reply) {
        if (reply.isEmpty() || reply.first() != "OK")
            return;
        QImage img(m_shotPath);   // freshly written PNG of Hatari's rendered frame
        if (img.isNull())
            return;
        m_fb->setImage(img);
        const bool beamVisible = updateBeamOverlay(img.size());
        recomputeWriteMarks(img.size());
        emit frameReceived(m_fb->composite(), beamVisible);
        if (m_reconstruct)
            m_reconstruct->setFrame(img);   // F-218 reality panel
    });
}

int MainWindow::cyclesPerLine() const
{
    return m_region == VideoRegion::Ntsc60 ? 508 : 512;   // C-007
}

bool MainWindow::updateBeamOverlay(QSize frameSize)
{
    const auto scanline = m_state.hbl();
    const auto cycle = m_state.lineCycles();
    if (!scanline || !cycle || frameSize.isEmpty()) {
        m_fb->setBeam(BeamMarker{});   // valid == false: nothing to draw
        return false;
    }
    return showBeamAt(static_cast<int>(*scanline), static_cast<int>(*cycle), frameSize);
}

// Draw the beam marker at an explicit (scanline, cycle-in-line). Shared by the
// live overlay and the frame scrubber.
bool MainWindow::showBeamAt(int scanline, int cycleInLine, QSize frameSize,
                            const QString &prefix)
{
    BeamMarker mk;
    if (frameSize.isEmpty()) {
        m_fb->setBeam(mk);
        return false;
    }
    const BeamGeometry geo(m_region, frameSize);
    const BeamMapping bm = geo.map(scanline, cycleInLine);
    const QString pos = prefix + QStringLiteral("HBL %1 · cyc %2").arg(scanline).arg(cycleInLine);

    mk.valid = true;
    if (bm.yVisible) {
        mk.scanline = true;
        mk.y = bm.y;
        if (bm.xVisible) {
            mk.crosshair = true;
            mk.x = bm.x;
            mk.label = pos;
        } else {
            mk.label = pos + QStringLiteral("  (h-blank)");
        }
    } else {
        mk.vblank = true;
        mk.label = QStringLiteral("beam in blanking · ") + pos;
    }
    m_fb->setBeam(mk);
    return mk.crosshair;
}

void MainWindow::updateRegisterPanel()
{
    // Beam counters first (highlighted), then the rest alphabetically.
    QStringList beam, rest;
    for (auto it = m_state.values.constBegin(); it != m_state.values.constEnd(); ++it) {
        (MachineState::isBeamCounter(it.key()) ? beam : rest).append(it.key());
    }
    beam.sort();
    rest.sort();
    const QStringList ordered = beam + rest;

    m_regTable->setRowCount(ordered.size());
    for (int row = 0; row < ordered.size(); ++row) {
        const QString &key = ordered[row];
        const bool isBeam = MachineState::isBeamCounter(key);

        auto *nameItem = new QTableWidgetItem(key);
        auto *valItem = new QTableWidgetItem(
            QStringLiteral("0x%1").arg(m_state.value(key), 0, 16));
        if (isBeam) {
            const QColor hl(40, 90, 60);
            nameItem->setBackground(hl);
            valItem->setBackground(hl);
        }
        m_regTable->setItem(row, 0, nameItem);
        m_regTable->setItem(row, 1, valItem);
    }
}

void MainWindow::updateStatusBar()
{
    auto fmt = [](const std::optional<quint32> &v) {
        return v ? QString::number(*v) : QStringLiteral("—");
    };
    m_posLabel->setText(QStringLiteral("HBL %1 · line-cyc %2 · frame-cyc %3 · VBL %4 · PC $%5")
                            .arg(fmt(m_state.hbl()), fmt(m_state.lineCycles()),
                                 fmt(m_state.frameCycles()), fmt(m_state.vbl()))
                            .arg(m_state.value("PC"), 0, 16));
}

void MainWindow::doBreak()
{
    // Pausing ends live view — otherwise the next coherent grab would run the
    // machine on again to reach a frame boundary, undoing the break.
    m_actLive->setChecked(false);   // toggled() stops the live timer
    m_grabbing = false;
    m_rdb->breakExec([this](const RdbClient::Tokens &) { refresh(); });
}

void MainWindow::doRun()
{
    m_rdb->run();
}

void MainWindow::doStep()
{
    m_rdb->step([this](const RdbClient::Tokens &) { refresh(); });
}

void MainWindow::runToLine()
{
    if (!m_rdb->isConnected())
        return;
    const int line = m_lineSpin->value();
    // Set a one-shot breakpoint on the scanline counter and run to it. Verified
    // syntax: single '=' (not '=='), ':once' auto-removes it after the hit.
    m_rdb->sendCommand(QByteArray("bp HBL = ") + QByteArray::number(line) + " :once");
    m_rdb->run();
    statusBar()->showMessage(QStringLiteral("Running to scanline %1…").arg(line), 2000);
    // The breakpoint hits within a frame; refresh shortly after to show the beam
    // parked on that line (robust whether or not Live is on).
    QTimer::singleShot(200, this, &MainWindow::refresh);
}

void MainWindow::beginCapture(quint32 address, int count)
{
    m_regEdit->setText(QString::number(address, 16));
    m_countSpin->setValue(count);
    onCaptureClicked();
}

void MainWindow::onCaptureClicked()
{
    if (!m_rdb->isConnected() || m_capture->isRunning())
        return;
    bool ok = false;
    const quint32 addr = m_regEdit->text().trimmed().toUInt(&ok, 16);
    if (!ok) {
        statusBar()->showMessage(QStringLiteral("Invalid register address"), 4000);
        return;
    }
    m_liveTimer->stop();          // don't interleave live refreshes with capture
    m_writes.clear();
    m_highlightRow = -1;
    m_timeline->setRowCount(0);
    m_fb->setWriteMarks({});
    setupScrub();
    setControlsEnabledForCapture(true);
    m_captureLabel->setStyleSheet(QString());
    m_captureLabel->setText(QStringLiteral("capture $%1: running…").arg(addr, 0, 16));
    m_capture->start(addr, m_countSpin->value());
}

void MainWindow::captureBlitTraffic()
{
    if (!m_rdb || !m_rdb->isConnected())
        return;
    const int depth = m_depthCombo->currentData().toInt();
    const int windowMs = m_windowSpin->value();
    m_liveTimer->stop();                 // don't interleave live polls with the run window
    m_captureLabel->setStyleSheet(QString());
    m_captureLabel->setText(QStringLiteral("blit trace: running…"));
    m_actBlitCapture->setEnabled(false);

    // Enable the tap (clears the buffer, caps at `depth` entries), let the machine
    // run the window, then break, dump, and disable. Opt-in, so it perturbs
    // nothing while off. The fork parses the entry cap as hex.
    m_rdb->sendCommand("blittrace on " + QByteArray::number(depth, 16));
    m_rdb->run();
    QTimer::singleShot(windowMs, this, [this, depth] {
        m_rdb->breakExec([this, depth](const RdbClient::Tokens &) {
            m_rdb->sendCommand("blittrace", [this, depth](const RdbClient::Tokens &r) {
                const QVector<BlitOp> ops = BlitterTrace::parse(r);
                m_blitView->setOps(ops);
                m_rdb->sendCommand("blittrace off");

                int reads = 0, writes = 0, blits = 0;
                for (const auto &op : ops) {
                    reads += op.reads();
                    writes += op.writes();
                    if (op.endCycle != 0)
                        ++blits;
                }
                const bool capped = (reads + writes + blits) >= depth;
                if (reads + writes > 0) {
                    m_captureLabel->setStyleSheet(
                        capped ? QStringLiteral("color:#b26a00;")     // amber: truncated
                               : QStringLiteral("color:#2e7d32;"));   // green
                    m_captureLabel->setText(
                        QStringLiteral("blit trace: %1 blits · %2 rd · %3 wr%4")
                            .arg(blits).arg(reads).arg(writes)
                            .arg(capped ? QStringLiteral(" · capped at %1 ⚠").arg(depth)
                                        : QStringLiteral(" ✓")));
                } else {
                    m_captureLabel->setStyleSheet(QStringLiteral("color:#c62828;"));   // red
                    m_captureLabel->setText(
                        QStringLiteral("blit trace: no blitter traffic in the run window"));
                }
                m_actBlitCapture->setEnabled(true);
                refresh();                                   // fresh frame after the break
                if (m_actLive->isChecked())
                    m_liveTimer->start();
            });
        });
    });
}

void MainWindow::captureDmaSound()
{
    if (!m_rdb || !m_rdb->isConnected())
        return;
    const int depth = m_depthCombo->currentData().toInt();
    const int windowMs = m_windowSpin->value();
    m_liveTimer->stop();
    m_captureLabel->setStyleSheet(QString());
    m_captureLabel->setText(QStringLiteral("DMA trace: running…"));
    m_actDmaCapture->setEnabled(false);

    m_rdb->sendCommand("dmatrace on " + QByteArray::number(depth, 16));
    m_rdb->run();
    QTimer::singleShot(windowMs, this, [this, depth] {
        m_rdb->breakExec([this, depth](const RdbClient::Tokens &) {
            m_rdb->sendCommand("dmatrace", [this, depth](const RdbClient::Tokens &r) {
                const DmaSndTrace t = DmaSndTrace::parse(r);
                m_dmaView->setTrace(t);
                m_rdb->sendCommand("dmatrace off");

                const int total = t.drain.size() + t.lmcSeq.size() + t.frames + (t.haveCtrl ? 1 : 0);
                const bool capped = total >= depth;
                const bool any = !t.drain.isEmpty() || t.haveCtrl || !t.lmcSeq.isEmpty();
                if (any) {
                    m_captureLabel->setStyleSheet(
                        capped ? QStringLiteral("color:#b26a00;")     // amber: truncated
                               : QStringLiteral("color:#2e7d32;"));
                    m_captureLabel->setText(
                        QStringLiteral("DMA trace: %1 drain · %2 EQ changes%3%4")
                            .arg(t.drain.size()).arg(t.lmcSeq.size())
                            .arg(t.playing() ? QStringLiteral(" · playing") : QString())
                            .arg(capped ? QStringLiteral(" · capped at %1 ⚠").arg(depth)
                                        : QStringLiteral(" ✓")));
                } else {
                    m_captureLabel->setStyleSheet(QStringLiteral("color:#c62828;"));
                    m_captureLabel->setText(
                        QStringLiteral("DMA trace: no DMA sound activity in the run window"));
                }
                m_actDmaCapture->setEnabled(true);
                refresh();
                if (m_actLive->isChecked())
                    m_liveTimer->start();
            });
        });
    });
}

namespace {
// <repo>/external/hatari/build/src/hatari  ->  <repo>
QString repoRootFrom(const QString &hatariBinary)
{
    QDir d(QFileInfo(hatariBinary).absolutePath());   // .../external/hatari/build/src
    for (int i = 0; i < 4; ++i)
        d.cdUp();                                      // -> repo root
    return d.absolutePath();
}
}   // namespace

QString MainWindow::rasterAsmForMode(const QVector<RasterCodegen::Bar> &bars,
                                     const QVector<RasterCodegen::Bar> &colBars,
                                     const QVector<quint16> &cols) const
{
    switch (m_raster->mode()) {
    case RasterWorkspace::Bars:
        return bars.isEmpty() ? QString() : RasterCodegen::generate(bars);
    case RasterWorkspace::Bands:
        return colBars.isEmpty() ? QString() : RasterCodegen::generateColumns(colBars);
    case RasterWorkspace::Copper:
        return bars.isEmpty() ? QString()
                              : RasterCodegen::generateCopper(bars, m_raster->copperSpeed());
    case RasterWorkspace::CopperBounce:
        return bars.isEmpty() ? QString()
                              : RasterCodegen::generateCopper(bars, m_raster->copperSpeed(), true);
    case RasterWorkspace::Cycle:
        return cols.isEmpty() ? QString() : RasterCodegen::generateColourCycle(cols);
    case RasterWorkspace::CycleColumn:
        return cols.isEmpty() ? QString() : RasterCodegen::generateColourCycle(cols, true);
    }
    return QString();
}

void MainWindow::buildRasterEffect(const QVector<RasterCodegen::Bar> &bars)
{
    const auto mode = m_raster->mode();
    const QVector<RasterCodegen::Bar> colBars = m_raster->columnBars();
    const QVector<quint16> cols = m_raster->colours();
    const QString asmText = rasterAsmForMode(bars, colBars, cols);
    if (asmText.isEmpty()) {
        m_raster->setResult(
            QStringLiteral("Add at least one %1 first.")
                .arg(mode == RasterWorkspace::Bands ? QStringLiteral("band")
                     : mode == RasterWorkspace::Cycle ? QStringLiteral("colour")
                                                      : QStringLiteral("bar")),
            false);
        return;
    }
    const QString repo = repoRootFrom(m_config.hatari.hatariBinary);
    const QString vasm = repo + QStringLiteral("/external/vasm-src/vasm/vasmm68k_mot");
    if (!QFileInfo::exists(vasm)) {
        m_raster->setResult(QStringLiteral("vasm not found — run scripts/bootstrap-vasm.sh"), false);
        return;
    }
    if (!m_rasterDir.isValid()) {
        m_raster->setResult(QStringLiteral("no scratch directory"), false);
        return;
    }

    const QString srcPath = m_rasterDir.filePath(QStringLiteral("raster.s"));
    QDir(m_rasterDir.path()).mkpath(QStringLiteral("AUTO"));
    QFile f(srcPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_raster->setResult(QStringLiteral("could not write raster.s"), false);
        return;
    }
    f.write(asmText.toUtf8());
    f.close();

    m_raster->setBusy(true);
    m_raster->setResult(QStringLiteral("Assembling…"), true);
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [this, proc, repo](int code, QProcess::ExitStatus) {
                m_raster->setBusy(false);
                if (code != 0) {
                    m_raster->setResult(QStringLiteral("vasm failed: %1").arg(
                        QString::fromUtf8(proc->readAllStandardError()).left(200)), false);
                } else {
                    // Preview on ST/PAL to match the codegen's timing (512 cyc/line).
                    {
                        const QSignalBlocker b1(m_machineCombo), b2(m_regionCombo);
                        m_machine = MachineType::ST;
                        m_region = VideoRegion::Pal50;
                        m_machineCombo->setCurrentIndex(Machines::all().indexOf(m_machine));
                        m_regionCombo->setCurrentIndex(0);
                    }
                    updateCapabilities();
                    m_config.hatari.gemdosDir = m_rasterDir.path();
                    m_config.hatari.diskImage.clear();
                    m_config.hatari.diskB.clear();
                    m_raster->setResult(QStringLiteral("Built RASTER.PRG — launching on ST/PAL…"), true);
                    if (m_launcher->isRunning() || m_rdb->isConnected())
                        relaunch();
                    else
                        onStartClicked();
                }
                proc->deleteLater();
            });
    proc->start(vasm, {QStringLiteral("-Ftos"),
                       QStringLiteral("-o"), m_rasterDir.filePath(QStringLiteral("AUTO/RASTER.PRG")),
                       srcPath});
}

void MainWindow::verifyRasterEffect(const QVector<RasterCodegen::Bar> &bars)
{
    const auto mode = m_raster->mode();
    const QVector<RasterCodegen::Bar> colBars = m_raster->columnBars();
    const QVector<quint16> cols = m_raster->colours();
    const QString asmText = rasterAsmForMode(bars, colBars, cols);
    if (asmText.isEmpty()) {
        m_raster->setResult(QStringLiteral("Add at least one bar / band / colour first."), false);
        return;
    }
    if (mode == RasterWorkspace::Bands && colBars.size() < 2) {
        m_raster->setResult(QStringLiteral("Bands verify needs 2+ bands (to check boundaries)."),
                            false);
        return;
    }
    const QString repo = repoRootFrom(m_config.hatari.hatariBinary);
    const bool copperLike = (mode == RasterWorkspace::Copper || mode == RasterWorkspace::CopperBounce);
    const bool animated = copperLike || mode == RasterWorkspace::Cycle
                          || mode == RasterWorkspace::CycleColumn;

    QStringList args;
    QString okMsg;
    if (animated) {
        // The animated codegens live in C++, so the harness assembles our stub.
        if (!m_rasterDir.isValid()) {
            m_raster->setResult(QStringLiteral("no scratch directory"), false);
            return;
        }
        const QString srcPath = m_rasterDir.filePath(QStringLiteral("raster.s"));
        QFile f(srcPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            m_raster->setResult(QStringLiteral("could not write raster.s"), false);
            return;
        }
        f.write(asmText.toUtf8());
        f.close();
        const QString tool = repo + QStringLiteral("/harness/anim_check.py");
        if (!QFileInfo::exists(tool)) {
            m_raster->setResult(QStringLiteral("verify harness not found: %1").arg(tool), false);
            return;
        }
        args = {tool, QStringLiteral("--hatari"), m_config.hatari.hatariBinary,
                QStringLiteral("--tos"), m_config.hatari.tosImage,
                QStringLiteral("--asm"), srcPath, QStringLiteral("--mode"),
                copperLike ? QStringLiteral("copper") : QStringLiteral("cycle")};
        okMsg = copperLike
                    ? QStringLiteral("Verified ✓ — the copper bars animate each frame")
                    : QStringLiteral("Verified ✓ — the palette cycles each frame");
    } else {
        const bool bands = mode == RasterWorkspace::Bands;
        const QString tool = repo + (bands ? QStringLiteral("/harness/intraline_split.py")
                                           : QStringLiteral("/harness/raster_roundtrip.py"));
        if (!QFileInfo::exists(tool)) {
            m_raster->setResult(QStringLiteral("verify harness not found: %1").arg(tool), false);
            return;
        }
        args = {tool, QStringLiteral("--hatari"), m_config.hatari.hatariBinary,
                QStringLiteral("--tos"), m_config.hatari.tosImage};
        if (bands) {
            QStringList bc;   // boundary columns (leftmost band fills from the edge)
            for (int i = 1; i < colBars.size(); ++i)
                bc << QString::number(colBars[i].line);
            args << QStringLiteral("--cols") << bc.join(QLatin1Char(','));
            okMsg = QStringLiteral("Verified ✓ — band boundaries land at their columns");
        } else {
            for (const auto &b : bars)
                args << QStringLiteral("--bar")
                     << QStringLiteral("%1:%2").arg(b.line).arg(b.colour, 0, 16);
            okMsg = QStringLiteral("Verified ✓ — authored bars reproduced in stock Hatari");
        }
    }

    // The harness launches its own Hatari on the fixed port, so free ours first.
    if (m_launcher->isRunning() || m_rdb->isConnected())
        doStop();

    m_raster->setBusy(true);
    m_raster->setResult(QStringLiteral("Verifying on Hatari (headless)…"), true);
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [this, proc, okMsg](int code, QProcess::ExitStatus) {
                m_raster->setBusy(false);
                const QString out = QString::fromUtf8(proc->readAllStandardOutput());
                const bool ok = (code == 0) && out.contains(QStringLiteral("RESULT: PASS"));
                m_raster->setResult(
                    ok ? okMsg
                       : QStringLiteral("Verify FAILED — %1").arg(out.section('\n', -2).trimmed()),
                    ok);
                proc->deleteLater();
            });
    proc->start(QStringLiteral("python3"), args);
}

void MainWindow::exportRasterEffect(const QVector<RasterCodegen::Bar> &bars)
{
    const bool bands = m_raster->mode() == RasterWorkspace::Bands;
    const QVector<RasterCodegen::Bar> colBars = m_raster->columnBars();
    const QVector<quint16> cols = m_raster->colours();
    const QString asmText = rasterAsmForMode(bars, colBars, cols);
    if (asmText.isEmpty()) {
        m_raster->setResult(QStringLiteral("Add at least one bar / band / colour first."), false);
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Export raster effect to folder"));
    if (dir.isEmpty())
        return;   // cancelled

    // 1. The asm stub (the runnable export artefact).
    QFile s(QDir(dir).filePath(QStringLiteral("raster.s")));
    if (!s.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_raster->setResult(QStringLiteral("could not write to %1").arg(dir), false);
        return;
    }
    s.write(asmText.toUtf8());
    s.close();

    // 2. The register sequence (portable data form): $ff8240 writes tagged with
    //    machine/region — per scanline (bars) or per column boundary (bands).
    QJsonArray writes;
    if (bands) {
        for (const auto &b : colBars)
            writes.append(QJsonObject{
                {QStringLiteral("column"), b.line},
                {QStringLiteral("value"),
                 QStringLiteral("%1").arg(b.colour, 3, 16, QLatin1Char('0'))}});
    } else {
        QVector<RasterCodegen::Bar> sorted = bars;
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto &a, const auto &b) { return a.line < b.line; });
        for (const auto &b : sorted)
            writes.append(QJsonObject{
                {QStringLiteral("scanline"), b.line},
                {QStringLiteral("value"),
                 QStringLiteral("%1").arg(b.colour, 3, 16, QLatin1Char('0'))}});
    }
    const QJsonObject seq{
        {QStringLiteral("effect"),
         bands ? QStringLiteral("vertical-bands") : QStringLiteral("raster-bars")},
        {QStringLiteral("generator"), QStringLiteral("Talos F-212")},
        {QStringLiteral("machine"), QStringLiteral("st")},
        {QStringLiteral("region"), QStringLiteral("pal")},
        {QStringLiteral("cyclesPerLine"), RasterCodegen::kCycPerLine},
        {QStringLiteral("sync"), bands ? QStringLiteral("hbl") : QStringLiteral("vbl")},
        {QStringLiteral("register"), QStringLiteral("ff8240")},
        {QStringLiteral("writes"), writes}};
    QFile j(QDir(dir).filePath(QStringLiteral("raster.json")));
    if (j.open(QIODevice::WriteOnly | QIODevice::Text)) {
        j.write(QJsonDocument(seq).toJson());
        j.close();
    }

    // 3. Assemble the runnable .PRG alongside (best-effort).
    const QString repo = repoRootFrom(m_config.hatari.hatariBinary);
    const QString vasm = repo + QStringLiteral("/external/vasm-src/vasm/vasmm68k_mot");
    QString prgNote;
    if (QFileInfo::exists(vasm)) {
        QProcess p;
        p.start(vasm, {QStringLiteral("-Ftos"), QStringLiteral("-o"),
                       QDir(dir).filePath(QStringLiteral("RASTER.PRG")),
                       QDir(dir).filePath(QStringLiteral("raster.s"))});
        prgNote = (p.waitForFinished(5000) && p.exitCode() == 0)
                      ? QStringLiteral(" + RASTER.PRG")
                      : QString();
    }

    m_raster->setResult(
        QStringLiteral("Exported raster.s + raster.json%1 to %2").arg(prgNote, dir), true);
}

void MainWindow::importRasterEffect()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import register sequence"), QString(),
        QStringLiteral("Register sequence (*.json)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_raster->setResult(QStringLiteral("could not open %1").arg(file), false);
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) {
        m_raster->setResult(QStringLiteral("not a valid register sequence"), false);
        return;
    }
    const QJsonObject o = doc.object();
    // "vertical-bands" -> Bands (column,colour); anything else -> Bars (scanline,colour).
    const bool bands = o.value(QStringLiteral("effect")).toString() == QStringLiteral("vertical-bands");
    QVector<QPair<int, quint16>> entries;
    for (const QJsonValue &wv : o.value(QStringLiteral("writes")).toArray()) {
        const QJsonObject w = wv.toObject();
        const int pos = bands ? w.value(QStringLiteral("column")).toInt()
                              : w.value(QStringLiteral("scanline")).toInt();
        const quint16 col = static_cast<quint16>(
            w.value(QStringLiteral("value")).toString().toUShort(nullptr, 16) & 0x777);
        entries.append({pos, col});
    }
    if (entries.isEmpty()) {
        m_raster->setResult(QStringLiteral("no writes found in %1").arg(file), false);
        return;
    }
    m_raster->loadEntries(bands ? RasterWorkspace::Bands : RasterWorkspace::Bars, entries);
}

void MainWindow::buildScrollerEffect(const QString &message, int speed)
{
    if (message.trimmed().isEmpty()) {
        m_scroller->setResult(QStringLiteral("Type a message first."), false);
        return;
    }
    const QString repo = repoRootFrom(m_config.hatari.hatariBinary);
    const QString vasm = repo + QStringLiteral("/external/vasm-src/vasm/vasmm68k_mot");
    if (!QFileInfo::exists(vasm)) {
        m_scroller->setResult(QStringLiteral("vasm not found — run scripts/bootstrap-vasm.sh"), false);
        return;
    }
    if (!m_scrollerDir.isValid()) {
        m_scroller->setResult(QStringLiteral("no scratch directory"), false);
        return;
    }
    const QString srcPath = m_scrollerDir.filePath(QStringLiteral("scroller.s"));
    QDir(m_scrollerDir.path()).mkpath(QStringLiteral("AUTO"));
    QFile f(srcPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_scroller->setResult(QStringLiteral("could not write scroller.s"), false);
        return;
    }
    f.write(ScrollerCodegen::generate(message, speed, ScrollerCodegen::kDefaultVScale, m_scroller->font()).toUtf8());
    f.close();

    m_scroller->setBusy(true);
    m_scroller->setResult(QStringLiteral("Assembling…"), true);
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        m_scroller->setBusy(false);
        if (code != 0) {
            m_scroller->setResult(QStringLiteral("vasm failed: %1").arg(
                QString::fromUtf8(proc->readAllStandardError()).left(200)), false);
        } else {
            // The effect is STE-only (hardware scroll), so preview on STE/PAL.
            {
                const QSignalBlocker b1(m_machineCombo), b2(m_regionCombo);
                m_machine = MachineType::STE;
                m_region = VideoRegion::Pal50;
                m_machineCombo->setCurrentIndex(Machines::all().indexOf(m_machine));
                m_regionCombo->setCurrentIndex(0);
            }
            updateCapabilities();
            m_config.hatari.gemdosDir = m_scrollerDir.path();
            m_config.hatari.diskImage.clear();
            m_config.hatari.diskB.clear();
            m_scroller->setResult(QStringLiteral("Built SCROLLER.PRG — launching on STE/PAL…"), true);
            if (m_launcher->isRunning() || m_rdb->isConnected())
                relaunch();
            else
                onStartClicked();
        }
        proc->deleteLater();
    });
    proc->start(vasm, {QStringLiteral("-Ftos"),
                       QStringLiteral("-o"), m_scrollerDir.filePath(QStringLiteral("AUTO/SCROLLER.PRG")),
                       srcPath});
}

void MainWindow::verifyScrollerEffect(const QString &message, int speed)
{
    if (message.trimmed().isEmpty()) {
        m_scroller->setResult(QStringLiteral("Type a message first."), false);
        return;
    }
    const QString repo = repoRootFrom(m_config.hatari.hatariBinary);
    const QString tool = repo + QStringLiteral("/harness/scroller_scroll.py");
    if (!QFileInfo::exists(tool)) {
        m_scroller->setResult(QStringLiteral("verify harness not found: %1").arg(tool), false);
        return;
    }
    // The harness assembles the client-generated stub (the font lives in C++, so
    // it can't regenerate it), so write scroller.s out and hand it the path.
    if (!m_scrollerDir.isValid()) {
        m_scroller->setResult(QStringLiteral("no scratch directory"), false);
        return;
    }
    const QString srcPath = m_scrollerDir.filePath(QStringLiteral("scroller.s"));
    QFile sf(srcPath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_scroller->setResult(QStringLiteral("could not write scroller.s"), false);
        return;
    }
    sf.write(ScrollerCodegen::generate(message, speed, ScrollerCodegen::kDefaultVScale, m_scroller->font()).toUtf8());
    sf.close();

    if (m_launcher->isRunning() || m_rdb->isConnected())
        doStop();   // the harness launches its own Hatari on the fixed port

    QStringList args{tool, QStringLiteral("--hatari"), m_config.hatari.hatariBinary,
                     QStringLiteral("--tos"), m_config.hatari.tosImage,
                     QStringLiteral("--asm"), srcPath,
                     QStringLiteral("--speed"), QString::number(speed),
                     QStringLiteral("--message"), message};

    m_scroller->setBusy(true);
    m_scroller->setResult(QStringLiteral("Verifying on Hatari (headless, ~15 s)…"), true);
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        m_scroller->setBusy(false);
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        const bool ok = (code == 0) && out.contains(QStringLiteral("RESULT: PASS"));
        m_scroller->setResult(
            ok ? QStringLiteral("Verified ✓ — message renders and scrolls smoothly left")
               : QStringLiteral("Verify FAILED — %1").arg(out.section('\n', -2).trimmed()),
            ok);
        proc->deleteLater();
    });
    proc->start(QStringLiteral("python3"), args);
}

void MainWindow::buildBorderEffect(BorderCodegen::Border border)
{
    if (border != BorderCodegen::Border::Left) {
        m_borderView->setResult(
            QStringLiteral("Only the left border is runnable; the others are teaching views."),
            false);
        return;
    }
    const QString repo = repoRootFrom(m_config.hatari.hatariBinary);
    const QString vasm = repo + QStringLiteral("/external/vasm-src/vasm/vasmm68k_mot");
    if (!QFileInfo::exists(vasm)) {
        m_borderView->setResult(QStringLiteral("vasm not found — run scripts/bootstrap-vasm.sh"), false);
        return;
    }
    if (!m_borderDir.isValid()) {
        m_borderView->setResult(QStringLiteral("no scratch directory"), false);
        return;
    }
    const QString srcPath = m_borderDir.filePath(QStringLiteral("border.s"));
    QDir(m_borderDir.path()).mkpath(QStringLiteral("AUTO"));
    QFile f(srcPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_borderView->setResult(QStringLiteral("could not write border.s"), false);
        return;
    }
    f.write(BorderCodegen::generateLeft().toUtf8());
    f.close();

    m_borderView->setBusy(true);
    m_borderView->setResult(QStringLiteral("Assembling…"), true);
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus) {
        m_borderView->setBusy(false);
        if (code != 0) {
            m_borderView->setResult(QStringLiteral("vasm failed: %1").arg(
                QString::fromUtf8(proc->readAllStandardError()).left(200)), false);
        } else {
            // Left-border timing is ST/PAL (512 cyc/line) — preview there.
            {
                const QSignalBlocker b1(m_machineCombo), b2(m_regionCombo);
                m_machine = MachineType::ST;
                m_region = VideoRegion::Pal50;
                m_machineCombo->setCurrentIndex(Machines::all().indexOf(m_machine));
                m_regionCombo->setCurrentIndex(0);
            }
            updateCapabilities();
            m_config.hatari.gemdosDir = m_borderDir.path();
            m_config.hatari.diskImage.clear();
            m_config.hatari.diskB.clear();
            m_borderView->setResult(
                QStringLiteral("Built BORDER.PRG — launching on ST/PAL; watch the left border open."),
                true);
            if (m_launcher->isRunning() || m_rdb->isConnected())
                relaunch();
            else
                onStartClicked();
        }
        proc->deleteLater();
    });
    proc->start(vasm, {QStringLiteral("-Ftos"),
                       QStringLiteral("-o"), m_borderDir.filePath(QStringLiteral("AUTO/BORDER.PRG")),
                       srcPath});
}

void MainWindow::verifyBorderEffect(BorderCodegen::Border border)
{
    if (border != BorderCodegen::Border::Left) {
        m_borderView->setResult(
            QStringLiteral("Only the left border has a runnable check in this build."), false);
        return;
    }
    const QString repo = repoRootFrom(m_config.hatari.hatariBinary);
    const QString vasm = repo + QStringLiteral("/external/vasm-src/vasm/vasmm68k_mot");
    const QString tool = repo + QStringLiteral("/harness/diff_harness.py");
    if (!QFileInfo::exists(vasm) || !QFileInfo::exists(tool)) {
        m_borderView->setResult(QStringLiteral("vasm or harness not found under %1").arg(repo), false);
        return;
    }
    if (!m_borderDir.isValid()) {
        m_borderView->setResult(QStringLiteral("no scratch directory"), false);
        return;
    }
    const QString srcPath = m_borderDir.filePath(QStringLiteral("border.s"));
    QDir(m_borderDir.path()).mkpath(QStringLiteral("AUTO"));
    QFile f(srcPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_borderView->setResult(QStringLiteral("could not write border.s"), false);
        return;
    }
    f.write(BorderCodegen::generateLeft().toUtf8());
    f.close();

    if (m_launcher->isRunning() || m_rdb->isConnected())
        doStop();   // the harness launches its own Hatari on the fixed port

    // Two steps: assemble border.s -> AUTO/BORDER.PRG, then run the border-check
    // harness against the folder. Chained so the harness sees the fresh PRG.
    m_borderView->setBusy(true);
    m_borderView->setResult(QStringLiteral("Assembling…"), true);
    auto *asmProc = new QProcess(this);
    connect(asmProc, &QProcess::finished, this,
            [this, asmProc, repo, tool](int code, QProcess::ExitStatus) {
        if (code != 0) {
            m_borderView->setBusy(false);
            m_borderView->setResult(QStringLiteral("vasm failed: %1").arg(
                QString::fromUtf8(asmProc->readAllStandardError()).left(200)), false);
            asmProc->deleteLater();
            return;
        }
        asmProc->deleteLater();
        m_borderView->setResult(QStringLiteral("Verifying on Hatari (headless, ~20 s)…"), true);
        QStringList args{tool, QStringLiteral("--hatari"), m_config.hatari.hatariBinary,
                         QStringLiteral("--tos"), m_config.hatari.tosImage,
                         QStringLiteral("--effect"), m_borderDir.path(),
                         QStringLiteral("--border-check")};
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, this, [this, proc](int c, QProcess::ExitStatus) {
            m_borderView->setBusy(false);
            const QString out = QString::fromUtf8(proc->readAllStandardOutput());
            const bool ok = (c == 0) && out.contains(QStringLiteral("BORDER CHECK PASS"));
            m_borderView->setResult(
                ok ? QStringLiteral("Verified ✓ — the left border opens on a band of scanlines")
                   : QStringLiteral("Verify FAILED — %1").arg(out.section('\n', -2).trimmed()),
                ok);
            proc->deleteLater();
        });
        proc->start(QStringLiteral("python3"), args);
    });
    asmProc->start(vasm, {QStringLiteral("-Ftos"),
                          QStringLiteral("-o"),
                          m_borderDir.filePath(QStringLiteral("AUTO/BORDER.PRG")), srcPath});
}

void MainWindow::exportScrollerEffect(const QString &message, int speed)
{
    if (message.trimmed().isEmpty()) {
        m_scroller->setResult(QStringLiteral("Type a message first."), false);
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Export scroller effect to folder"));
    if (dir.isEmpty())
        return;   // cancelled

    // 1. The asm stub (the runnable export artefact).
    QFile s(QDir(dir).filePath(QStringLiteral("scroller.s")));
    if (!s.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_scroller->setResult(QStringLiteral("could not write to %1").arg(dir), false);
        return;
    }
    s.write(ScrollerCodegen::generate(message, speed, ScrollerCodegen::kDefaultVScale, m_scroller->font()).toUtf8());
    s.close();

    // 2. The portable sequence form: the STE fine-scroll register + message/speed.
    const QJsonObject seq{
        {QStringLiteral("effect"), QStringLiteral("ste-hardware-scroller")},
        {QStringLiteral("generator"), QStringLiteral("Talos F-212")},
        {QStringLiteral("machine"), QStringLiteral("ste")},
        {QStringLiteral("region"), QStringLiteral("pal")},
        {QStringLiteral("register"), QStringLiteral("ff8265")},
        {QStringLiteral("sync"), QStringLiteral("vbl")},
        {QStringLiteral("speed"), speed},
        {QStringLiteral("message"), message}};
    QFile j(QDir(dir).filePath(QStringLiteral("scroller.json")));
    if (j.open(QIODevice::WriteOnly | QIODevice::Text)) {
        j.write(QJsonDocument(seq).toJson());
        j.close();
    }

    // 3. Assemble the runnable .PRG alongside (best-effort).
    const QString repo = repoRootFrom(m_config.hatari.hatariBinary);
    const QString vasm = repo + QStringLiteral("/external/vasm-src/vasm/vasmm68k_mot");
    QString prgNote;
    if (QFileInfo::exists(vasm)) {
        QProcess p;
        p.start(vasm, {QStringLiteral("-Ftos"), QStringLiteral("-o"),
                       QDir(dir).filePath(QStringLiteral("SCROLLER.PRG")),
                       QDir(dir).filePath(QStringLiteral("scroller.s"))});
        prgNote = (p.waitForFinished(5000) && p.exitCode() == 0)
                      ? QStringLiteral(" + SCROLLER.PRG")
                      : QString();
    }
    m_scroller->setResult(
        QStringLiteral("Exported scroller.s + scroller.json%1 to %2").arg(prgNote, dir), true);
}

void MainWindow::importScrollerEffect()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import scroller sequence"), QString(),
        QStringLiteral("Scroller sequence (*.json)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_scroller->setResult(QStringLiteral("could not open %1").arg(file), false);
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()
        || doc.object().value(QStringLiteral("effect")).toString()
               != QStringLiteral("ste-hardware-scroller")) {
        m_scroller->setResult(QStringLiteral("not a Talos scroller sequence"), false);
        return;
    }
    const QJsonObject o = doc.object();
    m_scroller->loadFrom(o.value(QStringLiteral("message")).toString(),
                         o.value(QStringLiteral("speed")).toInt(ScrollerCodegen::kDefaultSpeed));
}

void MainWindow::onFramebufferClicked(const QPointF &imagePixel)
{
    // Only meaningful while authoring: turn the clicked pixel into a visible
    // scanline (Bars) / framebuffer column (Bands) and hand it to the workspace.
    if (m_fb->imageSize().isEmpty())
        return;
    const BeamGeometry geo(m_region, m_fb->imageSize());
    const int line = geo.scanlineAtY(imagePixel.y()) - geo.firstVisibleHbl();
    const int column = static_cast<int>(imagePixel.x());
    m_raster->placeFromClick(line, column);
}

void MainWindow::onCaptureProgress(int count, int target)
{
    m_captureLabel->setText(QStringLiteral("capture $%1: %2/%3…")
                                .arg(m_capture->address(), 0, 16).arg(count).arg(target));
}

void MainWindow::onCaptureFinished(bool ok, const QString &reason)
{
    m_writes = m_capture->events();
    populateTimeline();
    setupScrub();
    updateReconstruct();   // F-218: rebuild the register field from the new writes
    setControlsEnabledForCapture(false);
    // Persist the result in the status bar so a 0-write timeout isn't easy to miss.
    const QString reg = QStringLiteral("$%1").arg(m_capture->address(), 0, 16);
    if (ok && !m_writes.isEmpty()) {
        m_captureLabel->setStyleSheet(QStringLiteral("color:#2e7d32;"));   // green
        m_captureLabel->setText(
            QStringLiteral("capture %1: %2 writes ✓").arg(reg).arg(m_writes.size()));
    } else {
        m_captureLabel->setStyleSheet(QStringLiteral("color:#c62828;"));   // red
        m_captureLabel->setText(QStringLiteral("capture %1: %2 writes — %3")
                                    .arg(reg).arg(m_writes.size()).arg(reason));
    }
    refresh();   // fresh frame; write marks are recomputed in refreshScreen
    emit captureCompleted(m_writes.size());
}

void MainWindow::onTimelineRowChanged(int row)
{
    // With a scrubber armed, selecting a write moves the cursor to it (which
    // draws the beam there and accumulates the writes up to it).
    if (m_scrub->isEnabled() && row >= 0 && row < m_writes.size()) {
        m_scrub->setValue(static_cast<int>(m_writes[row].frameCycle));
        return;
    }
    m_highlightRow = row;
    recomputeWriteMarks(m_fb->imageSize());
}

void MainWindow::setupScrub()
{
    m_scrubPlay->setChecked(false);
    if (m_writes.isEmpty()) {
        m_scrub->setEnabled(false);
        m_scrubPlay->setEnabled(false);
        m_scrubCycle = -1;
        m_scrubInfo->clear();
        return;
    }
    const int cpl = cyclesPerLine();
    int maxFc = 0;
    for (const WriteEvent &w : m_writes)
        maxFc = std::max(maxFc, static_cast<int>(w.frameCycle));
    const int smax = (maxFc / cpl + 2) * cpl;   // a couple of lines past the last write

    const QSignalBlocker block(m_scrub);
    m_scrub->setRange(0, smax);
    m_scrub->setSingleStep(cpl);
    m_scrub->setPageStep(cpl * 8);
    m_scrub->setValue(smax);            // default: whole frame shown
    m_scrub->setEnabled(true);
    m_scrubPlay->setEnabled(true);
    m_scrubCycle = smax;                // all writes visible until the user scrubs
    m_scrubInfo->setText(QStringLiteral("%1 writes over %2 lines — drag to scrub, ▶ to play")
                             .arg(m_writes.size())
                             .arg(maxFc / cpl + 1));
}

void MainWindow::onScrub(int frameCycle)
{
    m_scrubCycle = frameCycle;
    const int cpl = cyclesPerLine();
    const int line = frameCycle / cpl;
    const int cil = frameCycle % cpl;
    const QSize fs = m_fb->imageSize();

    showBeamAt(line, cil, fs, QStringLiteral("scrub · "));

    // The current write is the latest one the beam has reached.
    int seen = 0, last = -1;
    for (int i = 0; i < m_writes.size(); ++i)
        if (static_cast<int>(m_writes[i].frameCycle) <= frameCycle) {
            ++seen;
            last = i;
        }
    m_highlightRow = last;
    recomputeWriteMarks(fs);

    QString info = QStringLiteral("line %1 · cyc %2 · %3/%4 writes")
                       .arg(line).arg(cil).arg(seen).arg(m_writes.size());
    if (last >= 0)
        info += QStringLiteral(" · last $%1=$%2")
                    .arg(m_writes[last].address, 0, 16)
                    .arg(m_writes[last].value, 0, 16);
    m_scrubInfo->setText(info);
}

void MainWindow::toggleScrubPlay(bool on)
{
    m_scrubPlay->setText(on ? QStringLiteral("⏸") : QStringLiteral("▶"));
    if (on && m_scrub->isEnabled()) {
        if (m_scrub->value() >= m_scrub->maximum())
            m_scrub->setValue(0);   // restart the sweep from the top of the frame
        m_scrubTimer->start();
    } else {
        m_scrubTimer->stop();
    }
}

void MainWindow::recomputeWriteMarks(QSize frameSize)
{
    if (frameSize.isEmpty() || m_writes.isEmpty()) {
        m_fb->setWriteMarks({});
        return;
    }
    const BeamGeometry geo(m_region, frameSize);
    QVector<FramebufferView::WriteMark> marks;
    marks.reserve(m_writes.size());
    for (int i = 0; i < m_writes.size(); ++i) {
        const WriteEvent &w = m_writes[i];
        // While scrubbing, only show writes the beam has already reached.
        if (m_scrubCycle >= 0 && static_cast<int>(w.frameCycle) > m_scrubCycle)
            continue;
        const auto px = geo.toPixel(w.scanline, w.cycleInLine);
        if (!px)
            continue;   // write landed in blanking — not on the rendered frame
        FramebufferView::WriteMark m;
        m.pos = *px;
        m.color = stColour(w.value);   // draw in the colour the write set
        m.highlight = (i == m_highlightRow);
        marks.append(m);
    }
    m_fb->setWriteMarks(marks);
}

void MainWindow::updateReconstruct()
{
    if (m_reconstruct)
        m_reconstruct->setReconstruction(m_writes, m_region, m_capture->address());
}

void MainWindow::compareMachines(MachineType a, MachineType b)
{
    const QString effect = m_config.hatari.gemdosDir;
    if (effect.isEmpty()) {
        m_ab->setStatus(QStringLiteral("Build or load an effect first (Raster / Scroller / Border)."),
                        false);
        return;
    }
    const QString repo = repoRootFrom(m_config.hatari.hatariBinary);
    const QString tool = repo + QStringLiteral("/harness/ab_compare.py");
    if (!QFileInfo::exists(tool)) {
        m_ab->setStatus(QStringLiteral("A/B harness not found: %1").arg(tool), false);
        return;
    }
    if (m_launcher->isRunning() || m_rdb->isConnected())
        doStop();   // the harness launches its own Hatari on the fixed port

    const QString outA = m_shotDir.isValid() ? QDir(m_shotDir.path()).filePath("ab_a.png")
                                             : QString();
    const QString outB = m_shotDir.isValid() ? QDir(m_shotDir.path()).filePath("ab_b.png")
                                             : QString();
    const QString nameA = Machines::info(a).name, nameB = Machines::info(b).name;

    m_ab->setBusy(true);
    QStringList args{tool, QStringLiteral("--hatari"), m_config.hatari.hatariBinary,
                     QStringLiteral("--tos"), m_config.hatari.tosImage,
                     QStringLiteral("--effect"), effect,
                     QStringLiteral("--machine-a"), Machines::info(a).hatariMachine,
                     QStringLiteral("--machine-b"), Machines::info(b).hatariMachine,
                     QStringLiteral("--out-a"), outA, QStringLiteral("--out-b"), outB};
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [this, proc, outA, outB, nameA, nameB](int code, QProcess::ExitStatus) {
                m_ab->setBusy(false);
                const QImage a(outA), b(outB);
                if (code != 0 || a.isNull() || b.isNull()) {
                    const QString err = QString::fromUtf8(proc->readAllStandardOutput())
                                            .section('\n', -2).trimmed();
                    m_ab->setStatus(QStringLiteral("Compare failed — %1").arg(err), false);
                } else {
                    m_ab->setFrames(a, b, nameA, nameB);
                }
                proc->deleteLater();
            });
    proc->start(QStringLiteral("python3"), args);
}

void MainWindow::openProgram()
{
    if (m_config.attachOnly) {
        statusBar()->showMessage(
            QStringLiteral("Can't load a program into an attached Hatari."), 5000);
        return;
    }
    // Qt matches name-filter globs case-sensitively on Linux, so build each
    // pattern case-insensitively (*.[sS][tT]) — DEMO.ST shows as readily as .st.
    const QStringList exts = {"prg", "tos", "ttp", "app", "gtp",
                              "st", "msa", "stx", "dim", "ipf", "img", "raw", "zip"};
    QStringList pats;
    for (const QString &e : exts) {
        QString g = QStringLiteral("*.");
        for (const QChar c : e)
            g += QStringLiteral("[%1%2]").arg(c.toLower()).arg(c.toUpper());
        pats << g;
    }
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open ST program or disk image"), QString(),
        QStringLiteral("ST programs & disks (%1);;All files (*)").arg(pats.join(QLatin1Char(' '))));
    if (file.isEmpty())
        return;
    loadFile(file);
}

void MainWindow::loadFile(const QString &file)
{
    if (m_config.attachOnly || file.isEmpty())
        return;
    if (!QFileInfo::exists(file)) {
        statusBar()->showMessage(QStringLiteral("File no longer exists: %1").arg(file), 5000);
        removeRecent(file);
        return;
    }

    const QFileInfo fi(file);
    const QString ext = fi.suffix().toLower();
    static const QStringList diskExt = {"st", "msa", "stx", "dim", "ipf", "img", "raw", "zip"};
    static const QStringList autoExt = {"prg", "tos"};

    // A new load replaces any effect / prior program / snapshot / disk.
    m_config.hatari.gemdosDir.clear();
    m_config.hatari.diskImage.clear();
    m_config.hatari.diskB.clear();
    m_config.hatari.memStateFile.clear();

    if (diskExt.contains(ext)) {
        m_config.hatari.diskImage = file;   // boot the floppy (bootsector if bootable)
        statusBar()->showMessage(
            QStringLiteral("Disk A: %1 — booting…").arg(fi.fileName()), 6000);
    } else {
        // A program: stage it on a fresh GEMDOS drive so TOS auto-runs it from
        // AUTO\ (.prg/.tos) or shows it on the desktop to run by hand.
        if (!m_loadDir.isValid()) {
            statusBar()->showMessage(QStringLiteral("No scratch directory for the program."), 5000);
            return;
        }
        QDir root(m_loadDir.path());
        root.removeRecursively();               // clear any previously-loaded program
        QDir().mkpath(m_loadDir.path());
        const bool autoRun = autoExt.contains(ext);
        if (autoRun)
            root.mkpath(QStringLiteral("AUTO"));
        // GEMDOS is 8.3: stage under a safe name that always mounts/auto-runs.
        const QString target = autoRun
            ? root.filePath(QStringLiteral("AUTO/PROG.%1").arg(ext.toUpper()))
            : root.filePath(QStringLiteral("PROG.%1").arg(ext.toUpper()));
        QFile::remove(target);
        if (!QFile::copy(file, target)) {
            statusBar()->showMessage(
                QStringLiteral("Could not stage %1.").arg(fi.fileName()), 5000);
            return;
        }
        m_config.hatari.gemdosDir = m_loadDir.path();
        statusBar()->showMessage(
            autoRun ? QStringLiteral("C:\\AUTO\\%1 — auto-running…").arg(fi.fileName())
                    : QStringLiteral("C:\\%1 — run it from the ST desktop.").arg(fi.fileName()),
            8000);
    }

    addRecent(file);
    // Loaded content should boot in real time so it can be watched (no fast-boot).
    if (m_launcher->isRunning() || m_rdb->isConnected())
        relaunch();
    else
        onStartClicked();
}

void MainWindow::addRecent(const QString &file)
{
    const QString abs = QFileInfo(file).absoluteFilePath();
    m_recentFiles.removeAll(abs);
    m_recentFiles.prepend(abs);
    while (m_recentFiles.size() > kMaxRecent)
        m_recentFiles.removeLast();
    QSettings().setValue(QStringLiteral("recentFiles"), m_recentFiles);
    rebuildRecentMenu();
}

void MainWindow::removeRecent(const QString &file)
{
    int removed = m_recentFiles.removeAll(file);
    removed += m_recentFiles.removeAll(QFileInfo(file).absoluteFilePath());
    if (removed > 0) {
        QSettings().setValue(QStringLiteral("recentFiles"), m_recentFiles);
        rebuildRecentMenu();
    }
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    if (m_recentFiles.isEmpty()) {
        QAction *none = m_recentMenu->addAction(QStringLiteral("(no recent files)"));
        none->setEnabled(false);
        return;
    }
    for (const QString &f : m_recentFiles) {
        QAction *a = m_recentMenu->addAction(QFileInfo(f).fileName());
        a->setToolTip(f);
        connect(a, &QAction::triggered, this, [this, f] { loadFile(f); });
    }
    m_recentMenu->addSeparator();
    QAction *clear = m_recentMenu->addAction(QStringLiteral("Clear recent files"));
    connect(clear, &QAction::triggered, this, [this] {
        m_recentFiles.clear();
        QSettings().setValue(QStringLiteral("recentFiles"), m_recentFiles);
        rebuildRecentMenu();
    });
}

void MainWindow::manageDisks()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Floppy disks — drive A / B"));
    auto *grid = new QGridLayout(&dlg);

    // Case-insensitive disk-image filter (DEMO.ST as readily as demo.st).
    const QStringList exts = {"st", "msa", "stx", "dim", "ipf", "img", "raw", "zip"};
    QStringList pats;
    for (const QString &e : exts) {
        QString g = QStringLiteral("*.");
        for (const QChar c : e)
            g += QStringLiteral("[%1%2]").arg(c.toLower()).arg(c.toUpper());
        pats << g;
    }
    const QString filter = QStringLiteral("Disk images (%1);;All files (*)").arg(pats.join(QLatin1Char(' ')));

    const bool connected = m_rdb->isConnected();
    grid->addWidget(new QLabel(
        connected ? QStringLiteral("Insert / eject swaps the disk on the running machine — "
                                   "feed a multi-disk demo its next disk without a reboot.")
                  : QStringLiteral("Not running — the drives will be set for the next launch."),
        &dlg), 0, 0, 1, 4);

    auto addRow = [&](int rowIdx, int drive, QString *cfg, const QString &name) {
        grid->addWidget(new QLabel(QStringLiteral("Drive %1:").arg(name), &dlg), rowIdx, 0);
        auto *lbl = new QLabel(cfg->isEmpty() ? QStringLiteral("(empty)")
                                              : QFileInfo(*cfg).fileName(), &dlg);
        lbl->setMinimumWidth(200);
        grid->addWidget(lbl, rowIdx, 1);
        auto *ins = new QPushButton(QStringLiteral("Insert…"), &dlg);
        auto *ej = new QPushButton(QStringLiteral("Eject"), &dlg);
        grid->addWidget(ins, rowIdx, 2);
        grid->addWidget(ej, rowIdx, 3);
        connect(ins, &QPushButton::clicked, &dlg, [this, &dlg, filter, cfg, lbl, drive, name, connected] {
            const QString f = QFileDialog::getOpenFileName(
                &dlg, QStringLiteral("Insert disk into drive %1").arg(name), QString(), filter);
            if (f.isEmpty())
                return;
            *cfg = f;
            lbl->setText(QFileInfo(f).fileName());
            addRecent(f);
            if (connected)
                m_rdb->sendCommand(QStringLiteral("floppy %1 %2").arg(drive).arg(f).toUtf8());
            statusBar()->showMessage(
                QStringLiteral("Drive %1: %2%3").arg(name, QFileInfo(f).fileName(),
                    connected ? QStringLiteral(" — swapped live") : QStringLiteral(" — on next launch")),
                5000);
        });
        connect(ej, &QPushButton::clicked, &dlg, [this, cfg, lbl, drive, name, connected] {
            cfg->clear();
            lbl->setText(QStringLiteral("(empty)"));
            if (connected)
                m_rdb->sendCommand(QStringLiteral("floppy %1 none").arg(drive).toUtf8());
            statusBar()->showMessage(QStringLiteral("Drive %1 ejected").arg(name), 4000);
        });
    };
    addRow(1, 0, &m_config.hatari.diskImage, QStringLiteral("A"));
    addRow(2, 1, &m_config.hatari.diskB, QStringLiteral("B"));

    auto *desktop = new QPushButton(QStringLiteral("Boot to clean desktop"), &dlg);
    desktop->setToolTip(QStringLiteral("Eject everything and relaunch to a bare TOS desktop"));
    connect(desktop, &QPushButton::clicked, &dlg, [this, &dlg] {
        m_config.hatari.gemdosDir.clear();
        m_config.hatari.diskImage.clear();
        m_config.hatari.diskB.clear();
        m_config.hatari.memStateFile.clear();
        dlg.accept();
        if (m_launcher->isRunning() || m_rdb->isConnected())
            relaunch();
        else
            onStartClicked();
        statusBar()->showMessage(QStringLiteral("Booting to a clean desktop…"), 5000);
    });
    grid->addWidget(desktop, 3, 0, 1, 2);
    auto *closeBtn = new QPushButton(QStringLiteral("Close"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    grid->addWidget(closeBtn, 3, 3);

    dlg.exec();
}

void MainWindow::readMfp()
{
    if (!m_mfp || !m_rdb->isConnected())
        return;
    // The 68901 lives at $fffa01..$fffa2f (odd bytes); read from $fffa00 so the
    // register at $fffaNN is byte NN of the block.
    m_mfp->setBusy(true);
    m_rdb->sendCommand("mem fffa00 30", [this](const RdbClient::Tokens &r) {
        m_mfp->setBusy(false);
        if (r.size() < 4 || r.first() != "OK")
            return;
        const QByteArray block = MemCodec::decode(r.last(), 48);
        m_mfp->setState(Mfp::decode(block, m_region));
    });
}

void MainWindow::populateTimeline()
{
    m_timeline->setRowCount(m_writes.size());
    for (int i = 0; i < m_writes.size(); ++i) {
        const WriteEvent &w = m_writes[i];
        auto put = [&](int col, const QString &text, const QColor *bg = nullptr) {
            auto *item = new QTableWidgetItem(text);
            if (bg)
                item->setBackground(*bg);
            m_timeline->setItem(i, col, item);
        };
        put(0, QString::number(w.frameCycle));
        put(1, QString::number(w.scanline));
        put(2, QString::number(w.cycleInLine));
        put(3, QStringLiteral("$%1").arg(w.address, 0, 16));
        const QColor c = stColour(w.value);
        put(4, QStringLiteral("$%1").arg(w.value, 0, 16), &c);
        put(5, QStringLiteral("$%1").arg(w.pc, 0, 16));
    }
}

void MainWindow::updateBudget()
{
    if (!m_budget || !m_raster)
        return;
    ScanlineBudgetView::Model m;
    m.valid = true;
    m.cyclesPerLine = cyclesPerLine();                       // 512 PAL / 508 NTSC (sourced)
    m.dualSpeed = Machines::info(m_machine).dualSpeed;       // Mega STE -> 16 MHz doubling

    QSize fs = m_fb ? m_fb->imageSize() : QSize();
    if (fs.isEmpty())
        fs = QSize(832, 552);                               // default PAL low-res taken frame
    const BeamGeometry geo(m_region, fs);
    m.visibleStart = geo.cycleAtX(0);
    m.visibleEnd = geo.cycleAtX(fs.width());

    if (m_raster->mode() == RasterWorkspace::Bands) {
        // Each band boundary is a palette write; place it on the line via the
        // sourced column->cycle geometry (band.line holds a framebuffer column).
        const QVector<RasterCodegen::Bar> cols = m_raster->columnBars();
        int over = 0;
        for (const RasterCodegen::Bar &b : cols) {
            const int c = geo.cycleAtX(b.line);
            m.writeCycles.append(c);
            if (c > m.visibleEnd)
                ++over;
        }
        m.note = QStringLiteral("Bands: %1 palette writes/line (max %2)")
                     .arg(cols.size())
                     .arg(RasterCodegen::kMaxBands);
        if (over)
            m.note += QStringLiteral(" — %1 past the visible edge").arg(over);
        m.note += QStringLiteral(" · %1 cyc/line @ 8 MHz").arg(m.cyclesPerLine);
        if (m.dualSpeed)
            m.note += QStringLiteral(", %1 @ 16 MHz").arg(m.cyclesPerLine * 2);
    } else {
        // Raster bars: one palette write at each line's start; the codegen pads
        // the rest so every line is exactly kCycPerLine — cycle-locked, no overflow.
        m.writeCycles.append(m.visibleStart);
        m.note = QStringLiteral("Raster bars: 1 write/line, cycle-locked to %1 (codegen pad) · %2 bars")
                     .arg(RasterCodegen::kCycPerLine)
                     .arg(m_raster->bars().size());
        if (m.dualSpeed)
            m.note += QStringLiteral(" · Mega STE: double the per-line budget at 16 MHz");
    }
    m_budget->setModel(m);
}

void MainWindow::setControlsEnabledForCapture(bool capturing)
{
    const bool en = !capturing;
    m_actBreak->setEnabled(en);
    m_actRun->setEnabled(en);
    m_actStep->setEnabled(en);
    m_actRefresh->setEnabled(en);
    m_actLive->setEnabled(en);
    m_actRecord->setEnabled(en);
    m_actRunToLine->setEnabled(en);
    m_lineSpin->setEnabled(en);
    m_actCapture->setEnabled(en);
    m_actBlitCapture->setEnabled(en);
    m_actDmaCapture->setEnabled(en);
    m_actSaveState->setEnabled(en);
    m_regEdit->setEnabled(en);
    m_countSpin->setEnabled(en);
}

void MainWindow::setRunningControlsEnabled(bool connected)
{
    m_actBreak->setEnabled(connected);
    m_actRun->setEnabled(connected);
    m_actStep->setEnabled(connected);
    m_actRefresh->setEnabled(connected);
    m_actLive->setEnabled(connected);
    if (!connected && m_recording)
        m_actRecord->setChecked(false);   // disconnecting mid-record -> prompt to save
    m_actRecord->setEnabled(connected);
    m_actRunToLine->setEnabled(connected);
    m_lineSpin->setEnabled(connected);
    m_actCapture->setEnabled(connected);
    m_actBlitCapture->setEnabled(connected);
    m_actDmaCapture->setEnabled(connected);
    m_actSaveState->setEnabled(connected);   // F-217: snapshot needs a live machine
    m_regEdit->setEnabled(connected);
    m_countSpin->setEnabled(connected);
}
