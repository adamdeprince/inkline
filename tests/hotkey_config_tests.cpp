#include "rmt/hotkey_config.hpp"
#include "rmt/shortcut_chord.hpp"
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
    for (const bool folio : {false, true}) {
        rmt::ShortcutChord chord(folio);
        chord.update(folio ? 107 : 125, 1); // Plain Super remains available.
        CHECK(!chord.active());
        chord.update(56, 1); CHECK(!chord.active()); // Left Alt is not the launcher modifier.
        chord.update(56, 0); chord.update(100, 1); CHECK(chord.active());
        chord.update(14, 1); CHECK(chord.recovery_held());
        for (const auto extra : {29u, 97u, 56u, 42u, 54u, 58u}) {
            chord.update(extra, 1); CHECK(!chord.active() && !chord.recovery_held());
            chord.update(extra, 0); CHECK(chord.active() && chord.recovery_held());
        }
        chord.update(100, 0); CHECK(!chord.active());
        chord.reset();
        chord.update(100, 1); chord.update(folio ? 125 : 107, 1);
        CHECK(!chord.active()); // USB End cannot replace Super; Folio uses its own Opt code.
        chord.update(folio ? 107 : 126, 1); CHECK(chord.active());
    }
    const std::vector<std::string> literal{"echo", "two words", "", "'", "\\", "a\"b", "$HOME", "$(id)", "*", ";", "日本語"};
    CHECK(h::parse_command(h::display_command(literal)) == literal);
    CHECK(h::parse_command("echo a\\ b \"two words\" ''") == std::vector<std::string>({"echo", "a b", "two words", ""}));
    CHECK(h::parse_command("sh -c 'echo hello | cat'") == std::vector<std::string>({"sh", "-c", "echo hello | cat"}));
    for (const auto invalid : {"", "echo 'unfinished", "echo \"unfinished", "echo trailing\\", "echo\nnext"})
        rejects([&] { h::parse_command(invalid); });
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
