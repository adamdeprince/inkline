#ifndef RMT_STREAM_HPP
#define RMT_STREAM_HPP

#include "rmt/core.h"
#include "rmt/sixel.hpp"
#include <array>
#include <functional>
#include <memory>
#include <string_view>

namespace rmt {

/* Routes PTY bytes to libghostty and complete sixel images to a consumer.
 * The core must outlive this object; serialize writes and all terminal access.
 * The image handler runs synchronously at the original position in the stream,
 * with libghostty at ground. It may inspect terminal state but must not reenter
 * write(). It owns placement, scrolling, erasure and retained-image budgets.
 * This layer does not advertise sixel or implement DEC sixel display modes yet.
 */
class Stream {
public:
    using ImageHandler = std::function<void(sixel::Bitmap &&)>;
    explicit Stream(RmtCore &core, ImageHandler handler,
                    size_t encoded_limit = sixel::MAX_ENCODED_BYTES);
    void write(std::string_view bytes);
    void reset();
    // Observe complete CSI sequences and RIS before the terminal consumes them.
    void set_control_handler(std::function<void(std::string_view)> handler) { control_ = std::move(handler); }
    size_t images_decoded() const { return decoded_; }
    size_t images_rejected() const { return rejected_; }
    size_t buffered_bytes() const { return used_; }

private:
    enum class State { Normal, Escape, Csi, DcsHeader, Sixel, SixelEscape };
    RmtCore &core_;
    GhosttyTerminal terminal_;
    ImageHandler handler_;
    std::function<void(std::string_view)> control_;
    State state_ = State::Normal;
    std::array<char, 128> header_{};
    size_t header_size_ = 0;
    std::unique_ptr<char[]> buffer_;
    size_t limit_, used_ = 0, decoded_ = 0, rejected_ = 0;
    bool discard_ = false;
    sixel::Palette palette_;

    void send(std::string_view bytes);
    void append(char byte);
    void start_sixel();
    void finish_sixel();
    void cancel_sixel();
};
}
#endif
