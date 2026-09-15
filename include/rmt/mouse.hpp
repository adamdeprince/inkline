#ifndef RMT_MOUSE_HPP
#define RMT_MOUSE_HPP

#include "rmt/core.h"
#include <QPointF>
#include <Qt>
#include <string>

namespace rmt {

class Mouse {
public:
    enum class Action { Press, Release, Motion };
    explicit Mouse(RmtCore &core);
    ~Mouse();
    Mouse(const Mouse &) = delete;
    Mouse &operator=(const Mouse &) = delete;
    void resize(int width, int height, int cell_width, int cell_height);
    std::string encode(Action action, const QPointF &position,
                       Qt::KeyboardModifiers modifiers = Qt::NoModifier);
private:
    GhosttyTerminal terminal_;
    GhosttyMouseEncoder encoder_ = nullptr;
    GhosttyMouseEvent event_ = nullptr;
    GhosttyMouseEncoderSize size_{};
    bool pressed_ = false;
};

}
#endif
