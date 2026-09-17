#include "rmt/renderer.hpp"
#include <QFontMetrics>
#include <QFontDatabase>
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rmt {
namespace {
void check(GhosttyResult result) { if (result != GHOSTTY_SUCCESS) throw std::runtime_error("Terminal renderer allocation failed"); }
QColor gray(GhosttyColorRgb c) { const int g = qGray(c.r, c.g, c.b); return QColor(g, g, g); }
}
Renderer::Renderer(RmtCore &core, int pixels, size_t sixel_budget) : core_(core), terminal_(rmt_core_terminal(&core)), sixel_budget_(sixel_budget) {
    static const int cjk_font = QFontDatabase::addApplicationFont(QCoreApplication::applicationDirPath() + "/assets/fonts/NotoSansMonoCJKsc-Regular.otf");
    static const int symbol_font = QFontDatabase::addApplicationFont(QCoreApplication::applicationDirPath() + "/assets/fonts/NotoSansSymbols2-Regular.otf");
    static const int unifont = QFontDatabase::addApplicationFont(QCoreApplication::applicationDirPath() + "/assets/fonts/Unifont-18.0.01.otf");
    static const int unifont_upper = QFontDatabase::addApplicationFont(QCoreApplication::applicationDirPath() + "/assets/fonts/UnifontUpper-18.0.01.otf");
    if (cjk_font < 0 || symbol_font < 0 || unifont < 0 || unifont_upper < 0)
        throw std::runtime_error("Inkline Unicode fonts are missing; reinstall the complete bundle");
    font_.setFamilies({"Noto Mono", "Noto Sans Mono CJK SC", "Noto Sans Symbols 2", "Unifont", "Unifont Upper"});
    font_.setStyleHint(QFont::Monospace);
    set_font_size(pixels);
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
    // Destruction must not invalidate a render state that is about to be
    // released (or allow an error-returning C API to escape a destructor).
    while (!sixels_.empty()) drop_sixel(sixels_.begin());
    ghostty_kitty_graphics_placement_iterator_free(placements_);
    ghostty_render_state_row_cells_free(cells_);
    ghostty_render_state_row_iterator_free(row_);
    ghostty_render_state_free(render_);
}
void Renderer::set_font_size(int pixels) {
    font_.setPixelSize(pixels);
    const QFontMetrics metrics(font_);
    cw_ = metrics.horizontalAdvance('M');
    ch_ = metrics.height() + 2;
    ascent_ = metrics.ascent() + 1;
    surface_ = {};
    if (render_) invalidate();
}
void Renderer::set_text_darkness(int value) {
    value = std::clamp(value, 0, 100);
    if (darkness_ == value) return;
    darkness_ = value;
    const double exponent = std::pow(2.5, (50 - darkness_) / 50.0);
    for (size_t a = 0; a < coverage_.size(); ++a)
        coverage_[a] = uchar(std::lround(255 * std::pow(a / 255.0, exponent)));
    invalidate();
}
void Renderer::set_minimum_contrast(int value) {
    value = std::clamp(value, 0, 100);
    if (minimum_contrast_ == value) return;
    minimum_contrast_ = value;
    invalidate();
}
QColor Renderer::text_color(GhosttyColorRgb foreground, GhosttyColorRgb background) const {
    int fg = qGray(foreground.r, foreground.g, foreground.b);
    const int bg = qGray(background.r, background.g, background.b);
    const int minimum = int(std::lround(255 * minimum_contrast_ / 100.0));
    if (std::abs(fg - bg) < minimum) {
        const int darker = bg - minimum;
        const int lighter = bg + minimum;
        if (darker < 0) fg = std::min(255, lighter);
        else if (lighter > 255) fg = std::max(0, darker);
        else fg = std::abs(fg - darker) <= std::abs(lighter - fg) ? darker : lighter;
    }
    return QColor(fg, fg, fg);
}
void Renderer::resize(int width, int height) {
    cols_ = uint16_t(std::clamp(width / cw_, 2, 512));
    rows_ = uint16_t(std::clamp(height / ch_, 2, 256));
    check(ghostty_terminal_resize(terminal_, cols_, rows_, cw_, ch_));
    rmt_core_maintain(&core_, false);
    surface_ = {};
    invalidate();
}
void Renderer::invalidate() {
    if (!render_) return;
    const auto dirty = GHOSTTY_RENDER_STATE_DIRTY_FULL;
    check(ghostty_render_state_set(render_, GHOSTTY_RENDER_STATE_OPTION_DIRTY, &dirty));
}
void Renderer::drop_sixel(std::deque<Overlay>::iterator it) {
    sixel_bytes_ -= size_t(it->image.sizeInBytes());
    ghostty_tracked_grid_ref_free(it->anchor);
    sixels_.erase(it);
}
void Renderer::clear_sixel() {
    if (sixels_.empty()) return;
    while (!sixels_.empty()) drop_sixel(sixels_.begin());
    invalidate();
}

void Renderer::control(std::string_view sequence) {
    if (sequence != "\033c") {
        if (sequence.substr(0, 2) == "\033[") sequence.remove_prefix(2);
        else if (sequence.substr(0, 1) == "\233") sequence.remove_prefix(1);
        else return;
        if (sequence.empty() || sequence.back() != 'J') return;
        sequence.remove_suffix(1);
        while (!sequence.empty() && sequence.front() == '0') sequence.remove_prefix(1);
        if (sequence.empty()) {
            // BusyBox clear emits CUP + ED0. Only erase-below at the actual
            // top-left clears the whole display; another cursor position
            // (including origin mode with margins) must retain other images.
            uint16_t x = 0, y = 0;
            check(ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x));
            check(ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y));
            if (x != 0 || y != 0) return;
        } else if (sequence != "2") return;
    }
    // Ghostty applies Kitty erase semantics when the pending ED/RIS reaches
    // its parser. Sixel overlays are ours, and both layers need a repaint.
    clear_sixel();
    invalidate();
}
bool Renderer::visible(const Overlay &overlay) const {
    GhosttyTerminalScreen screen{};
    GhosttyTerminalScrollbar scrollbar{};
    GhosttyPointCoordinate pos{};
    ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &screen);
    ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar);
    if (screen != overlay.screen || !ghostty_tracked_grid_ref_has_value(overlay.anchor) ||
        ghostty_tracked_grid_ref_point(overlay.anchor, GHOSTTY_POINT_TAG_SCREEN, &pos) != GHOSTTY_SUCCESS) return false;
    const int64_t y = (int64_t(pos.y) - int64_t(scrollbar.offset)) * ch_;
    return y < int64_t(rows_) * ch_ && y + overlay.image.height() > 0;
}
void Renderer::reclaim(bool pressure) {
    pressure = pressure || rmt_core_under_pressure(&core_);
    rmt_core_maintain(&core_, pressure);
    const bool near_limit = sixel_bytes_ >= sixel_budget_ - sixel_budget_ / 4;
    const size_t target = pressure ? 0 : near_limit ? sixel_budget_ / 2 : sixel_bytes_;
    for (size_t i = 0; i < sixels_.size();) {
        const auto it = sixels_.begin() + qsizetype(i);
        if (!ghostty_tracked_grid_ref_has_value(it->anchor) || (sixel_bytes_ > target && !visible(*it))) drop_sixel(it);
        else ++i;
    }
}
void Renderer::sixel(sixel::Bitmap &&bitmap) {
    reclaim(false);
    const size_t budget = sixel_budget_;
    const size_t bytes = bitmap.rgba.size();
    if (!bytes || bytes > budget) return;
    while (!sixels_.empty() && (sixel_bytes_ + bytes > budget || sixels_.size() >= 128)) {
        auto victim = std::find_if(sixels_.begin(), sixels_.end(), [this](const Overlay &s) { return !visible(s); });
        drop_sixel(victim == sixels_.end() ? sixels_.begin() : victim);
    }
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
    invalidate();
    // Leave the cursor on the row below the image, scrolling normally.
    const std::string advance(size_t((bitmap.height + ch_ - 1) / ch_), '\n');
    ghostty_terminal_vt_write(terminal_, reinterpret_cast<const uint8_t *>(advance.data()), advance.size());
    const uint8_t cr = '\r';
    ghostty_terminal_vt_write(terminal_, &cr, 1);
}
void Renderer::cells(QPainter &p, const GhosttyRenderStateColors &colors, bool backgrounds) {
    check(ghostty_render_state_get(render_, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_));
    // Adjust glyph coverage only. A single reusable row bounds scratch memory;
    // images, backgrounds and the terminal's color choices are unaffected.
    QImage ink;
    if (!backgrounds && darkness_ != 50) {
        ink = QImage(cols_ * cw_, ch_, QImage::Format_ARGB32);
        if (ink.isNull()) throw std::bad_alloc();
    }
    uint16_t row_y = 0;
    while (ghostty_render_state_row_iterator_next_dirty(row_, &row_y)) {
        const int y = row_y;
        QPainter row_painter;
        if (!ink.isNull()) {
            ink.fill(Qt::transparent);
            row_painter.begin(&ink);
            row_painter.setRenderHint(QPainter::TextAntialiasing);
            row_painter.translate(0, -y * ch_);
        }
        QPainter &text_painter = ink.isNull() ? p : row_painter;
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
                    text_painter.setFont(f);
                    QColor color = text_color(fg, bg);
                    // Minimum contrast is an accessibility floor. Once it is
                    // enabled, a faint attribute cannot lower the pair below it.
                    if (style.faint && minimum_contrast_ == 0) color.setAlpha(150);
                    text_painter.setPen(color);
                    text_painter.save();
                    text_painter.setClipRect(QRect(rect.x(), rect.y(), cw_ * (wide == GHOSTTY_CELL_WIDE_WIDE ? 2 : 1), ch_));
                    text_painter.drawText(QPoint(rect.x(), rect.y() + ascent_), QString::fromUtf8(text, qsizetype(buffer.len)));
                    text_painter.restore();
                }
            }
            ++x;
        }
        if (!ink.isNull()) {
            row_painter.end();
            for (int line = 0; line < ink.height(); ++line) {
                auto *pixels = reinterpret_cast<QRgb *>(ink.scanLine(line));
                for (int col = 0; col < ink.width(); ++col) {
                    const auto pixel = pixels[col];
                    pixels[col] = (pixel & 0x00ffffffu) | (uint32_t(coverage_[qAlpha(pixel)]) << 24);
                }
            }
            p.drawImage(0, y * ch_, ink);
        }
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
QRect Renderer::render() {
    reclaim(false);
    check(ghostty_render_state_update(render_, terminal_));
    GhosttyRenderStateDirty dirty = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    check(ghostty_render_state_get(render_, GHOSTTY_RENDER_STATE_DATA_DIRTY, &dirty));
    GhosttyKittyGraphics graphics = nullptr;
    uint64_t generation = 0;
    if (ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_KITTY_GRAPHICS, &graphics) == GHOSTTY_SUCCESS)
        ghostty_kitty_graphics_get(graphics, GHOSTTY_KITTY_GRAPHICS_DATA_GENERATION, &generation);
    if (generation != kitty_generation_) {
        kitty_generation_ = generation;
        dirty = GHOSTTY_RENDER_STATE_DIRTY_FULL;
        check(ghostty_render_state_set(render_, GHOSTTY_RENDER_STATE_OPTION_DIRTY, &dirty));
    }
    const QSize size(cols_ * cw_, rows_ * ch_);
    if (surface_.size() != size || surface_.format() != QImage::Format_Grayscale8) {
        surface_ = QImage(size, QImage::Format_Grayscale8);
        if (surface_.isNull()) throw std::bad_alloc();
        dirty = GHOSTTY_RENDER_STATE_DIRTY_FULL;
        check(ghostty_render_state_set(render_, GHOSTTY_RENDER_STATE_OPTION_DIRTY, &dirty));
    }
    if (dirty == GHOSTTY_RENDER_STATE_DIRTY_FALSE) return {};

    check(ghostty_render_state_get(render_, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_));
    QRegion changed;
    uint16_t row_y = 0;
    while (ghostty_render_state_row_iterator_next_dirty(row_, &row_y))
        changed += QRect(0, row_y * ch_, surface_.width(), ch_);
    // A non-row global change (for example a palette change) must still
    // repaint safely even if a future libghostty version reports no row bits.
    if (changed.isEmpty()) changed = surface_.rect();

    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    check(ghostty_render_state_get(render_, GHOSTTY_RENDER_STATE_DATA_COLORS, &colors));
    QPainter p(&surface_);
    if (!p.isActive()) throw std::runtime_error("Terminal display surface is not paintable");
    p.setClipRegion(changed);
    p.fillRect(surface_.rect(), gray(colors.background));
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
            if (y < surface_.height() && y + it->image.height() > 0) p.drawImage(QPoint(pos.x * cw_, int(y)), it->image);
        }
        ++it;
    }
    kitty(p, GHOSTTY_KITTY_PLACEMENT_LAYER_ABOVE_TEXT);
    // Selection belongs to the terminal grid, including scrollback and reflow.
    // Invert after graphics so even cells under an image remain visible.
    check(ghostty_render_state_get(render_, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_));
    p.save(); p.setCompositionMode(QPainter::CompositionMode_Difference);
    while (ghostty_render_state_row_iterator_next_dirty(row_, &row_y)) {
        auto range = GHOSTTY_INIT_SIZED(GhosttyRenderStateRowSelection);
        if (ghostty_render_state_row_get(row_, GHOSTTY_RENDER_STATE_ROW_DATA_SELECTION, &range) == GHOSTTY_SUCCESS)
            p.fillRect(QRect(range.start_x * cw_, row_y * ch_, (range.end_x - range.start_x + 1) * cw_, ch_), Qt::white);
    }
    p.restore();
    GhosttyRenderStateCursor cursor = GHOSTTY_INIT_SIZED(GhosttyRenderStateCursor);
    ghostty_render_state_get(render_, GHOSTTY_RENDER_STATE_DATA_CURSOR, &cursor);
    if (cursor.viewport_has_value && cursor.visible) {
        p.setPen(QPen(text_color(colors.foreground, colors.background), 2));
        p.drawRect(QRect(cursor.viewport_x * cw_ + 1, cursor.viewport_y * ch_ + 1, cw_ - 2, ch_ - 2));
    }
    p.end();
    ghostty_render_state_clean(render_);
    return changed.boundingRect();
}
QImage Renderer::frame() {
    render();
    return surface_;
}
}
