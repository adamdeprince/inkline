#include "rmt/shortcut_settings.hpp"
#include <QFontMetricsF>
#include <QProcess>
#include <QTextBoundaryFinder>
#include <QTimer>
#include <algorithm>
#include <stdexcept>

namespace rmt {
namespace {
void font(QPainter &p, int size, bool bold = false) {
    QFont f("Noto Sans"); f.setPixelSize(size); f.setBold(bold); p.setFont(f);
}
void button(QPainter &p, const QRectF &box, const QString &text, bool selected, bool focused) {
    p.fillRect(box, selected ? Qt::black : Qt::white);
    p.setPen(QPen(Qt::black, 1)); p.drawRect(box);
    p.setPen(selected ? Qt::white : Qt::black);
    p.drawText(box.adjusted(5, 0, -5, 0), Qt::AlignCenter, text);
    if (focused) { p.setPen(QPen(selected ? Qt::white : Qt::black, 2, Qt::DashLine)); p.drawRect(box.adjusted(4, 4, -4, -4)); }
}
}
ShortcutSettings::ShortcutSettings(QObject &owner, Clipboard &clipboard) : owner_(owner), clipboard_(clipboard) {
    directory_ = qEnvironmentVariable("INKLINE_SHORTCUT_DIRECTORY").toStdString();
    if (directory_.empty()) directory_ = hotkeys::default_directory();
}
void ShortcutSettings::load() {
    std::vector<std::string> warnings;
    bindings_ = hotkeys::load(directory_, &warnings);
    if (!warnings.empty()) message_ = QString::fromStdString(warnings.front());
}
const hotkeys::Binding *ShortcutSettings::binding() const {
    const std::string key(1, char('a' + letter_));
    for (const auto &item : bindings_) if (item.key == key) return &item;
    return nullptr;
}
void ShortcutSettings::open() { message_.clear(); load(); reset(); focus_ = 0; }
void ShortcutSettings::reset() {
    const auto *item = binding();
    command_ = locked() ? "/home/root/inkline start" : item ? QString::fromStdString(hotkeys::display_command(item->command)) : QString();
    saved_ = command_; epaper_ = saved_epaper_ = item && item->mode == "epaper";
    cursor_ = anchor_ = int(command_.size()); scroll_ = 0; removing_ = false;
}
void ShortcutSettings::choose(int letter) {
    if (letter == letter_) return;
    if (dirty()) { message_ = "Save or Reset this draft before choosing another key."; return; }
    letter_ = (letter + 26) % 26; message_.clear(); load(); reset();
}
QRectF ShortcutSettings::letter_box(int index) const {
    const qreal width = (panel_.width() - 44 - 8 * 6) / 9;
    return {panel_.left() + 22 + (index % 9) * (width + 6), panel_.top() + 116 + (index / 9) * 52, width, 44};
}
QRectF ShortcutSettings::command_box() const { return {panel_.left() + 22, panel_.top() + 316, panel_.width() - 44, 56}; }
QRectF ShortcutSettings::control(int index) const {
    const qreal x = panel_.left() + 22, width = panel_.width() - 44;
    if (index == 2 || index == 3) return {x + (index - 2) * (width + 12) / 2, panel_.top() + 436, (width - 12) / 2, 44};
    if (index >= 4 && index <= 6) return {x + (index - 4) * (width + 12) / 3, panel_.top() + 492, (width - 24) / 3, 44};
    return {index == 7 ? x : panel_.right() - 160, panel_.bottom() - 66, index == 7 ? 240.0 : 138.0, 44};
}
void ShortcutSettings::paint(QPainter &p, const QRectF &panel) {
    panel_ = panel;
    p.fillRect(panel, Qt::white); p.setPen(QPen(Qt::black, 2)); p.drawRect(panel);
    font(p, 30, true);
    p.drawText(panel.adjusted(22, 20, -22, -panel.height() + 65), Qt::AlignVCenter, "Command shortcuts · 2/3");
    font(p, 19);
    p.drawText(panel.adjusted(22, 72, -22, -panel.height() + 110), Qt::AlignVCenter,
               "Opt+RightAlt + letter     • = assigned     T = locked");
    for (int i = 0; i < 26; ++i) {
        const std::string key(1, char('a' + i));
        const bool assigned = i == 19 || std::any_of(bindings_.begin(), bindings_.end(), [&](const auto &b) { return b.key == key; });
        font(p, 23);
        button(p, letter_box(i), QString(QChar('A' + i)) + (assigned ? " •" : ""), i == letter_, focus_ == 0 && i == letter_);
    }
    p.setPen(Qt::black); font(p, 21, true);
    p.drawText(QRectF(panel.left() + 22, panel.top() + 278, panel.width() - 44, 32), Qt::AlignVCenter,
               locked() ? "T always opens Inkline. It cannot be changed or removed." : QString("%1 — %2%3").arg(QChar('A' + letter_)).arg(binding() ? "Assigned command" : "New command").arg(dirty() ? " (unsaved)" : ""));
    const auto box = command_box();
    p.setPen(QPen(Qt::black, focus_ == 1 ? 3 : 1)); p.drawRect(box);
    font(p, 24); const QFontMetricsF metrics(p.font());
    const qreal cursor_x = metrics.horizontalAdvance(command_.left(cursor_));
    scroll_ = std::clamp(scroll_, std::max(qreal(0), cursor_x - box.width() + 28), cursor_x);
    const qreal baseline = box.center().y() + (metrics.ascent() - metrics.descent()) / 2;
    const qreal origin = box.left() + 10 - scroll_;
    p.save(); p.setClipRect(box.adjusted(7, 3, -7, -3));
    if (focus_ == 1 && anchor_ != cursor_) {
        const auto left = metrics.horizontalAdvance(command_.left(std::min(anchor_, cursor_)));
        const auto right = metrics.horizontalAdvance(command_.left(std::max(anchor_, cursor_)));
        p.fillRect(QRectF(origin + left, box.top() + 8, right - left, box.height() - 16), QColor(205, 205, 205));
    }
    p.setPen(Qt::black);
    p.drawText(QPointF(origin, baseline), command_);
    if (focus_ == 1 && !locked()) p.drawLine(QPointF(origin + cursor_x, box.top() + 9), QPointF(origin + cursor_x, box.bottom() - 9));
    p.restore();
    font(p, 17);
    p.drawText(QRectF(panel.left() + 22, panel.top() + 380, panel.width() - 44, 50), Qt::TextWordWrap,
               "Quote arguments containing spaces. No shell expansion. For pipes or redirection, use /bin/sh -c 'your command'. Ctrl+A/C/X/V edit text.");
    font(p, 21);
    button(p, control(2), "Terminal", !epaper_, focus_ == 2);
    button(p, control(3), "Native e-paper app", epaper_, focus_ == 3);
    button(p, control(4), locked() ? "Locked" : "Save", false, focus_ == 4);
    button(p, control(5), removing_ ? "Confirm remove" : "Remove", false, focus_ == 5);
    button(p, control(6), "Reset draft", false, focus_ == 6);
    p.setPen(Qt::black); font(p, 18);
    p.drawText(QRectF(panel.left() + 22, panel.top() + 548, panel.width() - 44, panel.height() - 630), Qt::TextWordWrap,
               message_.isEmpty() ? "Only Save and Remove write to storage. Hold Opt+RightAlt+Backspace for 2 seconds to recover; this closes all terminals." : message_);
    font(p, 21);
    button(p, control(7), "‹ Settings · 1/3", false, focus_ == 7);
    button(p, control(8), "Done", false, focus_ == 8);
}
void ShortcutSettings::notify() {
    (void)owner_; // No systemd notification on the macOS development host.
    if (directory_ != hotkeys::default_directory()) return;
#ifdef __linux__
    auto *process = new QProcess(&owner_);
    QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
    QObject::connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
    QTimer::singleShot(6000, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
    process->start("/usr/bin/systemctl", {"kill", "--kill-whom=main", "--signal=HUP", "inkline-hotkey.service"});
#endif
}
void ShortcutSettings::save() {
    if (locked()) { message_ = "T is permanently reserved for Inkline."; return; }
    try {
        hotkeys::register_binding(directory_, std::string(1, char('a' + letter_)), hotkeys::parse_command(command_.toStdString()), epaper_ ? "epaper" : "terminal");
        notify(); load(); reset(); message_ = "Saved. The shortcut is available to the launcher.";
    } catch (const std::exception &e) { message_ = QString::fromUtf8(e.what()); }
}
void ShortcutSettings::remove() {
    if (locked()) { message_ = "T is permanent and cannot be removed."; return; }
    if (!binding()) { message_ = "This key has no saved command."; return; }
    if (!removing_) { removing_ = true; message_ = "Press Confirm remove to delete this shortcut."; return; }
    try {
        hotkeys::deregister_binding(directory_, std::string(1, char('a' + letter_)));
        notify(); load(); reset(); message_ = "Shortcut removed.";
    } catch (const std::exception &e) { message_ = QString::fromUtf8(e.what()); }
}
ShortcutSettings::Result ShortcutSettings::leave(Result result) {
    if (dirty()) { message_ = "Save or Reset the unsaved draft before leaving."; return Stay; }
    removing_ = false; return result;
}
ShortcutSettings::Result ShortcutSettings::activate() {
    if (focus_ != 5) removing_ = false;
    if (focus_ == 0) focus_ = 1;
    else if (focus_ == 2 || focus_ == 3) { if (!locked()) epaper_ = focus_ == 3; }
    else if (focus_ == 4) save();
    else if (focus_ == 5) remove();
    else if (focus_ == 6) { message_.clear(); load(); reset(); }
    else if (focus_ == 7) return leave(Back);
    else if (focus_ == 8) return leave(Done);
    return Stay;
}
void ShortcutSettings::move_cursor(int position, bool select) {
    cursor_ = std::clamp(position, 0, int(command_.size()));
    if (!select) anchor_ = cursor_;
}
void ShortcutSettings::insert(const QString &text) {
    if (locked() || focus_ != 1 || text.contains('\n') || text.contains('\r') || text.contains(QChar(0))) return;
    const int first = std::min(anchor_, cursor_), count = std::abs(anchor_ - cursor_);
    if (command_.size() - count + text.size() > 65536) { message_ = "Command is too long."; return; }
    command_.replace(first, count, text); cursor_ = anchor_ = first + int(text.size());
    removing_ = false; message_.clear();
}
void ShortcutSettings::clipboard(InputAction action) {
    if (focus_ != 1) return;
    if (action == InputAction::Paste) insert(QString::fromUtf8(clipboard_.text()));
    else if (cursor_ != anchor_) {
        clipboard_.set(command_.mid(std::min(anchor_, cursor_), std::abs(anchor_ - cursor_)).toUtf8());
        if (action == InputAction::Cut) insert({});
    }
}
ShortcutSettings::Result ShortcutSettings::key(const MappedInput &event) {
    if (event.type != QEvent::KeyPress) return Stay;
    const int key = event.key;
    if (key == Qt::Key_Escape || key == Qt::Key_PageUp) return leave(Back);
    if (key == Qt::Key_PageDown) return leave(Next);
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
        const bool back = key == Qt::Key_Backtab || (event.modifiers & Qt::ShiftModifier);
        focus_ = (focus_ + (back ? 8 : 1)) % 9; removing_ = false; return Stay;
    }
    if (focus_ == 0) {
        if (key >= Qt::Key_A && key <= Qt::Key_Z) choose(key - Qt::Key_A);
        else if (key == Qt::Key_Left) choose(letter_ - 1);
        else if (key == Qt::Key_Right) choose(letter_ + 1);
        else if (key == Qt::Key_Up) choose(letter_ - 9);
        else if (key == Qt::Key_Down) choose(letter_ + 9);
    }
    if (focus_ == 1) {
        const bool select = event.modifiers & Qt::ShiftModifier;
        if (event.modifiers & Qt::ControlModifier) {
            if (key == Qt::Key_A) { anchor_ = 0; cursor_ = int(command_.size()); }
            else if (key == Qt::Key_C) clipboard(InputAction::Copy);
            else if (key == Qt::Key_V) clipboard(InputAction::Paste);
            else if (key == Qt::Key_X) clipboard(InputAction::Cut);
            else if (key == Qt::Key_Home) move_cursor(0, select);
            else if (key == Qt::Key_End) move_cursor(int(command_.size()), select);
            return Stay;
        }
        QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, command_); boundaries.setPosition(cursor_);
        if (key == Qt::Key_Home) move_cursor(0, select);
        else if (key == Qt::Key_End) move_cursor(int(command_.size()), select);
        else if (key == Qt::Key_Left) move_cursor(std::max(0, int(boundaries.toPreviousBoundary())), select);
        else if (key == Qt::Key_Right) { const auto next = boundaries.toNextBoundary(); move_cursor(next < 0 ? int(command_.size()) : int(next), select); }
        else if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
            if (anchor_ == cursor_) {
                const auto next = key == Qt::Key_Backspace ? boundaries.toPreviousBoundary() : boundaries.toNextBoundary();
                if (next >= 0) anchor_ = int(next);
            }
            insert({});
        } else if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !event.repeat) save();
        else if (!event.text.isEmpty() && !(event.modifiers & (Qt::AltModifier | Qt::MetaModifier))) insert(event.text);
        return Stay;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) && !event.repeat) return activate();
    return Stay;
}
ShortcutSettings::Result ShortcutSettings::click(const QPointF &point) {
    for (int i = 0; i < 26; ++i) if (letter_box(i).contains(point)) { focus_ = 0; choose(i); return Stay; }
    if (command_box().contains(point)) {
        focus_ = 1; removing_ = false;
        QFont f("Noto Sans"); f.setPixelSize(24); const QFontMetricsF metrics(f);
        const auto x = point.x() - command_box().left() - 10 + scroll_;
        QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, command_);
        int position = 0;
        while (true) {
            const auto next = boundaries.toNextBoundary();
            if (next < 0 || (metrics.horizontalAdvance(command_.left(position)) + metrics.horizontalAdvance(command_.left(next))) / 2 > x) break;
            position = int(next);
        }
        move_cursor(position, false); return Stay;
    }
    for (int i = 2; i <= 8; ++i) if (control(i).contains(point)) { focus_ = i; return activate(); }
    return Stay;
}
}
