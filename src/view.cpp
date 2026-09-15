#include "rmt/view.hpp"
#include "rmt/caps_leds.hpp"
#include "rmt/clipboard.hpp"
#include "rmt/input_method.hpp"
#include "rmt/input.hpp"
#include "rmt/keyboard.hpp"
#include "rmt/preferences.hpp"
#include "rmt/pty.hpp"
#include "rmt/renderer.hpp"
#include "rmt/stream.hpp"
#include <QCoreApplication>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QImageReader>
#include <QLibrary>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QTabletEvent>
#include <QLineF>
#include <cmath>
#include <QSocketNotifier>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <unistd.h>

namespace rmt {
namespace {
constexpr int margin = 12, footer = 60;
bool low_memory() {
#ifdef __linux__
    // procfs is RAM-backed. No cache or telemetry is written to the tablet.
    if (FILE *input = std::fopen("/proc/meminfo", "r")) {
        char line[256]; unsigned long long available = 0; bool low = false;
        while (std::fgets(line, sizeof(line), input)) {
            if (std::sscanf(line, "MemAvailable: %llu kB", &available) == 1) { low = available < 64 * 1024; break; }
        }
        std::fclose(input); return low;
    }
#endif
    return false;
}
using Core = std::unique_ptr<RmtCore, decltype(&rmt_core_free)>;
Core make_core() {
    auto options = rmt_core_defaults();
    options.memory_bytes = 64 * 1024 * 1024;
    options.image_bytes = 16 * 1024 * 1024;
    options.scrollback_bytes = 4 * 1024 * 1024;
    Core core(rmt_core_new(80, 24, &options), rmt_core_free);
    if (!core) throw std::runtime_error("Cannot allocate another terminal");
    GhosttyColorRgb foreground{0, 0, 0}, background{255, 255, 255};
    ghostty_terminal_set(rmt_core_terminal(core.get()), GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &foreground);
    ghostty_terminal_set(rmt_core_terminal(core.get()), GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &background);
    return core;
}
class Session final : public QObject {
public:
    Session(QObject *parent, int pixels, Clipboard &clipboard, std::function<void(bool)> changed,
            std::function<void(Session *)> exited, std::function<void(QString)> error)
        : QObject(parent), core_(make_core()), renderer_(*core_, pixels, 16 * 1024 * 1024),
          keyboard_(*core_), selection_(rmt_core_terminal(core_.get())), clipboard_(clipboard), stream_(*core_, [this](sixel::Bitmap &&b) { renderer_.sixel(std::move(b)); }),
          changed_(std::move(changed)), exited_(std::move(exited)), error_(std::move(error)) {
        stream_.set_control_handler([this](std::string_view c) {
            if (c == "\033c" || c == "\033[2J" || c == "\033[3J" || c == "\x9b" "2J" || c == "\x9b" "3J") renderer_.clear_sixel();
        });
        auto terminal = rmt_core_terminal(core_.get());
        ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_USERDATA, this);
        ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY, reinterpret_cast<const void *>(reply));
        ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_SIZE, reinterpret_cast<const void *>(size_report));
        ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_CLIPBOARD_WRITE, reinterpret_cast<const void *>(clipboard_write));
        ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_CLIPBOARD_READ, reinterpret_cast<const void *>(clipboard_read));
        connect(&reap_, &QTimer::timeout, this, [this] { guarded([this] {
            if (pty_ && pty_->poll_exit()) { reap_.stop(); exited_(this); }
        }); });
    }
    void start(int number, bool demo, const std::vector<std::string> &command, bool caps_control) {
        welcome(number, caps_control);
        if (demo) {
            stream_.write("\033[1mText, kitty graphics, and sixel\033[0m\r\n\r\n");
            stream_.write("\033_Ga=T,f=32,s=1,v=1,c=12,r=4,i=1;/wAA/w==\033\\\r\n");
            stream_.write("\033P0;1q\"1;1;160;60#0;2;0;0;0#0!160~-!160~-!160~-!160~-!160~-!160~-!160~-!160~-!160~-!160~\033\\\r\n");
            stream_.write("Graphics stay in RAM. Each terminal keeps its own shell and history.\r\n");
            return;
        }
        auto shell = command;
        if (shell.empty()) {
            const char *path = std::getenv("SHELL");
            if (!path || path[0] != '/' || access(path, X_OK) != 0) path = "/bin/sh";
            shell = {path, "-i"};
        }
        pty_ = std::make_unique<Pty>(shell, renderer_.cols(), renderer_.rows());
        resize_pty();
        reader_ = std::make_unique<QSocketNotifier>(pty_->fd(), QSocketNotifier::Read, this);
        writer_ = std::make_unique<QSocketNotifier>(pty_->fd(), QSocketNotifier::Write, this);
        writer_->setEnabled(false);
        connect(reader_.get(), &QSocketNotifier::activated, this, [this] { guarded([this] { read(); }); });
        connect(writer_.get(), &QSocketNotifier::activated, this, [this] { guarded([this] {
            pty_->flush(); writer_->setEnabled(pty_->pending_bytes() != 0);
        }); });
        reap_.start(250);
    }
    void welcome(int number, bool caps_control) {
        // Decode the installed asset once; scaled pixels and inline transport
        // stay in RAM. An implicit kitty ID avoids client image-ID collisions.
        static const QImage logo = [] {
            QImageReader reader(QCoreApplication::applicationDirPath() + "/assets/goblin.png");
            reader.setScaledSize(QSize(64, 64));
            return reader.read();
        }();
        int column = 1;
        if (!logo.isNull()) {
            const int side = std::min(64, 2 * renderer_.cell_height());
            const auto pixels = logo.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                   .convertToFormat(QImage::Format_RGBA8888);
            const auto bytes = QByteArray(reinterpret_cast<const char *>(pixels.constBits()), pixels.sizeInBytes()).toBase64();
            for (qsizetype offset = 0; offset < bytes.size(); offset += 4096) {
                const auto chunk = bytes.mid(offset, 4096);
                const bool more = offset + chunk.size() < bytes.size();
                const auto header = offset == 0
                    ? QString("\033_Ga=T,t=d,f=32,s=%1,v=%2,C=1,q=2,m=%3;").arg(pixels.width()).arg(pixels.height()).arg(int(more)).toLatin1()
                    : QString("\033_Gm=%1;").arg(int(more)).toLatin1();
                const auto data = header + chunk + "\033\\";
                stream_.write({data.constData(), size_t(data.size())});
            }
            column += (side + renderer_.cell_width() - 1) / renderer_.cell_width() + 1;
        }
        const auto banner = QString(
            "\033[%1G\033[1mInkline %2 | Terminal %3/6\033[0m\r\n"
            "\033[%1GOpt + 1-0: F1-F10\r\n"
            "Hold right Alt/Opt:\r\n"
            "  Tab: Esc   Up/Down: PgUp/PgDn\r\n"
            "  Left/Right: terminals   Space: Settings\r\n"
            "  Backspace: quit (confirm)   C/V: copy/paste\r\n"
            "Pinch: text size   Drag finger/pen: select\r\n"
            "US Folio: right Alt/Opt + 0: +; minus: =; Shift+6: ^.\r\n"
            "Two fingers: up/down scroll, sideways switch terminals.\r\n"
            "Scrollback: 500 lines, kept in RAM.\r\n"
            "Settings also selects the input method.\r\n"
            "Ctrl+Shift+B: bottom bar   Caps Lock: %4\r\n"
            "exit closes this terminal.\r\n\r\n")
            .arg(column).arg(QCoreApplication::applicationVersion()).arg(number)
            .arg(caps_control ? "Control" : "Caps Lock").toUtf8();
        stream_.write({banner.constData(), size_t(banner.size())});
    }
    void layout(int width, int height) { renderer_.resize(width, height); resize_pty(); }
    QRect render() { return renderer_.render(); }
    const QImage &frame() const { return renderer_.image(); }
    void maintain(bool pressure) { renderer_.reclaim(pressure); }
    InputMethod ime;
    void font_size(int pixels) { renderer_.set_font_size(pixels); }
    void text_darkness(int value) { renderer_.set_text_darkness(value); }
    void minimum_contrast(int value) { renderer_.set_minimum_contrast(value); }
    void key(const MappedInput &input) {
        const auto composed = ime.key(input);
        if (!composed.commit.isEmpty()) text_input(composed.commit);
        if (!composed.consumed) {
            auto event = input.event();
            const auto bytes = keyboard_.encode(event, input.caps_locked);
            const bool modifier = input.key == Qt::Key_Alt || input.key == Qt::Key_AltGr ||
                input.key == Qt::Key_Control || input.key == Qt::Key_Shift || input.key == Qt::Key_Meta;
            if (input.type == QEvent::KeyPress && !modifier && !bytes.empty()) {
                bottom(); interactive_response_ = true;
            }
            send(bytes);
        }
    }
    void bottom() {
        selection_.clear();
        GhosttyTerminalScrollViewport bottom{}; bottom.tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM;
        ghostty_terminal_scroll_viewport(rmt_core_terminal(core_.get()), bottom);
    }
    void text_input(const QByteArray &text) {
        if (text.isEmpty()) return;
        bottom(); interactive_response_ = true; send({text.constData(), size_t(text.size())}); changed_(true);
    }
    void select(const QPointF &point, bool begin) {
        const auto x = uint16_t(std::clamp(int(point.x()) / renderer_.cell_width(), 0, int(renderer_.cols()) - 1));
        const auto y = uint16_t(std::clamp(int(point.y()) / renderer_.cell_height(), 0, int(renderer_.rows()) - 1));
        if (begin) selection_.begin(x, y); else selection_.extend(x, y);
        changed_(true);
    }
    void selection_end(bool clear = false) { if (clear) selection_.clear(); else selection_.release(); changed_(true); }
    bool copy() {
        const auto text = selection_.text();
        if (!text) return false;
        if (!clipboard_.set(*text)) throw std::runtime_error("Cannot copy text to clipboard");
        return true;
    }
    void paste() {
        if (!pty_ || clipboard_.text().isEmpty()) return;
        if (pty_->pending_bytes() + size_t(clipboard_.text().size()) + 1024 > Pty::MAX_PENDING)
            throw std::runtime_error("Shell input queue is full; try pasting again shortly");
        ime.reset(); bottom(); interactive_response_ = true;
        if (clipboard_.paste(rmt_core_terminal(core_.get())) != GHOSTTY_SUCCESS)
            throw std::runtime_error("Cannot paste clipboard");
        changed_(true);
    }
    void send(std::string_view bytes) {
        if (!pty_ || bytes.empty()) return;
        if (!pty_->enqueue(bytes)) throw std::runtime_error("Shell input queue is full");
        writer_->setEnabled(pty_->pending_bytes() != 0);
    }
    void scroll(int direction) {
        scroll_lines(direction * std::max(1, int(renderer_.rows()) - 2));
    }
    int cell_height() const { return renderer_.cell_height(); }
    void scroll_lines(int lines) {
        GhosttyTerminalScrollViewport delta{}; delta.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
        delta.value.delta = lines;
        ghostty_terminal_scroll_viewport(rmt_core_terminal(core_.get()), delta);
        changed_(true);
    }
