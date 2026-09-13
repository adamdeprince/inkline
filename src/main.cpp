#include "rmt/keyboard.hpp"
#include "rmt/pty.hpp"
#include "rmt/renderer.hpp"
#include "rmt/stream.hpp"
#include <QCommandLineParser>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QMouseEvent>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unistd.h>

namespace {
using Core = std::unique_ptr<RmtCore, decltype(&rmt_core_free)>;
constexpr int margin = 12, footer = 60;
Core create_core() {
    Core core(rmt_core_new(80, 24, nullptr), rmt_core_free);
    if (!core) throw std::runtime_error("Cannot allocate the terminal");
    GhosttyColorRgb foreground{0, 0, 0}, background{255, 255, 255};
    ghostty_terminal_set(rmt_core_terminal(core.get()), GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &foreground);
    ghostty_terminal_set(rmt_core_terminal(core.get()), GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &background);
    return core;
}
class TerminalItem final : public QQuickPaintedItem {
public:
    TerminalItem(QQuickItem *parent, int pixels, bool demo) : QQuickPaintedItem(parent),
        core_(create_core()), renderer_(*core_, pixels), keyboard_(*core_),
        stream_(*core_, [this](rmt::sixel::Bitmap &&b) { renderer_.sixel(std::move(b)); }), demo_(demo) {
        setOpaquePainting(true);
        setFillColor(Qt::white);
        setAcceptedMouseButtons(Qt::LeftButton);
        setFlag(ItemAcceptsInputMethod);
        stream_.set_control_handler([this](std::string_view control) {
            if (control == "\033c" || control == "\033[2J" || control == "\033[3J" ||
                control == "\x9b" "2J" || control == "\x9b" "3J") renderer_.clear_sixel();
        });
        auto terminal = rmt_core_terminal(core_.get());
        ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_USERDATA, this);
        ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY, reinterpret_cast<const void *>(reply));
        ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_SIZE, reinterpret_cast<const void *>(size_report));
        repaint_.setSingleShot(true);
        repaint_.setInterval(80); // At most 12.5 updates/sec; no idle cursor blinking.
        connect(&repaint_, &QTimer::timeout, this, [this] { guarded([this] { refresh(); }); });
        connect(&reap_, &QTimer::timeout, this, [this] {
            guarded([this] { if (pty_ && pty_->poll_exit()) QCoreApplication::quit(); });
        });
    }
    void start() {
        layout();
        stream_.write("Inkline " INKLINE_VERSION "\r\nCtrl+Shift+Q returns to notebooks. Shift+PageUp/Down scrolls history.\r\n\r\n");
        if (demo_) {
            stream_.write("\033[1mText, kitty graphics, and sixel\033[0m\r\n\r\n");
            stream_.write("\033_Ga=T,f=32,s=1,v=1,c=12,r=4,i=1;/wAA/w==\033\\\r\n");
            stream_.write("\033P0;1q\"1;1;160;60#0;2;0;0;0#0!160~-!160~-!160~-!160~-!160~-!160~-!160~-!160~-!160~-!160~\033\\\r\n");
            stream_.write("Graphics stay in RAM. Tap Quit to return to your notebooks.\r\n");
        } else {
            const char *shell = std::getenv("SHELL");
            if (!shell || shell[0] != '/' || access(shell, X_OK) != 0) shell = "/bin/sh";
            pty_ = std::make_unique<rmt::Pty>(std::vector<std::string>{shell, "-i"}, renderer_.cols(), renderer_.rows());
            pty_->resize(renderer_.cols(), renderer_.rows(), renderer_.cols() * renderer_.cell_width(), renderer_.rows() * renderer_.cell_height());
            reader_ = std::make_unique<QSocketNotifier>(pty_->fd(), QSocketNotifier::Read, this);
            writer_ = std::make_unique<QSocketNotifier>(pty_->fd(), QSocketNotifier::Write, this);
            writer_->setEnabled(false);
            connect(reader_.get(), &QSocketNotifier::activated, this, [this] { guarded([this] { read_output(); }); });
            connect(writer_.get(), &QSocketNotifier::activated, this, [this] { guarded([this] {
                pty_->flush(); writer_->setEnabled(pty_->pending_bytes() != 0);
            }); });
            reap_.start(250);
        }
        refresh();
        forceActiveFocus();
    }
    void layout() {
        renderer_.resize(int(width()) - 2 * margin, int(height()) - footer - 2 * margin);
        if (pty_) pty_->resize(renderer_.cols(), renderer_.rows(), renderer_.cols() * renderer_.cell_width(), renderer_.rows() * renderer_.cell_height());
        previous_ = {};
        schedule();
    }
    void paint(QPainter *p) override {
        p->fillRect(boundingRect(), Qt::white);
        p->drawImage(margin, margin, previous_);
        QFont font("Noto Sans"); font.setPixelSize(23); p->setFont(font);
        p->setPen(Qt::black);
        p->drawLine(0, int(height()) - footer, int(width()), int(height()) - footer);
        const QString labels[] = {"Esc", "Page up", "Page down", "Quit"};
        for (int i = 0; i < 4; ++i) {
            QRectF box(width() * i / 4, height() - footer, width() / 4, footer);
            p->drawText(box, Qt::AlignCenter, labels[i]);
            if (i) p->drawLine(box.topLeft(), box.bottomLeft());
        }
    }
    bool snapshot(const QString &path) { return previous_.save(path, "PNG"); }
