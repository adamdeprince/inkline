#include "rmt/hotkey_config.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace rmt::hotkeys {
namespace {
struct KeyDef { const char *name; const char *symbol; unsigned code; };
// Linux evdev key codes are part of the input ABI. Keeping the small table
// here also lets the file-format tests run on macOS without Linux headers.
enum : unsigned { KEY_A=30, KEY_B=48, KEY_C=46, KEY_D=32, KEY_E=18, KEY_F=33,
    KEY_G=34, KEY_H=35, KEY_I=23, KEY_J=36, KEY_K=37, KEY_L=38, KEY_M=50,
    KEY_N=49, KEY_O=24, KEY_P=25, KEY_Q=16, KEY_R=19, KEY_S=31, KEY_T=20,
    KEY_U=22, KEY_V=47, KEY_W=17, KEY_X=45, KEY_Y=21, KEY_Z=44,
    KEY_0=11, KEY_1=2, KEY_2=3, KEY_3=4, KEY_4=5, KEY_5=6, KEY_6=7,
    KEY_7=8, KEY_8=9, KEY_9=10, KEY_MINUS=12, KEY_EQUAL=13,
    KEY_LEFTBRACE=26, KEY_RIGHTBRACE=27, KEY_BACKSLASH=43, KEY_SEMICOLON=39,
    KEY_APOSTROPHE=40, KEY_COMMA=51, KEY_DOT=52, KEY_SLASH=53, KEY_GRAVE=41 };
constexpr std::array<KeyDef, 47> keys{{
    {"a", "a", KEY_A}, {"b", "b", KEY_B}, {"c", "c", KEY_C}, {"d", "d", KEY_D},
    {"e", "e", KEY_E}, {"f", "f", KEY_F}, {"g", "g", KEY_G}, {"h", "h", KEY_H},
    {"i", "i", KEY_I}, {"j", "j", KEY_J}, {"k", "k", KEY_K}, {"l", "l", KEY_L},
    {"m", "m", KEY_M}, {"n", "n", KEY_N}, {"o", "o", KEY_O}, {"p", "p", KEY_P},
    {"q", "q", KEY_Q}, {"r", "r", KEY_R}, {"s", "s", KEY_S}, {"t", "t", KEY_T},
    {"u", "u", KEY_U}, {"v", "v", KEY_V}, {"w", "w", KEY_W}, {"x", "x", KEY_X},
    {"y", "y", KEY_Y}, {"z", "z", KEY_Z},
    {"0", "0", KEY_0}, {"1", "1", KEY_1}, {"2", "2", KEY_2}, {"3", "3", KEY_3},
    {"4", "4", KEY_4}, {"5", "5", KEY_5}, {"6", "6", KEY_6}, {"7", "7", KEY_7},
    {"8", "8", KEY_8}, {"9", "9", KEY_9},
    {"minus", "-", KEY_MINUS}, {"equal", "=", KEY_EQUAL},
    {"left-bracket", "[", KEY_LEFTBRACE}, {"right-bracket", "]", KEY_RIGHTBRACE},
    {"backslash", "\\", KEY_BACKSLASH}, {"semicolon", ";", KEY_SEMICOLON},
    {"apostrophe", "'", KEY_APOSTROPHE}, {"comma", ",", KEY_COMMA},
    {"period", ".", KEY_DOT}, {"slash", "/", KEY_SLASH}, {"grave", "`", KEY_GRAVE}
}};
constexpr const char *header = "inkline-shortcut-v2\n";

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return result;
}

const KeyDef *definition(std::string_view value) {
    const auto normalized = lower(value);
    for (const auto &key : keys)
        if (normalized == key.name || normalized == key.symbol) return &key;
    return nullptr;
}

std::string path_for(const std::string &directory, const KeyDef &key) {
    return directory + "/" + key.name;
}

void write_all(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
        const auto count = ::write(fd, bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error(std::string("Cannot write shortcut: ") + std::strerror(errno));
        bytes.remove_prefix(size_t(count));
    }
}

Binding decode(const std::string &path) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) throw std::runtime_error(std::string("Cannot open ") + path + ": " + std::strerror(errno));
    struct stat status{};
    if (fstat(fd, &status) < 0 || !S_ISREG(status.st_mode) || status.st_size < 0 || status.st_size > 65536) {
        close(fd); throw std::runtime_error("Shortcut file is not a small regular file: " + path);
    }
    std::string text(size_t(status.st_size), '\0');
    size_t offset = 0;
    while (offset < text.size()) {
        const auto count = read(fd, text.data() + offset, text.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); throw std::runtime_error("Cannot read shortcut: " + path); }
        offset += size_t(count);
    }
    close(fd);
    if (text.compare(0, std::strlen(header), header) != 0) throw std::runtime_error("Unknown shortcut format: " + path);
    if (text.find('\0') != std::string::npos) throw std::runtime_error("NUL byte in shortcut: " + path);
    Binding binding;
    size_t at = std::strlen(header);
    const auto mode_end = text.find('\n', at);
    if (mode_end == std::string::npos) throw std::runtime_error("Truncated shortcut: " + path);
    binding.mode = text.substr(at, mode_end - at);
    if (binding.mode != "terminal" && binding.mode != "epaper") throw std::runtime_error("Unknown shortcut mode: " + path);
    at = mode_end + 1;
    while (at < text.size()) {
        const auto end = text.find('\n', at);
        if (end == std::string::npos) throw std::runtime_error("Truncated shortcut: " + path);
        if (end - at > 4096) throw std::runtime_error("Shortcut argument too long: " + path);
        binding.command.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    if (binding.command.empty() || binding.command[0].empty() || binding.command.size() > 64) throw std::runtime_error("Invalid shortcut command: " + path);
    return binding;
}
}

