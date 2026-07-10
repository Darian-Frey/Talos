#include "MainWindow.h"

#include "protocol/RdbClient.h"
#include "view/FramebufferView.h"

#include <QAction>
#include <QColor>
#include <QDir>
#include <QDockWidget>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>

namespace {
constexpr int kLiveIntervalMs = 250;   // ~4 Hz live refresh
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

void MainWindow::setRunningControlsEnabled(bool connected)
{
    m_actBreak->setEnabled(connected);
    m_actRun->setEnabled(connected);
    m_actStep->setEnabled(connected);
    m_actRefresh->setEnabled(connected);
    m_actLive->setEnabled(connected);
    m_actRunToLine->setEnabled(connected);
    m_lineSpin->setEnabled(connected);
}
