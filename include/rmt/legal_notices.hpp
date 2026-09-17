// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Adam DePrince
#ifndef RMT_LEGAL_NOTICES_HPP
#define RMT_LEGAL_NOTICES_HPP
#include "rmt/input.hpp"
#include <QPainter>
#include <QTextDocument>

namespace rmt {
// Read-only, offline notices. Navigation and scrolling never write storage.
class LegalNotices {
public:
    void open();
    void paint(QPainter &p, const QRectF &panel);
    bool key(const MappedInput &event); // true returns to Settings
    bool click(const QPointF &point);
    void begin_drag(const QPointF &point);
    void drag(const QPointF &point);
    void scroll(qreal pixels);
    static int count();
    static QString title(int index);
    static QString text(int index);
    int current() const { return current_; }
    qreal position() const { return scroll_; }
private:
    QTextDocument document_;
    QRectF panel_;
    int current_ = 0, focus_ = 4;
    qreal scroll_ = 0, drag_scroll_ = 0, drag_y_ = 0;
    bool dragging_body_ = false;
    QRectF body() const;
    QRectF button(int index) const;
    qreal maximum() const;
    void choose(int index);
    bool activate(int index);
};
}
#endif
