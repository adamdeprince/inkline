#include "rmt/view.hpp"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QFile>
#include <cstdio>

// Run with the installed/staged profile and preview-test.el in environment
// variables. Qt's offscreen backend leaves the real tablet display untouched.
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid() || argc != 4) return 2;
    QQuickWindow window;
    window.resize(1404, 1030);
    std::vector<std::string> command{"/home/root/.local/bin/emacs", "-nw"};
    if (std::string(argv[1]) != "--installed")
        command.insert(command.end(), {"-Q", "-l", argv[1]});
    command.insert(command.end(), {"-l", argv[2]});
    rmt::TerminalView view(window.contentItem(), 20, false,
        temporary.filePath("settings.ini"), command);
    view.setSize(QSizeF(1404, 1030));
    view.layout(); view.start();
    QElapsedTimer timer; timer.start();
    QByteArray previous;
    while (timer.elapsed() < 180000) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
        const auto clipboard = view.clipboard_text();
        if (clipboard != previous) {
            previous = clipboard;
            if (clipboard.startsWith("INKLINE-TEX-PAGE-")) {
                view.snapshot().save(QString::fromUtf8(argv[3]) + "/" + clipboard + ".png");
                std::printf("%s\n", clipboard.constData()); std::fflush(stdout);
            }
            if (clipboard == "INKLINE-TEX-PASSED") {
                view.snapshot().save(QString::fromUtf8(argv[3]) + "/preview.png");
                view.send_text("\x18\x03"); // Normal Emacs exit runs RAM cleanup.
                for (int i = 0; i < 100; ++i) {
                    QCoreApplication::processEvents(); QThread::msleep(10);
                }
                std::puts("Real Inkline/Emacs preview and OSC 52 passed."); return 0;
            }
        }
        if (QFile::exists(QString::fromUtf8(argv[3]) + "/failed.txt")) break;
    }
    view.snapshot().save(QString::fromUtf8(argv[3]) + "/failed.png");
    std::fputs("Inkline/Emacs preview failed; inspect the RAM test directory.\n", stderr);
    return 1;
}
