#ifndef RMT_CLIPBOARD_HPP
#define RMT_CLIPBOARD_HPP
#include "rmt/core.h"
#include <QByteArray>
#include <optional>
namespace rmt {
// Private to Inkline. All six sessions share this RAM-only text clipboard.
class Clipboard {
public:
    static constexpr size_t MAX_BYTES = 128 * 1024;
    const QByteArray &text() const { return text_; }
    bool set(const QByteArray &text);
    void write(const GhosttyClipboardWrite *request) noexcept;
    void read(const GhosttyClipboardRead *request, bool ready = true) const noexcept;
    GhosttyResult paste(GhosttyTerminal terminal) const;
private:
    QByteArray text_;
};
class Selection {
public:
    explicit Selection(GhosttyTerminal terminal) : terminal_(terminal) {}
    ~Selection();
    void begin(uint16_t x, uint16_t y);
    void extend(uint16_t x, uint16_t y);
    void release();
    void clear();
    std::optional<QByteArray> text() const;
private:
    GhosttyTerminal terminal_;
    GhosttyTrackedGridRef anchor_ = nullptr;
    GhosttyTerminalScreen screen_ = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
};
}
#endif
