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
        QCoreApplication::sendEvent(&view, &click); CHECK(view.settings_open());
        press(view, Qt::Key_Escape, 9);
        alt(view, Qt::Key_Backspace, 22); CHECK(view.quit_confirmation_open());
        if (const char *path = std::getenv("INKLINE_TEST_SNAPSHOTS")) CHECK(view.snapshot().save(QString::fromUtf8(path) + "/terminal-quit.png"));
        press(view, Qt::Key_Return, 36); CHECK(!view.quit_confirmation_open()); // Cancel is the default.
        alt(view, Qt::Key_Backspace, 22); CHECK(view.quit_confirmation_open());
        press(view, Qt::Key_Escape, 9); CHECK(!view.quit_confirmation_open());
        CHECK(view.terminal_count() == 6);
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
    std::puts("View: six slots, independent PTYs, background output, saved settings, hidden bar and safe quit confirmation passed.");
}
