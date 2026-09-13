#include "rmt/stream.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n'; std::exit(1); \
} } while (0)

struct Frame {
    rmt::sixel::Bitmap bitmap;
    uint16_t x, y;
};

struct Fixture {
    std::unique_ptr<RmtCore, decltype(&rmt_core_free)> core{rmt_core_new(80, 24, nullptr), rmt_core_free};
    std::string replies;
    std::vector<Frame> frames;
    rmt::Stream stream;

    explicit Fixture(size_t limit = 4096) : stream(checked_core(), [this](rmt::sixel::Bitmap &&bitmap) {
        bool ground = false;
        CHECK(ghostty_terminal_get(terminal(), GHOSTTY_TERMINAL_DATA_VT_GROUND, &ground) == GHOSTTY_SUCCESS);
        CHECK(ground);
        frames.push_back({std::move(bitmap), cursor(GHOSTTY_TERMINAL_DATA_CURSOR_X),
                                            cursor(GHOSTTY_TERMINAL_DATA_CURSOR_Y)});
    }, limit) {
        CHECK(ghostty_terminal_resize(terminal(), 80, 24, 8, 16) == GHOSTTY_SUCCESS);
        CHECK(ghostty_terminal_set(terminal(), GHOSTTY_TERMINAL_OPT_USERDATA, this) == GHOSTTY_SUCCESS);
        CHECK(ghostty_terminal_set(terminal(), GHOSTTY_TERMINAL_OPT_WRITE_PTY,
                                  reinterpret_cast<const void *>(&Fixture::reply)) == GHOSTTY_SUCCESS);
    }
    RmtCore &checked_core() { CHECK(core); return *core; }
    GhosttyTerminal terminal() { return rmt_core_terminal(core.get()); }
    uint16_t cursor(GhosttyTerminalData field) {
        uint16_t value = 0;
        CHECK(ghostty_terminal_get(terminal(), field, &value) == GHOSTTY_SUCCESS);
        return value;
    }
    static void reply(GhosttyTerminal, void *ctx, const uint8_t *data, size_t len) {
        static_cast<Fixture *>(ctx)->replies.append(reinterpret_cast<const char *>(data), len);
    }
};

static void check_red(const rmt::sixel::Bitmap &bitmap) {
    CHECK(bitmap.width == 1 && bitmap.height == 1 && bitmap.rgba.size() == 4);
    CHECK(uint8_t(bitmap.rgba[0]) == 255 && bitmap.rgba[1] == 0 &&
          bitmap.rgba[2] == 0 && uint8_t(bitmap.rgba[3]) == 255);
}

