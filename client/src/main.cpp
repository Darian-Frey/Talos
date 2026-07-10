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
    const QCommandLineOption optSelftest(
        "selftest",
        "Headless CI check: auto-start, step, save one taken frame to <png>, exit.",
        "png");
    parser.addOptions(
        {optHatari, optTos, optMachine, optHeadless, optAttach, optHost, optSelftest});
    parser.process(app);

    MainWindow::Config cfg;
    cfg.attachOnly = parser.isSet(optAttach);
    cfg.host = parser.value(optHost);
    cfg.hatari.hatariBinary = parser.value(optHatari);
    cfg.hatari.tosImage = parser.value(optTos);
    cfg.hatari.machine = parser.value(optMachine);
    cfg.hatari.headless = parser.isSet(optHeadless);

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

    return QApplication::exec();
}
