// Exercise the production evdev state machine without launching or stopping
// services. This is safe beside real Inkline sessions on the tablet.
#define main inkline_hotkey_main
#include "../src/hotkey.cpp"
#undef main
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
int main() {
    const std::map<unsigned, rmt::hotkeys::Binding> bindings{{KEY_E, {KEY_E, "e", {"emacs"}, "terminal"}}};
    for (const bool folio : {false, true}) {
        Keyboard keyboard{"test", -1, rmt::ShortcutChord(folio)};
        auto key = [&](unsigned code, int value) {
            input_event event{}; event.type = EV_KEY; event.code = code; event.value = value;
            return keyboard.event(event, bindings, false).type;
        };
        key(KEY_LEFTCTRL, 1); key(KEY_LEFTALT, 1);
        CHECK(key(KEY_T, 1) == Trigger::None); key(KEY_T, 0);
        CHECK(key(KEY_E, 1) == Trigger::None); key(KEY_E, 0);
        key(KEY_BACKSPACE, 1);
        CHECK(!keyboard.emergency_due(std::chrono::steady_clock::now() + std::chrono::seconds(5)));
        key(folio ? KEY_END : KEY_LEFTMETA, 1);
        CHECK(key(KEY_T, 1) == Trigger::Terminal);
        CHECK(key(KEY_T, 2) == Trigger::None);
        CHECK(key(KEY_T, 0) == Trigger::None);
        CHECK(key(KEY_E, 1) == Trigger::Binding);
        CHECK(!keyboard.emergency_due(keyboard.emergency_since + std::chrono::milliseconds(1999)));
        CHECK(keyboard.emergency_due(keyboard.emergency_since + std::chrono::seconds(2)));
        CHECK(!keyboard.emergency_due(keyboard.emergency_since + std::chrono::seconds(5)));
        key(folio ? KEY_END : KEY_LEFTMETA, 0);
        CHECK(!keyboard.emergency_active);
        key(folio ? KEY_END : KEY_LEFTMETA, 1);
        input_event dropped{}; dropped.type = EV_SYN; dropped.code = SYN_DROPPED;
        keyboard.event(dropped, bindings, false);
        CHECK(!keyboard.emergency_due(std::chrono::steady_clock::now() + std::chrono::seconds(5)));
        CHECK(key(KEY_T, 1) == Trigger::None);
        dropped.code = SYN_REPORT; keyboard.event(dropped, bindings, false);
        CHECK(!keyboard.chord.active() && !keyboard.emergency_active);
        CHECK(key(KEY_T, 1) == Trigger::None);
    }
    std::puts("Evdev: triple chord, Emacs passthrough, repeat suppression, held recovery and dropped events passed.");
}
