#ifndef RMT_KEYBOARD_HPP
#define RMT_KEYBOARD_HPP
#include "rmt/core.h"
#include <QKeyEvent>
#include <string>
namespace rmt {
class Keyboard {
public:
    explicit Keyboard(RmtCore &core);
    ~Keyboard();
    Keyboard(const Keyboard &) = delete;
    Keyboard &operator=(const Keyboard &) = delete;
    std::string encode(const QKeyEvent &event, bool caps_locked = false);
private:
    GhosttyTerminal terminal_;
    GhosttyKeyEncoder encoder_ = nullptr;
    GhosttyKeyEvent event_ = nullptr;
};
}
#endif
