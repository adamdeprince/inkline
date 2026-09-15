#ifndef RMT_HOTKEY_CONFIG_HPP
#define RMT_HOTKEY_CONFIG_HPP

#include <string>
#include <string_view>
#include <vector>

namespace rmt::hotkeys {

struct Binding {
    unsigned code = 0;
    std::string key;
    std::vector<std::string> command;
    std::string mode = "terminal";
};

const char *default_directory();
int key_code(std::string_view name);
std::string key_name(unsigned code);
bool reserved(std::string_view name);
std::vector<Binding> load(const std::string &directory, std::vector<std::string> *warnings = nullptr);
void register_binding(const std::string &directory, std::string_view key,
                      const std::vector<std::string> &command, std::string_view mode = "terminal");
void deregister_binding(const std::string &directory, std::string_view key);
std::string display_command(const std::vector<std::string> &command);

}
#endif