private:
    Core core_;
    Renderer renderer_;
    Keyboard keyboard_;
    Selection selection_;
    Clipboard &clipboard_;
    Stream stream_;
    std::unique_ptr<Pty> pty_;
    std::unique_ptr<QSocketNotifier> reader_, writer_;
    QTimer reap_;
    std::function<void(bool)> changed_;
    std::function<void(Session *)> exited_;
    std::function<void(QString)> error_;
    bool interactive_response_ = false;
    template<class F> void guarded(F work) { try { work(); } catch (const std::exception &e) { error_(QString::fromUtf8(e.what())); } }
    void resize_pty() {
        if (pty_) pty_->resize(renderer_.cols(), renderer_.rows(), renderer_.cols() * renderer_.cell_width(), renderer_.rows() * renderer_.cell_height());
    }
    void read() {
        char bytes[16384]; size_t received = 0;
        while (received < 256 * 1024) {
            const auto count = ::read(pty_->fd(), bytes, sizeof(bytes));
            if (count > 0) { stream_.write({bytes, size_t(count)}); received += size_t(count); }
            else if (count < 0 && errno == EINTR) continue;
            else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            else { reader_->setEnabled(false); break; }
        }
        if (received) {
            maintain(false);
            const bool urgent = interactive_response_;
            interactive_response_ = false;
            changed_(urgent);
        }
    }
    static void reply(GhosttyTerminal, void *context, const uint8_t *bytes, size_t count) {
        auto *self = static_cast<Session *>(context);
        self->guarded([&] { self->send({reinterpret_cast<const char *>(bytes), count}); });
    }
    static void clipboard_write(GhosttyTerminal, void *context, const GhosttyClipboardWrite *request) {
        static_cast<Session *>(context)->clipboard_.write(request);
    }
    static void clipboard_read(GhosttyTerminal, void *context, const GhosttyClipboardRead *request) {
        auto *self = static_cast<Session *>(context);
        const bool ready = !self->pty_ || self->pty_->pending_bytes() + 2 * Clipboard::MAX_BYTES * 4 / 3 + 4096 <= Pty::MAX_PENDING;
        self->clipboard_.read(request, ready);
    }
    static bool size_report(GhosttyTerminal, void *context, GhosttySizeReportSize *size) {
        auto &r = static_cast<Session *>(context)->renderer_;
        *size = {r.rows(), r.cols(), uint32_t(r.cell_width()), uint32_t(r.cell_height())};
        return true;
    }
};
void font(QPainter &p, int pixels, bool bold = false) {
    QFont value("Noto Sans"); value.setPixelSize(pixels); value.setBold(bold); p.setFont(value);
}

