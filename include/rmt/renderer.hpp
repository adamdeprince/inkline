#ifndef RMT_RENDERER_HPP
#define RMT_RENDERER_HPP
#include "rmt/core.h"
#include "rmt/sixel.hpp"
#include <QFont>
#include <QImage>
#include <QPainter>
#include <deque>
#include <array>

namespace rmt {
// All terminal access and drawing occur on the GUI thread. Images are RAM only.
class Renderer {
public:
    explicit Renderer(RmtCore &core, int font_pixels = 26, size_t sixel_budget = 32 * 1024 * 1024);
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    void resize(int width, int height);
    void set_font_size(int pixels);
    void set_text_darkness(int value);
    QImage frame();
    void sixel(sixel::Bitmap &&bitmap);
    void clear_sixel();
    void reclaim(bool memory_pressure);
    int cell_width() const { return cw_; }
    int cell_height() const { return ch_; }
    uint16_t cols() const { return cols_; }
    uint16_t rows() const { return rows_; }
    size_t sixel_bytes() const { return sixel_bytes_; }
private:
    struct Overlay { QImage image; GhosttyTrackedGridRef anchor; GhosttyTerminalScreen screen; };
    RmtCore &core_;
    GhosttyTerminal terminal_;
    GhosttyRenderState render_ = nullptr;
    GhosttyRenderStateRowIterator row_ = nullptr;
    GhosttyRenderStateRowCells cells_ = nullptr;
    GhosttyKittyGraphicsPlacementIterator placements_ = nullptr;
    QFont font_;
    int darkness_ = 50;
    std::array<uchar, 256> coverage_{};
    int cw_, ch_, ascent_;
    uint16_t cols_ = 80, rows_ = 24;
    std::deque<Overlay> sixels_;
    size_t sixel_bytes_ = 0;
    size_t sixel_budget_;
    void cells(QPainter &p, const GhosttyRenderStateColors &colors, bool backgrounds);
    void kitty(QPainter &p, GhosttyKittyPlacementLayer layer);
    void drop_sixel(std::deque<Overlay>::iterator it);
    bool visible(const Overlay &overlay) const;
};
}
#endif
