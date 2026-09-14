#include "rmt/pty.hpp"
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pwd.h>
#include <stdexcept>
#include <system_error>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

extern char **environ;

namespace rmt {
namespace {
[[noreturn]] void system_error(const char *operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}
}

Pty::Pty(const std::vector<std::string> &argv, uint16_t cols, uint16_t rows) {
    if (argv.empty() || argv[0].empty() || argv[0][0] != '/' || !cols || !rows)
        throw std::invalid_argument("PTY requires an absolute executable path and nonzero dimensions");
    std::vector<char *> arguments;
    for (const auto &value : argv) arguments.push_back(const_cast<char *>(value.c_str()));
    arguments.push_back(nullptr);

    // Prepare all allocations before fork. The child only calls execve/write/
    // _exit, avoiding C++ and allocator locks inherited from other threads.
    std::vector<std::string> environment;
    bool has_home = false;
    for (char **entry = environ; *entry; ++entry) {
        const std::string_view value(*entry);
        if (value.substr(0, 5) == "HOME=") {
            if (value.size() == 5) continue;
            has_home = true;
        }
        if (value.substr(0, 5) != "TERM=" && value.substr(0, 10) != "COLORTERM=" &&
            value.substr(0, 13) != "TERM_PROGRAM=") environment.emplace_back(value);
    }
    environment.emplace_back("TERM=xterm-256color");
    environment.emplace_back("COLORTERM=truecolor");
    environment.emplace_back("TERM_PROGRAM=inkline");
    if (!has_home) {
        // A system service can have no login environment. Resolve the actual
        // account before fork, so bare `cd` and applications agree on HOME.
        std::vector<char> buffer(4096);
        passwd account{}, *found = nullptr;
        int result;
        while ((result = getpwuid_r(getuid(), &account, buffer.data(), buffer.size(), &found)) == ERANGE &&
               buffer.size() < 1024 * 1024) buffer.resize(buffer.size() * 2);
        if (result || !found || !found->pw_dir || !found->pw_dir[0])
            throw std::runtime_error("Cannot find the shell's home directory");
        environment.emplace_back(std::string("HOME=") + found->pw_dir);
    }
    std::vector<char *> env;
    for (auto &value : environment) env.push_back(value.data());
    env.push_back(nullptr);
    winsize size{};
    size.ws_col = cols;
    size.ws_row = rows;
    child_ = forkpty(&master_, nullptr, nullptr, &size);
    if (child_ < 0) system_error("forkpty");
    if (child_ == 0) {
        execve(arguments[0], arguments.data(), env.data());
        constexpr char message[] = "inkline: cannot execute shell\n";
        (void)::write(STDERR_FILENO, message, sizeof(message) - 1);
        _exit(127);
    }
    const int flags = fcntl(master_, F_GETFL);
    if (flags < 0 || fcntl(master_, F_SETFL, flags | O_NONBLOCK) < 0 ||
        fcntl(master_, F_SETFD, FD_CLOEXEC) < 0) {
        const int saved = errno;
        close();
        errno = saved;
        system_error("configure PTY");
    }
}

Pty::~Pty() { close(); }

bool Pty::enqueue(std::string_view bytes) {
    if (master_ < 0 || bytes.size() > MAX_PENDING - pending_bytes()) return false;
    if (offset_) {
        pending_.erase(0, offset_);
        offset_ = 0;
    }
    pending_.append(bytes);
    flush();
    return true;
}

void Pty::flush() {
    while (master_ >= 0 && pending_bytes()) {
        const ssize_t n = ::write(master_, pending_.data() + offset_, pending_bytes());
        if (n > 0) offset_ += size_t(n);
        else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        else system_error("write PTY");
    }
    if (offset_ == pending_.size()) {
        pending_.clear();
        offset_ = 0;
    }
}

void Pty::resize(uint16_t cols, uint16_t rows, uint16_t width_px, uint16_t height_px) {
    if (!cols || !rows) throw std::invalid_argument("PTY dimensions must be nonzero");
    if (master_ < 0) return;
    winsize size{};
    size.ws_col = cols;
    size.ws_row = rows;
    size.ws_xpixel = width_px;
    size.ws_ypixel = height_px;
    if (ioctl(master_, TIOCSWINSZ, &size) < 0) system_error("resize PTY");
}

std::optional<int> Pty::poll_exit() {
    if (child_ < 0) return exit_;
    int status;
    const pid_t result = waitpid(child_, &status, WNOHANG);
    if (result == child_) {
        exit_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        child_ = -1;
    } else if (result < 0 && errno != EINTR) system_error("waitpid");
    return exit_;
}

void Pty::close() {
    if (child_ > 0) {
        (void)kill(-child_, SIGHUP);
        (void)kill(child_, SIGHUP); // Covers the brief interval before setsid.
    }
    if (master_ >= 0) {
        ::close(master_);
        master_ = -1;
    }
    // Reap when already exited. A still-running child can be polled by the
    // event loop; if the application exits, the OS adopts and reaps it.
    if (child_ > 0) {
        int status;
        if (waitpid(child_, &status, WNOHANG) == child_) {
            exit_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            child_ = -1;
        }
    }
    pending_.clear();
    offset_ = 0;
}
}
