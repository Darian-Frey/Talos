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
#include "model/ScrollerCodegen.h"
#include "view/LedToolButton.h"
#include "view/CollapsibleDock.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
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
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>

namespace {
constexpr int kLiveIntervalMs = 70;    // ~14 Hz live refresh — each tick does a
                                       // coherent (fast-forward + VBL-synced) grab
                                       // so animated effects don't tear (see liveTick)
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
    if (m_shotDir.isValid())
        m_shotPath = QDir(m_shotDir.path()).filePath("frame.png");

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

    auto *tdock =
        new CollapsibleDock(QStringLiteral("Register-write timeline"), m_timeline, this);
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
    if (m_launcher->isRunning() || m_rdb->isConnected())
        relaunch();
}

void MainWindow::onRegionChanged(int index)
{
    m_region = (index == 1) ? VideoRegion::Ntsc60 : VideoRegion::Pal50;
    reconcileRegion();   // a language with no NTSC variant snaps back to PAL
    if (m_launcher->isRunning() || m_rdb->isConnected())
        relaunch();
}

void MainWindow::onLanguageChanged(int index)
{
    const auto langs = Languages::all();
    if (index < 0 || index >= langs.size())
        return;
    m_language = langs[index];
    reconcileRegion();
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
    });
}

bool MainWindow::updateBeamOverlay(QSize frameSize)
{
    const auto scanline = m_state.hbl();
    const auto cycle = m_state.lineCycles();
    BeamMarker mk;
    if (!scanline || !cycle || frameSize.isEmpty()) {
        m_fb->setBeam(mk);   // valid == false: nothing to draw
        return false;
    }

    const BeamGeometry geo(m_region, frameSize);
    const BeamMapping bm = geo.map(static_cast<int>(*scanline), static_cast<int>(*cycle));
    const QString pos = QStringLiteral("HBL %1 · cyc %2").arg(*scanline).arg(*cycle);

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
        // Beam above/below the rendered rows (e.g. after a break, at VBL).
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

void MainWindow::buildRasterEffect(const QVector<RasterCodegen::Bar> &bars)
{
    const bool bands = m_raster->mode() == RasterWorkspace::Bands;
    const QVector<RasterCodegen::Bar> colBars = m_raster->columnBars();
    if ((bands && colBars.isEmpty()) || (!bands && bars.isEmpty())) {
        m_raster->setResult(QStringLiteral("Add at least one %1 first.")
                                .arg(bands ? QStringLiteral("band") : QStringLiteral("bar")),
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
    f.write((bands ? RasterCodegen::generateColumns(colBars)
                   : RasterCodegen::generate(bars)).toUtf8());
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
    const bool bands = m_raster->mode() == RasterWorkspace::Bands;
    const QVector<RasterCodegen::Bar> colBars = m_raster->columnBars();
    if ((bands && colBars.isEmpty()) || (!bands && bars.isEmpty())) {
        m_raster->setResult(QStringLiteral("Add at least one %1 first.")
                                .arg(bands ? QStringLiteral("band") : QStringLiteral("bar")),
                            false);
        return;
    }
    if (bands && colBars.size() < 2) {
        m_raster->setResult(QStringLiteral("Bands verify needs 2+ bands (to check boundaries)."),
                            false);
        return;
    }
    const QString repo = repoRootFrom(m_config.hatari.hatariBinary);
    const QString tool = repo + (bands ? QStringLiteral("/harness/intraline_split.py")
                                       : QStringLiteral("/harness/raster_roundtrip.py"));
    if (!QFileInfo::exists(tool)) {
        m_raster->setResult(QStringLiteral("verify harness not found: %1").arg(tool), false);
        return;
    }
    // The harness launches its own Hatari on the fixed port, so free ours first.
    if (m_launcher->isRunning() || m_rdb->isConnected())
        doStop();

    QStringList args{tool, QStringLiteral("--hatari"), m_config.hatari.hatariBinary,
                     QStringLiteral("--tos"), m_config.hatari.tosImage};
    if (bands) {
        // Boundary columns (the leftmost band fills from the edge, so skip it).
        QStringList cols;
        for (int i = 1; i < colBars.size(); ++i)
            cols << QString::number(colBars[i].line);
        args << QStringLiteral("--cols") << cols.join(QLatin1Char(','));
    } else {
        for (const auto &b : bars)
            args << QStringLiteral("--bar")
                 << QStringLiteral("%1:%2").arg(b.line).arg(b.colour, 0, 16);
    }

    m_raster->setBusy(true);
    m_raster->setResult(QStringLiteral("Verifying on Hatari (headless, ~15 s)…"), true);
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [this, proc, bands](int code, QProcess::ExitStatus) {
                m_raster->setBusy(false);
                const QString out = QString::fromUtf8(proc->readAllStandardOutput());
                const bool ok = (code == 0) && out.contains(QStringLiteral("RESULT: PASS"));
                m_raster->setResult(
                    ok ? (bands ? QStringLiteral("Verified ✓ — band boundaries land at their columns")
                                : QStringLiteral("Verified ✓ — authored bars reproduced in stock Hatari"))
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
    if ((bands && colBars.isEmpty()) || (!bands && bars.isEmpty())) {
        m_raster->setResult(QStringLiteral("Add at least one %1 first.")
                                .arg(bands ? QStringLiteral("band") : QStringLiteral("bar")),
                            false);
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
    s.write((bands ? RasterCodegen::generateColumns(colBars)
                   : RasterCodegen::generate(bars)).toUtf8());
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
    f.write(ScrollerCodegen::generate(message, speed).toUtf8());
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
    sf.write(ScrollerCodegen::generate(message, speed).toUtf8());
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
    s.write(ScrollerCodegen::generate(message, speed).toUtf8());
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
    m_highlightRow = row;
    recomputeWriteMarks(m_fb->imageSize());
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

void MainWindow::setControlsEnabledForCapture(bool capturing)
{
    const bool en = !capturing;
    m_actBreak->setEnabled(en);
    m_actRun->setEnabled(en);
    m_actStep->setEnabled(en);
    m_actRefresh->setEnabled(en);
    m_actLive->setEnabled(en);
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
    m_actRunToLine->setEnabled(connected);
    m_lineSpin->setEnabled(connected);
    m_actCapture->setEnabled(connected);
    m_actBlitCapture->setEnabled(connected);
    m_actDmaCapture->setEnabled(connected);
    m_actSaveState->setEnabled(connected);   // F-217: snapshot needs a live machine
    m_regEdit->setEnabled(connected);
    m_countSpin->setEnabled(connected);
}
