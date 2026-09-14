#include "rmt/renderer.hpp"
#include <QFontMetrics>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rmt {
namespace {
void check(GhosttyResult result) { if (result != GHOSTTY_SUCCESS) throw std::runtime_error("Terminal renderer allocation failed"); }
QColor gray(GhosttyColorRgb c) { const int g = qGray(c.r, c.g, c.b); return QColor(g, g, g); }
}
Renderer::Renderer(RmtCore &core, int pixels, size_t sixel_budget) : terminal_(rmt_core_terminal(&core)), sixel_budget_(sixel_budget) {
    font_ = QFont("Noto Mono");
    font_.setStyleHint(QFont::Monospace);
    font_.setPixelSize(pixels);
    const QFontMetrics metrics(font_);
    cw_ = metrics.horizontalAdvance('M');
    ch_ = metrics.height() + 2;
    ascent_ = metrics.ascent() + 1;
    const auto *allocator = rmt_core_allocator(&core);
    try {
        check(ghostty_render_state_new(allocator, &render_));
        check(ghostty_render_state_row_iterator_new(allocator, &row_));
        check(ghostty_render_state_row_cells_new(allocator, &cells_));
        check(ghostty_kitty_graphics_placement_iterator_new(allocator, &placements_));
    } catch (...) {
        ghostty_kitty_graphics_placement_iterator_free(placements_);
        ghostty_render_state_row_cells_free(cells_);
        ghostty_render_state_row_iterator_free(row_);
        ghostty_render_state_free(render_);
        throw;
    }
}
Renderer::~Renderer() {
    clear_sixel();
    ghostty_kitty_graphics_placement_iterator_free(placements_);
    ghostty_render_state_row_cells_free(cells_);
    ghostty_render_state_row_iterator_free(row_);
    ghostty_render_state_free(render_);
}
void Renderer::resize(int width, int height) {
    cols_ = uint16_t(std::clamp(width / cw_, 2, 512));
    rows_ = uint16_t(std::clamp(height / ch_, 2, 256));
    check(ghostty_terminal_resize(terminal_, cols_, rows_, cw_, ch_));
}
void Renderer::drop_sixel() {
    sixel_bytes_ -= size_t(sixels_.front().image.sizeInBytes());
    ghostty_tracked_grid_ref_free(sixels_.front().anchor);
    sixels_.pop_front();
}
void Renderer::clear_sixel() { while (!sixels_.empty()) drop_sixel(); }
void Renderer::sixel(sixel::Bitmap &&bitmap) {
    const size_t budget = sixel_budget_;
    const size_t bytes = bitmap.rgba.size();
    if (!bytes || bytes > budget) return;
    while (!sixels_.empty() && (sixel_bytes_ + bytes > budget || sixels_.size() >= 128)) drop_sixel();
    QImage image(reinterpret_cast<const uchar *>(bitmap.rgba.data()), int(bitmap.width),
                 int(bitmap.height), int(bitmap.width * 4), QImage::Format_RGBA8888);
    // Own exactly one retained copy, independent of the decoder's staging buffer.
    image = image.copy();
    if (image.isNull()) return;
    uint16_t x = 0, y = 0;
    ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x);
    ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);
    GhosttyPoint point{};
    point.tag = GHOSTTY_POINT_TAG_ACTIVE;
    point.value.coordinate = {x, y};
    GhosttyTrackedGridRef anchor = nullptr;
    if (ghostty_terminal_grid_ref_track(terminal_, point, &anchor) != GHOSTTY_SUCCESS) return;
    GhosttyTerminalScreen screen = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
    ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &screen);
    sixels_.push_back({std::move(image), anchor, screen});
    sixel_bytes_ += bytes;
    // Leave the cursor on the row below the image, scrolling normally.
    const std::string advance(size_t((bitmap.height + ch_ - 1) / ch_), '\n');
    ghostty_terminal_vt_write(terminal_, reinterpret_cast<const uint8_t *>(advance.data()), advance.size());
    const uint8_t cr = '\r';
    ghostty_terminal_vt_write(terminal_, &cr, 1);
}
void Renderer::cells(QPainter &p, const GhosttyRenderStateColors &colors, bool backgrounds) {
    check(ghostty_render_state_get(render_, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_));
    int y = 0;
    while (ghostty_render_state_row_iterator_next(row_)) {
        check(ghostty_render_state_row_get(row_, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells_));
        int x = 0;
        while (ghostty_render_state_row_cells_next(cells_)) {
            GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
            ghostty_render_state_row_cells_get(cells_, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);
            GhosttyColorRgb fg = colors.foreground, bg = colors.background;
            ghostty_render_state_row_cells_get(cells_, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);
            const bool explicit_bg = ghostty_render_state_row_cells_get(cells_, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg) == GHOSTTY_SUCCESS;
            if (style.inverse) std::swap(fg, bg);
            GhosttyCell raw{};
            GhosttyCellWide wide{};
            ghostty_render_state_row_cells_get(cells_, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
            const QRect rect(x * cw_, y * ch_, cw_, ch_);
            if (backgrounds) {
                if (explicit_bg || style.inverse) p.fillRect(rect, gray(bg));
            } else if (wide != GHOSTTY_CELL_WIDE_SPACER_TAIL && wide != GHOSTTY_CELL_WIDE_SPACER_HEAD && !style.invisible) {
                char text[4096];
                GhosttyBuffer buffer{reinterpret_cast<uint8_t *>(text), sizeof(text), 0};
                if (ghostty_render_state_row_cells_get(cells_, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8, &buffer) == GHOSTTY_SUCCESS && buffer.len) {
                    QFont f = font_; f.setBold(style.bold); f.setItalic(style.italic);
                    f.setUnderline(style.underline != 0); f.setStrikeOut(style.strikethrough); f.setOverline(style.overline);
                    p.setFont(f);
                    QColor color = gray(fg); if (style.faint) color.setAlpha(150);
                    p.setPen(color);
                    p.save();
                    p.setClipRect(QRect(rect.x(), rect.y(), cw_ * (wide == GHOSTTY_CELL_WIDE_WIDE ? 2 : 1), ch_));
                    p.drawText(QPoint(rect.x(), rect.y() + ascent_), QString::fromUtf8(text, qsizetype(buffer.len)));
                    p.restore();
                }
            }
            ++x;
        }
        ++y;
    }
}
void Renderer::kitty(QPainter &p, GhosttyKittyPlacementLayer layer) {
    GhosttyKittyGraphics graphics = nullptr;
    if (ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_KITTY_GRAPHICS, &graphics) != GHOSTTY_SUCCESS) return;
    ghostty_kitty_graphics_get(graphics, GHOSTTY_KITTY_GRAPHICS_DATA_PLACEMENT_ITERATOR, &placements_);
    ghostty_kitty_graphics_placement_iterator_set(placements_, GHOSTTY_KITTY_GRAPHICS_PLACEMENT_ITERATOR_OPTION_LAYER, &layer);
    while (ghostty_kitty_graphics_placement_next(placements_)) {
        uint32_t id = 0, xo = 0, yo = 0;
        ghostty_kitty_graphics_placement_get(placements_, GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_IMAGE_ID, &id);
        auto image = ghostty_kitty_graphics_image(graphics, id);
        GhosttyKittyGraphicsPlacementRenderInfo info = GHOSTTY_INIT_SIZED(GhosttyKittyGraphicsPlacementRenderInfo);
        if (!image || ghostty_kitty_graphics_placement_render_info(placements_, image, terminal_, &info) != GHOSTTY_SUCCESS || !info.viewport_visible) continue;
        ghostty_kitty_graphics_placement_get(placements_, GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_X_OFFSET, &xo);
        ghostty_kitty_graphics_placement_get(placements_, GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_Y_OFFSET, &yo);
        uint32_t width = 0, height = 0;
        size_t len = 0;
        const uint8_t *data = nullptr;
        GhosttyKittyImageFormat format{};
        ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_WIDTH, &width);
        ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_HEIGHT, &height);
        ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_FORMAT, &format);
        ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_DATA_PTR, &data);
        ghostty_kitty_graphics_image_get(image, GHOSTTY_KITTY_IMAGE_DATA_DATA_LEN, &len);
        QImage::Format qformat = QImage::Format_Invalid;
        unsigned bpp = 0;
        switch (format) {
        case GHOSTTY_KITTY_IMAGE_FORMAT_RGBA: qformat = QImage::Format_RGBA8888; bpp = 4; break;
        case GHOSTTY_KITTY_IMAGE_FORMAT_RGB: qformat = QImage::Format_RGB888; bpp = 3; break;
        case GHOSTTY_KITTY_IMAGE_FORMAT_GRAY: qformat = QImage::Format_Grayscale8; bpp = 1; break;
        default: break;
        }
        if (!bpp || !data || uint64_t(width) * height * bpp != len || width > 8192 || height > 8192) continue;
        const QImage pixels(data, int(width), int(height), int(width * bpp), qformat);
        p.drawImage(QRectF(double(info.viewport_col) * cw_ + xo, double(info.viewport_row) * ch_ + yo,
                           info.pixel_width, info.pixel_height), pixels,
                    QRectF(info.source_x, info.source_y, info.source_width, info.source_height));
    }
}
QImage Renderer::frame() {
    check(ghostty_render_state_update(render_, terminal_));
    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    check(ghostty_render_state_get(render_, GHOSTTY_RENDER_STATE_DATA_COLORS, &colors));
    QImage image(cols_ * cw_, rows_ * ch_, QImage::Format_RGB32);
    if (image.isNull()) throw std::bad_alloc();
    image.fill(gray(colors.background));
    QPainter p(&image);
    p.setRenderHint(QPainter::TextAntialiasing);
    kitty(p, GHOSTTY_KITTY_PLACEMENT_LAYER_BELOW_BG);
    cells(p, colors, true);
    kitty(p, GHOSTTY_KITTY_PLACEMENT_LAYER_BELOW_TEXT);
    cells(p, colors, false);
    GhosttyTerminalScreen screen = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
    ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &screen);
    GhosttyTerminalScrollbar scrollbar{};
    ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar);
    for (auto it = sixels_.begin(); it != sixels_.end();) {
        if (!ghostty_tracked_grid_ref_has_value(it->anchor)) {
            sixel_bytes_ -= size_t(it->image.sizeInBytes());
            ghostty_tracked_grid_ref_free(it->anchor);
            it = sixels_.erase(it);
            continue;
        }
        GhosttyPointCoordinate pos{};
        if (screen == it->screen && ghostty_tracked_grid_ref_point(it->anchor, GHOSTTY_POINT_TAG_SCREEN, &pos) == GHOSTTY_SUCCESS) {
            const int64_t y = (int64_t(pos.y) - int64_t(scrollbar.offset)) * ch_;
            if (y < image.height() && y + it->image.height() > 0) p.drawImage(QPoint(pos.x * cw_, int(y)), it->image);
        }
        ++it;
    }
    kitty(p, GHOSTTY_KITTY_PLACEMENT_LAYER_ABOVE_TEXT);
    GhosttyRenderStateCursor cursor = GHOSTTY_INIT_SIZED(GhosttyRenderStateCursor);
    ghostty_render_state_get(render_, GHOSTTY_RENDER_STATE_DATA_CURSOR, &cursor);
    if (cursor.viewport_has_value && cursor.visible) {
        p.setPen(QPen(gray(colors.foreground), 2));
        p.drawRect(QRect(cursor.viewport_x * cw_ + 1, cursor.viewport_y * ch_ + 1, cw_ - 2, ch_ - 2));
    }
    p.end();
    ghostty_render_state_clean(render_);
    // The retained display surface is grayscale, independent of color traffic.
    return image.convertToFormat(QImage::Format_Grayscale8);
}
}