struct UpdatePolicy {
    const char *name;
    const char *detail;
    int screen_mode;
    int interactive_ms;
    int output_ms;
};
constexpr std::array<UpdatePolicy, 5> update_policies{{
    {"Fast", "fast grayscale · least delay · more ghosting", 2, 0, 16},
    {"Balanced", "clear UI waveform · 32 ms output batching", 3, 4, 32},
    {"Crisp", "content waveform · cleanest grayscale · slower", 4, 12, 60},
    {"Mono", "black and white only · fast text · no image grays", 1, 0, 24},
    {"Saver", "clear UI waveform · fewer updates · 120 ms batching", 3, 40, 120},
}};
constexpr int default_update_policy = 0;

// Firmware 3.27's scene-graph plugin exports EPScreenModeItem but does not
// install a public header for it. Resolve the two stable Qt meta-object
// symbols at runtime so host builds remain portable and newer firmware can
// fall back to Qt's normal UI mode if the private item ever disappears.
class EpaperModeControl {
public:
    explicit EpaperModeControl(QQuickItem &parent) {
        if (QGuiApplication::platformName() != "epaper") return;
        library_.setFileName("/usr/lib/plugins/scenegraph/libqsgepaper.so");
        if (!library_.load()) return;
        const auto construct = reinterpret_cast<Construct>(library_.resolve("_ZN16EPScreenModeItemC1EP10QQuickItem"));
        set_mode_ = reinterpret_cast<SetMode>(library_.resolve("_ZN16EPScreenModeItem7setModeENS_4ModeE"));
        if (!construct || !set_mode_) { library_.unload(); set_mode_ = nullptr; return; }
        void *storage = ::operator new(64);
        try { construct(storage, &parent); }
        catch (...) { ::operator delete(storage); library_.unload(); set_mode_ = nullptr; throw; }
        item_ = static_cast<QQuickItem *>(storage);
        item_->setObjectName("inkline-epaper-mode");
        item_->setZ(100000);
    }
    ~EpaperModeControl() {
        delete item_;
        if (library_.isLoaded()) library_.unload();
    }
    EpaperModeControl(const EpaperModeControl &) = delete;
    EpaperModeControl &operator=(const EpaperModeControl &) = delete;
    void resize(const QSizeF &size) { if (item_) item_->setSize(size); }
    void set_mode(int mode) { if (item_) set_mode_(item_, mode); }
private:
    using Construct = void (*)(void *, QQuickItem *);
    using SetMode = void (*)(void *, int);
    QLibrary library_;
    QQuickItem *item_ = nullptr;
    SetMode set_mode_ = nullptr;
};
}

