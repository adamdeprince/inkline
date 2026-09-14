#include "rmt/clipboard.hpp"
#include "rmt/renderer.hpp"
#include <QGuiApplication>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
struct Fixture {
    std::unique_ptr<RmtCore, decltype(&rmt_core_free)> core{rmt_core_new(4, 3, nullptr), rmt_core_free};
    rmt::Clipboard clipboard;
    std::string reply;
    Fixture() {
        CHECK(core); auto t = terminal();
        ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_USERDATA, this);
        ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_WRITE_PTY, reinterpret_cast<const void *>(write));
        ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_CLIPBOARD_READ, reinterpret_cast<const void *>(read));
        ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_CLIPBOARD_WRITE, reinterpret_cast<const void *>(copy));
    }
    GhosttyTerminal terminal() { return rmt_core_terminal(core.get()); }
    void feed(std::string_view s) { ghostty_terminal_vt_write(terminal(), reinterpret_cast<const uint8_t *>(s.data()), s.size()); }
    static void write(GhosttyTerminal, void *c, const uint8_t *p, size_t n) { static_cast<Fixture *>(c)->reply.append(reinterpret_cast<const char *>(p), n); }
    static void read(GhosttyTerminal, void *c, const GhosttyClipboardRead *r) { static_cast<Fixture *>(c)->clipboard.read(r); }
    static void copy(GhosttyTerminal, void *c, const GhosttyClipboardWrite *r) { static_cast<Fixture *>(c)->clipboard.write(r); }
};
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    Fixture f;
    const QByteArray value = QString::fromUtf8("你好\nhello").toUtf8();
    const QByteArray osc = "\033]52;c;" + value.toBase64() + "\033\\";
    // Parser boundaries may occur anywhere, including inside base64 and ST.
    for (const char c : osc) f.feed({&c, 1});
    CHECK(f.clipboard.text() == value);
    f.feed("\033]52;c;?\a"); CHECK(f.reply.find(value.toBase64().toStdString()) != std::string::npos);
    CHECK(!f.clipboard.set(QByteArray(rmt::Clipboard::MAX_BYTES + 1, 'a')));
    CHECK(!f.clipboard.set(QByteArray("\xff", 1))); CHECK(f.clipboard.text() == value);
    CHECK(!f.clipboard.set(QByteArray("\xe2", 1))); CHECK(f.clipboard.text() == value);
    CHECK(f.clipboard.set("a\nb\033[201~"));
    f.reply.clear(); f.feed("\033[?2004h");
    CHECK(f.clipboard.paste(f.terminal()) == GHOSTTY_SUCCESS);
    CHECK(f.reply == "\033[200~a\nb [201~\033[201~");
    f.reply.clear(); f.feed("\033[?2004l");
    CHECK(f.clipboard.paste(f.terminal()) == GHOSTTY_SUCCESS);
    CHECK(f.reply == "a\rb [201~");
    // Kitty paste-events mode advertises content; it does not type the bytes.
    f.reply.clear(); f.feed("\033[?5522h");
    CHECK(f.clipboard.paste(f.terminal()) == GHOSTTY_SUCCESS);
    CHECK(f.reply.find("5522") != std::string::npos);
    f.feed("\033]52;c;\a"); CHECK(f.clipboard.text().isEmpty());
    // Bounded writes preserve the previous clipboard rather than truncating.
    CHECK(f.clipboard.set("keep"));
    auto large = QByteArray(rmt::Clipboard::MAX_BYTES + 1, 'x').toBase64();
    auto large_osc = QByteArray("\033]52;c;") + large + "\a";
    f.feed({large_osc.constData(), size_t(large_osc.size())}); CHECK(f.clipboard.text() == "keep");
    Fixture grid; grid.feed("abcde界");
    rmt::Selection selection(grid.terminal());
    selection.begin(0, 0); selection.extend(2, 1); selection.release();
    CHECK(selection.text() == QByteArray("abcde界"));
    selection.begin(2, 1); selection.extend(0, 0); selection.release();
    CHECK(selection.text() == QByteArray("abcde界"));
    // Reflow must keep selected text, not stale row/column coordinates.
    CHECK(ghostty_terminal_resize(grid.terminal(), 8, 3, 10, 20) == GHOSTTY_SUCCESS);
    CHECK(selection.text() == QByteArray("abcde界"));
    rmt::Renderer renderer(*grid.core, 24); renderer.resize(600, 250);
    const auto selected = renderer.frame(); selection.clear(); CHECK(!selection.text());
    CHECK(renderer.frame() != selected);
    std::puts("Clipboard: OSC 52, Unicode, limits, bracketed paste, kitty paste events, selection and reflow passed.");
}
