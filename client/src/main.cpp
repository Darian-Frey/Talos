// Talos — Qt6 client entry point.
//
// PRE-M0 SCAFFOLD. This opens a placeholder window only, to prove the Qt6
// toolchain and the CMake wiring. It contains no Talos behaviour yet.
//
// The M0 target (ROADMAP Phase 0 exit criterion) replaces this with a client
// that: connects to the Hatari fork over the remote socket, reads registers +
// VBL/HBL/cycle counters, issues run/stop/step, and displays Hatari's taken
// framebuffer (D-007). Build that up under the session/ protocol/ model/ view/
// directories rather than inflating this file.

#include <QApplication>
#include <QLabel>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Talos");
    QApplication::setApplicationVersion("0.0.0");

    QLabel window(QStringLiteral(
        "Talos — pre-M0 scaffold\n\n"
        "The client does not connect to Hatari yet.\n"
        "Next: session manager + remote-protocol client (M0)."));
    window.setAlignment(Qt::AlignCenter);
    window.setMinimumSize(480, 240);
    window.setWindowTitle(QStringLiteral("Talos"));
    window.show();

    return QApplication::exec();
}
