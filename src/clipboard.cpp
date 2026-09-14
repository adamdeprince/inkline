#include "rmt/clipboard.hpp"
#include <QStringDecoder>
#include <stdexcept>
#include <string_view>
namespace rmt {
namespace {
GhosttyString string(const char *p, size_t len) { return {reinterpret_cast<const uint8_t *>(p), len}; }
bool plain(GhosttyString mime) {
    const std::string_view value(reinterpret_cast<const char *>(mime.ptr), mime.len);
    return value == "text/plain" || value == "text/plain;charset=utf-8";
}
GhosttyPoint viewport(uint16_t x, uint16_t y) {
    GhosttyPoint point{}; point.tag = GHOSTTY_POINT_TAG_VIEWPORT;
    point.value.coordinate = {x, y}; return point;
}
}
bool Clipboard::set(const QByteArray &text) {
    if (size_t(text.size()) > MAX_BYTES) return false;
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder.decode(text);
    if (decoder.hasError()) return false;
    text_ = text; return true;
}
void Clipboard::write(const GhosttyClipboardWrite *request) noexcept {
    auto reply = GHOSTTY_INIT_SIZED(GhosttyClipboardWriteReply);
    reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_UNSUPPORTED;
    try {
        if (!request->contents_len) { text_.clear(); reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_SUCCESS; }
        else for (size_t i = 0; i < std::min<size_t>(request->contents_len, 64); ++i) {
            const auto &c = request->contents[i];
            if (!plain(c.mime)) continue;
            reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_INVALID_DATA;
            if (c.data.len <= MAX_BYTES && set(QByteArray(reinterpret_cast<const char *>(c.data.ptr), qsizetype(c.data.len))))
                reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_SUCCESS;
            break;
        }
    } catch (...) { reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_IO_ERROR; }
    request->reply(request, &reply);
}
void Clipboard::read(const GhosttyClipboardRead *request, bool ready) const noexcept {
    auto reply = GHOSTTY_INIT_SIZED(GhosttyClipboardReadReply);
    reply.result = ready ? GHOSTTY_CLIPBOARD_READ_RESULT_SUCCESS : GHOSTTY_CLIPBOARD_READ_RESULT_BUSY;
    const GhosttyString types[] = {string("text/plain", 10), string("text/plain;charset=utf-8", 24)};
    const GhosttyClipboardContent contents[] = {{types[0], string(text_.constData(), size_t(text_.size()))},
                                               {types[1], string(text_.constData(), size_t(text_.size()))}};
    reply.contents = contents; reply.contents_len = 2;
    reply.available = types; reply.available_len = 2;
    request->reply(request, &reply);
}
GhosttyResult Clipboard::paste(GhosttyTerminal terminal) const {
    const auto mime = string("text/plain", 10);
    auto paste = GHOSTTY_INIT_SIZED(GhosttyPaste);
    paste.location = GHOSTTY_CLIPBOARD_LOCATION_STANDARD;
    paste.source = GHOSTTY_PASTE_SOURCE_CLIPBOARD;
    paste.mimes = &mime; paste.mimes_len = 1;
    paste.reader.userdata = const_cast<QByteArray *>(&text_);
    paste.reader.read = [](void *context, GhosttyString, GhosttyWriter writer) {
        const auto &text = *static_cast<QByteArray *>(context);
        return text.isEmpty() || writer.write(writer.userdata, reinterpret_cast<const uint8_t *>(text.constData()), size_t(text.size()));
    };
    // Option+V is an explicit paste, including multi-line text. Ghostty still
    // strips unsafe control bytes and applies the application's paste modes.
    paste.allow_unsafe = true;
    return ghostty_terminal_paste(terminal, &paste, nullptr);
}
Selection::~Selection() { release(); }
void Selection::release() { ghostty_tracked_grid_ref_free(anchor_); anchor_ = nullptr; }
void Selection::clear() { release(); ghostty_terminal_set(terminal_, GHOSTTY_TERMINAL_OPT_SELECTION, nullptr); }
void Selection::begin(uint16_t x, uint16_t y) {
    clear();
    ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &screen_);
    if (ghostty_terminal_grid_ref_track(terminal_, viewport(x, y), &anchor_) != GHOSTTY_SUCCESS)
        throw std::runtime_error("Cannot start text selection");
    extend(x, y);
}
void Selection::extend(uint16_t x, uint16_t y) {
    if (!anchor_) return;
    GhosttyTerminalScreen screen{};
    ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &screen);
    if (screen != screen_) { clear(); return; }
    auto selected = GHOSTTY_INIT_SIZED(GhosttySelection);
    if (ghostty_tracked_grid_ref_snapshot(anchor_, &selected.start) != GHOSTTY_SUCCESS ||
        ghostty_terminal_grid_ref(terminal_, viewport(x, y), &selected.end) != GHOSTTY_SUCCESS) { clear(); return; }
    if (ghostty_terminal_set(terminal_, GHOSTTY_TERMINAL_OPT_SELECTION, &selected) != GHOSTTY_SUCCESS)
        throw std::runtime_error("Cannot select terminal text");
}
std::optional<QByteArray> Selection::text() const {
    auto options = GHOSTTY_INIT_SIZED(GhosttyTerminalSelectionFormatOptions);
    options.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN; options.unwrap = true; options.trim = true;
    size_t length = 0;
    const auto result = ghostty_terminal_selection_format_buf(terminal_, options, nullptr, 0, &length);
    if (result == GHOSTTY_NO_VALUE) return std::nullopt;
    if (result != GHOSTTY_SUCCESS && result != GHOSTTY_OUT_OF_SPACE) throw std::runtime_error("Cannot copy terminal selection");
    if (length > Clipboard::MAX_BYTES) throw std::runtime_error("Selection exceeds the 128 KiB clipboard limit");
    QByteArray bytes(qsizetype(length), Qt::Uninitialized);
    if (length && ghostty_terminal_selection_format_buf(terminal_, options, reinterpret_cast<uint8_t *>(bytes.data()), length, &length) != GHOSTTY_SUCCESS)
        throw std::runtime_error("Cannot copy terminal selection");
    return bytes;
}
}