protected:
    void keyPressEvent(QKeyEvent *event) override {
        guarded([&] {
            const auto mods = event->modifiers();
            if (event->key() == Qt::Key_Q && (mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) QCoreApplication::quit();
            else if (event->key() == Qt::Key_T && (mods & Qt::ControlModifier) && (mods & Qt::AltModifier)) { /* Reserved for the device launcher. */ }
            else if ((mods & Qt::ShiftModifier) && (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown)) scroll(event->key() == Qt::Key_PageUp ? -1 : 1);
            else {
                GhosttyTerminalScrollViewport bottom{}; bottom.tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM;
                ghostty_terminal_scroll_viewport(rmt_core_terminal(core_.get()), bottom);
                send(keyboard_.encode(*event));
                schedule();
            }
        });
        event->accept();
    }
    void keyReleaseEvent(QKeyEvent *event) override {
        // Qt produces artificial releases during repeat. Kitty expects repeats.
        if (!event->isAutoRepeat()) guarded([&] { send(keyboard_.encode(*event)); });
        event->accept();
    }
    void inputMethodEvent(QInputMethodEvent *event) override {
        guarded([&] { const auto utf8 = event->commitString().toUtf8(); send({utf8.data(), size_t(utf8.size())}); });
        event->accept();
    }
    void mousePressEvent(QMouseEvent *event) override {
        forceActiveFocus();
        if (event->position().y() >= height() - footer) {
            const int button = std::clamp(int(4 * event->position().x() / width()), 0, 3);
            if (button == 0) {
                QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                guarded([&] { send(keyboard_.encode(key)); });
            } else if (button == 3) QCoreApplication::quit();
            else scroll(button == 1 ? -1 : 1);
        }
        event->accept();
    }
private:
    Core core_;
    rmt::Renderer renderer_;
    rmt::Keyboard keyboard_;
    rmt::Stream stream_;
    bool demo_;
    std::unique_ptr<rmt::Pty> pty_;
    std::unique_ptr<QSocketNotifier> reader_, writer_;
    QTimer repaint_, reap_;
    QImage previous_;
    template<class F> void guarded(F function) {
        try { function(); }
        catch (const std::exception &e) { std::fprintf(stderr, "Inkline: %s\n", e.what()); QCoreApplication::exit(1); }
    }
    static void reply(GhosttyTerminal, void *ctx, const uint8_t *data, size_t length) {
        auto *self = static_cast<TerminalItem *>(ctx);
        self->guarded([&] { self->send({reinterpret_cast<const char *>(data), length}); });
    }
    static bool size_report(GhosttyTerminal, void *ctx, GhosttySizeReportSize *size) {
        auto &r = static_cast<TerminalItem *>(ctx)->renderer_;
        *size = {r.rows(), r.cols(), uint32_t(r.cell_width()), uint32_t(r.cell_height())};
        return true;
    }
    void send(std::string_view bytes) {
        if (!pty_ || bytes.empty()) return;
        if (!pty_->enqueue(bytes)) throw std::runtime_error("Shell input queue is full");
        writer_->setEnabled(pty_->pending_bytes() != 0);
    }
    void scroll(int direction) {
        GhosttyTerminalScrollViewport delta{}; delta.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
        delta.value.delta = direction * std::max(1, int(renderer_.rows()) - 2);
        ghostty_terminal_scroll_viewport(rmt_core_terminal(core_.get()), delta);
        schedule();
    }
    void read_output() {
        char buffer[16384]; size_t read = 0;
        // Yield to input and painting even when a remote program streams images.
        while (read < 256 * 1024) {
            const ssize_t count = ::read(pty_->fd(), buffer, sizeof(buffer));
            if (count > 0) { stream_.write({buffer, size_t(count)}); read += size_t(count); }
            else if (count < 0 && errno == EINTR) continue;
            else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            else { reader_->setEnabled(false); break; }
        }
        if (read) schedule();
    }
    void schedule() { if (!repaint_.isActive()) repaint_.start(); }
    void refresh() {
        const QImage next = renderer_.frame();
        if (next.size() != previous_.size()) { previous_ = next; update(); return; }
        int first = -1, last = -1;
        for (int y = 0; y < next.height(); ++y) {
            if (std::memcmp(next.constScanLine(y), previous_.constScanLine(y), size_t(next.width())) != 0) {
                if (first < 0) first = y;
                last = y;
            }
        }
        previous_ = next;
        if (first >= 0) update(QRect(margin, margin + first, next.width(), last - first + 1));
    }
};
}

