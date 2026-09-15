#include "rmt/keyboard.hpp"
#include "rmt/mouse.hpp"
#include "rmt/renderer.hpp"
#include "rmt/stream.hpp"
#include "rmt/unicode_keyboard.hpp"
#include <QGuiApplication>
#include <algorithm>
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
        stream.set_control_handler([&](std::string_view control) { if (control == "\033[2J") renderer.clear_graphics(); });
        stream.write("\033[?25l");
        const QRect initial_dirty = renderer.render();
        CHECK(initial_dirty == renderer.image().rect());
        CHECK(renderer.render().isEmpty());
        const QImage empty = renderer.frame();
        stream.write("Inkline \033[1mBOLD\033[0m\r\n");
        const QRect text_dirty = renderer.render();
        CHECK(!text_dirty.isEmpty() && text_dirty.height() <= 2 * renderer.cell_height());
        CHECK(renderer.image() != empty);
        const auto normal = renderer.frame();
        const auto cols = renderer.cols(), rows = renderer.rows();
        renderer.set_text_darkness(100); const auto dark = renderer.frame();
        renderer.set_text_darkness(0); const auto light = renderer.frame();
        int darker = 0, lighter = 0;
        for (int y = 0; y < normal.height(); ++y) for (int x = 0; x < normal.width(); ++x) {
            const int n = qGray(normal.pixel(x, y)), d = qGray(dark.pixel(x, y)), l = qGray(light.pixel(x, y));
            CHECK(d <= n && n <= l);
            if (n == 0 || n == 255) CHECK(d == n && l == n);
            darker += d < n; lighter += l > n;
        }
        CHECK(darker > 0 && lighter > 0);
        CHECK(renderer.cols() == cols && renderer.rows() == rows);
        renderer.set_text_darkness(50); CHECK(renderer.frame() == normal);
        // A minimum luminance gap moves only foreground colors that are too
        // close to their effective cell background.
        renderer.set_minimum_contrast(0);
        stream.write("\033[2J\033[H\033[38;2;230;230;230mM\033[0m");
        const auto low_contrast = renderer.frame();
        renderer.set_minimum_contrast(50);
        const auto readable = renderer.frame();
        int low_ink = 255, readable_ink = 255;
        for (int y = 0; y < renderer.cell_height(); ++y)
            for (int x = 0; x < renderer.cell_width(); ++x) {
                low_ink = std::min(low_ink, qGray(low_contrast.pixel(x, y)));
                readable_ink = std::min(readable_ink, qGray(readable.pixel(x, y)));
            }
        CHECK(low_ink >= 220);
        CHECK(readable_ink <= 140);
        renderer.set_minimum_contrast(35);
        stream.write("\033[2J\033[H\033_Ga=T,f=32,s=1,v=1,c=4,r=3,i=1,C=1;AAAA/w==\033\\");
        const QImage kitty = renderer.frame();
        CHECK(qGray(kitty.pixel(2, 2)) == 0);
        renderer.set_text_darkness(100); CHECK(renderer.frame() == kitty);
        renderer.set_text_darkness(0); CHECK(renderer.frame() == kitty);
        renderer.set_text_darkness(50);
        stream.write("\033[3J"); // history-only erase preserves visible graphics
        CHECK(qGray(renderer.frame().pixel(2, 2)) == 0);
        stream.write("\033[2J");
        CHECK(qGray(renderer.frame().pixel(2, 2)) == 255);
        stream.write("\033[H\033_Ga=T,f=32,s=1,v=1,c=4,r=3,i=1,C=1;AAAA/w==\033\\");
        CHECK(qGray(renderer.frame().pixel(2, 2)) == 0);
        stream.write("\033_Ga=d,d=A\033\\");
        CHECK(qGray(renderer.frame().pixel(2, 2)) == 255);
        stream.write("\033P0;1q\"1;1;30;6#0;2;0;0;0#0!30~\033\\");
        CHECK(renderer.sixel_bytes() == 30 * 6 * 4);
        CHECK(qGray(renderer.frame().pixel(2, 2)) == 0);
        const auto sixel_frame = renderer.frame();
        renderer.set_text_darkness(100); CHECK(renderer.frame() == sixel_frame);
        renderer.set_text_darkness(50);
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
        rmt::Mouse mouse(*core); mouse.resize(800, 480, 10, 20);
        stream.write("\033[?1000h\033[?1006h");
        CHECK(mouse.encode(rmt::Mouse::Action::Press, {15, 25}) == "\033[<0;2;2M");
        CHECK(mouse.encode(rmt::Mouse::Action::Motion, {25, 25}).empty());
        CHECK(mouse.encode(rmt::Mouse::Action::Release, {25, 25}) == "\033[<0;3;2m");
        stream.write("\033[?1003h");
        CHECK(mouse.encode(rmt::Mouse::Action::Motion, {35, 45}) == "\033[<35;4;3M");
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
    CHECK(rmt::UnicodeKeyboard::category_for(U'A') == rmt::UnicodeKeyboard::Letters);
    CHECK(rmt::UnicodeKeyboard::category_for(U'\u0301') == rmt::UnicodeKeyboard::Marks);
    CHECK(rmt::UnicodeKeyboard::category_for(U'\U0001f642') == rmt::UnicodeKeyboard::Symbols);
    CHECK(rmt::UnicodeKeyboard::category_for(U'\u200d') == rmt::UnicodeKeyboard::Format);
    CHECK(rmt::UnicodeKeyboard::category_for(U'\ue000') == rmt::UnicodeKeyboard::PrivateUse);
    CHECK(!rmt::UnicodeKeyboard::selectable(0x1b) && !rmt::UnicodeKeyboard::selectable(0xd800));
    CHECK(rmt::UnicodeKeyboard::utf8(U'\U0001f642') == QString::fromUtf8("🙂").toUtf8());
    CHECK(rmt::UnicodeKeyboard::index_of(rmt::UnicodeKeyboard::Symbols, U'\U0001f642') >= 0);
    std::puts("UI: incremental text, graphics, keyboard, pen mouse encoding and categorized Unicode passed.");
}
