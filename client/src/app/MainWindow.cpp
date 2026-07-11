#include "MainWindow.h"

#include "capture/CaptureController.h"
#include "protocol/RdbClient.h"
#include "view/FramebufferView.h"

#include <QAction>
#include <QColor>
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
constexpr int kLiveIntervalMs = 250;   // ~4 Hz live refresh

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
    if (m_shotDir.isValid())
        m_shotPath = QDir(m_shotDir.path()).filePath("frame.png");

    buildUi();

    connect(m_rdb, &RdbClient::connected, this, &MainWindow::onConnected);
    connect(m_rdb, &RdbClient::connectionFailed, this, &MainWindow::onConnectionFailed);
    connect(m_rdb, &RdbClient::notification, this, &MainWindow::onNotification);
    connect(m_launcher, &HatariLauncher::failed, this, [this](const QString &r) {
        statusBar()->showMessage(QStringLiteral("Hatari launch failed: %1").arg(r), 8000);
    });

    m_capture = new CaptureController(m_rdb, this);
    connect(m_capture, &CaptureController::progress, this, &MainWindow::onCaptureProgress);
    connect(m_capture, &CaptureController::finished, this, &MainWindow::onCaptureFinished);

    m_liveTimer->setInterval(kLiveIntervalMs);
    connect(m_liveTimer, &QTimer::timeout, this, &MainWindow::refresh);

    setRunningControlsEnabled(false);
    statusBar()->showMessage(QStringLiteral("Ready. Press %1.")
                                 .arg(m_config.attachOnly ? "Connect" : "Launch"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Talos — M0"));
    resize(900, 640);

    auto *tb = addToolBar(QStringLiteral("Controls"));
    tb->setMovable(false);

    m_actStart = tb->addAction(m_config.attachOnly ? QStringLiteral("Connect")
                                                    : QStringLiteral("Launch"));
    connect(m_actStart, &QAction::triggered, this, &MainWindow::onStartClicked);
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

    m_fb = new FramebufferView(this);
    setCentralWidget(m_fb);

    m_regTable = new QTableWidget(0, 2, this);
    m_regTable->setHorizontalHeaderLabels({QStringLiteral("Reg"), QStringLiteral("Value")});
    m_regTable->horizontalHeader()->setStretchLastSection(true);
    m_regTable->verticalHeader()->setVisible(false);
    m_regTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_regTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_regTable->setMinimumWidth(220);

    auto *dock = new QDockWidget(QStringLiteral("Registers / counters"), this);
    dock->setWidget(m_regTable);
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

    auto *tdock = new QDockWidget(QStringLiteral("Register-write timeline"), this);
    tdock->setWidget(m_timeline);
    addDockWidget(Qt::BottomDockWidgetArea, tdock);

    m_captureLabel = new QLabel(QString(), this);
    statusBar()->addWidget(m_captureLabel);   // left, persistent (not a timed message)

    m_connLabel = new QLabel(QStringLiteral("disconnected"), this);
    m_posLabel = new QLabel(QString(), this);
    statusBar()->addPermanentWidget(m_posLabel);
    statusBar()->addPermanentWidget(m_connLabel);
}

void MainWindow::startSession()
{
    onStartClicked();
}

void MainWindow::onStartClicked()
{
    if (!m_config.attachOnly) {
        if (!m_launcher->launch(m_config.hatari))
            return;
        statusBar()->showMessage(QStringLiteral("Launching Hatari…"));
    }
    m_connLabel->setText(QStringLiteral("connecting…"));
    m_rdb->connectToHatari(m_config.host, m_config.hatari.remotePort);
}

void MainWindow::onConnected()
{
    setRunningControlsEnabled(true);
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
    // snapshot that matches the frame we then grab.
    refreshRegs();
}

void MainWindow::refreshRegs()
{
    m_rdb->reqRegs([this](const RdbClient::Tokens &tokens) {
        m_state = MachineState::fromRegsReply(tokens);
        updateRegisterPanel();
        updateStatusBar();
        refreshScreen();
    });
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
    m_regEdit->setEnabled(connected);
    m_countSpin->setEnabled(connected);
}
