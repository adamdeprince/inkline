// Global Ctrl+Alt shortcuts for Folio and USB keyboards. The daemon never
// grabs an input device, records typed text, or writes runtime state to disk.
#include "rmt/hotkey_config.hpp"
#include "rmt/power_key.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <map>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr size_t bit_bytes = (KEY_MAX + 8) / 8;
constexpr auto emergency_hold = std::chrono::seconds(2);
volatile std::sig_atomic_t reload_requested = 0;

bool bit(const std::array<unsigned char, bit_bytes> &bits, unsigned code) {
    return code <= KEY_MAX && (bits[code / 8] & (1U << (code % 8)));
}

void reload_signal(int) { reload_requested = 1; }

struct Trigger {
    enum Type { None, Terminal, Binding, Sleep } type = None;
    unsigned code = 0;
};

struct Keyboard {
    std::string path;
    int fd;
    bool left_ctrl = false, right_ctrl = false, left_alt = false, right_alt = false;
    bool backspace = false, desynchronized = false, emergency_active = false, emergency_fired = false;
    std::chrono::steady_clock::time_point emergency_since{};
    rmt::PowerKey power{}, sleep{};

    bool ctrl() const { return left_ctrl || right_ctrl; }
    bool alt() const { return left_alt || right_alt; }
    void update_emergency() {
        const bool active = ctrl() && alt() && backspace;
        if (active && !emergency_active) emergency_since = std::chrono::steady_clock::now();
        if (!active) emergency_fired = false;
        emergency_active = active;
    }
    void snapshot() {
        power.reset(); sleep.reset();
        std::array<unsigned char, bit_bytes> held{};
        if (ioctl(fd, EVIOCGKEY(held.size()), held.data()) < 0) return;
        left_ctrl = bit(held, KEY_LEFTCTRL); right_ctrl = bit(held, KEY_RIGHTCTRL);
        left_alt = bit(held, KEY_LEFTALT); right_alt = bit(held, KEY_RIGHTALT);
        backspace = bit(held, KEY_BACKSPACE); update_emergency();
    }
    Trigger event(const input_event &input, const std::map<unsigned, rmt::hotkeys::Binding> &bindings, bool power_enabled) {
        if (input.type == EV_SYN && input.code == SYN_DROPPED) { desynchronized = true; return {}; }
        if (desynchronized) {
            if (input.type == EV_SYN && input.code == SYN_REPORT) { snapshot(); desynchronized = false; }
            return {};
        }
        if (input.type != EV_KEY) return {};
        if (input.code == KEY_POWER || input.code == KEY_SLEEP) {
            auto &key = input.code == KEY_POWER ? power : sleep;
            return key.event(input.value, power_enabled) ? Trigger{Trigger::Sleep, input.code} : Trigger{};
        }
        switch (input.code) {
        case KEY_LEFTCTRL: left_ctrl = input.value != 0; break;
        case KEY_RIGHTCTRL: right_ctrl = input.value != 0; break;
        case KEY_LEFTALT: left_alt = input.value != 0; break;
        case KEY_RIGHTALT: right_alt = input.value != 0; break;
        case KEY_BACKSPACE: backspace = input.value != 0; break;
        default: break;
        }
        update_emergency();
        if (input.value != 1 || !ctrl() || !alt()) return {};
        if (input.code == KEY_T) return {Trigger::Terminal, input.code};
        const auto found = bindings.find(input.code);
        return found == bindings.end() ? Trigger{} : Trigger{Trigger::Binding, input.code};
    }
    bool emergency_due(std::chrono::steady_clock::time_point now) {
        if (!emergency_active || emergency_fired || now - emergency_since < emergency_hold) return false;
        emergency_fired = true; return true;
    }
};

void discover(std::vector<Keyboard> &keyboards) {
    DIR *directory = opendir("/dev/input");
    if (!directory) return;
    while (auto *entry = readdir(directory)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0 || keyboards.size() >= 32) continue;
        const std::string path = std::string("/dev/input/") + entry->d_name;
        if (std::any_of(keyboards.begin(), keyboards.end(), [&](const Keyboard &keyboard) { return keyboard.path == path; })) continue;
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        std::array<unsigned char, bit_bytes> keys{};
        const bool readable = ioctl(fd, EVIOCGBIT(EV_KEY, keys.size()), keys.data()) >= 0;
        const bool shortcuts = bit(keys, KEY_T) && bit(keys, KEY_BACKSPACE) &&
            (bit(keys, KEY_LEFTCTRL) || bit(keys, KEY_RIGHTCTRL)) && (bit(keys, KEY_LEFTALT) || bit(keys, KEY_RIGHTALT));
        if (!readable || (!shortcuts && !bit(keys, KEY_POWER) && !bit(keys, KEY_SLEEP))) {
            close(fd); continue;
        }
        keyboards.push_back({path, fd}); keyboards.back().snapshot();
    }
    closedir(directory);
}

