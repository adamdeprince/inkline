#ifndef RMT_INPUT_HPP
#define RMT_INPUT_HPP
#include <QHash>
#include <QKeyEvent>
#include <vector>

namespace rmt {
enum class InputAction { Send, Ignore, Settings, Quit, Previous, Next, ToggleBar, HistoryUp, HistoryDown, ZoomIn, ZoomOut, Copy, Paste, Cut };
struct MappedInput {
    InputAction action = InputAction::Ignore;
    QEvent::Type type = QEvent::KeyPress;
    int key = 0;
    Qt::KeyboardModifiers modifiers;
    QString text;
    quint32 scan = 0;
    bool repeat = false;
    bool caps_locked = false;
    int terminal = -1;
    QKeyEvent event() const { return QKeyEvent(type, key, modifiers, scan, 0, 0, text, repeat); }
};

// Shared by all terminal sessions. A release returns to the terminal that
// received its press, including when a shortcut changes the active session.
class InputMapper {
public:
    MappedInput map(const QKeyEvent &event, int terminal);
    std::vector<MappedInput> reset();
    void set_caps_control(bool enabled);
    bool caps_control() const { return caps_control_; }
    bool caps_locked() const { return caps_locked_; }
private:
    struct Held { MappedInput input; bool consumes_alt = false; };
    QHash<quint32, Held> held_;
    bool right_alt_ = false, left_alt_ = false;
    bool caps_control_ = true, caps_locked_ = false, caps_held_control_ = false;
};
}
#endif