const char *default_directory() { return "/home/root/.config/inkline/shortcuts.d"; }

int key_code(std::string_view name) {
    const auto *key = definition(name);
    return key ? int(key->code) : -1;
}

std::string key_name(unsigned code) {
    for (const auto &key : keys) if (key.code == code) return key.name;
    return {};
}

bool reserved(std::string_view name) {
    const auto *key = definition(name);
    return key && key->code == KEY_T;
}

std::vector<Binding> load(const std::string &directory, std::vector<std::string> *warnings) {
    std::vector<Binding> result;
    DIR *dir = opendir(directory.c_str());
    if (!dir) {
        if (errno != ENOENT && warnings) warnings->push_back(std::string("Cannot open ") + directory + ": " + std::strerror(errno));
        return result;
    }
    while (auto *entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const auto *key = definition(entry->d_name);
        if (!key || key->code == KEY_T || std::string(entry->d_name) != key->name) continue;
        try {
            auto binding = decode(path_for(directory, *key));
            binding.code = key->code; binding.key = key->name;
            result.push_back(std::move(binding));
        }
        catch (const std::exception &error) { if (warnings) warnings->push_back(error.what()); }
    }
    closedir(dir);
    std::sort(result.begin(), result.end(), [](const Binding &a, const Binding &b) { return a.key < b.key; });
    return result;
}

void register_binding(const std::string &directory, std::string_view name,
                      const std::vector<std::string> &command, std::string_view mode) {
    const auto *key = definition(name);
    if (!key) throw std::runtime_error("Key must be a letter, digit, or US punctuation key");
    if (key->code == KEY_T) throw std::runtime_error("Ctrl+Opt+Alt+T is permanently reserved for Inkline");
    if (command.empty() || command[0].empty() || command.size() > 64) throw std::runtime_error("A program and at most 63 arguments are required");
    if (mode != "terminal" && mode != "epaper") throw std::runtime_error("Mode must be terminal or epaper");
    std::string contents = std::string(header) + std::string(mode) + '\n';
    for (const auto &argument : command) {
        if (argument.find('\n') != std::string::npos || argument.find('\0') != std::string::npos || argument.size() > 4096)
            throw std::runtime_error("Shortcut arguments cannot contain NUL/newlines or exceed 4096 bytes");
        contents += argument + '\n';
    }
    if (contents.size() > 65536) throw std::runtime_error("Shortcut exceeds 64 KiB");
    const auto path = path_for(directory, *key);
    try {
        const auto existing = decode(path);
        if (existing.mode == mode && existing.command == command) return; // No repeated flash writes.
    } catch (const std::exception &) { /* Missing or invalid entry is replaced atomically. */ }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) throw std::runtime_error("Cannot create shortcut directory: " + error.message());
    chmod(directory.c_str(), 0700);
    const auto temporary = path + ".tmp." + std::to_string(getpid());
    int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) throw std::runtime_error(std::string("Cannot create shortcut: ") + std::strerror(errno));
    try {
        write_all(fd, contents);
        const int closed = close(fd); fd = -1;
        if (closed < 0) throw std::runtime_error(std::string("Cannot close shortcut: ") + std::strerror(errno));
        if (rename(temporary.c_str(), path.c_str()) < 0)
            throw std::runtime_error(std::string("Cannot install shortcut: ") + std::strerror(errno));
    } catch (...) {
        if (fd >= 0) close(fd);
        unlink(temporary.c_str()); throw;
    }
}

void deregister_binding(const std::string &directory, std::string_view name) {
    const auto *key = definition(name);
    if (!key) throw std::runtime_error("Unknown shortcut key");
    if (key->code == KEY_T) throw std::runtime_error("Ctrl+Opt+Alt+T is permanent and cannot be deregistered");
    if (unlink(path_for(directory, *key).c_str()) < 0 && errno != ENOENT)
        throw std::runtime_error(std::string("Cannot remove shortcut: ") + std::strerror(errno));
}

std::string display_command(const std::vector<std::string> &command) {
    std::string result;
    for (const auto &argument : command) {
        if (!result.empty()) result += ' ';
        const bool quote = argument.empty() || argument.find_first_of(" \t\r\n'\\\"$`;&|<>()*?[]{}!~#") != std::string::npos;
        if (!quote) { result += argument; continue; }
        result += '\'';
        for (const char c : argument) result += c == '\'' ? "'\\''" : std::string(1, c);
        result += '\'';
    }
    return result;
}

std::vector<std::string> parse_command(std::string_view text) {
    std::vector<std::string> result;
    std::string argument;
    char quote = 0;
    bool started = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\0' || c == '\n' || c == '\r') throw std::runtime_error("Use a single-line command");
        if (!quote && (c == ' ' || c == '\t')) {
            if (started) { result.push_back(argument); argument.clear(); started = false; }
            continue;
        }
        started = true;
        if (c == quote) { quote = 0; continue; }
        if (!quote && (c == '\'' || c == '"')) { quote = c; continue; }
        if (c == '\\' && quote != '\'') {
            if (i + 1 == text.size()) throw std::runtime_error("Finish the backslash escape");
            const char next = text[i + 1];
            if (!quote || next == '"' || next == '\\' || next == '$' || next == '`') {
                argument += next; ++i; continue;
            }
        }
        argument += c;
    }
    if (quote) throw std::runtime_error("Close the quoted argument");
    if (started) result.push_back(argument);
    if (result.empty() || result.front().empty()) throw std::runtime_error("Enter a program to run");
    return result;
}
}
