#include "rmt/view.hpp"
#include "rmt/preferences.hpp"
#include <QCommandLineParser>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QFile>
#include <QHttpMultiPart>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstring>
#include <cstdio>
#include <exception>
#include <stdexcept>

int main(int argc, char **argv) {
    // The 64 KiB literal shortcut format can expand up to sixfold in JSON.
    constexpr qsizetype control_max_packet = 512 * 1024;
    if (argc >= 2 && std::strcmp(argv[1], "--import-manual") == 0) {
        if (argc != 3) { std::fputs("Usage: inkline --import-manual FILE.pdf\n", stderr); return 2; }
        QCoreApplication app(argc, argv);
        QFile pdf(QString::fromLocal8Bit(argv[2]));
        if (!pdf.open(QIODevice::ReadOnly)) return 1;
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        QNetworkRequest request(QUrl("http://10.11.99.1/upload"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        QHttpMultiPart form(QHttpMultiPart::FormDataType);
        QHttpPart file;
        file.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"file\"; filename=\"Inkline Manual.pdf\"");
        file.setHeader(QNetworkRequest::ContentTypeHeader, "application/pdf");
        file.setBodyDevice(&pdf); form.append(file);
        auto *reply = manager.post(request, &form);
        QEventLoop loop;
        QTimer timeout; timeout.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
        timeout.start(15000);
        if (!reply->isFinished()) loop.exec();
        timeout.stop();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        delete reply;
        return ok ? 0 : 1;
    }
    const auto control_path = qEnvironmentVariable("INKLINE_CONTROL_SOCKET", "/run/inkline/control");
    // Control clients need only Qt Core/Network; never connect to the display.
    if (argc >= 2 && (std::strcmp(argv[1], "--open-program") == 0 || std::strcmp(argv[1], "--redraw") == 0)) {
        QCoreApplication app(argc, argv);
        QJsonArray request; request.append(QString::fromLocal8Bit(argv[1]));
        for (int i = 2; i < argc; ++i) request.append(QString::fromLocal8Bit(argv[i]));
        QLocalSocket socket; socket.connectToServer(control_path);
        if (!socket.waitForConnected(3000)) { std::fputs("Inkline is not ready; quit and reopen after upgrading.\n", stderr); return 1; }
        const auto packet = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        if (packet.size() > control_max_packet) { std::fputs("Encoded program arguments exceed 512 KiB.\n", stderr); return 2; }
        socket.write(packet);
        if (socket.bytesToWrite() && !socket.waitForBytesWritten(3000)) return 1;
        if (!socket.bytesAvailable() && !socket.waitForReadyRead(5000)) return 1;
        QByteArray reply = socket.readAll();
        while (!reply.contains('\n') && socket.waitForReadyRead(1000)) reply += socket.readAll();
        if (reply != "OK\n") { std::fwrite(reply.constData(), 1, size_t(reply.size()), stderr); return 1; }
        return 0;
    }
    qputenv("QSG_RENDER_LOOP", "basic");
    QGuiApplication app(argc, argv);
    app.setApplicationName("Inkline");
    app.setApplicationVersion(INKLINE_VERSION);
    QCommandLineParser args;
    args.setApplicationDescription("Inkline: a terminal for reMarkable 2");
    args.addHelpOption(); args.addVersionOption();
    args.addOption({"rotate", "Screen rotation: 0, 90, or 270.", "degrees", "90"});
    args.addOption({"font-size", "Override the saved font size (6 to 48 pixels).", "pixels"});
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
        if (!valid || pixels < rmt::Preferences::MIN_FONT || pixels > rmt::Preferences::MAX_FONT) return 2;
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
        QLocalServer control;
        if (tablet || qEnvironmentVariableIsSet("INKLINE_CONTROL_SOCKET")) {
            control.setSocketOptions(QLocalServer::UserAccessOption);
            QLocalServer::removeServer(control_path);
            if (!control.listen(control_path)) throw std::runtime_error("Cannot open Inkline control socket");
            QObject::connect(&control, &QLocalServer::newConnection, &item, [&] {
                while (auto *socket = control.nextPendingConnection()) {
                    socket->setReadBufferSize(control_max_packet + 1);
                    QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                    QTimer::singleShot(5000, socket, &QLocalSocket::disconnectFromServer);
                    QObject::connect(socket, &QLocalSocket::readyRead, &item, [&, socket] {
                        if (socket->bytesAvailable() > control_max_packet) { socket->disconnectFromServer(); return; }
                        if (!socket->canReadLine()) return;
                        const auto request = QJsonDocument::fromJson(socket->readLine()).array();
                        bool ok = false;
                        if (request.size() == 1 && request[0] == "--redraw") {
                            try { item.redraw(); ok = true; } catch (...) { ok = false; }
                        }
                        // Up to 64 registered argv entries, plus the four
                        // literal Bash wrapper entries used by the launcher.
                        else if (request.size() >= 2 && request.size() <= 69 && request[0] == "--open-program") {
                            std::vector<std::string> command;
                            bool valid_command = true;
                            for (qsizetype i = 1; i < request.size(); ++i) {
                                if (!request[i].isString() || request[i].toString().contains(QChar(0))) { valid_command = false; break; }
                                command.push_back(request[i].toString().toStdString());
                            }
                            if (valid_command && !command.empty() && !command[0].empty())
                                try { ok = item.open_program(command); } catch (...) { ok = false; }
                        }
                        socket->write(ok ? "OK\n" : "Cannot open program; all nine terminals may be in use.\n");
                        socket->disconnectFromServer();
                    });
                }
            });
        }
        if (args.isSet("settings")) item.settings();
        if (args.isSet("snapshot") && !item.snapshot().save(args.value("snapshot"), "PNG")) return 1;
        if (duration) QTimer::singleShot(duration * 1000, &app, &QCoreApplication::quit);
        return app.exec();
    } catch (const std::exception &e) { std::fprintf(stderr, "Inkline: %s\n", e.what()); return 1; }
}
