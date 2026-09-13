// Inkline's device launcher. Observe Ctrl+Alt+T without grabbing a keyboard
// or retaining/logging typed text. Discover both Folio and USB input devices.
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr size_t bytes = (KEY_MAX + 8) / 8;
bool bit(const std::array<unsigned char, bytes> &bits, unsigned code) {
    return code <= KEY_MAX && (bits[code / 8] & (1U << (code % 8)));
}
struct Keyboard {
    std::string path;
    int fd;
    bool left_ctrl = false, right_ctrl = false, left_alt = false, right_alt = false;
    bool desynchronized = false;
    void snapshot() {
        std::array<unsigned char, bytes> held{};
        if (ioctl(fd, EVIOCGKEY(held.size()), held.data()) < 0) return;
        left_ctrl = bit(held, KEY_LEFTCTRL); right_ctrl = bit(held, KEY_RIGHTCTRL);
        left_alt = bit(held, KEY_LEFTALT); right_alt = bit(held, KEY_RIGHTALT);
    }
    bool event(const input_event &e) {
        if (e.type == EV_SYN && e.code == SYN_DROPPED) { desynchronized = true; return false; }
        if (desynchronized) {
            if (e.type == EV_SYN && e.code == SYN_REPORT) { snapshot(); desynchronized = false; }
            return false;
        }
        if (e.type != EV_KEY) return false;
        switch (e.code) {
        case KEY_LEFTCTRL: left_ctrl = e.value != 0; break;
        case KEY_RIGHTCTRL: right_ctrl = e.value != 0; break;
        case KEY_LEFTALT: left_alt = e.value != 0; break;
        case KEY_RIGHTALT: right_alt = e.value != 0; break;
        case KEY_T: return e.value == 1 && (left_ctrl || right_ctrl) && (left_alt || right_alt);
        default: break;
        }
        return false;
    }
};
void discover(std::vector<Keyboard> &keyboards) {
    DIR *directory = opendir("/dev/input");
    if (!directory) return;
    while (auto *entry = readdir(directory)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0 || keyboards.size() >= 32) continue;
        const std::string path = std::string("/dev/input/") + entry->d_name;
        if (std::any_of(keyboards.begin(), keyboards.end(), [&](const Keyboard &k) { return k.path == path; })) continue;
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        std::array<unsigned char, bytes> keys{};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, keys.size()), keys.data()) < 0 || !bit(keys, KEY_T) ||
            !(bit(keys, KEY_LEFTCTRL) || bit(keys, KEY_RIGHTCTRL)) || !(bit(keys, KEY_LEFTALT) || bit(keys, KEY_RIGHTALT))) {
            close(fd); continue;
        }
        keyboards.push_back({path, fd});
        keyboards.back().snapshot();
    }
    closedir(directory);
}
}
int main() {
    std::vector<Keyboard> keyboards;
    pid_t child = -1;
    for (;;) {
        discover(keyboards);
        std::vector<pollfd> descriptors;
        for (const auto &keyboard : keyboards) descriptors.push_back({keyboard.fd, POLLIN, 0});
        const int ready = poll(descriptors.data(), descriptors.size(), 1000);
        if (ready < 0 && errno != EINTR) return 1;
        if (child > 0 && waitpid(child, nullptr, WNOHANG) == child) child = -1;
        bool launch = false;
        for (size_t i = 0; i < keyboards.size(); ++i) {
            bool lost = descriptors[i].revents & (POLLERR | POLLHUP | POLLNVAL);
            if (descriptors[i].revents & POLLIN) {
                input_event events[32];
                const ssize_t n = read(keyboards[i].fd, events, sizeof(events));
                if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) lost = true;
                if (n > 0) for (size_t at = 0; at < size_t(n) / sizeof(input_event); ++at) launch |= keyboards[i].event(events[at]);
            }
            if (lost) { close(keyboards[i].fd); keyboards[i].fd = -1; }
        }
        keyboards.erase(std::remove_if(keyboards.begin(), keyboards.end(), [](const Keyboard &k) { return k.fd < 0; }), keyboards.end());
        if (launch && child < 0) {
            child = fork();
            if (child == 0) {
                execl("/home/root/inkline", "inkline", "start", static_cast<char *>(nullptr));
                _exit(127);
            }
        }
    }
}
