#include "rmt/keyboard.hpp"
#include "rmt/renderer.hpp"
#include "rmt/stream.hpp"
#include <QGuiApplication>
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    RmtCore *core = rmt_core_new(80, 24, nullptr); CHECK(core);
    auto terminal = rmt_core_terminal(core);
    GhosttyColorRgb black{0, 0, 0}, white{255, 255, 255};
    ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &black);
    ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &white);
    {
        rmt::Renderer renderer(*core, 24);
        renderer.resize(640, 480);
        rmt::Stream stream(*core, [&](rmt::sixel::Bitmap &&bitmap) { renderer.sixel(std::move(bitmap)); });
        stream.set_control_handler([&](std::string_view control) { if (control == "\033[2J") renderer.clear_sixel(); });
        stream.write("\033[?25l");
        const QImage empty = renderer.frame();
        stream.write("Inkline \033[1mBOLD\033[0m\r\n");
        CHECK(renderer.frame() != empty);
        stream.write("\033[2J\033[H\033_Ga=T,f=32,s=1,v=1,c=4,r=3,i=1,C=1;AAAA/w==\033\\");
        const QImage kitty = renderer.frame();
        CHECK(qGray(kitty.pixel(2, 2)) == 0);
        stream.write("\033_Ga=d,d=A\033\\");
        CHECK(qGray(renderer.frame().pixel(2, 2)) == 255);
        stream.write("\033P0;1q\"1;1;30;6#0;2;0;0;0#0!30~\033\\");
        CHECK(renderer.sixel_bytes() == 30 * 6 * 4);
        CHECK(qGray(renderer.frame().pixel(2, 2)) == 0);
        for (const char c : std::string("\033[2J")) stream.write({&c, 1});
        CHECK(renderer.sixel_bytes() == 0);
        rmt::Keyboard keyboard(*core);
        QKeyEvent letter(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
        CHECK(keyboard.encode(letter) == "a");
        QKeyEvent interrupt(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, QString(QChar(3)));
        CHECK(keyboard.encode(interrupt) == "\003");
        QKeyEvent arrow(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
        CHECK(keyboard.encode(arrow) == "\033[A");
        stream.write("\033[?1h");
        CHECK(keyboard.encode(arrow) == "\033OA");
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\r");
        CHECK(keyboard.encode(enter) == "\r");
        stream.write("\033[>3u"); // Disambiguation and press/repeat/release reports.
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Up, Qt::NoModifier);
        CHECK(keyboard.encode(release).find(":3") != std::string::npos);
    }
    rmt_core_free(core);
    core = rmt_core_new(80, 24, nullptr); CHECK(core);
    {
        rmt::Renderer renderer(*core, 6, 1024 * 1024);
        renderer.resize(640, 480);
        CHECK(renderer.cell_width() > 0 && renderer.cell_height() > 0);
        CHECK(renderer.cols() > 100 && renderer.rows() > 24);
        rmt::Stream stream(*core, [&](rmt::sixel::Bitmap &&bitmap) { renderer.sixel(std::move(bitmap)); });
        const std::string sixel = "\033P0;1q\"1;1;30;6#0;2;0;0;0#0!30~\033\\";
        stream.write(sixel);
        const auto bytes = renderer.sixel_bytes(); CHECK(bytes == 30 * 6 * 4);
        stream.write(std::string(100, '\n'));
        renderer.reclaim(false); CHECK(renderer.sixel_bytes() == bytes);
        stream.write(sixel);
        CHECK(renderer.sixel_bytes() == 2 * bytes);
        renderer.reclaim(true); CHECK(renderer.sixel_bytes() == bytes);
        CHECK(!renderer.frame().isNull());
        // Once its anchor passes the history limit, reclaim without pressure.
        stream.write(std::string(600, '\n'));
        renderer.reclaim(false); CHECK(renderer.sixel_bytes() == 0);
    }
    rmt_core_free(core);
    std::puts("UI: text, kitty drawing/deletion, sixel drawing/clear, Ctrl-C, cursor mode and kitty key release passed.");
}
