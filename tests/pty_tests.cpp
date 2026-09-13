#include "rmt/pty.hpp"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <poll.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n'; std::exit(1); \
} } while (0)

template<class Done> static std::string drain(rmt::Pty &pty, Done done) {
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        CHECK(std::chrono::steady_clock::now() < deadline);
        pollfd descriptor{pty.fd(), short(POLLIN | (pty.pending_bytes() ? POLLOUT : 0)), 0};
        const int ready = poll(&descriptor, 1, 10);
        CHECK(ready >= 0 || errno == EINTR);
        if (ready < 0) continue;
        if (descriptor.revents & POLLOUT) pty.flush();
        char bytes[4096];
        for (;;) {
            const ssize_t n = read(pty.fd(), bytes, sizeof(bytes));
            if (n > 0) output.append(bytes, size_t(n));
            else {
                CHECK(n == 0 || errno == EAGAIN || errno == EWOULDBLOCK || errno == EIO || errno == EINTR);
                break;
            }
        }
        // Drain before checking child exit: a fast child may already be dead
        // while its final output remains buffered in the PTY.
        if (done(output)) break;
    }
    return output;
}

int main() {
    {
        rmt::Pty pty({"/bin/sh", "-c", "test -t 0 && stty size; printf '%s:%s' \"$TERM\" \"$COLORTERM\"; exit 7"}, 80, 24);
        const auto output = drain(pty, [&](const std::string &) { return pty.poll_exit().has_value(); });
        CHECK(output.find("24 80") != std::string::npos);
        CHECK(output.find("xterm-256color:truecolor") != std::string::npos);
        CHECK(pty.poll_exit() == 7);
    }
    {
        rmt::Pty pty({"/bin/sh", "-c", "printf READY; read line; stty size"}, 80, 24);
        CHECK(drain(pty, [](const std::string &out) { return out.find("READY") != std::string::npos; }) == "READY");
        pty.resize(120, 40, 1440, 960);
        CHECK(pty.enqueue("\n"));
        CHECK(drain(pty, [&](const std::string &) { return pty.poll_exit().has_value(); }).find("40 120") != std::string::npos);
    }
    {
        rmt::Pty pty({"/bin/sh", "-c", "stty raw -echo; printf READY; exec /bin/cat"}, 80, 24);
        CHECK(drain(pty, [](const std::string &out) { return out.find("READY") != std::string::npos; }) == "READY");
        std::string payload;
        for (unsigned i = 0; i < 128 * 1024; ++i) payload.push_back(char('a' + i % 26));
        CHECK(!pty.enqueue(std::string(rmt::Pty::MAX_PENDING + 1, 'x')));
        CHECK(pty.pending_bytes() == 0);
        CHECK(pty.enqueue(payload));
        const auto output = drain(pty, [&](const std::string &out) { return out.size() == payload.size(); });
        CHECK(output == payload && pty.pending_bytes() == 0);
        pty.close();
        CHECK(pty.fd() == -1 && !pty.enqueue("x"));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!pty.poll_exit()) {
            CHECK(std::chrono::steady_clock::now() < deadline);
            poll(nullptr, 0, 10);
        }
    }
    {
        rmt::Pty pty({"/rmt-nonexistent-executable"}, 80, 24);
        drain(pty, [&](const std::string &) { return pty.poll_exit().has_value(); });
        CHECK(pty.poll_exit() == 127);
    }
    std::cout << "PTY: controlling terminal, dimensions, environment, backpressure, resize, hangup and exit passed.\n";
}