int main(int argc, char **argv) {
    qputenv("QSG_RENDER_LOOP", "basic");
    QGuiApplication app(argc, argv);
    app.setApplicationName("Inkline");
    app.setApplicationVersion(INKLINE_VERSION);
    QCommandLineParser args;
    args.setApplicationDescription("Inkline: a terminal for reMarkable 2");
    args.addHelpOption(); args.addVersionOption();
    args.addOption({"rotate", "Screen rotation: 0, 90, or 270.", "degrees", "90"});
    args.addOption({"font-size", "Font size in pixels (16 to 48).", "pixels", "26"});
    args.addOption({"check", "Check the terminal and renderer, then exit without opening a window."});
    args.addOption({"demo", "Display the graphics demonstration without starting a shell."});
    args.addOption({"quit-after", "Exit automatically after 1 to 60 seconds for installation testing.", "seconds"});
    args.addOption({"snapshot", "Save the initial terminal frame as a PNG (explicit diagnostics only).", "path"});
    args.process(app);
    bool valid = false;
    const int rotation = args.value("rotate").toInt(&valid);
    if (!valid || (rotation != 0 && rotation != 90 && rotation != 270)) return 2;
    const int pixels = args.value("font-size").toInt(&valid);
    if (!valid || pixels < 16 || pixels > 48) return 2;
    int duration = 0;
    if (args.isSet("quit-after")) {
        duration = args.value("quit-after").toInt(&valid);
        if (!valid || duration < 1 || duration > 60) return 2;
    }
    QFontDatabase::addApplicationFont("/usr/share/fonts/ttf/noto/NotoMono-Regular.ttf");
    try {
        if (args.isSet("check")) {
            auto core = create_core();
            rmt::Renderer renderer(*core, pixels);
            renderer.resize(1404, 1000);
            rmt::Keyboard keyboard(*core);
            if (renderer.frame().isNull()) return 1;
            std::puts("Inkline " INKLINE_VERSION ": terminal, font, keyboard encoder and renderer ready.");
            return 0;
        }
        QQuickWindow window;
        window.setTitle("Inkline"); window.setColor(Qt::white);
        const bool tablet = QGuiApplication::platformName() == "epaper";
        const QSize size = tablet ? app.primaryScreen()->size() : QSize(1000, 750);
        window.resize(size);
        TerminalItem item(window.contentItem(), pixels, args.isSet("demo"));
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
        if (args.isSet("snapshot") && !item.snapshot(args.value("snapshot"))) return 1;
        if (duration) QTimer::singleShot(duration * 1000, &app, &QCoreApplication::quit);
        return app.exec();
    } catch (const std::exception &e) { std::fprintf(stderr, "Inkline: %s\n", e.what()); return 1; }
}
