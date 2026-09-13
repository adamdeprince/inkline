/* Distributed under the GNU GPL, version 3 or later. */
#ifndef RMT_SIXEL_HPP
#define RMT_SIXEL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rmt::sixel {
constexpr size_t MAX_ENCODED_BYTES = 8 * 1024 * 1024;
constexpr size_t MAX_PIXELS = 8 * 1024 * 1024;
constexpr uint32_t MAX_DIMENSION = 4096;
constexpr unsigned MAX_COLORS = 1024;

struct Palette {
    std::array<uint32_t, MAX_COLORS> colors;
    Palette();
};

struct Bitmap {
    uint32_t width = 0, height = 0;
    std::string rgba;
};

/* Decode a complete DCS in memory. Output is empty on invalid input.
 * Passing a palette enables palette reuse; it changes only after success.
 * This codec does not implement terminal placement/scroll/erase semantics. */
bool decode(std::string_view dcs, Bitmap &output, std::string &error,
            uint32_t background_rgb = 0xffffff, Palette *palette = nullptr);
}
#endif