void redirect_to_null() {
    const int null = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (null < 0) return;
    dup2(null, STDIN_FILENO); dup2(null, STDOUT_FILENO); dup2(null, STDERR_FILENO);
    if (null > STDERR_FILENO) close(null);
}

pid_t spawn(const std::vector<std::string> &arguments) {
    if (arguments.empty()) return -1;
    const pid_t child = fork();
    if (child != 0) return child;
    redirect_to_null();
    std::vector<char *> argv;
    for (const auto &argument : arguments) argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);
    execv(argv[0], argv.data());
    _exit(127);
}

int run_wait(const std::vector<std::string> &arguments, std::chrono::seconds timeout = std::chrono::seconds(6)) {
    const pid_t child = spawn(arguments);
    if (child < 0) return -1;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto result = waitpid(child, &status, WNOHANG);
        if (result == child) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (result < 0 && errno != EINTR) return -1;
        usleep(50000);
    }
    kill(child, SIGKILL); waitpid(child, &status, 0); return -1;
}

constexpr const char *launcher = "/home/root/.local/share/inkline/current/shortcut-launch.sh";
constexpr const char *power_control = "/home/root/.local/share/inkline/current/power-control.sh";

std::int64_t clock_ms(clockid_t clock) {
    timespec value{};
    if (clock_gettime(clock, &value) != 0) throw std::runtime_error("Cannot read suspend clock");
    return std::int64_t(value.tv_sec) * 1000 + value.tv_nsec / 1000000;
}

void launch_terminal() { spawn({launcher, "show"}); }

void launch_binding(const rmt::hotkeys::Binding &binding) {
    std::vector<std::string> command{launcher, binding.mode};
    command.insert(command.end(), binding.command.begin(), binding.command.end());
    spawn(command);
}

void emergency_recover() {
    // Transient shortcut units have KillMode=control-group, so this also ends
    // descendants. Restarting Inkline forces a full repaint if an app left the
    // display or input state unusable. This deliberately sacrifices open PTYs.
    spawn({launcher, "recover"}); // Keep reading input while systemd stops the app.
}

std::map<unsigned, rmt::hotkeys::Binding> read_bindings(const std::string &directory) {
    std::map<unsigned, rmt::hotkeys::Binding> result;
    for (auto &binding : rmt::hotkeys::load(directory)) result.emplace(binding.code, std::move(binding));
    // The terminal suppresses these chords while it has focus. This index is
    // rebuilt in tmpfs only at startup or after a registration change.
    if (FILE *index = std::fopen("/run/inkline-shortcuts/keys.new", "w")) {
        for (const auto &[code, binding] : result) { (void)binding; std::fprintf(index, "%u\n", code); }
        if (std::fclose(index) == 0) std::rename("/run/inkline-shortcuts/keys.new", "/run/inkline-shortcuts/keys");
    }
    return result;
}

void notify_daemon(bool custom_directory) {
    if (!custom_directory)
        run_wait({"/usr/bin/systemctl", "kill", "--kill-whom=main", "--signal=HUP", "inkline-hotkey.service"});
}

int manage(int argc, char **argv, const std::string &directory, bool custom_directory) {
    const std::string action = argc > 1 ? argv[1] : "help";
    if (action == "register") {
        if (argc < 4) throw std::runtime_error("Usage: ~/inkline shortcut register KEY [--terminal|--epaper] PROGRAM [ARG ...]");
        int first = 3;
        std::string mode = "terminal";
        if (std::strcmp(argv[first], "--epaper") == 0 || std::strcmp(argv[first], "--terminal") == 0) mode = std::string(argv[first++]) .substr(2);
        if (first >= argc) throw std::runtime_error("A program is required");
        std::vector<std::string> command(argv + first, argv + argc);
        rmt::hotkeys::register_binding(directory, argv[2], command, mode); notify_daemon(custom_directory);
        std::printf("Registered Ctrl+Alt+%s -> %s\n", argv[2], rmt::hotkeys::display_command(command).c_str());
        return 0;
    }
    if (action == "deregister" || action == "remove") {
        if (argc != 3) throw std::runtime_error("Usage: ~/inkline shortcut deregister KEY");
        rmt::hotkeys::deregister_binding(directory, argv[2]); notify_daemon(custom_directory);
        std::printf("Deregistered Ctrl+Alt+%s\n", argv[2]); return 0;
    }
    if (action == "list") {
        std::puts("Ctrl+Alt+t -> /home/root/inkline start  [permanent]");
        std::vector<std::string> warnings;
        for (const auto &binding : rmt::hotkeys::load(directory, &warnings))
            std::printf("Ctrl+Alt+%s -> [%s] %s\n", binding.key.c_str(), binding.mode.c_str(), rmt::hotkeys::display_command(binding.command).c_str());
        for (const auto &warning : warnings) std::fprintf(stderr, "Warning: %s\n", warning.c_str());
        return warnings.empty() ? 0 : 1;
    }
    if (action == "check") {
        std::vector<std::string> warnings; (void)rmt::hotkeys::load(directory, &warnings);
        if (!warnings.empty()) throw std::runtime_error(warnings.front());
        std::puts("Inkline global shortcuts ready; Ctrl+Alt+T and the emergency chord are permanent."); return 0;
    }
    if (action == "help" || action == "--help" || action == "-h") {
        std::puts("Usage: ~/inkline shortcut register KEY [--terminal|--epaper] PROGRAM [ARG ...]");
        std::puts("       ~/inkline shortcut deregister KEY");
        std::puts("       ~/inkline shortcut list");
        std::puts("Ctrl+Alt+T always opens Inkline and cannot be changed.");
        std::puts("Programs open in a new terminal by default. --epaper gives a native app the screen.");
        std::puts("Hold Ctrl+Alt+Backspace for 2 seconds to stop shortcut apps and restore Inkline.");
        std::puts("Emergency recovery closes all open terminals; ordinary Ctrl+Alt+T preserves them.");
        return 0;
    }
    throw std::runtime_error("Unknown shortcut command; run ~/inkline shortcut help");
}

