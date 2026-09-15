#include "rmt/mouse.hpp"
#include <algorithm>
#include <stdexcept>

namespace rmt {

Mouse::Mouse(RmtCore &core) : terminal_(rmt_core_terminal(&core)) {
    if (ghostty_mouse_encoder_new(rmt_core_allocator(&core), &encoder_) != GHOSTTY_SUCCESS) throw std::bad_alloc();
    if (ghostty_mouse_event_new(rmt_core_allocator(&core), &event_) != GHOSTTY_SUCCESS) {
        ghostty_mouse_encoder_free(encoder_); throw std::bad_alloc();
    }
    size_.size = sizeof(size_);
    const bool track_last_cell = true;
    ghostty_mouse_encoder_setopt(encoder_, GHOSTTY_MOUSE_ENCODER_OPT_TRACK_LAST_CELL, &track_last_cell);
}

Mouse::~Mouse() {
    ghostty_mouse_event_free(event_);
    ghostty_mouse_encoder_free(encoder_);
}

void Mouse::resize(int width, int height, int cell_width, int cell_height) {
    size_.screen_width = uint32_t(std::max(1, width));
    size_.screen_height = uint32_t(std::max(1, height));
    size_.cell_width = uint32_t(std::max(1, cell_width));
    size_.cell_height = uint32_t(std::max(1, cell_height));
    ghostty_mouse_encoder_setopt(encoder_, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &size_);
    ghostty_mouse_encoder_reset(encoder_);
}

std::string Mouse::encode(Action action, const QPointF &position, Qt::KeyboardModifiers modifiers) {
    ghostty_mouse_encoder_setopt_from_terminal(encoder_, terminal_);
    if (action == Action::Press) pressed_ = true;
    const bool button_pressed = pressed_;
    ghostty_mouse_encoder_setopt(encoder_, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &button_pressed);
    ghostty_mouse_event_set_action(event_, action == Action::Press ? GHOSTTY_MOUSE_ACTION_PRESS :
                                            action == Action::Release ? GHOSTTY_MOUSE_ACTION_RELEASE : GHOSTTY_MOUSE_ACTION_MOTION);
    if (pressed_ || action != Action::Motion) ghostty_mouse_event_set_button(event_, GHOSTTY_MOUSE_BUTTON_LEFT);
    else ghostty_mouse_event_clear_button(event_);
    GhosttyMods mods = 0;
    if (modifiers & Qt::ShiftModifier) mods |= GHOSTTY_MODS_SHIFT;
    if (modifiers & Qt::ControlModifier) mods |= GHOSTTY_MODS_CTRL;
    if (modifiers & Qt::AltModifier) mods |= GHOSTTY_MODS_ALT;
    ghostty_mouse_event_set_mods(event_, mods);
    ghostty_mouse_event_set_position(event_, {float(position.x()), float(position.y())});
    char buffer[128]; size_t length = 0;
    const auto result = ghostty_mouse_encoder_encode(encoder_, event_, buffer, sizeof(buffer), &length);
    if (action == Action::Release || (action == Action::Press && !length)) pressed_ = false;
    if (result != GHOSTTY_SUCCESS) throw std::runtime_error("Cannot encode terminal mouse event");
    return {buffer, length};
}

}
