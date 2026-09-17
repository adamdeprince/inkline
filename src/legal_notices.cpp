// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Adam DePrince
#include "rmt/legal_notices.hpp"
#include "legal_text.hpp"
#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QTextOption>
#include <algorithm>
#include <iterator>

namespace rmt {
int LegalNotices::count() { return int(std::size(legal::documents)); }
QString LegalNotices::title(int index) { return index >= 0 && index < count() ? QString::fromUtf8(legal::documents[index].title) : QString(); }
QString LegalNotices::text(int index) { return index >= 0 && index < count() ? QString::fromUtf8(legal::documents[index].text) : QString(); }
void LegalNotices::open() { focus_ = 4; choose(0); }
void LegalNotices::choose(int index) {
    current_ = (index + count()) % count(); scroll_ = 0; dragging_body_ = false;
    QFont font("Noto Sans"); font.setPixelSize(21);
    document_.setDefaultFont(font);
    QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document_.setDefaultTextOption(option);
    document_.setDocumentMargin(8);
    document_.setPlainText(text(current_));
    if (!panel_.isEmpty()) document_.setTextWidth(body().width() - 16);
}
QRectF LegalNotices::body() const { return panel_.adjusted(22, 162, -22, -84); }
QRectF LegalNotices::button(int index) const {
    const auto width = (panel_.width() - 68) / 3;
    if (index < 2) return {index ? panel_.right() - 202 : panel_.left() + 22, panel_.top() + 102, 180, 44};
    return {panel_.left() + 22 + (index - 2) * (width + 12), panel_.bottom() - 66, width, 44};
}
qreal LegalNotices::maximum() const { return std::max(qreal(0), document_.size().height() - body().height()); }
void LegalNotices::scroll(qreal pixels) { scroll_ = std::clamp(scroll_ + pixels, qreal(0), maximum()); }
void LegalNotices::paint(QPainter &p, const QRectF &panel) {
    panel_ = panel;
    if (document_.textWidth() != body().width() - 16) document_.setTextWidth(body().width() - 16);
    scroll(0);
    p.fillRect(panel, Qt::white); p.setPen(QPen(Qt::black, 2)); p.drawRect(panel);
    QFont font("Noto Sans"); font.setPixelSize(28); font.setBold(true); p.setFont(font);
    p.drawText(panel.adjusted(22, 18, -22, -panel.height() + 62), Qt::AlignVCenter,
               "About & licenses · Inkline " + QCoreApplication::applicationVersion());
    font.setBold(false); font.setPixelSize(21); p.setFont(font);
    p.drawText(panel.adjusted(22, 62, -22, -panel.height() + 98), Qt::AlignVCenter,
               QFontMetrics(font).elidedText(title(current_), Qt::ElideRight, int(panel.width()) - 44));
    p.drawText(QRectF(button(0).right(), button(0).top(), button(1).left() - button(0).right(), 44),
               Qt::AlignCenter, QString("%1 / %2").arg(current_ + 1).arg(count()));
    const QString labels[] = {"‹ Previous", "Next ›", "Page up", "Page down", "Back"};
    for (int i = 0; i < 5; ++i) {
        const auto box = button(i); p.setPen(QPen(Qt::black, 1)); p.drawRect(box);
        p.drawText(box, Qt::AlignCenter, labels[i]);
        if (focus_ == i) { p.setPen(QPen(Qt::black, 2, Qt::DashLine)); p.drawRect(box.adjusted(4, 4, -4, -4)); }
    }
    p.save(); p.setClipRect(body()); p.translate(body().topLeft() - QPointF(0, scroll_));
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, Qt::black);
    context.clip = QRectF(0, scroll_, body().width() - 16, body().height());
    document_.documentLayout()->draw(&p, context); p.restore();
    if (maximum() > 0) {
        const auto track = QRectF(body().right() - 5, body().top(), 5, body().height());
        const auto height = std::max(qreal(24), track.height() * body().height() / document_.size().height());
        p.fillRect(QRectF(track.left(), track.top() + (track.height() - height) * scroll_ / maximum(), 5, height), Qt::black);
    }
}
bool LegalNotices::activate(int index) {
    if (index < 2) choose(current_ + (index ? 1 : -1));
    else if (index == 2 || index == 3) scroll((index == 2 ? -1 : 1) * std::max(qreal(21), body().height() - 42));
    return index == 4;
}
bool LegalNotices::key(const MappedInput &event) {
    if (event.type != QEvent::KeyPress) return false;
    const int key = event.key;
    if (key == Qt::Key_Escape) return true;
    if (key == Qt::Key_Left || key == Qt::Key_Right) choose(current_ + (key == Qt::Key_Left ? -1 : 1));
    else if (key == Qt::Key_Up || key == Qt::Key_Down) scroll(key == Qt::Key_Up ? -42 : 42);
    else if (key == Qt::Key_PageUp || key == Qt::Key_PageDown) activate(key == Qt::Key_PageUp ? 2 : 3);
    else if (key == Qt::Key_Home) scroll(-maximum());
    else if (key == Qt::Key_End) scroll(maximum());
    else if (key == Qt::Key_Tab || key == Qt::Key_Backtab) focus_ = (focus_ + (key == Qt::Key_Backtab || (event.modifiers & Qt::ShiftModifier) ? 4 : 1)) % 5;
    else if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) && !event.repeat) return activate(focus_);
    return false;
}
bool LegalNotices::click(const QPointF &point) {
    for (int i = 0; i < 5; ++i) if (button(i).contains(point)) { focus_ = i; return activate(i); }
    return false;
}
void LegalNotices::begin_drag(const QPointF &point) { dragging_body_ = body().contains(point); drag_y_ = point.y(); drag_scroll_ = scroll_; }
void LegalNotices::drag(const QPointF &point) { if (dragging_body_) scroll(drag_scroll_ + drag_y_ - point.y() - scroll_); }
}