int daemon(const std::string &directory) {
    std::signal(SIGHUP, reload_signal);
    auto bindings = read_bindings(directory);
    std::vector<Keyboard> keyboards;
    rmt::ResumeGuard resume_guard;
    auto observe_resume = [&] {
        const auto monotonic = clock_ms(CLOCK_MONOTONIC);
        if (resume_guard.observe(clock_ms(CLOCK_BOOTTIME), monotonic)) {
            for (auto &keyboard : keyboards) { keyboard.power.reset(); keyboard.sleep.reset(); }
            spawn({power_control, "resume"});
        }
        return monotonic;
    };
    observe_resume();
    auto next_discovery = std::chrono::steady_clock::time_point{};
    for (;;) {
        if (reload_requested) { reload_requested = 0; bindings = read_bindings(directory); }
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_discovery) { discover(keyboards); next_discovery = now + std::chrono::seconds(1); }
        std::vector<pollfd> descriptors;
        for (const auto &keyboard : keyboards) descriptors.push_back({keyboard.fd, POLLIN, 0});
        const bool emergency_held = std::any_of(keyboards.begin(), keyboards.end(), [](const auto &k) { return k.emergency_active && !k.emergency_fired; });
        const int ready = poll(descriptors.data(), descriptors.size(), emergency_held ? 100 : 1000);
        if (ready < 0 && errno != EINTR) return 1;
        const auto monotonic = observe_resume();
        bool recover = false;
        std::vector<Trigger> triggers;
        for (size_t i = 0; i < keyboards.size(); ++i) {
            bool lost = descriptors[i].revents & (POLLERR | POLLHUP | POLLNVAL);
            if (descriptors[i].revents & POLLIN) {
                input_event events[32];
                const ssize_t count = read(keyboards[i].fd, events, sizeof(events));
                if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) lost = true;
                if (count > 0) for (size_t at = 0; at < size_t(count) / sizeof(input_event); ++at) {
                    const auto trigger = keyboards[i].event(events[at], bindings, !resume_guard.blocked(monotonic));
                    if (trigger.type != Trigger::None) triggers.push_back(trigger);
                }
            }
            if (keyboards[i].emergency_due(std::chrono::steady_clock::now())) recover = true;
            if (lost) { close(keyboards[i].fd); keyboards[i].fd = -1; }
        }
        keyboards.erase(std::remove_if(keyboards.begin(), keyboards.end(), [](const Keyboard &keyboard) { return keyboard.fd < 0; }), keyboards.end());
        if (recover) emergency_recover();
        else for (const auto &trigger : triggers) {
            if (trigger.type == Trigger::Terminal) launch_terminal();
            else if (trigger.type == Trigger::Sleep) {
                if (!resume_guard.blocked(monotonic)) {
                    resume_guard.block(monotonic);
                    spawn({power_control, "sleep"});
                }
            }
            else if (const auto found = bindings.find(trigger.code); found != bindings.end()) launch_binding(found->second);
        }
        while (waitpid(-1, nullptr, WNOHANG) > 0) {}
    }
}
}

int main(int argc, char **argv) {
    const char *override_directory = std::getenv("INKLINE_SHORTCUT_DIRECTORY");
    const bool custom_directory = override_directory && *override_directory;
    const std::string directory = custom_directory ? override_directory : rmt::hotkeys::default_directory();
    try { return argc == 1 ? daemon(directory) : manage(argc, argv, directory, custom_directory); }
    catch (const std::exception &error) { std::fprintf(stderr, "inkline shortcut: %s\n", error.what()); return 1; }
}
