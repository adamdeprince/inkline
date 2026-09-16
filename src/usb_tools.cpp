#include "rmt/usb_tools.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextDocument>
#include <QTextBoundaryFinder>
#include <algorithm>

namespace rmt {
namespace {
constexpr const char *profiles[] = {"us", "mac", "linux", "windows"};
constexpr const char *profile_names[] = {"US / ASCII", "Mac", "Linux", "Windows"};
constexpr const char *profile_help[] = {
    "Any computer: select a US keyboard layout; turn Caps Lock off. ASCII text only.",
    "Mac: select Unicode Hex Input in Keyboard → Input Sources. Characters through U+FFFF; no emoji outside that range.",
    "Linux: select a US layout. The receiving app must support Ctrl+Shift+U Unicode entry (GNOME/GTK or compatible input method).",
    "Windows: select a US layout; install WinCompose. Set Compose to Right Alt and enable Unicode input. Turn Caps Lock off."
};
void font(QPainter &p, int size, bool bold = false) {
    QFont f("Noto Sans"); f.setPixelSize(size); f.setBold(bold); p.setFont(f);
}
void button(QPainter &p, const QRectF &box, const QString &label, bool chosen, bool focused) {
    p.fillRect(box, chosen ? Qt::black : Qt::white); p.setPen(Qt::black); p.drawRect(box);
    p.setPen(chosen ? Qt::white : Qt::black); font(p, 22, chosen);
    p.drawText(box.adjusted(4, 0, -4, 0), Qt::AlignCenter, label);
    if (focused) { p.setPen(QPen(chosen ? Qt::white : Qt::black, 2, Qt::DashLine)); p.drawRect(box.adjusted(4, 4, -4, -4)); }
    p.setPen(Qt::black);
}
QByteArray request(const char *name, const QString &value) {
    return QJsonDocument(QJsonObject{{QString::fromLatin1(name), value}}).toJson(QJsonDocument::Compact) + '\n';
}
}
UsbTools::UsbTools(QObject &owner, Preferences &prefs, Clipboard &clipboard,
                   std::function<void()> changed, const QString &directory)
    : QObject(&owner), prefs_(prefs), clipboard_(clipboard), changed_(std::move(changed)),
      directory_(directory.isEmpty() ? QCoreApplication::applicationDirPath() + "/usb" : directory) {
    ime_.set_method(prefs_.input_method());
    polling_.setInterval(3000);
    connect(&polling_, &QTimer::timeout, this, [this] { status(); });
    connect(&status_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        mode_ = code == 0 && exit == QProcess::NormalExit ? QString::fromUtf8(status_.readAllStandardOutput()).trimmed() : "unavailable";
        if (mode_ != "keyboard" && writer_.state() != QProcess::NotRunning) fail_writer("USB mode changed. Typing stopped.");
        changed_();
    });
    connect(&status_, &QProcess::errorOccurred, this, [this] { mode_ = "unavailable"; changed_(); });
    action_timeout_.setSingleShot(true); action_timeout_.setInterval(25000);
    connect(&action_timeout_, &QTimer::timeout, this, [this] { action_.kill(); message_ = "USB change timed out. Try Network again."; changed_(); });
    connect(&action_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        action_timeout_.stop();
        const auto output = QString::fromUtf8(action_.readAllStandardOutput() + action_.readAllStandardError()).trimmed().left(600);
        message_ = output.isEmpty() ? (code == 0 && exit == QProcess::NormalExit ? "USB mode changed." : "USB mode change failed.") : output;
        status(); changed_();
    });
    connect(&action_, &QProcess::errorOccurred, this, [this] { action_timeout_.stop(); message_ = "Could not run the installed USB helper."; changed_(); });
    writer_timeout_.setSingleShot(true); writer_timeout_.setInterval(6000);
    connect(&writer_timeout_, &QTimer::timeout, this, [this] { fail_writer("USB stopped responding. Remaining text was not sent; reconnect before starting again."); });
    kill_writer_.setSingleShot(true); kill_writer_.setInterval(1500);
    connect(&kill_writer_, &QTimer::timeout, this, [this] { writer_.kill(); });
    connect(&writer_, &QProcess::readyReadStandardOutput, this, [this] { receive(); });
    connect(&writer_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        writer_timeout_.stop(); kill_writer_.stop(); ready_ = false; inflight_ = false; queue_.clear(); pending_bytes_ = 0;
        const auto error = QString::fromUtf8(writer_.readAllStandardError()).trimmed().left(600);
        if (!stopping_ && (code != 0 || exit != QProcess::NormalExit)) message_ = error.isEmpty() ? "Typing stopped unexpectedly. Check the cable and host profile." : error;
        stopping_ = false; changed_();
    });
    connect(&writer_, &QProcess::errorOccurred, this, [this] {
        if (writer_.error() == QProcess::FailedToStart) fail_writer("Could not start the typing helper. Install the Python utility first.");
    });
}
UsbTools::~UsbTools() {
    polling_.stop(); stop_writer();
    if (writer_.state() != QProcess::NotRunning && !writer_.waitForFinished(1300)) { writer_.kill(); writer_.waitForFinished(200); }
    for (auto *process : {&action_, &status_}) {
        if (process->state() != QProcess::NotRunning) { process->terminate(); if (!process->waitForFinished(1000)) { process->kill(); process->waitForFinished(200); } }
    }
}
void UsbTools::open() { message_.clear(); focus_ = 0; ime_.set_method(prefs_.input_method()); status(); polling_.start(); }
void UsbTools::close() { polling_.stop(); stop_writer(); typewriter_ = false; ime_.reset(); }
void UsbTools::pause() { stop_writer(); message_ = "Paused. Press Start to resume typing."; changed_(); }
void UsbTools::quiesce() {
    pause();
    if (writer_.state() != QProcess::NotRunning && !writer_.waitForFinished(1300)) {
        writer_.kill(); writer_.waitForFinished(200);
    }
}
void UsbTools::set_method(int method) { ime_.set_method(method); changed_(); }
void UsbTools::status() {
    if (status_.state() != QProcess::NotRunning || action_.state() != QProcess::NotRunning) return;
    status_.start("/bin/sh", {directory_ + "/inkline-usb", "state"});
}
void UsbTools::change_mode(const QString &mode) {
    if (action_.state() != QProcess::NotRunning) return;
    stop_writer();
    if (status_.state() != QProcess::NotRunning) status_.kill();
    message_ = "Changing USB mode…";
    action_.start("/bin/sh", {directory_ + "/inkline-usb", mode}); action_timeout_.start(); changed_();
}
void UsbTools::start_writer() {
    if (action_.state() != QProcess::NotRunning || writer_.state() != QProcess::NotRunning) return;
    if (mode_ != "keyboard") { message_ = "Select Send keyboard in USB settings and connect a computer first."; changed_(); return; }
    ready_ = inflight_ = stopping_ = false; replies_.clear(); queue_.clear(); pending_bytes_ = 0;
    message_ = "Connecting… Focus the destination document before typing.";
    writer_.start("/bin/sh", {directory_ + "/inkline-type", "--stream", "--profile", profiles[prefs_.usb_profile()]});
    writer_timeout_.start(); changed_();
}
void UsbTools::stop_writer() {
    ready_ = false; queue_.clear(); pending_bytes_ = 0; inflight_ = false; writer_timeout_.stop();
    if (writer_.state() != QProcess::NotRunning) { stopping_ = true; writer_.terminate(); kill_writer_.start(); }
}
void UsbTools::fail_writer(const QString &message) { stop_writer(); message_ = message; changed_(); }
void UsbTools::enqueue(const QByteArray &wire, const QString &preview) {
    if (!ready_) { message_ = "Paused: press Start before typing. Nothing was sent."; changed_(); return; }
    if (wire.size() > 8192 || pending_bytes_ + wire.size() > 16384) { fail_writer("Typing queue is full. Remaining text was not sent. Use inkline-type for large files."); return; }
    pending_bytes_ += int(wire.size()); queue_.enqueue({wire, preview}); pump();
}
void UsbTools::insert(const QString &text) { if (!text.isEmpty()) enqueue(request("text", text), text); }
void UsbTools::pump() {
    if (!ready_ || inflight_ || queue_.isEmpty()) return;
    inflight_ = true;
    if (writer_.write(queue_.head().wire) != queue_.head().wire.size()) { fail_writer("Could not queue USB text. Typing stopped."); return; }
    // Large pastes can legitimately take minutes. USB writes themselves have
    // a five-second deadline in the helper. Startup is separately bounded.
    writer_timeout_.stop();
}
void UsbTools::receive() {
    replies_ += writer_.readAllStandardOutput();
    if (replies_.size() > 8192) { fail_writer("Invalid typing-helper response."); return; }
    while (replies_.contains('\n')) {
        const auto end = replies_.indexOf('\n'); const auto line = replies_.left(end); replies_.remove(0, end + 1);
        const auto object = QJsonDocument::fromJson(line).object();
        if (object.value("ready").toBool() && !stopping_) {
            ready_ = true; writer_timeout_.stop(); message_ = "Typing to the connected computer. Escape or Stop pauses."; pump();
        } else if (object.value("ok").toBool() && inflight_ && !queue_.isEmpty()) {
            const auto item = queue_.dequeue(); pending_bytes_ -= int(item.wire.size()); inflight_ = false;
            if (item.preview == "\b") {
                QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, preview_); boundary.toEnd();
                const int previous = int(boundary.toPreviousBoundary()); if (previous >= 0) preview_.truncate(previous);
            } else preview_ += item.preview;
            if (preview_.size() > 4096) { preview_ = preview_.right(4096); if (preview_.front().isLowSurrogate()) preview_.remove(0, 1); }
            pump();
        }
    }
    changed_();
}
void UsbTools::clipboard(InputAction action) {
    if (action == InputAction::Paste) insert(QString::fromUtf8(clipboard_.text()));
    else if (action == InputAction::Copy) { clipboard_.set(preview_.toUtf8()); message_ = "Copied the recent-text preview."; }
    else message_ = "The preview is not a document. Use the receiving editor's Cut command.";
    changed_();
}
QRectF UsbTools::control(int index) const {
    const qreal x = panel_.left() + 22, width = panel_.width() - 44;
    if (typewriter_) { const qreal w = (width - 32) / 5; return {x + index * (w + 8), panel_.bottom() - 72, w, 50}; }
    if (index <= 2) { const qreal w = (width - 20) / 3; return {x + index * (w + 10), panel_.top() + 150, w, 56}; }
    if (index >= 3 && index <= 6) { const qreal w = (width - 24) / 4; return {x + (index - 3) * (w + 8), panel_.top() + 260, w, 52}; }
    if (index == 7) return {x, panel_.top() + 408, width, 52};
    if (index == 8) return {x, panel_.top() + 474, width, 56};
    return {index == 9 ? x : panel_.right() - 160, panel_.bottom() - 70, index == 9 ? 270.0 : 138.0, 48};
}
QRectF UsbTools::candidate_box(int index) const {
    const qreal w = (panel_.width() - 44) / 9;
    return {panel_.left() + 22 + index * w, panel_.bottom() - 186, w, 46};
}
void UsbTools::paint(QPainter &p, const QRectF &panel) {
    panel_ = panel; p.fillRect(panel, Qt::white); p.setPen(QPen(Qt::black, 2)); p.drawRect(panel);
    font(p, 30, true); p.drawText(panel.adjusted(22, 18, -22, -panel.height() + 64), Qt::AlignVCenter,
        typewriter_ ? (ready_ ? "Typewriter · typing over USB" : "Typewriter · paused") : "USB and typewriter · 3/3");
    font(p, 21); p.drawText(panel.adjusted(22, 74, -22, -panel.height() + 130), Qt::TextWordWrap,
        typewriter_ ? QString("%1 · %2\nRecent sent text stays in RAM; this preview is not a saved document.").arg(profile_names[prefs_.usb_profile()], InputMethod::name(ime_.method()))
                    : QString("USB: %1\nLeaving Inkline restores USB networking. Mode changes take effect immediately.").arg(mode_));
    if (typewriter_) {
        const QRectF preview(panel.left() + 22, panel.top() + 140, panel.width() - 44, std::max<qreal>(80, panel.height() - 378));
        p.drawRect(preview);
        QTextDocument document; QFont text_font("Noto Sans"); text_font.setPixelSize(26); document.setDefaultFont(text_font);
        document.setPlainText(preview_); document.setTextWidth(preview.width() - 20);
        p.save(); p.setClipRect(preview.adjusted(8, 8, -8, -8));
        p.translate(preview.left() + 10, preview.top() + 8 - std::max<qreal>(0, document.size().height() - preview.height() + 16));
        document.drawContents(&p); p.restore();
        font(p, 22, true); p.drawText(QRectF(panel.left() + 22, panel.bottom() - 230, panel.width() - 44, 40), Qt::AlignVCenter,
            QString("%1  %2").arg(InputMethod::name(ime_.method()), ime_.preedit()));
        const auto candidates = ime_.candidates();
        for (int i = 0; i < candidates.size(); ++i) button(p, candidate_box(i), QString("%1 %2").arg(i + 1).arg(candidates[i]), false, false);
        font(p, 17); p.drawText(QRectF(panel.left() + 22, panel.bottom() - 130, panel.width() - 44, 50), Qt::TextWordWrap, message_);
        const QString labels[] = {ready_ ? "Stop" : "Start", "Network", "Input method", "Unicode", "USB settings"};
        for (int i = 0; i < 5; ++i) button(p, control(i), labels[i], i == 0 && ready_, false);
        return;
    }
    const QString modes[] = {"Network", "Send keyboard", "Receive keyboard"};
    const QString states[] = {"network", "keyboard", "host"};
    for (int i = 0; i < 3; ++i) button(p, control(i), modes[i], mode_ == states[i], focus_ == i);
    font(p, 22, true); p.drawText(QRectF(panel.left() + 22, panel.top() + 222, panel.width() - 44, 30), "Receiving computer / Unicode profile");
    for (int i = 0; i < 4; ++i) button(p, control(i + 3), profile_names[i], prefs_.usb_profile() == i, focus_ == i + 3);
    font(p, 20); p.drawText(QRectF(panel.left() + 22, panel.top() + 326, panel.width() - 44, 76), Qt::TextWordWrap, profile_help[prefs_.usb_profile()]);
    button(p, control(7), "Input method: " + InputMethod::name(prefs_.input_method()), false, focus_ == 7);
    button(p, control(8), "Open typewriter", false, focus_ == 8);
    font(p, 19); p.drawText(QRectF(panel.left() + 22, panel.top() + 545, panel.width() - 44, std::max<qreal>(40, panel.height() - 630)), Qt::TextWordWrap, message_);
    button(p, control(9), "‹  Command shortcuts", false, focus_ == 9); button(p, control(10), "Done", false, focus_ == 10);
}
UsbTools::Result UsbTools::activate(int index) {
    if (typewriter_) {
        if (index == 0) { if (ready_ || writer_.state() != QProcess::NotRunning) pause(); else start_writer(); }
        else if (index == 1) change_mode("network");
        else if (index == 2) { pause(); return Methods; }
        else if (index == 3) return Unicode;
        else if (index == 4) { pause(); typewriter_ = false; status(); }
    } else {
        if (index < 3) { const QString modes[] = {"network", "send-keyboard", "receive-keyboard"}; change_mode(modes[index]); }
        else if (index <= 6) { prefs_.set_usb_profile(index - 3); }
        else if (index == 7) return Methods;
        else if (index == 8) { typewriter_ = true; message_ = "Focus the destination document, then press Start. Escape always stops typing."; }
        else if (index == 9) { close(); return Back; }
        else if (index == 10) { close(); return Done; }
    }
    changed_(); return Stay;
}
UsbTools::Result UsbTools::click(const QPointF &point) {
    for (int i = 0; i < (typewriter_ ? 5 : 11); ++i) if (control(i).contains(point)) { focus_ = i; return activate(i); }
    if (typewriter_) {
        for (int i = 0; i < ime_.candidates().size(); ++i) if (candidate_box(i).contains(point)) { insert(QString::fromUtf8(ime_.candidate(i))); changed_(); break; }
    }
    return Stay;
}
UsbTools::Result UsbTools::key(const MappedInput &event) {
    if (event.type != QEvent::KeyPress) { if (typewriter_) ime_.key(event); return Stay; }
    if (!typewriter_) {
        if (event.key == Qt::Key_Escape) { close(); return Done; }
        if (event.key == Qt::Key_PageUp) { close(); return Back; }
        if (event.key == Qt::Key_Tab || event.key == Qt::Key_Down) focus_ = (focus_ + 1) % 11;
        else if (event.key == Qt::Key_Backtab || event.key == Qt::Key_Up) focus_ = (focus_ + 10) % 11;
        else if ((event.key == Qt::Key_Return || event.key == Qt::Key_Enter || event.key == Qt::Key_Space) && !event.repeat) return activate(focus_);
        changed_(); return Stay;
    }
    if (event.key == Qt::Key_Escape) { ime_.reset(); pause(); return Stay; }
    if (!ready_) { message_ = "Paused: press Start before typing. Nothing was sent."; changed_(); return Stay; }
    const auto composition = ime_.key(event);
    if (!composition.commit.isEmpty()) insert(QString::fromUtf8(composition.commit));
    if (composition.consumed) { changed_(); return Stay; }
    QString key;
    switch (event.key) {
    case Qt::Key_Backspace: key = "backspace"; break;
    case Qt::Key_Return: case Qt::Key_Enter: key = "enter"; break;
    case Qt::Key_Tab: case Qt::Key_Backtab: key = "tab"; break;
    case Qt::Key_Delete: key = "delete"; break;
    case Qt::Key_Left: key = "left"; break; case Qt::Key_Right: key = "right"; break;
    case Qt::Key_Up: key = "up"; break; case Qt::Key_Down: key = "down"; break;
    case Qt::Key_Home: key = "home"; break; case Qt::Key_End: key = "end"; break;
    case Qt::Key_PageUp: key = "pageup"; break; case Qt::Key_PageDown: key = "pagedown"; break;
    default:
        if (event.key >= Qt::Key_F1 && event.key <= Qt::Key_F12) key = QString("f%1").arg(event.key - Qt::Key_F1 + 1);
        else if (event.modifiers & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier)) {
            if ((event.key >= Qt::Key_A && event.key <= Qt::Key_Z) || (event.key >= Qt::Key_0 && event.key <= Qt::Key_9)) key = QString(QChar(ushort(event.key))).toLower();
        }
        break;
    }
    if (!key.isEmpty()) {
        const auto plain = key; QString prefix;
        if (event.modifiers & Qt::ControlModifier) prefix += "ctrl+";
        if (event.modifiers & Qt::ShiftModifier) prefix += "shift+";
        if (event.modifiers & Qt::AltModifier) prefix += "alt+";
        if (event.modifiers & Qt::MetaModifier) prefix += "super+";
        enqueue(request("key", prefix + key), prefix.isEmpty() ? (plain == "backspace" ? "\b" : plain == "enter" ? "\n" : plain == "tab" ? "\t" : "") : "");
    } else if (!(event.modifiers & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier))) {
        QString text = event.text;
        if (event.caps_locked) for (auto &c : text) if (c.isLetter()) c = c.isLower() ? c.toUpper() : c.toLower();
        insert(text);
    }
    return Stay;
}
}
