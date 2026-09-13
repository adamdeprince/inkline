#include "rmt/sixel.hpp"
#include <cstdlib>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n'; std::exit(1); \
} } while (0)

int main() {
    using namespace rmt::sixel;
    Bitmap bitmap;
    std::string error;
    CHECK(decode("\033P0;1q\"1;1;2;6#1;2;100;0;0!2~\033\\", bitmap, error));
    CHECK(bitmap.width == 2 && bitmap.height == 6 && bitmap.rgba.size() == 48);
    for (size_t i = 0; i < bitmap.rgba.size(); i += 4)
        CHECK(uint8_t(bitmap.rgba[i]) == 255 && bitmap.rgba[i + 1] == 0 &&
              bitmap.rgba[i + 2] == 0 && uint8_t(bitmap.rgba[i + 3]) == 255);

    CHECK(decode("\033P0;1q\"1;1;2;6#1;1;120;50;100@\033\\", bitmap, error));
    CHECK(uint8_t(bitmap.rgba[0]) == 255 && bitmap.rgba[1] == 0 && bitmap.rgba[2] == 0);
    CHECK(bitmap.rgba[7] == 0); /* Transparent untouched column. */
    CHECK(decode("\033P0;0q\"1;1;2;6@\033\\", bitmap, error, 0x123456));
    CHECK(uint8_t(bitmap.rgba[4]) == 0x12 && uint8_t(bitmap.rgba[5]) == 0x34 &&
          uint8_t(bitmap.rgba[6]) == 0x56 && uint8_t(bitmap.rgba[7]) == 255);

    Palette palette;
    CHECK(decode("\033Pq#42;2;0;100;0@\033\\", bitmap, error, 0, &palette));
    CHECK(decode("\033Pq#42@\033\\", bitmap, error, 0, &palette));
    CHECK(bitmap.rgba[0] == 0 && uint8_t(bitmap.rgba[1]) == 255 && bitmap.rgba[2] == 0);
    const auto saved = palette.colors;
    CHECK(!decode("\033Pq#42;2;100;0;0!999999~\033\\", bitmap, error, 0, &palette));
    CHECK(palette.colors == saved && bitmap.rgba.empty());

    CHECK(decode("\033Pq\"1;1;1;7@-@\033\\", bitmap, error));
    CHECK(bitmap.width == 1 && bitmap.height == 7);
    CHECK(decode(std::string("\x90") + "0;1q\"1;1;1;1@" + "\x9c", bitmap, error));
    CHECK(bitmap.width == 1 && bitmap.height == 1);

    for (const auto &bad : {
        "\033Pq@", "\033Pq!0~\033\\", "\033Pq!65535~\033\\",
        "\033Pq\"1;1;4096;4096~\033\\", "\033Pq#1024~\033\\",
        "\033Pq#1;2;101;0;0~\033\\", "\033Pq~\030\033\\",
        "\033Pq~\032\033\\", "\033P$q~\033\\"
    }) {
        CHECK(!decode(bad, bitmap, error));
        CHECK(bitmap.rgba.empty() && !error.empty());
    }
    CHECK(!decode(std::string(MAX_ENCODED_BYTES + 1, '~'), bitmap, error));
    std::cout << "Sixel: RGB/HLS, transparency, palette reuse, rows, C1 framing and bounds passed.\n";
}
