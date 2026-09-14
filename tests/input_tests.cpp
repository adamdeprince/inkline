#include "rmt/input.hpp"
#include "rmt/keyboard.hpp"
#include <QGuiApplication>
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
using rmt::InputAction;
QKeyEvent event(QEvent::Type type, int key, quint32 scan, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString &text = {}, bool repeat = false) {
    return QKeyEvent(type, key, mods, scan, 0, 0, text, repeat);
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    auto *core = rmt_core_new(80, 24, nullptr); CHECK(core);
    {
        rmt::Keyboard encoder(*core), reference(*core);
        rmt::InputMapper map;
        auto encode = [&](const QKeyEvent &e, int terminal = 0) {
            const auto input = map.map(e, terminal); CHECK(input.action == InputAction::Send);
            auto output = input.event(); return encoder.encode(output, input.caps_locked);
        };
        const auto alt_down = event(QEvent::KeyPress, Qt::Key_AltGr, 108, Qt::GroupSwitchModifier);
        const auto alt_up = event(QEvent::KeyRelease, Qt::Key_AltGr, 108);
        (void)encode(alt_down);
        const char *expected[] = {"\033OP", "\033OQ", "\033OR", "\033OS", "\033[15~", "\033[17~", "\033[18~", "\033[19~", "\033[20~", "\033[21~"};
        for (int i = 0; i < 10; ++i) {
            const int key = i == 9 ? Qt::Key_0 : Qt::Key_1 + i;
            CHECK(encode(event(QEvent::KeyPress, key, 10 + i, Qt::AltModifier, QString(QChar(key)))) == expected[i]);
            CHECK(encode(event(QEvent::KeyPress, key, 10 + i, Qt::AltModifier, QString(QChar(key)), true)) == expected[i]);
            (void)encode(event(QEvent::KeyRelease, key, 10 + i, Qt::AltModifier));
        }
        // Shifted punctuation is still the physical number row.
        const auto shifted = event(QEvent::KeyPress, Qt::Key_Exclam, 10, Qt::AltModifier | Qt::ShiftModifier, "!");
        QKeyEvent real_shift_f1(QEvent::KeyPress, Qt::Key_F1, Qt::ShiftModifier);
        CHECK(encode(shifted) == reference.encode(real_shift_f1));
        (void)encode(event(QEvent::KeyRelease, Qt::Key_Exclam, 10));
        QKeyEvent real_ctrl_f10(QEvent::KeyPress, Qt::Key_F10, Qt::ControlModifier);
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_0, 19, Qt::AltModifier | Qt::ControlModifier, "0")) == reference.encode(real_ctrl_f10));
        (void)encode(event(QEvent::KeyRelease, Qt::Key_0, 19));
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_Tab, 23, Qt::AltModifier, "\t")) == "\033");
        (void)encode(event(QEvent::KeyRelease, Qt::Key_Tab, 23));
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_Up, 111, Qt::AltModifier)) == "\033[5~");
        (void)encode(event(QEvent::KeyRelease, Qt::Key_Up, 111));
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_Down, 116, Qt::AltModifier)) == "\033[6~");
        (void)encode(event(QEvent::KeyRelease, Qt::Key_Down, 116));
        for (const auto item : {std::pair<int, InputAction>{Qt::Key_Left, InputAction::Previous},
                               {Qt::Key_Right, InputAction::Next}, {Qt::Key_Space, InputAction::Settings}, {Qt::Key_Backspace, InputAction::Quit}}) {
            CHECK(map.map(event(QEvent::KeyPress, item.first, 0, Qt::AltModifier), 0).action == item.second);
            CHECK(map.map(event(QEvent::KeyPress, item.first, 0, Qt::AltModifier, {}, true), 0).action == InputAction::Ignore);
            CHECK(map.map(event(QEvent::KeyRelease, item.first, 0), 0).action == InputAction::Ignore);
        }
        // A USB numeric keypad keeps its usual Alt+digit behavior.
        const auto keypad = event(QEvent::KeyPress, Qt::Key_1, 87, Qt::KeypadModifier | Qt::AltModifier, "1");
        CHECK(encode(keypad) == reference.encode(keypad));
        (void)encode(event(QEvent::KeyRelease, Qt::Key_1, 87));
        (void)encode(alt_up);
        (void)encode(event(QEvent::KeyPress, Qt::Key_Alt, 64, Qt::AltModifier));
        const auto left_digit = event(QEvent::KeyPress, Qt::Key_1, 10, Qt::AltModifier, "1");
        const auto left_bytes = encode(left_digit);
        CHECK(left_bytes == "\0331");
        (void)encode(event(QEvent::KeyRelease, Qt::Key_1, 10));
        (void)encode(alt_down);
        QKeyEvent real_alt_f1(QEvent::KeyPress, Qt::Key_F1, Qt::AltModifier);
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_1, 10, Qt::AltModifier, "1")) == reference.encode(real_alt_f1));
        (void)map.reset();
        // Do not change a held digit into a function key when Alt arrives later.
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_1, 10, Qt::NoModifier, "1")) == "1");
        (void)encode(alt_down);
        CHECK(map.map(event(QEvent::KeyPress, Qt::Key_1, 10, Qt::AltModifier, "1", true), 0).key == Qt::Key_1);
        (void)map.reset();
        // Kitty release reports retain their F-key identity and original session.
        ghostty_terminal_vt_write(rmt_core_terminal(core), reinterpret_cast<const uint8_t *>("\033[>31u"), 6);
        (void)encode(alt_down);
        const auto down = event(QEvent::KeyPress, Qt::Key_1, 10, Qt::AltModifier, "1");
        QKeyEvent real_f1(QEvent::KeyPress, Qt::Key_F1, Qt::NoModifier);
        CHECK(encode(down, 2) == reference.encode(real_f1));
        (void)encode(alt_up, 4);
        const auto released = map.map(event(QEvent::KeyRelease, Qt::Key_1, 10), 4);
        CHECK(released.terminal == 2 && released.key == Qt::Key_F1);
        QKeyEvent real_release(QEvent::KeyRelease, Qt::Key_F1, Qt::NoModifier);
        auto mapped_release = released.event();
        CHECK(encoder.encode(mapped_release) == reference.encode(real_release));
        CHECK(map.map(event(QEvent::KeyRelease, Qt::Key_1, 10), 4).action == InputAction::Ignore);
        ghostty_terminal_vt_write(rmt_core_terminal(core), reinterpret_cast<const uint8_t *>("\033[<u"), 4);
        (void)map.reset();
        // Caps-as-Control must not leave later text affected by Qt's native lock.
        CHECK(map.caps_control());
        (void)encode(event(QEvent::KeyPress, Qt::Key_CapsLock, 66));
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_C, 54, Qt::NoModifier, "C")) == "\003");
        (void)encode(event(QEvent::KeyRelease, Qt::Key_C, 54));
        (void)encode(event(QEvent::KeyRelease, Qt::Key_CapsLock, 66));
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_A, 38, Qt::NoModifier, "A")) == "a");
        (void)encode(event(QEvent::KeyRelease, Qt::Key_A, 38));
        map.set_caps_control(false);
        (void)encode(event(QEvent::KeyPress, Qt::Key_CapsLock, 66));
        (void)encode(event(QEvent::KeyRelease, Qt::Key_CapsLock, 66));
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_A, 38, Qt::NoModifier, "a")) == "A");
        (void)encode(event(QEvent::KeyRelease, Qt::Key_A, 38));
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_A, 38, Qt::ShiftModifier, "A")) == "a");
        (void)encode(event(QEvent::KeyRelease, Qt::Key_A, 38));
        map.set_caps_control(true);
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_Eacute, 26, Qt::NoModifier, QString::fromUtf8("É"))) == "é");
        const auto reset = map.reset(); CHECK(!reset.empty());
        CHECK(map.map(event(QEvent::KeyRelease, Qt::Key_Eacute, 26), 0).action == InputAction::Ignore);
    }
    rmt_core_free(core);
    std::puts("Input: Folio/USB function keys, navigation, modifiers, repeat/release, session routing and Caps Lock modes passed.");
}
