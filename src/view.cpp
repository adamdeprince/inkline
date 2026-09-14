#include "rmt/view.hpp"
#include "rmt/caps_leds.hpp"
#include "rmt/input.hpp"
#include "rmt/keyboard.hpp"
#include "rmt/preferences.hpp"
#include "rmt/pty.hpp"
#include "rmt/renderer.hpp"
#include "rmt/stream.hpp"
#include <QCoreApplication>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QMouseEvent>
#include <QSocketNotifier>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <unistd.h>

namespace rmt {
namespace {
constexpr int margin = 12, footer = 60;
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
    Session(QObject *parent, int pixels, std::function<void()> changed,
            std::function<void(Session *)> exited, std::function<void(QString)> error)
        : QObject(parent), core_(make_core()), renderer_(*core_, pixels, 16 * 1024 * 1024),
          keyboard_(*core_), stream_(*core_, [this](sixel::Bitmap &&b) { renderer_.sixel(std::move(b)); }),
          changed_(std::move(changed)), exited_(std::move(exited)), error_(std::move(error)) {
        stream_.set_control_handler([this](std::string_view c) {
            if (c == "\033c" || c == "\033[2J" || c == "\033[3J" || c == "\x9b" "2J" || c == "\x9b" "3J") renderer_.clear_sixel();
        });
        auto terminal = rmt_core_terminal(core_.get());
        ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_USERDATA, this);
        ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY, reinterpret_cast<const void *>(reply));
        ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_SIZE, reinterpret_cast<const void *>(size_report));
        connect(&reap_, &QTimer::timeout, this, [this] { guarded([this] {
            if (pty_ && pty_->poll_exit()) { reap_.stop(); exited_(this); }
        }); });
    }
    void start(int number, bool demo, const std::vector<std::string> &command) {
        const auto banner = QString("Inkline %1 — terminal %2 of 6\r\nRight Alt/Option + Space: settings. Ctrl+Shift+B: bottom bar.\r\n\r\n")
            .arg(QCoreApplication::applicationVersion()).arg(number).toUtf8();
        stream_.write({banner.constData(), size_t(banner.size())});
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
    void layout(int width, int height) { renderer_.resize(width, height); resize_pty(); }
    QImage frame() { return renderer_.frame(); }
    void key(const MappedInput &input) {
        auto event = input.event();
        if (input.type == QEvent::KeyPress) {
            GhosttyTerminalScrollViewport bottom{}; bottom.tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM;
            ghostty_terminal_scroll_viewport(rmt_core_terminal(core_.get()), bottom);
        }
        send(keyboard_.encode(event, input.caps_locked));
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
        changed_();
    }
private:
    Core core_;
    Renderer renderer_;
    Keyboard keyboard_;
    Stream stream_;
    std::unique_ptr<Pty> pty_;
    std::unique_ptr<QSocketNotifier> reader_, writer_;
    QTimer reap_;
    std::function<void()> changed_;
    std::function<void(Session *)> exited_;
    std::function<void(QString)> error_;
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
        if (received) changed_();
    }
    static void reply(GhosttyTerminal, void *context, const uint8_t *bytes, size_t count) {
        auto *self = static_cast<Session *>(context);
        self->guarded([&] { self->send({reinterpret_cast<const char *>(bytes), count}); });
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
}

