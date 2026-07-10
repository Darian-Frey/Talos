// Talos — Qt6 client entry point (M0).
//
// Connects to the patched Hatari fork over the remote-debug socket, reads
// registers + VBL/HBL/cycle counters, drives run/stop/step, and displays
// Hatari's taken framebuffer (D-007). See app/MainWindow.

#include "app/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QTimer>

namespace {

// Best-effort default for the fork binary and TOS, relative to the repo. Real
// deployments pass --hatari/--tos explicitly.
QString guessRepoRelative(const QString &rel)
{
    // Try CWD, then a couple of ancestors (running from build/ dirs).
    QDir dir(QDir::currentPath());
    for (int i = 0; i < 4; ++i) {
        const QString candidate = dir.filePath(rel);
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
        if (!dir.cdUp())
            break;
    }
    return rel;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Talos");
    QApplication::setApplicationVersion("0.0.0-M0");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Talos — Atari ST/STE hardware visualiser (drives a patched Hatari).");
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption optHatari(
        "hatari", "Path to the patched Hatari binary.", "path",
        qEnvironmentVariable("TALOS_HATARI",
                             guessRepoRelative("external/hatari/build/src/hatari")));
    const QCommandLineOption optTos(
        "tos", "Path to a TOS/EmuTOS ROM image.", "path",
        qEnvironmentVariable("TALOS_TOS", guessRepoRelative("tos/etos512uk.img")));
    const QCommandLineOption optMachine("machine", "Machine: st|ste|megast|megaste.",
                                        "type", "st");
    const QCommandLineOption optHeadless("headless",
                                         "Run Hatari off-screen (no Hatari window).");
    const QCommandLineOption optAttach(
        "attach", "Attach to an already-running Hatari instead of launching one.");
    const QCommandLineOption optHost("host", "Remote host to connect to.", "host",
                                     "127.0.0.1");
    const QCommandLineOption optEffect(
        "effect",
        "GEMDOS drive dir whose AUTO folder holds an effect to auto-run "
        "(e.g. tests/effects/disk).",
        "dir");
    const QCommandLineOption optSelftest(
        "selftest",
        "Headless CI check: auto-start, step, save one taken frame to <png>, exit.",
        "png");
    const QCommandLineOption optSelftestCapture(
        "selftest-capture",
        "Headless CI check: run the --effect, capture writes to $ffff8240, save the "
        "frame with write markers to <png>, exit.",
        "png");
    parser.addOptions({optHatari, optTos, optMachine, optHeadless, optAttach, optHost,
                       optEffect, optSelftest, optSelftestCapture});
    parser.process(app);

    MainWindow::Config cfg;
    cfg.attachOnly = parser.isSet(optAttach);
    cfg.host = parser.value(optHost);
    cfg.hatari.hatariBinary = parser.value(optHatari);
    cfg.hatari.tosImage = parser.value(optTos);
    cfg.hatari.machine = parser.value(optMachine);
    cfg.hatari.headless = parser.isSet(optHeadless);
    if (parser.isSet(optEffect))
        cfg.hatari.gemdosDir = QFileInfo(parser.value(optEffect)).absoluteFilePath();

    MainWindow window(cfg);
    window.show();

    if (parser.isSet(optSelftest)) {
        const QString outPng = parser.value(optSelftest);
        // Prefer a frame where the beam is inside the visible area (so the overlay
        // is drawn); the free-running beam is often in blanking. Fall back to
        // saving whatever we have after several frames so the test still ends.
        QObject::connect(&window, &MainWindow::frameReceived, &app,
                         [&app, outPng](const QImage &frame, bool beamVisible) {
                             static bool done = false;
                             static int frames = 0;
                             if (done)
                                 return;
                             ++frames;
                             if (!beamVisible && frames < 20)
                                 return; // wait for a visible-beam frame
                             done = true;
                             const bool ok = !frame.isNull() && frame.save(outPng);
                             qInfo().noquote()
                                 << "selftest: frame" << frame.width() << "x"
                                 << frame.height() << "beamVisible" << beamVisible
                                 << (ok ? "saved" : "SAVE FAILED") << outPng;
                             app.exit(ok ? 0 : 3);
                         });
        // Fail if nothing arrives in time.
        QTimer::singleShot(20000, &app, [&app]() {
            qWarning("selftest: timed out with no frame");
            app.exit(2);
        });
        QTimer::singleShot(0, &window, &MainWindow::startSession);
    }

    if (parser.isSet(optSelftestCapture)) {
        const QString outPng = parser.value(optSelftestCapture);
        auto *captured = new int(-1);
        QObject::connect(&window, &MainWindow::captureCompleted, &app,
                         [captured](int n) { *captured = n; });
        // The post-capture frame (with write markers) follows captureCompleted.
        QObject::connect(&window, &MainWindow::frameReceived, &app,
                         [&app, outPng, captured](const QImage &frame, bool) {
                             if (*captured < 0)
                                 return;   // capture not done yet
                             const bool ok = *captured > 0 && !frame.isNull()
                                             && frame.save(outPng);
                             qInfo().noquote() << "selftest-capture:" << *captured
                                               << "writes" << (ok ? "saved" : "FAILED")
                                               << outPng;
                             app.exit(ok ? 0 : 3);
                         });
        QTimer::singleShot(45000, &app, [&app]() {
            qWarning("selftest-capture: timed out");
            app.exit(2);
        });
        // Give EmuTOS time to boot and the AUTO effect to start writing (~14 s).
        QTimer::singleShot(18000, &window,
                           [&window]() { window.beginCapture(0xffff8240u, 48); });
        QTimer::singleShot(0, &window, &MainWindow::startSession);
    }

    return QApplication::exec();
}
