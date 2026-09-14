#include "rmt/view.hpp"
#include <QCommandLineParser>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <cstdio>
#include <exception>

int main(int argc, char **argv) {
    qputenv("QSG_RENDER_LOOP", "basic");
    QGuiApplication app(argc, argv);
    app.setApplicationName("Inkline");
    app.setApplicationVersion(INKLINE_VERSION);
    QCommandLineParser args;
    args.setApplicationDescription("Inkline: a terminal for reMarkable 2");
    args.addHelpOption(); args.addVersionOption();
    args.addOption({"rotate", "Screen rotation: 0, 90, or 270.", "degrees", "90"});
    args.addOption({"font-size", "Override the saved font size (16 to 48 pixels).", "pixels"});
    args.addOption({"check", "Check the terminal and renderer, then exit without opening a window."});
    args.addOption({"settings", "Open the settings screen on startup."});
    args.addOption({"demo", "Display the graphics demonstration without starting a shell."});
    args.addOption({"quit-after", "Exit automatically after 1 to 60 seconds for installation testing.", "seconds"});
    args.addOption({"snapshot", "Save the initial terminal frame as a PNG (explicit diagnostics only).", "path"});
    args.process(app);
    bool valid = false;
    const int rotation = args.value("rotate").toInt(&valid);
    if (!valid || (rotation != 0 && rotation != 90 && rotation != 270)) return 2;
    int pixels = 0;
    if (args.isSet("font-size")) {
        pixels = args.value("font-size").toInt(&valid);
        if (!valid || pixels < 16 || pixels > 48) return 2;
    }
    int duration = 0;
    if (args.isSet("quit-after")) {
        duration = args.value("quit-after").toInt(&valid);
        if (!valid || duration < 1 || duration > 60) return 2;
    }
    QFontDatabase::addApplicationFont("/usr/share/fonts/ttf/noto/NotoMono-Regular.ttf");
    try {
        if (args.isSet("check")) {
            QQuickWindow check_window;
            rmt::TerminalView check(check_window.contentItem(), pixels, true);
            check.setSize(QSizeF(1404, 1030)); check.layout(); check.start();
            if (check.snapshot().isNull()) return 1;
            std::puts("Inkline " INKLINE_VERSION ": terminal, font, keyboard, settings and renderer ready.");
            return 0;
        }
        QQuickWindow window;
        window.setTitle("Inkline"); window.setColor(Qt::white);
        const bool tablet = QGuiApplication::platformName() == "epaper";
        const QSize size = tablet ? app.primaryScreen()->size() : QSize(1000, 750);
        window.resize(size);
        rmt::TerminalView item(window.contentItem(), pixels, args.isSet("demo"));
        auto layout = [&] {
            const int turn = tablet ? rotation : 0;
            item.setTransformOrigin(QQuickItem::TopLeft);
            item.setRotation(turn);
            item.setSize(turn ? QSizeF(window.height(), window.width()) : QSizeF(window.width(), window.height()));
            item.setPosition(turn == 90 ? QPointF(window.width(), 0) : turn == 270 ? QPointF(0, window.height()) : QPointF());
            item.layout();
        };
        layout();
        QObject::connect(&window, &QQuickWindow::widthChanged, &item, layout);
        QObject::connect(&window, &QQuickWindow::heightChanged, &item, layout);
        if (tablet) window.showFullScreen(); else window.show();
        item.start();
        if (args.isSet("settings")) item.settings();
        if (args.isSet("snapshot") && !item.snapshot().save(args.value("snapshot"), "PNG")) return 1;
        if (duration) QTimer::singleShot(duration * 1000, &app, &QCoreApplication::quit);
        return app.exec();
    } catch (const std::exception &e) { std::fprintf(stderr, "Inkline: %s\n", e.what()); return 1; }
}
