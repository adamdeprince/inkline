#include "rmt/input.hpp"
#include <QFile>

namespace rmt {
namespace {
// Qt's Linux keyboard backends report evdev codes plus eight. The Folio and
// USB keyboards therefore share this mapping, even with shifted number keys.
bool right_alt(const QKeyEvent &e) {
    return e.nativeScanCode() == 108 || (!e.nativeScanCode() && e.key() == Qt::Key_AltGr);
}
bool caps(const QKeyEvent &e) { return e.nativeScanCode() == 66 || e.key() == Qt::Key_CapsLock; }
quint32 identity(const QKeyEvent &e) {
    return e.nativeScanCode() ? e.nativeScanCode() : quint32(e.key()) | 0x80000000u;
}
int number_function_key(const QKeyEvent &e) {
    if (!(e.modifiers() & Qt::KeypadModifier)) {
        const auto scan = e.nativeScanCode();
        if (scan >= 10 && scan <= 19) return Qt::Key_F1 + int(scan - 10);
        if (!scan && e.key() >= Qt::Key_1 && e.key() <= Qt::Key_9) return Qt::Key_F1 + e.key() - Qt::Key_1;
        if (!scan && e.key() == Qt::Key_0) return Qt::Key_F10;
    }
    return 0;
}
int number_terminal(const QKeyEvent &e) {
    if (e.modifiers() & Qt::KeypadModifier) return -1;
    const auto scan = e.nativeScanCode();
    if (scan >= 10 && scan <= 18) return int(scan - 10);
    if (!scan && e.key() >= Qt::Key_1 && e.key() <= Qt::Key_9) return e.key() - Qt::Key_1;
    return -1;
}
int physical_key(const QKeyEvent &e) {
    switch (e.nativeScanCode()) {
    case 23: return Qt::Key_Tab;
    case 22: return Qt::Key_Backspace;
    case 65: return Qt::Key_Space;
    case 111: return Qt::Key_Up;
    case 116: return Qt::Key_Down;
    case 113: return Qt::Key_Left;
    case 114: return Qt::Key_Right;
    default: return e.key() == Qt::Key_Backtab ? Qt::Key_Tab : e.key();
    }
}
int literal_accent(const QKeyEvent &e) {
    // The e-paper backend sends standalone combining marks for printed ASCII
    // accents (e.g. Shift+6 is U+0302). Let Inkline's selected input method
    // handle composition; direct input must type a caret instead of modifying
    // the preceding cell. Text committed by an IME or pasted is not changed.
    if (e.text().size() != 1) return 0;
    const auto mark = e.text().at(0).unicode();
    switch (e.key()) {
    case Qt::Key_Dead_Grave: return mark == 0x0300 ? Qt::Key_QuoteLeft : 0;
    case Qt::Key_Dead_Acute: return mark == 0x0301 ? Qt::Key_Apostrophe : 0;
    case Qt::Key_Dead_Circumflex: return mark == 0x0302 ? Qt::Key_AsciiCircum : 0;
    case Qt::Key_Dead_Tilde: return mark == 0x0303 ? Qt::Key_AsciiTilde : 0;
    case Qt::Key_Dead_Diaeresis: return mark == 0x0308 ? Qt::Key_QuoteDbl : 0;
    default: return 0;
    }
}
bool global_shortcut(const QKeyEvent &e) {
    if (e.key() == Qt::Key_T || e.key() == Qt::Key_Backspace || e.nativeScanCode() == 28 || e.nativeScanCode() == 22) return true;
    // This file lives in tmpfs and contains only evdev key numbers. Reading it
    // on a Ctrl+Opt+Alt press makes registration changes immediate without polling.
    if (e.nativeScanCode() < 8) return false;
    QFile file("/run/inkline-shortcuts/keys");
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto wanted = QByteArray::number(e.nativeScanCode() - 8);
    for (const auto &line : file.read(512).split('\n')) if (line == wanted) return true;
    return false;
}
}

void InputMapper::set_caps_control(bool enabled) {
    caps_control_ = enabled;
    caps_locked_ = false;
}

MappedInput InputMapper::map(const QKeyEvent &e, int terminal) {
    const bool release = e.type() == QEvent::KeyRelease;
    // Qt synthesizes release events between repeats. They are not releases.
    if (release && e.isAutoRepeat()) return {};
    const auto id = identity(e);
    if (!e.isAutoRepeat()) {
        if (right_alt(e)) right_alt_ = !release;
        else if (e.key() == Qt::Key_Alt) left_alt_ = !release;
        if (caps(e)) {
            if (release) caps_held_control_ = false;
            else {
                caps_held_control_ = caps_control_;
                if (!caps_control_) caps_locked_ = !caps_locked_;
            }
        }
    }
    auto mods = e.modifiers();
    if (caps_held_control_) mods |= Qt::ControlModifier;
    Held held;
    const auto previous = held_.constFind(id);
    if (release && previous == held_.cend()) return {};
    if (previous != held_.cend()) {
        held = *previous;
    } else {
        held.input.action = InputAction::Send;
        held.input.key = e.key();
        held.input.scan = e.nativeScanCode();
        held.input.terminal = terminal;
        if ((e.modifiers() & Qt::ControlModifier) && (e.modifiers() & Qt::MetaModifier) && (right_alt_ || left_alt_ || (e.modifiers() & Qt::AltModifier)) && global_shortcut(e))
            held.input.action = InputAction::Ignore;
        else if (caps(e)) held.input.key = caps_control_ ? Qt::Key_Control : Qt::Key_CapsLock;
        else if (!(mods & (Qt::ControlModifier | Qt::MetaModifier)) && physical_key(e) == Qt::Key_Space &&
                 (right_alt_ || left_alt_ || (mods & Qt::AltModifier))) {
            held.input.action = InputAction::UnicodeKeyboard;
            held.consumes_alt = true;
        }
        else if (const auto function = number_function_key(e); function && (mods & Qt::MetaModifier)) {
            // The Folio's separate Opt key is Qt Meta (evdev 107, scan 115).
            // Right Alt/Opt must keep the number row's printed symbols, e.g. +.
            held.input.key = function;
            held.consumes_meta = true;
        }
        else if (right_alt_ && !right_alt(e) && !(mods & Qt::ControlModifier)) {
            const auto key = physical_key(e);
            if (const int target = number_terminal(e); target >= 0) {
                held.input.action = InputAction::SelectTerminal;
                held.input.target_terminal = target;
            } else switch (key) {
            case Qt::Key_Tab: held.input.key = Qt::Key_Escape; break;
            case Qt::Key_Up: held.input.key = Qt::Key_PageUp; break;
            case Qt::Key_Down: held.input.key = Qt::Key_PageDown; break;
            case Qt::Key_Backspace: held.input.action = InputAction::Quit; break;
            case Qt::Key_Space: held.input.action = InputAction::UnicodeKeyboard; break;
            case Qt::Key_Left: held.input.action = InputAction::Previous; break;
            case Qt::Key_Right: held.input.action = InputAction::Next; break;
            case Qt::Key_C: held.input.action = InputAction::Copy; break;
            case Qt::Key_V: held.input.action = InputAction::Paste; break;
            case Qt::Key_X: held.input.action = InputAction::Cut; break;
            default: break;
            }
            held.consumes_alt = held.input.key != e.key() || held.input.action != InputAction::Send;
        }
        if (held.input.action == InputAction::Send && !held.consumes_alt && !held.consumes_meta) {
            if (const auto accent = literal_accent(e)) {
                held.input.key = accent;
                held.input.text = QString(QChar(ushort(accent)));
            }
        }
        if (held.input.action == InputAction::Send) {
            if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
                if (e.key() == Qt::Key_B) held.input.action = InputAction::ToggleBar;
                if (e.key() == Qt::Key_Q) held.input.action = InputAction::Quit;
            }
            if ((mods & Qt::ShiftModifier) && held.input.key == Qt::Key_PageUp) held.input.action = InputAction::HistoryUp;
            if ((mods & Qt::ShiftModifier) && held.input.key == Qt::Key_PageDown) held.input.action = InputAction::HistoryDown;
        }
        if (!release) held_.insert(id, held);
    }
    if (release) held_.remove(id);
    auto result = held.input;
    result.type = e.type();
    result.repeat = e.isAutoRepeat();
    result.modifiers = mods;
    result.caps_locked = caps_locked_;
    if (held.consumes_meta) result.modifiers &= ~Qt::MetaModifier;
    if (held.consumes_alt) {
        result.modifiers &= ~Qt::GroupSwitchModifier;
        if (!left_alt_) result.modifiers &= ~Qt::AltModifier;
    }
    if (result.action != InputAction::Send) {
        if (release || (result.repeat && result.action != InputAction::HistoryUp && result.action != InputAction::HistoryDown))
            result.action = InputAction::Ignore;
        return result;
    }
    if (!held.consumes_alt && !held.consumes_meta && !caps(e)) {
        result.text = held.input.text.isEmpty() ? e.text() : held.input.text;
        // Caps Lock is an application setting. Ignore the platform's lock
        // state so a Caps-as-Control press cannot leave later letters capitalized.
        if (result.text.size() == 1 && result.text.at(0).isLetter()) {
            const bool upper = bool(mods & Qt::ShiftModifier) != caps_locked_;
            result.text = upper ? result.text.toUpper() : result.text.toLower();
        }
    }
    return result;
}

std::vector<MappedInput> InputMapper::reset() {
    std::vector<MappedInput> releases;
    for (const auto &held : held_) {
        if (held.input.action != InputAction::Send || held.input.terminal < 0) continue;
        auto event = held.input;
        event.type = QEvent::KeyRelease;
        event.caps_locked = caps_locked_;
        releases.push_back(event);
    }
    held_.clear();
    right_alt_ = left_alt_ = caps_held_control_ = false;
    return releases;
}
}