int main() {
    const std::string sixel = "\033P0;1q\"1;1;1;1#1;2;100;0;0@\033\\";
    const std::string mixed = "ab" + sixel + "cd\033[6n";
    // Every two-part PTY fragmentation, including midway through ESC/ST.
    for (size_t split = 0; split <= mixed.size(); ++split) {
        Fixture f;
        f.stream.write(std::string_view(mixed).substr(0, split));
        f.stream.write(std::string_view(mixed).substr(split));
        CHECK(f.frames.size() == 1 && f.stream.images_rejected() == 0);
        CHECK(f.frames[0].x == 2 && f.frames[0].y == 0);
        check_red(f.frames[0].bitmap);
        CHECK(f.replies == "\033[1;5R");
        CHECK(f.stream.buffered_bytes() == 0);
    }
    {
        Fixture f;
        for (char c : mixed) f.stream.write({&c, 1});
        CHECK(f.frames.size() == 1 && f.replies == "\033[1;5R");
    }
    {
        Fixture f;
        const std::string c1 = std::string("\x90") + "0;1q\"1;1;1;1#1;2;100;0;0@" + "\x9c";
        for (char c : c1) f.stream.write({&c, 1});
        CHECK(f.frames.size() == 1);
        check_red(f.frames[0].bitmap);
        // C0/DEL in the introducer must not hide a valid sixel DCS.
        f.stream.write(std::string("\033\0P\x7f", 4) + sixel.substr(2));
        CHECK(f.frames.size() == 2);
    }

    // Compare unrelated traffic to the unwrapped VT engine, across every split.
    // D0 90 encodes Cyrillic A; C2 90 is a UTF-8 encoded C1 code point. Neither
    // continuation byte is a raw DCS introducer.
    for (const std::string text : {
        "a\033[3;4Hb\033[6n", "\033P$qm\033\\\033[6n",
        "\xd0\x90q@\033\\Z\033[6n", "\xc2\x90q@\033\\Z\033[6n",
        "\033]0;title\x90q@\007Z\033[6n", "\033P$qunknown\x90q@\033\\Z\033[6n"
    }) {
        Fixture direct;
        ghostty_terminal_vt_write(direct.terminal(), reinterpret_cast<const uint8_t *>(text.data()), text.size());
        for (size_t split = 0; split <= text.size(); ++split) {
            Fixture f;
            f.stream.write(std::string_view(text).substr(0, split));
            f.stream.write(std::string_view(text).substr(split));
            CHECK(f.frames.empty());
            CHECK(f.replies == direct.replies);
            CHECK(f.cursor(GHOSTTY_TERMINAL_DATA_CURSOR_X) == direct.cursor(GHOSTTY_TERMINAL_DATA_CURSOR_X));
            CHECK(f.cursor(GHOSTTY_TERMINAL_DATA_CURSOR_Y) == direct.cursor(GHOSTTY_TERMINAL_DATA_CURSOR_Y));
        }
    }

    for (char cancel : {'\030', '\032'}) {
        Fixture f;
        f.stream.write(std::string("\033Pq~") + cancel + "ABC\033[6n");
        CHECK(f.frames.empty() && f.stream.images_rejected() == 1);
        CHECK(f.replies == "\033[1;4R");
        f.stream.write(sixel);
        CHECK(f.frames.size() == 1);
    }
    {
        Fixture f;
        f.stream.write("\033Pq~\033[2;3HB\033[6n");
        CHECK(f.frames.empty() && f.stream.images_rejected() == 1);
        CHECK(f.replies == "\033[2;4R");
        f.stream.write("\033Pq~\033");
        f.stream.reset();
        f.stream.write(sixel);
        CHECK(f.frames.size() == 1 && f.frames[0].x == 0 && f.frames[0].y == 0);
    }
    {
        Fixture f(32);
        f.stream.write("\033Pq" + std::string(500, '?') + "\033");
        CHECK(f.frames.empty() && f.stream.buffered_bytes() <= 32);
        f.stream.write("\\ABC\033[6n");
        CHECK(f.stream.images_rejected() == 1 && f.replies == "\033[1;4R");
        f.stream.write("\033Pq\"1;1;1;1@\033\\");
        CHECK(f.frames.size() == 1);
    }
    {
        Fixture f(rmt::sixel::MAX_ENCODED_BYTES);
        f.stream.write("\033Pq" + std::string(rmt::sixel::MAX_ENCODED_BYTES, '?') + "\033\\Z");
        CHECK(f.frames.empty() && f.stream.images_rejected() == 1);
        CHECK(f.cursor(GHOSTTY_TERMINAL_DATA_CURSOR_X) == 1);
        f.stream.write(sixel);
        CHECK(f.frames.size() == 1);
    }
    {
        Fixture f;
        // A sixel between chunks must not replace libghostty's in-flight kitty
        // image. The sixel path must not inject its own kitty transmission.
        f.stream.write("\033_Ga=T,t=d,f=32,s=1,v=1,i=9,m=1;/wAA\033\\");
        f.stream.write(sixel);
        f.stream.write("\033_Gm=0;/w==\033\\");
        CHECK(f.frames.size() == 1 && f.replies.find("i=9;OK") != std::string::npos);
        GhosttyKittyGraphics graphics = nullptr;
        CHECK(ghostty_terminal_get(f.terminal(), GHOSTTY_TERMINAL_DATA_KITTY_GRAPHICS, &graphics) == GHOSTTY_SUCCESS);
        const auto img = ghostty_kitty_graphics_image(graphics, 9);
        CHECK(img);
        const uint8_t *pixels = nullptr;
        CHECK(ghostty_kitty_graphics_image_get(img, GHOSTTY_KITTY_IMAGE_DATA_DATA_PTR, &pixels) == GHOSTTY_SUCCESS);
        CHECK(pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 255);
    }
    std::cout << "Stream: all split points, UTF-8, passthrough, C1/C0, cancellation, limits and interleaved kitty passed.\n";
}