class TerminalView::Private {
public:
    enum class Overlay { None, Settings, Quit, Error };
    TerminalView &view;
    Preferences prefs;
    InputMapper input;
    CapsLeds leds;
    std::array<std::unique_ptr<Session>, TERMINALS> sessions;
    int active = 0, pixels, selected = 0;
    bool demo;
    std::vector<std::string> shell;
    QTimer repaint, toast;
    QImage frame;
    Overlay overlay = Overlay::None;
    QString error;
    bool show_terminal = false;
    Private(TerminalView &v, int p, bool demonstration, const QString &path, std::vector<std::string> command)
        : view(v), prefs(path), pixels(p), demo(demonstration), shell(std::move(command)) {
        input.set_caps_control(prefs.caps_control());
        repaint.setSingleShot(true); repaint.setInterval(80);
        QObject::connect(&repaint, &QTimer::timeout, &view, [this] { guarded([this] { refresh(); }); });
        toast.setSingleShot(true); toast.setInterval(1400);
        QObject::connect(&toast, &QTimer::timeout, &view, [this] { show_terminal = false; view.update(); });
    }
    template<class F> void guarded(F work) {
        try { work(); } catch (const std::exception &e) { error = QString::fromUtf8(e.what()); overlay = Overlay::Error; view.update(); }
    }
    int count() const { return int(std::count_if(sessions.begin(), sessions.end(), [](const auto &s) { return bool(s); })); }
    int footer_height() const { return prefs.bottom_bar() ? footer : 0; }
    void schedule() { if (!repaint.isActive()) repaint.start(); }
    void refresh() {
        if (!sessions[active]) return;
        const QImage next = sessions[active]->frame();
        if (next.size() != frame.size()) { frame = next; view.update(); return; }
        int first = -1, last = -1;
        for (int y = 0; y < next.height(); ++y) {
            if (std::memcmp(next.constScanLine(y), frame.constScanLine(y), size_t(next.width())) != 0) {
                if (first < 0) first = y;
                last = y;
            }
        }
        frame = next;
        if (first >= 0) view.update(QRect(margin, margin + first, next.width(), last - first + 1));
    }
    void layout() {
        for (auto &s : sessions) if (s) s->layout(int(view.width()) - 2 * margin, int(view.height()) - 2 * margin - footer_height());
        frame = {}; schedule(); view.update();
    }
    void choose(int index) {
        index = (index + TERMINALS) % TERMINALS;
        if (!sessions[index]) {
            auto next = std::make_unique<Session>(&view, pixels,
                [this, index] { if (active == index) schedule(); },
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
            next->layout(int(view.width()) - 2 * margin, int(view.height()) - 2 * margin - footer_height());
            next->start(index + 1, demo, shell);
            sessions[index] = std::move(next);
        }
        active = index; overlay = Overlay::None; frame = {};
        show_terminal = true; toast.start(); refresh(); view.update();
    }
    void settings() { overlay = overlay == Overlay::Settings ? Overlay::None : Overlay::Settings; selected = 0; view.update(); }
    void quit() { overlay = Overlay::Quit; selected = 0; view.update(); }
    void toggle_bar() { prefs.set_bottom_bar(!prefs.bottom_bar()); layout(); }
    void toggle_caps() { prefs.set_caps_control(!prefs.caps_control()); input.set_caps_control(prefs.caps_control()); leds.set_locked(input.caps_locked()); view.update(); }
    void activate() {
        if (overlay == Overlay::Settings) {
            if (selected == 0) toggle_caps();
            else if (selected == 1) toggle_bar();
            else overlay = Overlay::None;
        } else if (overlay == Overlay::Quit) {
            if (selected == 1) QCoreApplication::quit();
            else overlay = Overlay::None;
        } else overlay = Overlay::None;
        view.update();
    }
    void dialog_key(const MappedInput &event) {
        if (event.type != QEvent::KeyPress) return;
        const int key = event.key;
        if (key == Qt::Key_Escape) { overlay = Overlay::None; view.update(); return; }
        if (overlay == Overlay::Quit && key == Qt::Key_N) { overlay = Overlay::None; view.update(); return; }
        if (overlay == Overlay::Quit && key == Qt::Key_Y) { selected = 1; activate(); return; }
        const int choices = overlay == Overlay::Settings ? 3 : overlay == Overlay::Quit ? 2 : 1;
        if (key == Qt::Key_Tab || key == Qt::Key_Down || key == Qt::Key_Right) selected = (selected + 1) % choices;
        else if (key == Qt::Key_Backtab || key == Qt::Key_Up || key == Qt::Key_Left) selected = (selected + choices - 1) % choices;
        else if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) && !event.repeat) activate();
        view.update();
    }
    void key(const QKeyEvent &event) {
        const auto mapped = input.map(event, overlay == Overlay::None ? active : -1);
        if (event.key() == Qt::Key_CapsLock || event.nativeScanCode() == 66) leds.set_locked(input.caps_locked());
        switch (mapped.action) {
        case InputAction::Settings: settings(); break;
        case InputAction::Quit: quit(); break;
        case InputAction::Previous: choose(active + TERMINALS - 1); break;
        case InputAction::Next: choose(active + 1); break;
        case InputAction::ToggleBar: toggle_bar(); break;
        case InputAction::HistoryUp: if (sessions[active] && overlay == Overlay::None) sessions[active]->scroll(-1); break;
        case InputAction::HistoryDown: if (sessions[active] && overlay == Overlay::None) sessions[active]->scroll(1); break;
        case InputAction::Send:
            if (mapped.terminal < 0) dialog_key(mapped);
            else if (sessions[mapped.terminal]) { sessions[mapped.terminal]->key(mapped); if (mapped.terminal == active) schedule(); }
            break;
        case InputAction::Ignore: break;
        }
    }
    QRectF panel() const {
        const qreal w = std::min<qreal>(840, view.width() - 48);
        const qreal h = std::min<qreal>(overlay == Overlay::Settings ? 650 : 310, view.height() - 48);
        return {(view.width() - w) / 2, (view.height() - h) / 2, w, h};
    }
    QRectF row(int index) const { const auto p = panel(); return {p.x() + 22, p.y() + 85 + index * 72, p.width() - 44, 60}; }
    QRectF close_button() const { const auto p = panel(); return {p.right() - 160, p.bottom() - 70, 138, 48}; }
    QRectF confirm_button(int index) const {
        const auto p = panel(); const qreal w = (p.width() - 66) / 2;
        return {p.x() + 22 + index * (w + 22), p.bottom() - 78, w, 54};
    }
    void button(QPainter &p, const QRectF &box, const QString &text, bool focused) {
        p.fillRect(box, focused ? Qt::black : Qt::white); p.setPen(Qt::black); p.drawRect(box);
        p.setPen(focused ? Qt::white : Qt::black); font(p, 22); p.drawText(box.adjusted(12, 0, -12, 0), Qt::AlignCenter, text); p.setPen(Qt::black);
    }
    void paint(QPainter &p) {
        p.fillRect(view.boundingRect(), Qt::white);
        p.drawImage(margin, margin, frame);
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
        if (overlay == Overlay::None) return;
        p.fillRect(view.boundingRect(), QColor(255, 255, 255, 205));
        const auto box = panel(); p.fillRect(box, Qt::white); p.setPen(QPen(Qt::black, 2)); p.drawRect(box);
        font(p, 30, true);
        p.drawText(box.adjusted(22, 20, -22, -box.height() + 65), Qt::AlignLeft | Qt::AlignVCenter,
                   overlay == Overlay::Settings ? "Inkline settings" : overlay == Overlay::Quit ? "Quit Inkline?" : "Inkline");
        if (overlay == Overlay::Settings) {
            button(p, row(0), QString("Caps Lock key: %1").arg(prefs.caps_control() ? "Control" : "Caps Lock"), selected == 0);
            button(p, row(1), QString("Bottom bar: %1").arg(prefs.bottom_bar() ? "Shown" : "Hidden"), selected == 1);
            font(p, 20);
            const QString help = "Right Alt/Option shortcuts\n\n1–0  →  F1–F10       Tab  →  Escape\n↑ / ↓  →  Page up / down\n← / →  →  Terminal 1–6\nSpace  →  Settings       Backspace  →  Quit\n\nCtrl+Shift+B hides or shows the bottom bar.\nTab or ↑/↓ selects; Enter changes a setting.\nEscape closes this screen. Settings are saved.";
            p.drawText(QRectF(box.x() + 22, box.y() + 245, box.width() - 44, box.height() - 325), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, help);
            button(p, close_button(), "Done", selected == 2);
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
            if (row(0).contains(point)) { selected = 0; activate(); }
            else if (row(1).contains(point)) { selected = 1; activate(); }
            else if (close_button().contains(point)) { selected = 2; activate(); }
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
                    sessions[active]->key(escape);
                    escape.type = QEvent::KeyRelease;
                    sessions[active]->key(escape);
                    schedule();
                }
                else sessions[active]->scroll(index == 1 ? -1 : 1);
            }
        }
    }
};

TerminalView::TerminalView(QQuickItem *parent, int pixels, bool demo, const QString &path, std::vector<std::string> shell)
    : QQuickPaintedItem(parent), d_(std::make_unique<Private>(*this, pixels, demo, path, std::move(shell))) {
    setOpaquePainting(true); setFillColor(Qt::white); setAcceptedMouseButtons(Qt::LeftButton); setFlag(ItemAcceptsInputMethod);
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
    d_->guarded([&] { for (const auto &release : d_->input.reset()) if (d_->sessions[release.terminal]) d_->sessions[release.terminal]->key(release); });
    QQuickPaintedItem::focusOutEvent(event);
}
void TerminalView::inputMethodEvent(QInputMethodEvent *event) {
    if (d_->overlay == Private::Overlay::None) d_->guarded([&] { const auto bytes = event->commitString().toUtf8(); send_text({bytes.constData(), size_t(bytes.size())}); });
    event->accept();
}
void TerminalView::mousePressEvent(QMouseEvent *event) { forceActiveFocus(); d_->guarded([&] { d_->click(event->position()); }); event->accept(); }
}
