#include "rmt/hotkey_config.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
template<class F> void rejects(F work) { bool rejected = false; try { work(); } catch (const std::exception &) { rejected = true; } CHECK(rejected); }
int main() {
    namespace h = rmt::hotkeys;
    char path[] = "/tmp/inkline-hotkeys.XXXXXX"; CHECK(mkdtemp(path));
    const std::string directory(path);
    const std::vector<std::string> command{"/bin/echo", "two words", "$(touch /tmp/never)", "'quote'", "", "日本語"};
    h::register_binding(directory, "E", command);
    auto bindings = h::load(directory);
    CHECK(bindings.size() == 1 && bindings[0].key == "e" && bindings[0].code == 18 && bindings[0].command == command && bindings[0].mode == "terminal");
    struct stat before{}, after{};
    CHECK(stat((directory + "/e").c_str(), &before) == 0 && (before.st_mode & 0777) == 0600);
    h::register_binding(directory, "e", command);
    CHECK(stat((directory + "/e").c_str(), &after) == 0 && before.st_ino == after.st_ino); // identical update causes no write
    h::register_binding(directory, ";", {"/bin/true"}, "epaper");
    bindings = h::load(directory);
    CHECK(bindings.size() == 2 && bindings[1].key == "semicolon" && bindings[1].mode == "epaper");
    rejects([&] { h::register_binding(directory, "T", command); });
    rejects([&] { h::deregister_binding(directory, "t"); });
    rejects([&] { h::register_binding(directory, "../outside", command); });
    rejects([&] { h::register_binding(directory, "a", {""}); });
    rejects([&] { h::register_binding(directory, "a", {"echo", "line\nbreak"}); });
    rejects([&] { h::register_binding(directory, "a", {"echo", std::string("nul\0byte", 8)}); });
    rejects([&] { h::register_binding(directory, "a", std::vector<std::string>(64, std::string(4096, 'x'))); });
    rejects([&] { h::register_binding(directory, "a", command, "invalid"); });
    CHECK(h::reserved("T") && h::key_code("Backspace") < 0 && h::key_code("9") == 10);
    // A handwritten T entry cannot replace the protected launcher; symlinks
    // and corrupt entries are rejected without preventing other bindings.
    std::filesystem::copy_file(directory + "/e", directory + "/t");
    CHECK(symlink((directory + "/e").c_str(), (directory + "/a").c_str()) == 0);
    std::ofstream(directory + "/b") << "not a shortcut\n";
    std::vector<std::string> warnings;
    CHECK(h::load(directory, &warnings).size() == 2 && warnings.size() == 2);
    h::register_binding(directory, "a", {"/bin/true"}); // replaces the link, not its target
    CHECK(h::load(directory).size() == 3 && h::load(directory)[1].command == command);
    h::deregister_binding(directory, "e"); h::deregister_binding(directory, "e");
    CHECK(h::load(directory).size() == 2);
    std::filesystem::remove_all(directory);
    std::puts("Global shortcuts: reserved T, literal argv, validation, atomic updates and no-op writes passed.");
}