class TerminalView::Private {
public:
    enum class Overlay { None, Settings, Methods, Quit, Error };
    TerminalView &view;
    EpaperModeControl epaper;
    Preferences prefs;
    InputMapper input;
    CapsLeds leds;
    Clipboard clipboard;
    std::array<std::unique_ptr<Session>, TERMINALS> sessions;
    int active = 0, pixels, selected = 0, darkness = Preferences::DEFAULT_DARKNESS;
    int contrast = Preferences::DEFAULT_MINIMUM_CONTRAST;
    int update_policy = default_update_policy;
    bool demo;
    std::vector<std::string> shell;
    QTimer repaint, toast, font_save, autoscroll, maintenance;
    bool font_dirty = false;
    int slider_drag = -1, press_slider = 0;
    QString notice;
    bool pointer_down = false, dragging = false, text_drag = false, pen_down = false;
    QPointF press_point, last_point;
    Overlay press_overlay = Overlay::None;
    enum class Gesture { None, Pending, Pinch, Scroll, Swipe };
    Gesture gesture = Gesture::None;
    int touch_id = -1, pinch_a = -1, pinch_b = -1, pinch_pixels = 26;
    qreal pinch_distance = 0;
    QPointF gesture_start, gesture_last;
    qreal scroll_remainder = 0;
    bool can_scroll = false;
    Overlay overlay = Overlay::None;
    QString error;
    bool show_terminal = false;
    Private(TerminalView &v, int p, bool demonstration, const QString &path, std::vector<std::string> command)
        : view(v), epaper(v), prefs(path), pixels(p > 0 ? std::clamp(p, Preferences::MIN_FONT, Preferences::MAX_FONT) : prefs.font_pixels()), demo(demonstration), shell(std::move(command)) {
        input.set_caps_control(prefs.caps_control());
        repaint.setSingleShot(true);
        QObject::connect(&repaint, &QTimer::timeout, &view, [this] { guarded([this] { refresh(); }); });
        epaper.set_mode(update_policies[update_policy].screen_mode);
        toast.setSingleShot(true); toast.setInterval(1400);
        QObject::connect(&toast, &QTimer::timeout, &view, [this] { show_terminal = false; notice.clear(); view.update(); });
        font_save.setSingleShot(true); font_save.setInterval(700);
        QObject::connect(&font_save, &QTimer::timeout, &view, [this] { guarded([this] { save_font(); }); });
        autoscroll.setInterval(160);
        maintenance.setInterval(1000);
        QObject::connect(&maintenance, &QTimer::timeout, &view, [this] { guarded([this] {
            const bool pressure = low_memory();
            for (auto &session : sessions) if (session) session->maintain(pressure);
            if (pressure) schedule();
        }); });
        maintenance.start();
        QObject::connect(&autoscroll, &QTimer::timeout, &view, [this] { guarded([this] {
            if (!pointer_down || !text_drag || !dragging || !sessions[active]) return;
            const auto area = text_area();
            if (last_point.y() < area.top() || last_point.y() > area.bottom()) {
                sessions[active]->scroll(last_point.y() < area.top() ? -1 : 1);
                sessions[active]->select(last_point - QPointF(margin, margin), false);
            }
        }); });
    }
    template<class F> void guarded(F work) {
        try { work(); } catch (const std::exception &e) { error = QString::fromUtf8(e.what()); overlay = Overlay::Error; view.update(); }
    }
    int count() const { return int(std::count_if(sessions.begin(), sessions.end(), [](const auto &s) { return bool(s); })); }
    ~Private() { try { if (font_dirty) prefs.set_font_pixels(pixels); } catch (...) {} }
    int ime_height() const { return prefs.input_method() == InputMethod::Off ? 0 : 104; }
    QRectF text_area() const { return {margin, margin, view.width() - 2 * margin, view.height() - 2 * margin - footer_height() - ime_height()}; }
    void save_font() { if (font_dirty) { prefs.set_font_pixels(pixels); font_dirty = false; } }
    void zoom(int size, bool defer = true) {
        size = std::clamp(size, Preferences::MIN_FONT, Preferences::MAX_FONT);
        if (pixels == size) return;
        pixels = size; font_dirty = true;
        for (auto &session : sessions) if (session) session->font_size(pixels);
        layout();
        if (defer) font_save.start();
    }
    void set_darkness(int value) {
        value = std::clamp(value, 0, 100);
        if (darkness == value) return;
        darkness = value;
        for (auto &session : sessions) if (session) session->text_darkness(value);
        schedule(); view.update();
    }
    void slide_darkness(const QPointF &point) {
        const auto track = slider_track(5);
        set_darkness(int(std::lround(100 * (point.x() - track.left()) / track.width())));
    }
    void set_contrast(int value) {
        value = std::clamp(value, 0, 100);
        if (contrast == value) return;
        contrast = value;
        for (auto &session : sessions) if (session) session->minimum_contrast(value);
        schedule(); view.update();
    }
    void slide_contrast(const QPointF &point) {
        const auto track = slider_track(6);
        set_contrast(int(std::lround(100 * (point.x() - track.left()) / track.width())));
    }
    void set_update_policy(int value) {
        value = (value + int(update_policies.size())) % int(update_policies.size());
        if (update_policy == value) return;
        update_policy = value;
        epaper.set_mode(update_policies[update_policy].screen_mode);
        if (repaint.isActive()) { repaint.stop(); schedule(); }
        view.update();
    }
    void tell(const QString &message) { notice = message; toast.start(); view.update(); }
    int footer_height() const { return prefs.bottom_bar() ? footer : 0; }
    void schedule(bool urgent = false) {
        const auto &policy = update_policies[update_policy];
        const int delay = urgent ? policy.interactive_ms : policy.output_ms;
        if (!repaint.isActive() || repaint.remainingTime() > delay) repaint.start(delay);
    }
    void refresh() {
        if (!sessions[active]) return;
        const QRect dirty = sessions[active]->render();
        if (!dirty.isEmpty()) view.update(dirty.translated(margin, margin));
    }
    void layout() {
        for (auto &s : sessions) if (s) s->layout(int(view.width()) - 2 * margin, int(view.height()) - 2 * margin - footer_height() - ime_height());
        epaper.resize(view.size());
        schedule(); view.update();
    }
    void choose(int index) {
        index = (index + TERMINALS) % TERMINALS;
        if (!sessions[index]) {
            auto next = std::make_unique<Session>(&view, pixels, clipboard,
                [this, index](bool urgent) { if (active == index) schedule(urgent); },
                [this, index](Session *session) { QTimer::singleShot(0, &view, [this, index, session] {
                    if (sessions[index].get() != session) return;
                    sessions[index].reset();
                    if (!count()) { QCoreApplication::quit(); return; }
                    if (active == index) {
                        for (int step = 1; step < TERMINALS; ++step) {
                            const int next = (index + step) % TERMINALS;
                            if (sessions[next]) { choose(next); break; }
                        }
                    }
                    view.update();
                }); },
                [this](const QString &message) { error = message; overlay = Overlay::Error; view.update(); });
            next->layout(int(view.width()) - 2 * margin, int(view.height()) - 2 * margin - footer_height() - ime_height());
            next->ime.set_method(prefs.input_method());
            next->text_darkness(darkness);
            next->minimum_contrast(contrast);
            next->start(index + 1, demo, shell, prefs.caps_control());
            sessions[index] = std::move(next);
        }
        cancel_pointer();
        active = index; overlay = Overlay::None;
        show_terminal = true; toast.start(); refresh(); view.update();
    }
    void settings() { cancel_pointer(); overlay = overlay == Overlay::Settings || overlay == Overlay::Methods ? Overlay::None : Overlay::Settings; selected = 0; view.update(); }
    void quit() { cancel_pointer(); overlay = Overlay::Quit; selected = 0; view.update(); }
    void set_bar(bool visible) { prefs.set_bottom_bar(visible); layout(); }
    void toggle_bar() { set_bar(!prefs.bottom_bar()); }
    void set_caps(bool control) { prefs.set_caps_control(control); input.set_caps_control(control); leds.set_locked(input.caps_locked()); view.update(); }
    void set_method(int method) {
        // Validate/load before changing any saved preference.
        InputMethod probe; probe.set_method(method);
        prefs.set_input_method(method);
        for (auto &session : sessions) if (session) session->ime.set_method(method);
        overlay = Overlay::Settings; selected = 3; layout();
    }
    void activate() {
        if (overlay == Overlay::Settings) {
            if (selected == 0) set_caps(!prefs.caps_control());
            else if (selected == 1) toggle_bar();
            else if (selected == 3) { overlay = Overlay::Methods; selected = prefs.input_method(); }
            else if (selected == 4) set_update_policy(update_policy + 1);
            else if (selected == 7) overlay = Overlay::None;
        } else if (overlay == Overlay::Methods) {
            if (selected < InputMethod::COUNT) set_method(selected);
            else { overlay = Overlay::Settings; selected = 3; }
        } else if (overlay == Overlay::Quit) {
            if (selected == 1) QCoreApplication::quit();
            else overlay = Overlay::None;
        } else overlay = Overlay::None;
        view.update();
    }
    void dialog_key(const MappedInput &event) {
        if (event.type != QEvent::KeyPress) return;
        const int key = event.key;
        if (key == Qt::Key_Escape) { overlay = overlay == Overlay::Methods ? Overlay::Settings : Overlay::None; selected = 0; view.update(); return; }
        if (overlay == Overlay::Quit && key == Qt::Key_N) { overlay = Overlay::None; view.update(); return; }
        if (overlay == Overlay::Quit && key == Qt::Key_Y) { selected = 1; activate(); return; }
        if (overlay == Overlay::Settings && (key == Qt::Key_Left || key == Qt::Key_Right)) {
            if (selected == 0) set_caps(key == Qt::Key_Left);
            else if (selected == 1) set_bar(key == Qt::Key_Left);
            else if (selected == 2) zoom(pixels + (key == Qt::Key_Left ? -2 : 2));
            else if (selected == 4) set_update_policy(update_policy + (key == Qt::Key_Left ? -1 : 1));
            else if (selected == 5) set_darkness(darkness + (key == Qt::Key_Left ? -5 : 5));
            else if (selected == 6) set_contrast(contrast + (key == Qt::Key_Left ? -5 : 5));
            return;
        }
        if (overlay == Overlay::Settings && (selected == 5 || selected == 6) &&
            (key == Qt::Key_Home || key == Qt::Key_End)) {
            if (selected == 5) set_darkness(key == Qt::Key_Home ? 0 : 100);
            else set_contrast(key == Qt::Key_Home ? 0 : 100);
            return;
        }
        const int choices = overlay == Overlay::Settings ? 8 : overlay == Overlay::Methods ? InputMethod::COUNT + 1 : overlay == Overlay::Quit ? 2 : 1;
        if (key == Qt::Key_Tab || key == Qt::Key_Down || key == Qt::Key_Right) selected = (selected + 1) % choices;
        else if (key == Qt::Key_Backtab || key == Qt::Key_Up || key == Qt::Key_Left) selected = (selected + choices - 1) % choices;
        else if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) && !event.repeat) activate();
        view.update();
    }
    void key(const QKeyEvent &event) {
        const auto mapped = input.map(event, overlay == Overlay::None ? active : -1);
        if (event.key() == Qt::Key_CapsLock || event.nativeScanCode() == 66) leds.set_locked(input.caps_locked());
        switch (mapped.action) {
        case InputAction::Copy:
            if (sessions[active] && overlay == Overlay::None) tell(sessions[active]->copy() ? "Copied" : "Drag across text to select it first");
            break;
        case InputAction::Paste:
            if (sessions[active] && overlay == Overlay::None) { sessions[active]->paste(); schedule(true); view.update(); }
            break;
        case InputAction::Cut:
            if (sessions[active] && overlay == Overlay::None) {
                const bool copied = sessions[active]->copy();
                error = copied ? "Copied the selection. Terminal output is read-only; use the running editor's Cut command to delete text. OSC 52 shares the clipboard, but cannot delete an editor's text."
                               : "Use the running editor's Cut command. OSC 52 can copy its selection into Inkline's clipboard.";
                overlay = Overlay::Error; view.update();
            }
            break;
        case InputAction::Settings: settings(); break;
        case InputAction::Quit: quit(); break;
        case InputAction::Previous: choose(active + TERMINALS - 1); break;
        case InputAction::Next: choose(active + 1); break;
        case InputAction::ToggleBar: toggle_bar(); break;
        case InputAction::HistoryUp: if (sessions[active] && overlay == Overlay::None) sessions[active]->scroll(-1); break;
        case InputAction::HistoryDown: if (sessions[active] && overlay == Overlay::None) sessions[active]->scroll(1); break;
        case InputAction::Send:
            if (mapped.terminal < 0) dialog_key(mapped);
            else if (sessions[mapped.terminal]) { sessions[mapped.terminal]->key(mapped); if (mapped.terminal == active) { schedule(true); if (ime_height()) view.update(); } }
            break;
        case InputAction::Ignore: break;
        }
    }
    QRectF panel() const {
        const qreal w = std::min<qreal>(840, view.width() - 48);
        const qreal h = std::min<qreal>(overlay == Overlay::Settings ? 940 : overlay == Overlay::Methods ? 650 : 360, view.height() - 48);
        return {(view.width() - w) / 2, (view.height() - h) / 2, w, h};
    }
    qreal row_step() const { return std::clamp((panel().height() - 150) / 7, qreal(68), qreal(92)); }
    QRectF row(int index) const { const auto p = panel(); const auto step = row_step(); return {p.x() + 22, p.y() + 74 + index * step, p.width() - 44, step - 2}; }
    QRectF slider_track(int index) const { const auto r = row(index); return {r.left() + 100, r.y() + r.height() - 25, r.width() - 200, 4}; }
    QRectF choice(int group, int index) const { const auto r = row(group); const qreal w = (r.width() - 18) / 2; return {r.x() + index * (w + 18), r.y() + 28, w, r.height() - 32}; }
    QRectF method_row(int index) const { const auto p = panel(); return {p.x() + 22, p.y() + 78 + index * 64, p.width() - 44, 54}; }
    QRectF font_button(int index) const { const auto r = row(2); return {index ? r.right() - 90 : r.left(), r.y() + 28, 90, r.height() - 32}; }
    QRectF update_choice(int index) const {
        const auto r = row(4); const qreal gap = 8;
        const qreal w = (r.width() - (update_policies.size() - 1) * gap) / update_policies.size();
        return {r.x() + index * (w + gap), r.y() + 28, w, r.height() - 32};
    }
    QRectF close_button() const { const auto p = panel(); return {p.right() - 160, p.bottom() - 70, 138, 48}; }
    QRectF confirm_button(int index) const {
        const auto p = panel(); const qreal w = (p.width() - 66) / 2;
        return {p.x() + 22 + index * (w + 22), p.bottom() - 78, w, 54};
    }
    void focus(QPainter &p, const QRectF &box) {
        p.save(); p.setPen(QPen(Qt::black, 2, Qt::DashLine)); p.setBrush(Qt::NoBrush); p.drawRect(box.adjusted(-5, -5, 5, 5)); p.restore();
    }
    void button(QPainter &p, const QRectF &box, const QString &text, bool focused, bool chosen = false) {
        p.fillRect(box, chosen ? Qt::black : Qt::white); p.setPen(Qt::black); p.drawRect(box);
        p.setPen(chosen ? Qt::white : Qt::black); font(p, 26, chosen);
        p.drawText(box.adjusted(12, 0, -12, 0), Qt::AlignCenter, text); p.setPen(Qt::black);
        if (focused) focus(p, box);
    }
    QRectF ime_area() const { return {0, view.height() - footer_height() - ime_height(), view.width(), qreal(ime_height())}; }
    QRectF candidate_box(int index) const { const auto area = ime_area(); const qreal w = area.width() / 9; return {index * w, area.y() + 48, w, 54}; }
    void paint_ime(QPainter &p) {
        if (!ime_height() || !sessions[active]) return;
        const auto &ime = sessions[active]->ime;
        const auto area = ime_area(); p.fillRect(area, Qt::white); p.setPen(Qt::black); p.drawLine(area.topLeft(), area.topRight());
        font(p, 24, true);
        p.drawText(area.adjusted(12, 3, -12, -area.height() + 42), Qt::AlignLeft | Qt::AlignVCenter,
            QString("%1  %2").arg(InputMethod::name(ime.method()), ime.preedit()));
        font(p, 17);
        p.drawText(area.adjusted(12, 3, -12, -area.height() + 42), Qt::AlignRight | Qt::AlignVCenter,
                   ime.pending() ? QString("Space / Enter: choose    [ ]: page %1    Esc: cancel").arg(ime.page() + 1) : "Option+Space: input method");
        const auto candidates = ime.candidates();
        font(p, 23);
        for (int i = 0; i < candidates.size(); ++i) {
            const auto box = candidate_box(i); p.drawRect(box);
            const auto label = QString("%1 %2").arg(i + 1).arg(candidates[i]);
            p.drawText(box.adjusted(4, 0, -4, 0), Qt::AlignCenter, QFontMetrics(p.font()).elidedText(label, Qt::ElideRight, int(box.width()) - 8));
        }
    }
    void paint(QPainter &p) {
        p.fillRect(view.boundingRect(), Qt::white);
        if (sessions[active]) {
            const auto &image = sessions[active]->frame();
            const QRectF terminal(margin, margin, image.width(), image.height());
            const QRectF exposed = p.hasClipping() ? terminal.intersected(p.clipBoundingRect()) : terminal;
            if (!exposed.isEmpty())
                p.drawImage(exposed, image, exposed.translated(-margin, -margin));
        }
        paint_ime(p);
        if (prefs.bottom_bar()) {
            const auto top = view.height() - footer;
            p.setPen(Qt::black); p.drawLine(QPointF(0, top), QPointF(view.width(), top));
            const QString labels[] = {"Esc", "Page up", "Page down", "Quit", "Settings"};
            font(p, 23);
            for (int i = 0; i < 5; ++i) {
                QRectF box(view.width() * i / 5, top, view.width() / 5, footer);
                if (i) p.drawLine(box.topLeft(), box.bottomLeft());
                if (i < 4) p.drawText(box, Qt::AlignCenter, labels[i]);
                else {
                    p.drawText(box.adjusted(0, 2, 0, -20), Qt::AlignCenter, labels[i]);
                    font(p, 14); p.drawText(box.adjusted(0, 34, 0, 0), Qt::AlignCenter, QString("Terminal %1 / 6").arg(active + 1));
                }
            }
        }
        if (show_terminal && !prefs.bottom_bar() && overlay == Overlay::None) {
            const QRectF box(view.width() - 190, 12, 178, 40);
            p.fillRect(box, Qt::white); p.setPen(Qt::black); p.drawRect(box); font(p, 20);
            p.drawText(box, Qt::AlignCenter, QString("Terminal %1 / 6").arg(active + 1));
        }
        if (!notice.isEmpty() && overlay == Overlay::None) {
            font(p, 24); const auto w = std::min<qreal>(view.width() - 24, QFontMetrics(p.font()).horizontalAdvance(notice) + 36);
            const QRectF box((view.width() - w) / 2, 14, w, 48); p.fillRect(box, Qt::white); p.drawRect(box); p.drawText(box, Qt::AlignCenter, notice);
        }
        if (overlay == Overlay::None) return;
        // Keep the terminal around Settings visible as a live text preview.
        if (overlay != Overlay::Settings) p.fillRect(view.boundingRect(), QColor(255, 255, 255, 205));
        const auto box = panel(); p.fillRect(box, Qt::white); p.setPen(QPen(Qt::black, 2)); p.drawRect(box);
        font(p, 30, true);
        p.drawText(box.adjusted(22, 20, -22, -box.height() + 65), Qt::AlignLeft | Qt::AlignVCenter,
                   overlay == Overlay::Settings ? "Inkline settings" : overlay == Overlay::Methods ? "Input method" : overlay == Overlay::Quit ? "Quit Inkline?" : "Inkline");
        if (overlay == Overlay::Settings) {
            const QString labels[] = {"Caps Lock key", "Bottom bar", "Text size", "Input method", "E-paper updates", "Text darkness", "Minimum contrast"};
            for (int i = 0; i < 7; ++i) {
                const auto r = row(i); font(p, 22); p.drawText(r.adjusted(0, 0, 0, -r.height() + 28), Qt::AlignLeft | Qt::AlignVCenter, labels[i]);
                if (selected == i) focus(p, r);
            }
            button(p, choice(0, 0), prefs.caps_control() ? "●  Control" : "○  Control", false, prefs.caps_control());
            button(p, choice(0, 1), !prefs.caps_control() ? "●  Caps Lock" : "○  Caps Lock", false, !prefs.caps_control());
            button(p, choice(1, 0), prefs.bottom_bar() ? "●  Shown" : "○  Shown", false, prefs.bottom_bar());
            button(p, choice(1, 1), !prefs.bottom_bar() ? "●  Hidden" : "○  Hidden", false, !prefs.bottom_bar());
            button(p, font_button(0), "−", false); button(p, font_button(1), "+", false);
            font(p, 28, true); p.drawText(row(2).adjusted(110, 32, -110, -4), Qt::AlignCenter, QString("%1 px").arg(pixels));
            const auto r = row(3); button(p, r.adjusted(0, 32, 0, -4), InputMethod::name(prefs.input_method()) + "  ›", false);
            for (int i = 0; i < int(update_policies.size()); ++i)
                button(p, update_choice(i), update_policies[i].name, false, update_policy == i);
            font(p, 16);
            p.drawText(row(4).adjusted(190, 0, 0, -row(4).height() + 27), Qt::AlignRight | Qt::AlignVCenter,
                       QString::fromUtf8(update_policies[update_policy].detail));
            for (int index : {5, 6}) {
                const int value = index == 5 ? darkness : contrast;
                const auto track = slider_track(index); p.fillRect(track, Qt::black);
                const QPointF knob(track.left() + value * track.width() / 100, track.center().y());
                p.setBrush(Qt::white); p.drawEllipse(knob, 13, 13); p.setBrush(Qt::NoBrush);
                font(p, 18);
                const auto r = row(index);
                p.drawText(r.adjusted(0, 28, -r.width() + 90, -2), Qt::AlignVCenter, index == 5 ? "Lighter" : "Off");
                p.drawText(r.adjusted(r.width() - 90, 28, 0, -2), Qt::AlignVCenter | Qt::AlignRight, index == 5 ? "Darker" : "Strong");
                p.drawText(r.adjusted(220, 0, 0, -r.height() + 26), Qt::AlignRight | Qt::AlignVCenter,
                           index == 5 ? QString("%1%  ·  50% = normal").arg(value)
                                      : QString("%1% luminance gap  ·  0% = off").arg(value));
            }
            const auto help_top = row(6).bottom() + 8;
            if (close_button().top() - help_top >= 24) {
                font(p, 18);
                p.drawText(QRectF(box.left() + 22, help_top, box.width() - 44, close_button().top() - help_top - 5),
                           Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                           "Solid fill = active. Dashed outline = keyboard focus.  Tab / ↑↓: focus; ←→: change.\nDisplay tuning stays in RAM; changing it never writes the tablet storage.");
            }
            button(p, close_button(), "Done", selected == 7);
        } else if (overlay == Overlay::Methods) {
            const QString names[] = {"Off — direct keyboard", "Romaji — Japanese hiragana", "US-International — accented letters", "Pinyin — Chinese", "Zhuyin — Chinese (basic layout)", "Wubi 86 — Chinese"};
            for (int i = 0; i < InputMethod::COUNT; ++i)
                button(p, method_row(i), (prefs.input_method() == i ? "●  " : "○  ") + names[i], selected == i, prefs.input_method() == i);
            font(p, 19); p.drawText(box.adjusted(22, 480, -22, -84), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                "Type to compose. Space / Enter picks the first candidate.\n1–9 or a tap picks a candidate; [ / ] changes pages.\nBackspace edits composition; Escape cancels it.");
            button(p, close_button(), "Back", selected == InputMethod::COUNT);
        } else if (overlay == Overlay::Quit) {
            font(p, 23);
            const auto message = QString("This ends all %1 open terminal%2 and returns to your notebooks.").arg(count()).arg(count() == 1 ? "" : "s");
            p.drawText(box.adjusted(22, 90, -22, -105), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, message);
            button(p, confirm_button(0), "Cancel", selected == 0); button(p, confirm_button(1), "Quit", selected == 1);
        } else {
            font(p, 23); p.drawText(box.adjusted(22, 85, -22, -80), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, error);
            button(p, close_button(), "Close", true);
        }
    }
    void click(const QPointF &point) {
        if (overlay == Overlay::Settings) {
            for (int i = 0; i < 2; ++i) {
                if (choice(0, i).contains(point)) { selected = 0; set_caps(i == 0); }
                if (choice(1, i).contains(point)) { selected = 1; set_bar(i == 0); }
                if (font_button(i).contains(point)) { selected = 2; zoom(pixels + (i ? 2 : -2)); }
            }
            for (int i = 0; i < int(update_policies.size()); ++i)
                if (update_choice(i).contains(point)) { selected = 4; set_update_policy(i); }
            if (row(3).contains(point)) { selected = 3; activate(); }
            else if (close_button().contains(point)) { selected = 7; activate(); }
        } else if (overlay == Overlay::Methods) {
            for (int i = 0; i < InputMethod::COUNT; ++i) if (method_row(i).contains(point)) { set_method(i); return; }
            if (close_button().contains(point)) { overlay = Overlay::Settings; selected = 3; view.update(); }
        } else if (overlay == Overlay::Quit) {
            for (int i = 0; i < 2; ++i) if (confirm_button(i).contains(point)) { selected = i; activate(); }
        } else if (overlay == Overlay::Error) {
            if (close_button().contains(point)) { overlay = Overlay::None; view.update(); }
        } else if (prefs.bottom_bar() && point.y() >= view.height() - footer) {
            const int index = std::clamp(int(5 * point.x() / view.width()), 0, 4);
            if (index == 4) settings();
            else if (index == 3) quit();
            else if (sessions[active]) {
                if (index == 0) {
                    MappedInput escape; escape.key = Qt::Key_Escape;
                    sessions[active]->key(escape); escape.type = QEvent::KeyRelease; sessions[active]->key(escape); schedule(true); view.update();
                } else sessions[active]->scroll(index == 1 ? -1 : 1);
            }
        } else if (ime_height() && sessions[active]) {
            for (int i = 0; i < sessions[active]->ime.candidates().size(); ++i)
                if (candidate_box(i).contains(point)) { sessions[active]->text_input(sessions[active]->ime.candidate(i)); view.update(); return; }
        }
    }
    void pointer_press(const QPointF &point) {
        cancel_pointer();
        press_point = last_point = point; pointer_down = true; dragging = false; press_overlay = overlay;
        if (overlay == Overlay::Settings && (row(5).contains(point) || row(6).contains(point))) {
            slider_drag = row(5).contains(point) ? 5 : 6; selected = slider_drag;
            press_slider = slider_drag == 5 ? darkness : contrast;
            if (slider_drag == 5) slide_darkness(point); else slide_contrast(point);
        }
        text_drag = overlay == Overlay::None && text_area().contains(point) && bool(sessions[active]);
        if (text_drag) sessions[active]->select(point - QPointF(margin, margin), true);
    }
    void pointer_move(const QPointF &point) {
        if (!pointer_down) return;
        last_point = point;
        if (slider_drag >= 0) { if (slider_drag == 5) slide_darkness(point); else slide_contrast(point); return; }
        if (QLineF(press_point, point).length() >= 8) dragging = true;
        if (dragging && text_drag && sessions[active]) {
            sessions[active]->select(point - QPointF(margin, margin), false);
            if (!text_area().contains(point)) { if (!autoscroll.isActive()) autoscroll.start(); }
            else autoscroll.stop();
        }
    }
    void pointer_release(const QPointF &point) {
        if (!pointer_down) return;
        pointer_move(point);
        if (slider_drag >= 0) {
            slider_drag = -1; pointer_down = false;
            return;
        }
        const bool tap = !dragging && press_overlay == overlay;
        pointer_down = false; autoscroll.stop();
        if (text_drag && sessions[active]) sessions[active]->selection_end(!dragging);
        if (tap && !text_drag) click(point);
        text_drag = false;
    }
    void cancel_pointer() {
        if (slider_drag >= 0) {
            if (slider_drag == 5) set_darkness(press_slider); else set_contrast(press_slider);
            slider_drag = -1;
        }
        if (pointer_down && text_drag && sessions[active]) sessions[active]->selection_end(true);
        pointer_down = dragging = text_drag = false; autoscroll.stop();
    }
    void touch(QTouchEvent &event) {
        if (pen_down) return;
        if (event.type() == QEvent::TouchCancel) {
            cancel_pointer();
            if (gesture == Gesture::Pinch) { zoom(pinch_pixels, false); font_save.start(); }
            gesture = Gesture::None; touch_id = -1; return;
        }
        QList<QEventPoint> live;
        for (const auto &point : event.points()) if (point.state() != QEventPoint::State::Released) live.append(point);
        if (live.size() >= 2 && gesture == Gesture::None) {
            cancel_pointer(); gesture = Gesture::Pending;
            pinch_pixels = pixels; pinch_a = live[0].id(); pinch_b = live[1].id();
            const auto a = view.mapFromScene(live[0].scenePosition()), b = view.mapFromScene(live[1].scenePosition());
            pinch_distance = QLineF(a, b).length();
            gesture_start = gesture_last = (a + b) / 2;
            scroll_remainder = 0;
            can_scroll = overlay == Overlay::None && sessions[active] && text_area().contains(a) && text_area().contains(b);
        }
        if (gesture != Gesture::None) {
            const QEventPoint *a = nullptr, *b = nullptr;
            for (const auto &point : live) { if (point.id() == pinch_a) a = &point; if (point.id() == pinch_b) b = &point; }
            if (a && b) {
                const auto first = view.mapFromScene(a->scenePosition()), second = view.mapFromScene(b->scenePosition());
                const auto center = (first + second) / 2;
                const auto distance = QLineF(first, second).length();
                if (gesture == Gesture::Pending) {
                    const qreal stretch = std::abs(distance - pinch_distance);
                    const qreal travel = std::abs(center.y() - gesture_start.y());
                    const qreal sideways = center.x() - gesture_start.x();
                    // Lock the gesture after deliberate movement. Small changes
                    // in finger spacing during a scroll must not resize text.
                    if (pinch_distance >= 20 && stretch >= std::max(qreal(12), pinch_distance * 0.06) && stretch > 2 * std::max(travel, std::abs(sideways))) {
                        gesture = Gesture::Pinch; font_save.stop();
                    } else if (can_scroll && std::abs(sideways) >= 60 && std::abs(sideways) > 1.5 * travel) {
                        gesture = Gesture::Swipe;
                        choose(active + (sideways < 0 ? 1 : -1));
                    } else if (can_scroll && travel >= 12 && travel >= std::abs(sideways)) gesture = Gesture::Scroll;
                }
                if (gesture == Gesture::Pinch) {
                    // Half the proportional response, in single-pixel steps.
                    // Square-root scaling treats opening and closing equally.
                    const auto scale = std::sqrt(std::clamp(distance / pinch_distance, qreal(0.0625), qreal(16)));
                    zoom(int(std::lround(pinch_pixels * scale)), false);
                }
                if (gesture == Gesture::Scroll && sessions[active]) {
                    scroll_remainder -= center.y() - gesture_last.y();
                    const int height = sessions[active]->cell_height();
                    const int lines = int(scroll_remainder / height);
                    if (lines) { sessions[active]->scroll_lines(lines); scroll_remainder -= lines * height; }
                    gesture_last = center;
                }
            }
            if (live.isEmpty() || event.type() == QEvent::TouchEnd) {
                if (gesture == Gesture::Pinch) save_font();
                gesture = Gesture::None; touch_id = -1;
            }
            return;
        }
        if (event.type() == QEvent::TouchBegin && live.size() == 1) {
            touch_id = live[0].id(); pointer_press(view.mapFromScene(live[0].scenePosition()));
        }
        for (const auto &point : event.points()) if (point.id() == touch_id) {
            const auto position = view.mapFromScene(point.scenePosition());
            if (point.state() == QEventPoint::State::Released) { pointer_release(position); touch_id = -1; }
            else pointer_move(position);
        }
        if (event.type() == QEvent::TouchEnd) { if (pointer_down) pointer_release(last_point); touch_id = -1; }
    }

};

