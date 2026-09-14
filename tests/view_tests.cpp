#include "rmt/view.hpp"
#include "rmt/preferences.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>
#include <QTouchEvent>
#include <QPointingDevice>
#include <QFontMetrics>
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
void key(rmt::TerminalView &view, QEvent::Type type, int code, quint32 scan, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString &text = {}) {
    QKeyEvent event(type, code, mods, scan, 0, 0, text);
    QCoreApplication::sendEvent(&view, &event);
}
void press(rmt::TerminalView &view, int code, quint32 scan = 0, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString &text = {}) {
    key(view, QEvent::KeyPress, code, scan, mods, text); key(view, QEvent::KeyRelease, code, scan, mods);
}
void alt(rmt::TerminalView &view, int code, quint32 scan) {
    key(view, QEvent::KeyPress, Qt::Key_AltGr, 108, Qt::GroupSwitchModifier);
    press(view, code, scan, Qt::AltModifier);
    key(view, QEvent::KeyRelease, Qt::Key_AltGr, 108);
}
void pump(int ms) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < ms) { QCoreApplication::processEvents(); QThread::msleep(5); }
}
void touch(rmt::TerminalView &view, QEvent::Type type, std::initializer_list<QEventPoint> points) {
    static QPointingDevice device("test fingers", 123, QInputDevice::DeviceType::TouchScreen,
        QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 10, 0);
    QTouchEvent event(type, &device, Qt::NoModifier, QList<QEventPoint>(points));
    QCoreApplication::sendEvent(&view, &event);
}
QEventPoint point(rmt::TerminalView &view, int id, QEventPoint::State state, int x, int y) {
    return QEventPoint(id, state, view.mapToScene(QPointF(x, y)), view.mapToScene(QPointF(x, y)));
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv); app.setApplicationVersion("test");
    QTemporaryDir temporary; CHECK(temporary.isValid());
    const auto settings = temporary.filePath("settings.ini");
    {
        QQuickWindow window; window.resize(1000, 750);
        rmt::TerminalView view(window.contentItem(), 24, true, settings);
        view.setSize(QSizeF(1000, 750)); view.layout(); view.start();
        CHECK(view.terminal_count() == 1 && view.active_terminal() == 0);
        const auto first = view.snapshot(); CHECK(!first.isNull());
        // Keep Right Alt held across switches: the modifier belongs to the view.
        key(view, QEvent::KeyPress, Qt::Key_AltGr, 108, Qt::GroupSwitchModifier);
        for (int index = 1; index < 6; ++index) {
            press(view, Qt::Key_Right, 114, Qt::AltModifier);
            CHECK(view.active_terminal() == index && view.terminal_count() == index + 1);
        }
        press(view, Qt::Key_Right, 114, Qt::AltModifier);
        CHECK(view.active_terminal() == 0 && view.terminal_count() == 6);
        key(view, QEvent::KeyRelease, Qt::Key_AltGr, 108);
        CHECK(view.snapshot() == first);
        alt(view, Qt::Key_Space, 65); CHECK(view.settings_open());
        if (const char *path = std::getenv("INKLINE_TEST_SNAPSHOTS")) CHECK(view.snapshot().save(QString::fromUtf8(path) + "/terminal-settings.png"));
        press(view, Qt::Key_Space, 65); // Caps Lock row: Control -> Caps Lock.
        CHECK(!rmt::Preferences(settings).caps_control());
        press(view, Qt::Key_Tab, 23);
        press(view, Qt::Key_Return, 36);
        CHECK(!view.bottom_bar() && !rmt::Preferences(settings).bottom_bar());
        alt(view, Qt::Key_Tab, 23); CHECK(!view.settings_open());
        // Settings can still open while the entire bottom bar is hidden.
        alt(view, Qt::Key_Space, 65); CHECK(view.settings_open());
        press(view, Qt::Key_Escape, 9); CHECK(!view.settings_open());
        press(view, Qt::Key_B, 56, Qt::ControlModifier | Qt::ShiftModifier);
        CHECK(view.bottom_bar());
        QMouseEvent click(QEvent::MouseButtonPress, QPointF(950, 720), QPointF(950, 720), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &click);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(950, 720), QPointF(950, 720), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &release); CHECK(view.settings_open());
        press(view, Qt::Key_Escape, 9);
        alt(view, Qt::Key_Backspace, 22); CHECK(view.quit_confirmation_open());
        if (const char *path = std::getenv("INKLINE_TEST_SNAPSHOTS")) CHECK(view.snapshot().save(QString::fromUtf8(path) + "/terminal-quit.png"));
        press(view, Qt::Key_Return, 36); CHECK(!view.quit_confirmation_open()); // Cancel is the default.
        alt(view, Qt::Key_Backspace, 22); CHECK(view.quit_confirmation_open());
        press(view, Qt::Key_Escape, 9); CHECK(!view.quit_confirmation_open());
        CHECK(view.terminal_count() == 6);
        // Zoom never replaces any running terminal, and saves after key repeat pauses.
        alt(view, Qt::Key_Equal, 21); CHECK(view.font_pixels() == 26);
        CHECK(rmt::Preferences(settings).font_pixels() == 26); // default, no write yet
        alt(view, Qt::Key_Equal, 21); CHECK(view.font_pixels() == 28);
        CHECK(rmt::Preferences(settings).font_pixels() == 26);
        pump(800); CHECK(rmt::Preferences(settings).font_pixels() == 28);
        CHECK(view.terminal_count() == 6);
        // Pinch coordinates must work under the tablet's rotated view as well.
        view.setRotation(90);
        using S = QEventPoint::State;
        touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 400, 300)});
        touch(view, QEvent::TouchUpdate, {point(view, 1, S::Stationary, 400, 300), point(view, 2, S::Pressed, 600, 300)});
        touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 350, 300), point(view, 2, S::Updated, 650, 300)});
        CHECK(view.font_pixels() == 42); CHECK(rmt::Preferences(settings).font_pixels() == 28);
        touch(view, QEvent::TouchEnd, {point(view, 1, S::Released, 350, 300), point(view, 2, S::Released, 650, 300)});
        CHECK(rmt::Preferences(settings).font_pixels() == 42);
        touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 400, 300), point(view, 2, S::Pressed, 600, 300)});
        touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 480, 300), point(view, 2, S::Updated, 520, 300)});
        CHECK(view.font_pixels() == 16);
        touch(view, QEvent::TouchCancel, {}); CHECK(view.font_pixels() == 42);
        CHECK(rmt::Preferences(settings).font_pixels() == 42);
        view.setRotation(0);
        // Input methods are selected through Option+Space, and persist.
        alt(view, Qt::Key_Space, 65);
        for (int n = 0; n < 3; ++n) press(view, Qt::Key_Tab, 23);
        press(view, Qt::Key_Return, 36); // method chooser
        for (int n = 0; n < 3; ++n) press(view, Qt::Key_Down, 116);
        press(view, Qt::Key_Return, 36); CHECK(view.input_method() == 3);
        CHECK(rmt::Preferences(settings).input_method() == 3);
        if (const char *path = std::getenv("INKLINE_TEST_SNAPSHOTS")) CHECK(view.snapshot().save(QString::fromUtf8(path) + "/terminal-settings-selected.png"));
        press(view, Qt::Key_Escape, 9);
        press(view, Qt::Key_N, 57, Qt::NoModifier, "n"); press(view, Qt::Key_I, 31, Qt::NoModifier, "i");
        if (const char *path = std::getenv("INKLINE_TEST_SNAPSHOTS")) CHECK(view.snapshot().save(QString::fromUtf8(path) + "/terminal-input-method.png"));
        // Return to direct input for the following shell test.
        alt(view, Qt::Key_Space, 65);
        for (int n = 0; n < 3; ++n) press(view, Qt::Key_Tab, 23);
        press(view, Qt::Key_Return, 36);
        for (int n = 0; n < 3; ++n) press(view, Qt::Key_Up, 111);
        press(view, Qt::Key_Return, 36); press(view, Qt::Key_Escape, 9);
        CHECK(view.input_method() == 0);
        pump(800);
        // The preference file is unaffected by ordinary typing and navigation.
        QFile file(settings); CHECK(file.open(QIODevice::ReadOnly)); const auto saved = file.readAll(); file.close();
        for (int n = 0; n < 50; ++n) press(view, Qt::Key_A, 38, Qt::NoModifier, "a");
        CHECK(file.open(QIODevice::ReadOnly)); CHECK(file.readAll() == saved);
    }
    {
        QQuickWindow window;
        const std::vector<std::string> shell = {"/bin/sh", "-c", "stty -echo; printf '\033[2J\033[H'; while IFS= read -r line; do printf '%s\\r\\n' \"$line\"; done"};
        rmt::TerminalView view(window.contentItem(), 24, false, settings, shell);
        view.setSize(QSizeF(1000, 750)); view.layout(); view.start(); pump(100);
        CHECK(view.bottom_bar()); // Saved preference survives a new application view.
        view.send_text("first-shell\n"); pump(150); const auto first = view.snapshot();
        view.select_terminal(1); pump(100); view.send_text("second-shell\n"); pump(150);
        const auto second = view.snapshot(); CHECK(second != first);
        view.select_terminal(0); CHECK(view.snapshot() == first);
        view.send_text("background-one\n"); view.select_terminal(1); pump(150);
        CHECK(view.snapshot() == second); // Background output stays in its own terminal.
        view.select_terminal(0); CHECK(view.snapshot() != first);
        view.select_terminal(1); view.send_text("\004"); pump(500);
        CHECK(view.terminal_count() == 1 && view.active_terminal() == 0);
    }
    {
        // Finger selection -> Option+C -> another PTY -> Option+V.
        QQuickWindow window;
        const std::vector<std::string> shell = {"/bin/sh", "-c", "stty -echo; printf '\\033[2J\\033[Hhello world\\r\\n'; while IFS= read -r line; do printf '%s\\r\\n' \"$line\"; done"};
        rmt::TerminalView view(window.contentItem(), 24, false, settings, shell);
        view.setSize(QSizeF(1000, 750)); view.layout(); view.start(); pump(150);
        QFont font; font.setFamilies({"Noto Mono", "Noto Sans Mono CJK SC"}); font.setPixelSize(24);
        const int cw = QFontMetrics(font).horizontalAdvance('M');
        using S = QEventPoint::State;
        touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 13, 15)});
        touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 13 + 4 * cw, 15)});
        touch(view, QEvent::TouchEnd, {point(view, 1, S::Released, 13 + 4 * cw, 15)});
        alt(view, Qt::Key_C, 54); CHECK(view.clipboard_text() == "hello");
        view.select_terminal(1); pump(150); const auto before = view.snapshot();
        alt(view, Qt::Key_V, 55); view.send_text("\n"); pump(150);
        CHECK(view.snapshot() != before); CHECK(view.clipboard_text() == "hello");
        CHECK(view.terminal_count() == 2);
    }
    std::puts("View: six slots, independent PTYs, background output, saved settings, hidden bar and safe quit confirmation passed.");
}
