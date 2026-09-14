#include "rmt/input.hpp"
#include "rmt/keyboard.hpp"
#include "rmt/input_method.hpp"
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
        const auto opt_down = event(QEvent::KeyPress, Qt::Key_Meta, 115, Qt::MetaModifier);
        const auto opt_up = event(QEvent::KeyRelease, Qt::Key_Meta, 115);
        (void)encode(opt_down);
        const char *expected[] = {"\033OP", "\033OQ", "\033OR", "\033OS", "\033[15~", "\033[17~", "\033[18~", "\033[19~", "\033[20~", "\033[21~"};
        for (int i = 0; i < 10; ++i) {
            const int key = i == 9 ? Qt::Key_0 : Qt::Key_1 + i;
            // The firmware reports Opt+number with Meta and an empty text field.
            CHECK(encode(event(QEvent::KeyPress, key, 10 + i, Qt::MetaModifier)) == expected[i]);
            CHECK(encode(event(QEvent::KeyPress, key, 10 + i, Qt::MetaModifier, {}, true)) == expected[i]);
            (void)encode(event(QEvent::KeyRelease, key, 10 + i, Qt::MetaModifier));
        }
        // Shifted punctuation is still the physical number row.
        const auto shifted = event(QEvent::KeyPress, Qt::Key_Exclam, 10, Qt::MetaModifier | Qt::ShiftModifier, "!");
        QKeyEvent real_shift_f1(QEvent::KeyPress, Qt::Key_F1, Qt::ShiftModifier);
        CHECK(encode(shifted) == reference.encode(real_shift_f1));
        (void)encode(event(QEvent::KeyRelease, Qt::Key_Exclam, 10));
        QKeyEvent real_ctrl_f10(QEvent::KeyPress, Qt::Key_F10, Qt::ControlModifier);
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_0, 19, Qt::MetaModifier | Qt::ControlModifier)) == reference.encode(real_ctrl_f10));
        (void)encode(event(QEvent::KeyRelease, Qt::Key_0, 19));
        // USB keypad input and non-number Meta shortcuts keep their usual encoding.
        for (const auto &key : {event(QEvent::KeyPress, Qt::Key_1, 87, Qt::KeypadModifier | Qt::MetaModifier, "1"),
                               event(QEvent::KeyPress, Qt::Key_A, 38, Qt::MetaModifier, "a"),
                               event(QEvent::KeyPress, Qt::Key_F1, 67, Qt::MetaModifier)}) {
            CHECK(encode(key) == reference.encode(key));
            (void)encode(event(QEvent::KeyRelease, key.key(), key.nativeScanCode(), key.modifiers()));
        }
        (void)encode(opt_up);
        (void)encode(alt_down);
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
        // Local clipboard actions fire once.
        for (const auto &item : std::vector<std::pair<int, rmt::InputAction>>{{Qt::Key_C, rmt::InputAction::Copy}, {Qt::Key_V, rmt::InputAction::Paste}, {Qt::Key_X, rmt::InputAction::Cut}}) {
            CHECK(map.map(event(QEvent::KeyPress, item.first, 0, Qt::AltModifier), 0).action == item.second);
            const auto repeat = map.map(event(QEvent::KeyPress, item.first, 0, Qt::AltModifier, {}, true), 0).action;
            CHECK(repeat == rmt::InputAction::Ignore);
            CHECK(map.map(event(QEvent::KeyRelease, item.first, 0), 0).action == rmt::InputAction::Ignore);
        }
        // Punctuation is no longer intercepted for zoom, including repeats.
        for (const auto code : {Qt::Key_Minus, Qt::Key_Underscore, Qt::Key_Plus, Qt::Key_Equal}) {
            for (quint32 scan : {20u, 21u}) {
                CHECK(map.map(event(QEvent::KeyPress, code, scan, Qt::AltModifier), 0).action == InputAction::Send);
                CHECK(map.map(event(QEvent::KeyPress, code, scan, Qt::AltModifier, {}, true), 0).action == InputAction::Send);
                CHECK(map.map(event(QEvent::KeyRelease, code, scan), 0).action == InputAction::Send);
            }
        }
        // Firmware 3.27's US Folio map consumes AltGr for this printable key.
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_Equal, 21, Qt::NoModifier, "=")) == "=");
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_Equal, 21, Qt::NoModifier, "=", true)) == "=");
        (void)encode(event(QEvent::KeyRelease, Qt::Key_Equal, 21, Qt::NoModifier, "="));
        // Right Alt/Opt+0 is a literal plus, not F10; other number keys pass through.
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_Plus, 19, Qt::NoModifier, "+")) == "+");
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_Plus, 19, Qt::NoModifier, "+", true)) == "+");
        (void)encode(event(QEvent::KeyRelease, Qt::Key_Plus, 19, Qt::NoModifier, "+"));
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_1, 10, Qt::NoModifier, "1")) == "1");
        (void)encode(event(QEvent::KeyRelease, Qt::Key_1, 10));
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
        for (const auto code : {Qt::Key_Minus, Qt::Key_Underscore, Qt::Key_Plus, Qt::Key_Equal}) {
            const auto down = event(QEvent::KeyPress, code, 21, Qt::AltModifier, QString(QChar(ushort(code))));
            CHECK(encode(down) == reference.encode(down));
            (void)encode(event(QEvent::KeyRelease, code, 21, Qt::AltModifier));
        }
        (void)encode(opt_down);
        QKeyEvent real_alt_f1(QEvent::KeyPress, Qt::Key_F1, Qt::AltModifier);
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_1, 10, Qt::AltModifier | Qt::MetaModifier)) == reference.encode(real_alt_f1));
        (void)map.reset();
        // Do not change a held digit into a function key when Opt arrives later.
        CHECK(encode(event(QEvent::KeyPress, Qt::Key_1, 10, Qt::NoModifier, "1")) == "1");
        (void)encode(opt_down);
        CHECK(map.map(event(QEvent::KeyPress, Qt::Key_1, 10, Qt::MetaModifier, "1", true), 0).key == Qt::Key_1);
        (void)map.reset();
        // Kitty release reports retain their F-key identity and original session.
        ghostty_terminal_vt_write(rmt_core_terminal(core), reinterpret_cast<const uint8_t *>("\033[>31u"), 6);
        (void)encode(opt_down);
        const auto down = event(QEvent::KeyPress, Qt::Key_1, 10, Qt::MetaModifier);
        QKeyEvent real_f1(QEvent::KeyPress, Qt::Key_F1, Qt::NoModifier);
        CHECK(encode(down, 2) == reference.encode(real_f1));
        (void)encode(opt_up, 4);
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
    {
        rmt::InputMapper map;
        rmt::InputMethod im;
        rmt::Keyboard encoder(*core);
        auto pipeline = [&](const QKeyEvent &e) {
            const auto input = map.map(e, 0); CHECK(input.action == InputAction::Send);
            const auto result = im.key(input);
            std::string bytes(result.commit.constData(), size_t(result.commit.size()));
            if (!result.consumed) bytes += encoder.encode(input.event(), input.caps_locked);
            return bytes;
        };
        auto tap = [&](int code, quint32 scan, Qt::KeyboardModifiers mods, const QString &text) {
            auto bytes = pipeline(event(QEvent::KeyPress, code, scan, mods, text));
            bytes += pipeline(event(QEvent::KeyRelease, code, scan, mods, text));
            return bytes;
        };
        // Actual Qt events from the tablet: Shift+6 produces a combining mark.
        auto bytes = tap(Qt::Key_C, 54, Qt::NoModifier, "c");
        bytes += tap(Qt::Key_Dead_Circumflex, 15, Qt::ShiftModifier, QString(QChar(0x0302)));
        bytes += tap(Qt::Key_2, 11, Qt::NoModifier, "2");
        CHECK(bytes == "c^2" && im.method() == rmt::InputMethod::Off);
        CHECK(tap(Qt::Key_Dead_Tilde, 51, Qt::NoModifier, QString(QChar(0x0303))) == "~");
        CHECK(tap(Qt::Key_Dead_Diaeresis, 51, Qt::ShiftModifier, QString(QChar(0x0308))) == "\"");
        CHECK(tap(Qt::Key_Dead_Grave, 49, Qt::NoModifier, QString(QChar(0x0300))) == "`");
        CHECK(tap(Qt::Key_Dead_Acute, 48, Qt::NoModifier, QString(QChar(0x0301))) == "'");
        // Normal Unicode text is not stripped of accents.
        CHECK(tap(Qt::Key_Eacute, 26, Qt::NoModifier, QString::fromUtf8("é")) == "é");
        CHECK(tap(Qt::Key_E, 26, Qt::NoModifier, QString::fromUtf8("e\u0302")) == "e\u0302");
        im.set_method(rmt::InputMethod::USInternational);
        CHECK(tap(Qt::Key_Dead_Circumflex, 15, Qt::ShiftModifier, QString(QChar(0x0302))).empty());
        CHECK(tap(Qt::Key_A, 38, Qt::NoModifier, "a") == "â");
        im.set_method(rmt::InputMethod::Off);
        CHECK(pipeline(event(QEvent::KeyPress, Qt::Key_Dead_Circumflex, 15, Qt::ShiftModifier, QString(QChar(0x0302)))) == "^");
        CHECK(pipeline(event(QEvent::KeyPress, Qt::Key_Dead_Circumflex, 15, Qt::ShiftModifier, QString(QChar(0x0302)), true)) == "^");
        (void)pipeline(event(QEvent::KeyRelease, Qt::Key_6, 15, Qt::NoModifier, "6"));
    }
    rmt_core_free(core);
    std::puts("Input: Folio/USB function keys, navigation, modifiers, repeat/release, session routing and Caps Lock modes passed.");
}