TerminalView::TerminalView(QQuickItem *parent, int pixels, bool demo, const QString &path, std::vector<std::string> shell)
    : QQuickPaintedItem(parent), d_(std::make_unique<Private>(*this, pixels, demo, path, std::move(shell))) {
    setOpaquePainting(true); setFillColor(Qt::white); setAcceptedMouseButtons(Qt::LeftButton); setFlag(ItemAcceptsInputMethod); setAcceptTouchEvents(true);
}
TerminalView::~TerminalView() = default;
void TerminalView::start() { d_->choose(0); forceActiveFocus(); }
void TerminalView::layout() { d_->layout(); }
void TerminalView::paint(QPainter *p) { d_->paint(*p); }
QImage TerminalView::snapshot() {
    d_->refresh(); QImage image(int(width()), int(height()), QImage::Format_RGB32); image.fill(Qt::white);
    QPainter p(&image); paint(&p); return image;
}
void TerminalView::settings() { d_->settings(); }
void TerminalView::select_terminal(int index) { if (index < 0 || index >= TERMINALS) throw std::out_of_range("Terminal number"); d_->choose(index); }
void TerminalView::send_text(std::string_view text) { if (d_->sessions[d_->active]) d_->sessions[d_->active]->send(text); }
int TerminalView::active_terminal() const { return d_->active; }
int TerminalView::terminal_count() const { return d_->count(); }
bool TerminalView::bottom_bar() const { return d_->prefs.bottom_bar(); }
bool TerminalView::settings_open() const { return d_->overlay == Private::Overlay::Settings; }
bool TerminalView::quit_confirmation_open() const { return d_->overlay == Private::Overlay::Quit; }
void TerminalView::keyPressEvent(QKeyEvent *event) { d_->guarded([&] { d_->key(*event); }); event->accept(); }
void TerminalView::keyReleaseEvent(QKeyEvent *event) { d_->guarded([&] { d_->key(*event); }); event->accept(); }
void TerminalView::focusOutEvent(QFocusEvent *event) {
    d_->guarded([&] {
        d_->cancel_pointer();
        if (d_->gesture == Private::Gesture::Pinch) { d_->zoom(d_->pinch_pixels, false); d_->font_save.start(); }
        d_->gesture = Private::Gesture::None; d_->touch_id = -1; d_->pen_down = false;
    });
    d_->guarded([&] { for (const auto &release : d_->input.reset()) if (d_->sessions[release.terminal]) d_->sessions[release.terminal]->key(release); });
    QQuickPaintedItem::focusOutEvent(event);
}
void TerminalView::inputMethodEvent(QInputMethodEvent *event) {
    if (d_->overlay == Private::Overlay::None) d_->guarded([&] { const auto bytes = event->commitString().toUtf8(); send_text({bytes.constData(), size_t(bytes.size())}); });
    event->accept();
}
void TerminalView::mousePressEvent(QMouseEvent *event) { forceActiveFocus(); d_->guarded([&] { d_->pointer_press(event->position()); }); event->accept(); }
void TerminalView::mouseMoveEvent(QMouseEvent *event) { d_->guarded([&] { d_->pointer_move(event->position()); }); event->accept(); }
void TerminalView::mouseReleaseEvent(QMouseEvent *event) { d_->guarded([&] { d_->pointer_release(event->position()); }); event->accept(); }
void TerminalView::touchEvent(QTouchEvent *event) { forceActiveFocus(); d_->guarded([&] { d_->touch(*event); }); event->accept(); }
bool TerminalView::event(QEvent *event) {
    if (event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove || event->type() == QEvent::TabletRelease) {
        auto *tablet = static_cast<QTabletEvent *>(event);
        d_->guarded([&] {
            if (event->type() == QEvent::TabletPress) { forceActiveFocus(); d_->pen_down = true; d_->pointer_press(tablet->position()); }
            else if (event->type() == QEvent::TabletMove) d_->pointer_move(tablet->position());
            else { d_->pointer_release(tablet->position()); d_->pen_down = false; }
        });
        event->accept(); return true;
    }
    return QQuickPaintedItem::event(event);
}
int TerminalView::font_pixels() const { return d_->pixels; }
int TerminalView::text_darkness() const { return d_->darkness; }
int TerminalView::minimum_contrast() const { return d_->contrast; }
int TerminalView::update_profile() const { return d_->update_policy; }
int TerminalView::input_method() const { return d_->prefs.input_method(); }
QByteArray TerminalView::clipboard_text() const { return d_->clipboard.text(); }
}
