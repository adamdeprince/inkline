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
        // Darkness and minimum contrast stay in RAM through finger lifts,
        // Settings exit and shutdown.
        const auto path = temporary.filePath("darkness.ini");
        QQuickWindow window; window.resize(1000, 750);
        rmt::TerminalView view(window.contentItem(), 24, true, path);
        view.setSize(QSizeF(1000, 750)); view.layout(); view.start(); view.settings();
        CHECK(view.text_darkness() == 50 && view.minimum_contrast() == 35 && view.update_profile() == 0);
        for (int n = 0; n < 4; ++n) press(view, Qt::Key_Tab);
        press(view, Qt::Key_Right); CHECK(view.update_profile() == 1);
        press(view, Qt::Key_Tab); press(view, Qt::Key_Right); CHECK(view.text_darkness() == 55);
        press(view, Qt::Key_Tab); press(view, Qt::Key_Right);
        CHECK(view.minimum_contrast() == 40);
        pump(800); CHECK(!QFile::exists(path));
        using S = QEventPoint::State;
        for (int rotation : {0, 90, 270}) {
            view.setRotation(rotation);
            touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 202, 530)});
            touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 798, 530)});
            CHECK(view.text_darkness() == 100);
            pump(800); CHECK(!QFile::exists(path));
            touch(view, QEvent::TouchCancel, {});
            CHECK(view.text_darkness() == 55);
        }
        view.setRotation(0);
        touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 202, 530)});
        touch(view, QEvent::TouchEnd, {point(view, 1, S::Released, 798, 530)});
        CHECK(view.text_darkness() == 100 && !QFile::exists(path));
        touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 202, 610)});
        touch(view, QEvent::TouchEnd, {point(view, 1, S::Released, 798, 610)});
        CHECK(view.minimum_contrast() == 100 && !QFile::exists(path));
        view.settings(); view.select_terminal(1);
        CHECK(view.text_darkness() == 100 && view.minimum_contrast() == 100 && view.update_profile() == 1 && view.terminal_count() == 2);
        view.settings(); CHECK(view.text_darkness() == 100 && view.minimum_contrast() == 100 && view.update_profile() == 1);
        press(view, Qt::Key_Escape); pump(800); CHECK(!QFile::exists(path));
        rmt::TerminalView restored(window.contentItem(), 0, true, path);
        CHECK(restored.text_darkness() == 50 && restored.minimum_contrast() == 35 && restored.update_profile() == 0);
    }
    CHECK(!QFile::exists(temporary.filePath("darkness.ini")));
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
        // Sideways two-finger swipes switch once, even with more motion.
        using S = QEventPoint::State;
        for (int rotation : {0, 90, 270}) {
            view.setRotation(rotation);
            touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 400, 300), point(view, 2, S::Pressed, 600, 300)});
            touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 310, 301), point(view, 2, S::Updated, 510, 301)});
            CHECK(view.active_terminal() == 1 && view.font_pixels() == 24);
            touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 150, 301), point(view, 2, S::Updated, 350, 301)});
            CHECK(view.active_terminal() == 1);
            touch(view, QEvent::TouchEnd, {point(view, 1, S::Released, 150, 301), point(view, 2, S::Released, 350, 301)});
            touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 400, 300), point(view, 2, S::Pressed, 600, 300)});
            touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 490, 300), point(view, 2, S::Updated, 690, 300)});
            touch(view, QEvent::TouchEnd, {});
            CHECK(view.active_terminal() == 0 && view.terminal_count() == 6);
        }
        view.setRotation(0);
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
        // Neither Alt key intercepts punctuation to resize the terminal.
        for (const auto modifier : {Qt::Key_Alt, Qt::Key_AltGr}) {
            const quint32 scan = modifier == Qt::Key_Alt ? 64 : 108;
            key(view, QEvent::KeyPress, modifier, scan, Qt::AltModifier);
            for (const auto symbol : {Qt::Key_Equal, Qt::Key_Plus, Qt::Key_Minus, Qt::Key_Underscore})
                press(view, symbol, 21, Qt::AltModifier, QString(QChar(ushort(symbol))));
            key(view, QEvent::KeyRelease, modifier, scan);
            CHECK(view.font_pixels() == 24);
        }
        pump(800); CHECK(rmt::Preferences(settings).font_pixels() == 26); // default, no font write
        CHECK(view.terminal_count() == 6);
        // Pinch coordinates must work under the tablet's rotated view as well.
        view.setRotation(90);
        touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 400, 300)});
        touch(view, QEvent::TouchUpdate, {point(view, 1, S::Stationary, 400, 300), point(view, 2, S::Pressed, 600, 300)});
        touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 390, 300), point(view, 2, S::Updated, 610, 300)});
        CHECK(view.font_pixels() == 25); // fine adjustment by a single pixel
        touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 350, 300), point(view, 2, S::Updated, 650, 300)});
        CHECK(view.font_pixels() == 29); CHECK(rmt::Preferences(settings).font_pixels() == 26);
        touch(view, QEvent::TouchEnd, {point(view, 1, S::Released, 350, 300), point(view, 2, S::Released, 650, 300)});
        CHECK(rmt::Preferences(settings).font_pixels() == 29);
        CHECK(view.terminal_count() == 6); // resizing preserves all PTYs
        touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 400, 300), point(view, 2, S::Pressed, 600, 300)});
        touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 480, 300), point(view, 2, S::Updated, 520, 300)});
        CHECK(view.font_pixels() == 13);
        touch(view, QEvent::TouchCancel, {}); CHECK(view.font_pixels() == 29);
        CHECK(rmt::Preferences(settings).font_pixels() == 29);
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
        const auto tiny_settings = temporary.filePath("tiny.ini");
        QQuickWindow window;
        rmt::TerminalView view(window.contentItem(), 24, true, tiny_settings);
        view.setSize(QSizeF(1000, 750)); view.layout(); view.start();
        using S = QEventPoint::State;
        touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 400, 300), point(view, 2, S::Pressed, 600, 300)});
        touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 496, 300), point(view, 2, S::Updated, 504, 300)});
        touch(view, QEvent::TouchEnd, {});
        CHECK(view.font_pixels() == 6 && rmt::Preferences(tiny_settings).font_pixels() == 6);
        alt(view, Qt::Key_Minus, 21); CHECK(view.font_pixels() == 6);
        alt(view, Qt::Key_Plus, 20); CHECK(view.font_pixels() == 6);
        alt(view, Qt::Key_Minus, 21); CHECK(view.font_pixels() == 6);
        CHECK(!view.snapshot().isNull() && view.terminal_count() == 1);
        pump(800); CHECK(rmt::Preferences(tiny_settings).font_pixels() == 6);
        rmt::TerminalView restored(window.contentItem(), 0, true, tiny_settings);
        CHECK(restored.font_pixels() == 6);
        for (int n = 0; n < 2; ++n) {
            touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 480, 300), point(view, 2, S::Pressed, 520, 300)});
            touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 100, 300), point(view, 2, S::Updated, 900, 300)});
            touch(view, QEvent::TouchEnd, {});
        }
        CHECK(view.font_pixels() == 48 && rmt::Preferences(tiny_settings).font_pixels() == 48);
    }
    {
        // Two fingers drag history naturally; changing their spacing after
        // scrolling starts must not resize text or change terminals.
        QQuickWindow window;
        const auto scroll_settings = temporary.filePath("scroll.ini");
        const std::vector<std::string> shell = {"/bin/sh", "-c", "stty -echo; i=0; while [ $i -lt 600 ]; do printf 'line %04d\\r\\n' $i; i=$((i+1)); done; while read line; do :; done"};
        rmt::TerminalView view(window.contentItem(), 24, false, scroll_settings, shell);
        view.setSize(QSizeF(1000, 750)); view.layout(); view.start(); pump(800);
        const auto bottom = view.snapshot();
        using S = QEventPoint::State;
        for (int rotation : {0, 90, 270}) {
            view.setRotation(rotation);
            touch(view, QEvent::TouchBegin, {point(view, 1, S::Pressed, 400, 300)});
            touch(view, QEvent::TouchUpdate, {point(view, 1, S::Stationary, 400, 300), point(view, 2, S::Pressed, 600, 300)});
            touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 398, 450), point(view, 2, S::Updated, 602, 450)});
            CHECK(view.snapshot() != bottom && view.font_pixels() == 24);
            touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 330, 480), point(view, 2, S::Updated, 670, 480)});
            CHECK(view.font_pixels() == 24 && view.active_terminal() == 0);
            touch(view, QEvent::TouchUpdate, {point(view, 1, S::Updated, 400, 300), point(view, 2, S::Updated, 600, 300)});
            touch(view, QEvent::TouchEnd, {});
            CHECK(view.snapshot() == bottom);
        }
        CHECK(!QFile::exists(scroll_settings)); // Scroll gestures do not save preferences.
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
        // Firmware Folio events reach a real shell as + and F10, respectively.
        QQuickWindow window;
        const std::vector<std::string> shell = {"/bin/sh", "-c",
            "stty -echo; printf '\\033]52;c;cmVhZHk=\\007'; "
            "IFS= read -r line; [ \"$line\" = '+' ] || exit 1; "
            "printf '\\033]52;c;cGx1cw==\\007'; "
            "IFS= read -r line; [ \"$line\" = \"$(printf '\\033[21~')\" ] || exit 1; "
            "printf '\\033]52;c;ZjEw\\007'; IFS= read -r line"};
        rmt::TerminalView view(window.contentItem(), 24, false, temporary.filePath("folio.ini"), shell);
        view.setSize(QSizeF(1000, 750)); view.layout(); view.start(); pump(200);
        CHECK(view.clipboard_text() == "ready");
        key(view, QEvent::KeyPress, Qt::Key_AltGr, 108, Qt::GroupSwitchModifier);
        press(view, Qt::Key_Plus, 19, Qt::NoModifier, "+");
        key(view, QEvent::KeyRelease, Qt::Key_AltGr, 108);
        view.send_text("\n"); pump(200); CHECK(view.clipboard_text() == "plus");
        key(view, QEvent::KeyPress, Qt::Key_Meta, 115, Qt::MetaModifier);
        press(view, Qt::Key_0, 19, Qt::MetaModifier);
        key(view, QEvent::KeyRelease, Qt::Key_Meta, 115);
        view.send_text("\n"); pump(200); CHECK(view.clipboard_text() == "f10");
        CHECK(view.font_pixels() == 24 && view.terminal_count() == 1);
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
