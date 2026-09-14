#include "rmt/input.hpp"

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
int physical_key(const QKeyEvent &e) {
    if (!(e.modifiers() & Qt::KeypadModifier)) {
        const auto scan = e.nativeScanCode();
        if (scan >= 10 && scan <= 19) return Qt::Key_F1 + int(scan - 10);
        if (!scan && e.key() >= Qt::Key_1 && e.key() <= Qt::Key_9) return Qt::Key_F1 + e.key() - Qt::Key_1;
        if (!scan && e.key() == Qt::Key_0) return Qt::Key_F10;
    }
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
        if (caps(e)) held.input.key = caps_control_ ? Qt::Key_Control : Qt::Key_CapsLock;
        else if (right_alt_ && !right_alt(e)) {
            const auto key = physical_key(e);
            if (key >= Qt::Key_F1 && key <= Qt::Key_F10) held.input.key = key;
            else switch (key) {
            case Qt::Key_Tab: held.input.key = Qt::Key_Escape; break;
            case Qt::Key_Up: held.input.key = Qt::Key_PageUp; break;
            case Qt::Key_Down: held.input.key = Qt::Key_PageDown; break;
            case Qt::Key_Backspace: held.input.action = InputAction::Quit; break;
            case Qt::Key_Space: held.input.action = InputAction::Settings; break;
            case Qt::Key_Left: held.input.action = InputAction::Previous; break;
            case Qt::Key_Right: held.input.action = InputAction::Next; break;
            case Qt::Key_C: held.input.action = InputAction::Copy; break;
            case Qt::Key_V: held.input.action = InputAction::Paste; break;
            case Qt::Key_X: held.input.action = InputAction::Cut; break;
            default: break;
            }
            held.consumes_alt = held.input.key != e.key() || held.input.action != InputAction::Send;
        }
        if (held.input.action == InputAction::Send &&
            (right_alt_ || left_alt_ || (mods & Qt::AltModifier)) &&
            !(mods & (Qt::ControlModifier | Qt::MetaModifier))) {
            if (e.key() == Qt::Key_Plus || e.key() == Qt::Key_Equal ||
                (right_alt_ && e.nativeScanCode() == 21)) held.input.action = InputAction::ZoomIn;
            else if (e.key() == Qt::Key_Minus || e.key() == Qt::Key_Underscore ||
                     (right_alt_ && e.nativeScanCode() == 20)) held.input.action = InputAction::ZoomOut;
        }
        if (held.input.action == InputAction::Send) {
            if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
                if (e.key() == Qt::Key_B) held.input.action = InputAction::ToggleBar;
                if (e.key() == Qt::Key_Q) held.input.action = InputAction::Quit;
            }
            if ((mods & Qt::ControlModifier) && (mods & Qt::AltModifier) && e.key() == Qt::Key_T)
                held.input.action = InputAction::Ignore; // Device launcher.
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
    if (held.consumes_alt) {
        result.modifiers &= ~Qt::GroupSwitchModifier;
        if (!left_alt_) result.modifiers &= ~Qt::AltModifier;
    }
    if (result.action != InputAction::Send) {
        if (release || (result.repeat && result.action != InputAction::HistoryUp && result.action != InputAction::HistoryDown &&
                       result.action != InputAction::ZoomIn && result.action != InputAction::ZoomOut))
            result.action = InputAction::Ignore;
        return result;
    }
    if (!held.consumes_alt && !caps(e)) {
        result.text = e.text();
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
