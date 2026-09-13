#include "rmt/stream.hpp"
#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace rmt {
Stream::Stream(RmtCore &core, ImageHandler handler, size_t encoded_limit)
    : terminal_(rmt_core_terminal(&core)), handler_(std::move(handler)),
      limit_(std::min(encoded_limit, sixel::MAX_ENCODED_BYTES)) {
    if (!handler_) throw std::invalid_argument("A sixel image handler is required");
}

void Stream::send(std::string_view bytes) {
    ghostty_terminal_vt_write(terminal_, reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
}

void Stream::reset() {
    state_ = State::Normal;
    header_size_ = used_ = 0;
    discard_ = false;
    palette_ = sixel::Palette();
    if (control_) control_("\033c");
    ghostty_terminal_reset(terminal_);
}

void Stream::append(char byte) {
    if (discard_) return;
    if (used_ == limit_) {
        discard_ = true;
        used_ = 0;
        return;
    }
    buffer_[used_++] = byte;
}

void Stream::start_sixel() {
    state_ = State::Sixel;
    used_ = 0;
    discard_ = header_size_ > limit_;
    if (!discard_ && !buffer_) buffer_.reset(new (std::nothrow) char[limit_]);
    if (!buffer_) discard_ = true;
    if (!discard_) {
        std::memcpy(buffer_.get(), header_.data(), header_size_);
        used_ = header_size_;
    }
    header_size_ = 0;
}

void Stream::cancel_sixel() {
    ++rejected_;
    used_ = 0;
    discard_ = false;
    state_ = State::Normal;
}

void Stream::finish_sixel() {
    state_ = State::Normal;
    sixel::Bitmap bitmap;
    bool success = false;
    if (!discard_) {
        GhosttyColorRgb bg = {255, 255, 255};
        ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_COLOR_BACKGROUND, &bg);
        const uint32_t background = uint32_t(bg.r) << 16 | uint32_t(bg.g) << 8 | bg.b;
        std::string error;
        try {
            success = sixel::decode({buffer_.get(), used_}, bitmap, error, background, &palette_);
        } catch (const std::bad_alloc &) {
            // Memory pressure drops this image; framing still recovers at ST.
        }
    }
    used_ = 0;
    discard_ = false;
    if (success) {
        ++decoded_;
        handler_(std::move(bitmap));
    } else {
        ++rejected_;
    }
}

void Stream::write(std::string_view bytes) {
    size_t at = 0;
    while (at < bytes.size()) {
        if (state_ == State::Normal) {
            // Respect libghostty's parser AND UTF-8 state. In particular, a
            // UTF-8 continuation byte 0x90 must never become a sixel introducer.
            bool ground = false;
            ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_VT_GROUND, &ground);
            if (!ground) {
                size_t consumed = 0;
                ghostty_terminal_vt_write_until_ground(terminal_,
                    reinterpret_cast<const uint8_t *>(bytes.data() + at), bytes.size() - at, &consumed);
                at += consumed;
                continue;
            }
            const unsigned char byte = bytes[at];
            if (byte == 0x1b) {
                state_ = State::Escape;
                ++at;
            } else if (byte == 0x90 || byte == 0x9b) {
                header_[0] = char(byte);
                header_size_ = 1;
                state_ = byte == 0x90 ? State::DcsHeader : State::Csi;
                ++at;
            } else {
                size_t end = bytes.find_first_of("\033\x90\x9b", at);
                if (end == std::string_view::npos) end = bytes.size();
                send(bytes.substr(at, end - at));
                at = end;
            }
            continue;
        }

        const char byte = bytes[at++];
        switch (state_) {
        case State::Escape:
            if (byte == '\030' || byte == '\032') {
                send({&byte, 1});
                state_ = State::Normal;
            } else if (uint8_t(byte) == 0x7f) {
                // DEL is ignored inside an escape sequence.
            } else if (uint8_t(byte) < 0x20 && byte != '\033') {
                send({&byte, 1});
            } else if (byte == 'P' || byte == '[') {
                header_[0] = '\033';
                header_[1] = byte;
                header_size_ = 2;
                state_ = byte == 'P' ? State::DcsHeader : State::Csi;
            } else if (byte != '\033') {
                if (byte == 'c') palette_ = sixel::Palette();
                const char sequence[] = {'\033', byte};
                if (byte == 'c' && control_) control_({sequence, sizeof(sequence)});
                send({sequence, sizeof(sequence)});
                state_ = State::Normal;
            }
            break;

        case State::Csi:
            if (byte == '\033') {
                send({header_.data(), header_size_});
                header_size_ = 0;
                state_ = State::Escape;
            } else if (header_size_ == header_.size() || byte == '\030' || byte == '\032') {
                send({header_.data(), header_size_});
                send({&byte, 1});
                header_size_ = 0;
                state_ = State::Normal;
            } else if (uint8_t(byte) < 0x20) {
                send({&byte, 1});
            } else if (uint8_t(byte) != 0x7f) {
                header_[header_size_++] = byte;
                if (byte >= 0x40 && byte <= 0x7e) {
                    const std::string_view control(header_.data(), header_size_);
                    if (control_) control_(control);
                    send(control);
                    header_size_ = 0;
                    state_ = State::Normal;
                }
            }
            break;

        case State::DcsHeader:
            if (byte == '\033') {
                header_size_ = 0;
                state_ = State::Escape;
            } else if (byte == '\030' || byte == '\032' || uint8_t(byte) == 0x9c) {
                header_size_ = 0;
                state_ = State::Normal;
            } else if (uint8_t(byte) < 0x20) {
                send({&byte, 1}); // C0 controls execute within a DCS header.
            } else if (uint8_t(byte) == 0x7f) {
                // DEL is ignored within a DCS header.
            } else if (header_size_ < header_.size()) {
                header_[header_size_++] = byte;
                if (byte == 'q') {
                    start_sixel();
                } else if ((byte < '0' || byte > '9') && byte != ';') {
                    // Non-sixel DCS, e.g. DECRQSS or XTGETTCAP, belongs to VT.
                    send({header_.data(), header_size_});
                    header_size_ = 0;
                    state_ = State::Normal;
                }
            } else {
                send({header_.data(), header_size_});
                send({&byte, 1});
                header_size_ = 0;
                state_ = State::Normal;
            }
            break;

        case State::Sixel:
            if (byte == '\030' || byte == '\032') {
                cancel_sixel();
            } else {
                append(byte);
                if (byte == '\033') state_ = State::SixelEscape;
                else if (uint8_t(byte) == 0x9c) finish_sixel();
            }
            break;

        case State::SixelEscape:
            if (byte == '\\') {
                append(byte);
                finish_sixel();
            } else {
                // ESC aborts DCS and starts a new escape sequence. Reprocess
                // its following byte, which may begin a CSI or another DCS.
                cancel_sixel();
                state_ = State::Escape;
                --at;
            }
            break;
        case State::Normal:
            break;
        }
    }
}
}
