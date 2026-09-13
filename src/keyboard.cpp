#include "rmt/keyboard.hpp"
#include <stdexcept>
namespace rmt {
namespace {
GhosttyKey key(int k, bool keypad) {
    if (k >= Qt::Key_A && k <= Qt::Key_Z) return GhosttyKey(GHOSTTY_KEY_A + k - Qt::Key_A);
    if (k >= Qt::Key_0 && k <= Qt::Key_9) return GhosttyKey((keypad ? GHOSTTY_KEY_NUMPAD_0 : GHOSTTY_KEY_DIGIT_0) + k - Qt::Key_0);
    if (k >= Qt::Key_F1 && k <= Qt::Key_F25) return GhosttyKey(GHOSTTY_KEY_F1 + k - Qt::Key_F1);
    switch (k) {
    case Qt::Key_Escape: return GHOSTTY_KEY_ESCAPE;
    case Qt::Key_Return: return GHOSTTY_KEY_ENTER;
    case Qt::Key_Enter: return GHOSTTY_KEY_NUMPAD_ENTER;
    case Qt::Key_Backspace: return GHOSTTY_KEY_BACKSPACE;
    case Qt::Key_Tab: case Qt::Key_Backtab: return GHOSTTY_KEY_TAB;
    case Qt::Key_Space: return GHOSTTY_KEY_SPACE;
    case Qt::Key_Left: return GHOSTTY_KEY_ARROW_LEFT;
    case Qt::Key_Right: return GHOSTTY_KEY_ARROW_RIGHT;
    case Qt::Key_Up: return GHOSTTY_KEY_ARROW_UP;
    case Qt::Key_Down: return GHOSTTY_KEY_ARROW_DOWN;
    case Qt::Key_Home: return GHOSTTY_KEY_HOME;
    case Qt::Key_End: return GHOSTTY_KEY_END;
    case Qt::Key_PageUp: return GHOSTTY_KEY_PAGE_UP;
    case Qt::Key_PageDown: return GHOSTTY_KEY_PAGE_DOWN;
    case Qt::Key_Insert: return GHOSTTY_KEY_INSERT;
    case Qt::Key_Delete: return GHOSTTY_KEY_DELETE;
    case Qt::Key_Shift: return GHOSTTY_KEY_SHIFT_LEFT;
    case Qt::Key_Control: return GHOSTTY_KEY_CONTROL_LEFT;
    case Qt::Key_Alt: case Qt::Key_AltGr: return GHOSTTY_KEY_ALT_LEFT;
    case Qt::Key_Meta: return GHOSTTY_KEY_META_LEFT;
    case Qt::Key_CapsLock: return GHOSTTY_KEY_CAPS_LOCK;
    case Qt::Key_NumLock: return GHOSTTY_KEY_NUM_LOCK;
    case Qt::Key_Minus: case Qt::Key_Underscore: return GHOSTTY_KEY_MINUS;
    case Qt::Key_Equal: case Qt::Key_Plus: return GHOSTTY_KEY_EQUAL;
    case Qt::Key_BracketLeft: case Qt::Key_BraceLeft: return GHOSTTY_KEY_BRACKET_LEFT;
    case Qt::Key_BracketRight: case Qt::Key_BraceRight: return GHOSTTY_KEY_BRACKET_RIGHT;
    case Qt::Key_Backslash: case Qt::Key_Bar: return GHOSTTY_KEY_BACKSLASH;
    case Qt::Key_Semicolon: case Qt::Key_Colon: return GHOSTTY_KEY_SEMICOLON;
    case Qt::Key_Apostrophe: case Qt::Key_QuoteDbl: return GHOSTTY_KEY_QUOTE;
    case Qt::Key_Comma: case Qt::Key_Less: return GHOSTTY_KEY_COMMA;
    case Qt::Key_Period: case Qt::Key_Greater: return GHOSTTY_KEY_PERIOD;
    case Qt::Key_Slash: case Qt::Key_Question: return GHOSTTY_KEY_SLASH;
    case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: return GHOSTTY_KEY_BACKQUOTE;
    default: return GHOSTTY_KEY_UNIDENTIFIED;
    }
}
}
Keyboard::Keyboard(RmtCore &core) : terminal_(rmt_core_terminal(&core)) {
    if (ghostty_key_encoder_new(rmt_core_allocator(&core), &encoder_) != GHOSTTY_SUCCESS) throw std::bad_alloc();
    if (ghostty_key_event_new(rmt_core_allocator(&core), &event_) != GHOSTTY_SUCCESS) {
        ghostty_key_encoder_free(encoder_); throw std::bad_alloc();
    }
}
Keyboard::~Keyboard() { ghostty_key_event_free(event_); ghostty_key_encoder_free(encoder_); }
std::string Keyboard::encode(const QKeyEvent &input) {
    GhosttyMods mods = 0;
    const auto qmods = input.modifiers();
    if (qmods & Qt::ShiftModifier) mods |= GHOSTTY_MODS_SHIFT;
    if (qmods & Qt::ControlModifier) mods |= GHOSTTY_MODS_CTRL;
    if (qmods & Qt::AltModifier) mods |= GHOSTTY_MODS_ALT;
    if (qmods & Qt::MetaModifier) mods |= GHOSTTY_MODS_SUPER;
    if (input.key() == Qt::Key_Backtab) mods |= GHOSTTY_MODS_SHIFT;
    const auto utf8 = input.text().toUtf8();
    const bool printable = !input.text().isEmpty() && input.text().at(0).unicode() >= 0x20 && input.text().at(0).unicode() != 0x7f;
    ghostty_key_event_set_key(event_, key(input.key(), qmods & Qt::KeypadModifier));
    ghostty_key_event_set_action(event_, input.type() == QEvent::KeyRelease ? GHOSTTY_KEY_ACTION_RELEASE :
                               input.isAutoRepeat() ? GHOSTTY_KEY_ACTION_REPEAT : GHOSTTY_KEY_ACTION_PRESS);
    ghostty_key_event_set_mods(event_, mods);
    ghostty_key_event_set_consumed_mods(event_, printable ? mods & GHOSTTY_MODS_SHIFT : 0);
    ghostty_key_event_set_composing(event_, false);
    ghostty_key_event_set_utf8(event_, printable ? utf8.constData() : "", printable ? size_t(utf8.size()) : 0);
    const uint32_t unshifted = input.key() >= 0x20 && input.key() < 0x10000 ? QChar(ushort(input.key())).toLower().unicode() : 0;
    ghostty_key_event_set_unshifted_codepoint(event_, unshifted);
    ghostty_key_encoder_setopt_from_terminal(encoder_, terminal_);
    char bytes[1024]; size_t len = 0;
    if (ghostty_key_encoder_encode(encoder_, event_, bytes, sizeof(bytes), &len) != GHOSTTY_SUCCESS) return {};
    return {bytes, len};
}
}
