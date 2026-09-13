#ifndef RMT_PTY_HPP
#define RMT_PTY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <sys/types.h>

namespace rmt {
/* Nonblocking shell transport. The event loop owns reading fd() and calls
 * flush() when writable. No terminal output is written to files or logs.
 * Supply an absolute executable path. Keep unrelated parent fds close-on-exec.
 */
class Pty {
public:
    static constexpr size_t MAX_PENDING = 256 * 1024;
    Pty(const std::vector<std::string> &argv, uint16_t cols, uint16_t rows);
    ~Pty();
    Pty(const Pty &) = delete;
    Pty &operator=(const Pty &) = delete;
    int fd() const { return master_; }
    size_t pending_bytes() const { return pending_.size() - offset_; }
    // Returns false without queuing any bytes if the queue is full or closed.
    bool enqueue(std::string_view bytes);
    // Throws std::system_error on a write failure other than backpressure.
    void flush();
    void resize(uint16_t cols, uint16_t rows, uint16_t width_px = 0, uint16_t height_px = 0);
    std::optional<int> poll_exit();
    // Sends SIGHUP to this session, closes the master, and never blocks.
    void close();

private:
    int master_ = -1;
    pid_t child_ = -1;
    std::optional<int> exit_;
    std::string pending_;
    size_t offset_ = 0;
};
}
#endif
