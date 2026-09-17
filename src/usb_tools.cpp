#include "rmt/usb_tools.hpp"
#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextBlock>
#include <QTextBoundaryFinder>
#include <QTextCursor>
#include <QTextLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
constexpr int speeds[] = {5, 10, 20, 40, 80};

void font(QPainter &p, int size, bool bold = false) {
    QFont f; f.setFamilies({"Noto Sans", "Noto Sans Mono CJK SC", "Noto Sans Symbols 2", "Unifont", "Unifont Upper"});
    f.setPixelSize(size); f.setBold(bold); p.setFont(f);
}
QFont document_font(int pixels) {
    QFont f; f.setFamilies({"Noto Sans", "Noto Sans Mono CJK SC", "Noto Sans Symbols 2", "Unifont", "Unifont Upper"});
    f.setPixelSize(pixels); return f;
}
void button(QPainter &p, const QRectF &box, const QString &label, bool chosen, bool focused) {
    p.fillRect(box, chosen ? Qt::black : Qt::white); p.setPen(Qt::black); p.drawRect(box);
    p.setPen(chosen ? Qt::white : Qt::black); font(p, 21, chosen);
    p.drawText(box.adjusted(4, 0, -4, 0), Qt::AlignCenter, label);
    if (focused) { p.setPen(QPen(chosen ? Qt::white : Qt::black, 2, Qt::DashLine)); p.drawRect(box.adjusted(4, 4, -4, -4)); }
    p.setPen(Qt::black);
}
QByteArray request(const char *name, const QString &value) {
    return QJsonDocument(QJsonObject{{QString::fromLatin1(name), value}}).toJson(QJsonDocument::Compact) + '\n';
}
QString display_path(const QString &path) {
    const auto home = QDir::homePath();
    return path.startsWith(home + '/') ? '~' + path.mid(home.size()) : path;
}
}

UsbTools::UsbTools(QObject &owner, Preferences &prefs, Clipboard &clipboard,
                   std::function<void()> changed, const QString &directory,
                   const QString &draft_path)
    : QObject(&owner), prefs_(prefs), clipboard_(clipboard), changed_(std::move(changed)),
      directory_(directory.isEmpty() ? QCoreApplication::applicationDirPath() + "/usb" : directory),
      draft_path_(draft_path.isEmpty() ? QDir::home().filePath(".inkline-typewriter-draft") : draft_path),
      path_entry_(QDir::home().filePath("typewriter.txt")) {
    text_layout_.setDocumentMargin(0);
    text_layout_.setDefaultFont(document_font(27));
    QFile draft(draft_path_);
    if (draft.open(QIODevice::ReadOnly)) {
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString restored = decoder.decode(draft.readAll());
        if (!decoder.hasError()) {
            document_ = restored;
            if (document_.startsWith(QChar(0xfeff))) document_.remove(0, 1);
            cursor_ = anchor_ = document_.size(); dirty_ = true;
            message_ = "Recovered the RAM editor from its hidden shutdown draft.";
        }
    }
    ime_.set_method(prefs_.input_method());
    polling_.setInterval(3000);
    connect(&polling_, &QTimer::timeout, this, [this] { status(); });
    connect(&status_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        mode_ = code == 0 && exit == QProcess::NormalExit ? QString::fromUtf8(status_.readAllStandardOutput()).trimmed() : "unavailable";
        if (mode_ != "keyboard" && writer_.state() != QProcess::NotRunning) fail_writer("USB mode changed. Typing stopped.");
        else if (mode_ == "keyboard" && typewriter_ && live_requested_ && writer_.state() == QProcess::NotRunning) start_writer();
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
        const bool restart = restart_bulk_;
        restart_bulk_ = false;
        const auto error = QString::fromUtf8(writer_.readAllStandardError()).trimmed().left(600);
        if (!stopping_ && (code != 0 || exit != QProcess::NormalExit)) {
            bulk_ = false; live_requested_ = false;
            message_ = error.isEmpty() ? "Typing stopped unexpectedly. Check the cable and host profile." : error;
        }
        stopping_ = false;
        if (restart) QTimer::singleShot(0, this, [this] { start_writer(); });
        changed_();
    });
    connect(&writer_, &QProcess::errorOccurred, this, [this] {
        if (writer_.error() == QProcess::FailedToStart) fail_writer("Could not start keyboard-send. Install the Python utility first.");
    });
}

UsbTools::~UsbTools() {
    polling_.stop(); stop_writer();
    if (writer_.state() != QProcess::NotRunning && !writer_.waitForFinished(1300)) { writer_.kill(); writer_.waitForFinished(200); }
    for (auto *process : {&action_, &status_}) {
        if (process->state() != QProcess::NotRunning) { process->terminate(); if (!process->waitForFinished(1000)) { process->kill(); process->waitForFinished(200); } }
    }
}

void UsbTools::open() { focus_ = 0; ime_.set_method(prefs_.input_method()); status(); polling_.start(); }
void UsbTools::close() { polling_.stop(); live_requested_ = false; stop_writer(); typewriter_ = files_ = false; ime_.reset(); }
void UsbTools::pause() { live_requested_ = false; stop_writer(); message_ = "Live USB typing is paused. Editing remains in RAM."; changed_(); }
void UsbTools::quiesce() {
    pause();
    if (writer_.state() != QProcess::NotRunning && !writer_.waitForFinished(1300)) {
        writer_.kill(); writer_.waitForFinished(200);
    }
}
void UsbTools::persist() {
    if (dirty_) {
        const QString target = file_path_.isEmpty() ? draft_path_ : file_path_;
        if (!save_document(target, false)) throw std::runtime_error(message_.toStdString());
    }
    if (remove_draft_on_exit_ && !file_path_.isEmpty()) {
        QFile::remove(draft_path_); remove_draft_on_exit_ = false;
    }
}
void UsbTools::set_method(int method) { ime_.set_method(method); changed_(); }

void UsbTools::status() {
    if (status_.state() != QProcess::NotRunning || action_.state() != QProcess::NotRunning) return;
    status_.start("/bin/sh", {directory_ + "/inkline-usb", "state"});
}
void UsbTools::change_mode(const QString &mode) {
    if (action_.state() != QProcess::NotRunning) return;
    live_requested_ = false; stop_writer();
    if (status_.state() != QProcess::NotRunning) status_.kill();
    message_ = "Changing USB mode…";
    action_.start("/bin/sh", {directory_ + "/inkline-usb", mode}); action_timeout_.start(); changed_();
}
void UsbTools::start_writer() {
    live_requested_ = true;
    if (action_.state() != QProcess::NotRunning || writer_.state() != QProcess::NotRunning) return;
    if (mode_ != "keyboard") { bulk_ = false; message_ = "Choose Send keyboard in USB settings and connect a computer first."; changed_(); return; }
    ready_ = inflight_ = stopping_ = false; replies_.clear(); queue_.clear(); pending_bytes_ = 0;
    writer_speed_ = speed_;
    message_ = bulk_ ? QString("Connecting to send %1 characters…").arg(document_.size())
                     : "Connecting… Focus the destination document before typing.";
    writer_.start("/bin/sh", {directory_ + "/keyboard-send", "--stream", "--profile",
                               profiles[prefs_.usb_profile()], "--cps", QString::number(writer_speed_)});
    writer_timeout_.start(); changed_();
}
void UsbTools::stop_writer(bool cancel_bulk) {
    if (cancel_bulk) { bulk_ = false; restart_bulk_ = false; }
    ready_ = false; queue_.clear(); pending_bytes_ = 0; inflight_ = false; writer_timeout_.stop();
    if (writer_.state() != QProcess::NotRunning) { stopping_ = true; writer_.terminate(); kill_writer_.start(); }
}
void UsbTools::fail_writer(const QString &message) { stop_writer(); message_ = message; changed_(); }
void UsbTools::enqueue(const QByteArray &wire, const QString &preview, int bulk_end) {
    if (!ready_) { message_ = "Edited in RAM. Live USB typing is paused."; changed_(); return; }
    if (wire.size() > 8192 || pending_bytes_ + wire.size() > 16384) { fail_writer("Typing queue is full. Sending stopped; the editor text is safe in RAM."); return; }
    pending_bytes_ += int(wire.size()); queue_.enqueue({wire, preview, bulk_end}); pump();
}
void UsbTools::pump() {
    if (!ready_ || inflight_ || queue_.isEmpty()) return;
    inflight_ = true;
    if (writer_.write(queue_.head().wire) != queue_.head().wire.size()) { fail_writer("Could not queue USB text. Typing stopped."); return; }
    writer_timeout_.stop();
}
void UsbTools::receive() {
    replies_ += writer_.readAllStandardOutput();
    if (replies_.size() > 8192) { fail_writer("Invalid keyboard-send response."); return; }
    while (replies_.contains('\n')) {
        const auto end = replies_.indexOf('\n'); const auto line = replies_.left(end); replies_.remove(0, end + 1);
        const auto object = QJsonDocument::fromJson(line).object();
        if (object.value("ready").toBool() && !stopping_) {
            ready_ = true; writer_timeout_.stop();
            if (bulk_) { message_ = QString("Sending document at %1 characters/second…").arg(speed_); next_bulk(); }
            else message_ = "Live typing is on. Keystrokes edit here and go to the connected computer.";
            pump();
        } else if (object.value("ok").toBool() && inflight_ && !queue_.isEmpty()) {
            const auto item = queue_.dequeue(); pending_bytes_ -= int(item.wire.size()); inflight_ = false;
            if (item.bulk_end >= 0) {
                bulk_offset_ = item.bulk_end;
                if (bulk_offset_ >= document_.size()) {
                    bulk_ = false; message_ = "Document sent. Live typing remains on.";
                } else {
                    message_ = QString("Sending document: %1 / %2 characters…").arg(bulk_offset_).arg(document_.size());
                    next_bulk();
                }
            }
            pump();
        }
    }
    changed_();
}

void UsbTools::next_bulk() {
    if (!bulk_ || !ready_ || inflight_ || !queue_.isEmpty()) return;
    if (bulk_offset_ >= document_.size()) { bulk_ = false; message_ = "Document sent. Live typing remains on."; return; }
    int end = std::min(int(document_.size()), bulk_offset_ + 512);
    if (end < document_.size() && document_[end - 1].isHighSurrogate() && document_[end].isLowSurrogate()) --end;
    const auto text = document_.mid(bulk_offset_, end - bulk_offset_);
    enqueue(request("text", text), {}, end);
}
QString UsbTools::validate_document() const {
    int position = 0;
    for (uint code : document_.toUcs4()) {
        ++position;
        if (code == '\n' || code == '\t' || (code >= 32 && code <= 126)) continue;
        if (code < 32 || (code >= 127 && code <= 159) || (code >= 0xfdd0 && code <= 0xfdef) || (code & 0xffff) == 0xfffe || (code & 0xffff) == 0xffff)
            return QString("Unsupported control/noncharacter U+%1 at character %2.").arg(QString::number(code, 16).rightJustified(4, '0').toUpper()).arg(position);
        if (prefs_.usb_profile() == 0)
            return QString("U+%1 needs the Mac, Linux, or Windows Unicode profile.").arg(QString::number(code, 16).rightJustified(4, '0').toUpper());
        if (prefs_.usb_profile() == 1 && code > 0xffff)
            return QString("U+%1 is outside the Mac Unicode Hex Input range.").arg(QString::number(code, 16).rightJustified(4, '0').toUpper());
    }
    return {};
}
void UsbTools::send_document() {
    const auto problem = validate_document();
    if (!problem.isEmpty()) { message_ = problem; changed_(); return; }
    bulk_ = true; bulk_offset_ = 0;
    if (ready_ && writer_speed_ == speed_) { next_bulk(); changed_(); return; }
    if (writer_.state() != QProcess::NotRunning) {
        restart_bulk_ = true; stop_writer(false);
    } else start_writer();
    changed_();
}

void UsbTools::remove_selection() {
    const int first = std::min(cursor_, anchor_), last = std::max(cursor_, anchor_);
    if (first == last) return;
    document_.remove(first, last - first); cursor_ = anchor_ = first; dirty_ = true;
}
void UsbTools::insert_document(const QString &value, bool transmit) {
    if (bulk_) { message_ = "Stop document sending before editing."; changed_(); return; }
    QString text = value; text.replace("\r\n", "\n"); text.replace('\r', '\n');
    if (text.isEmpty()) return;
    remove_selection(); document_.insert(cursor_, text); cursor_ += text.size(); anchor_ = cursor_; dirty_ = true;
    update_layout(); ensure_cursor_visible();
    if (transmit && ready_) enqueue(request("text", text), text);
    else if (transmit && !ready_) message_ = "Edited in RAM. Live USB typing is paused.";
    changed_();
}
void UsbTools::insert(const QString &text) {
    if (files_) { path_entry_ += text; changed_(); }
    else insert_document(text);
}
void UsbTools::move_cursor(int position, bool selecting) {
    cursor_ = std::clamp(position, 0, int(document_.size()));
    if (!selecting) anchor_ = cursor_;
    ensure_cursor_visible(); changed_();
}

QString UsbTools::expanded_path() const {
    QString path = path_entry_.trimmed();
    if (path == "~") path = QDir::homePath();
    else if (path.startsWith("~/")) path = QDir::home().filePath(path.mid(2));
    else if (!path.isEmpty() && QDir::isRelativePath(path)) path = QDir::home().absoluteFilePath(path);
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}
bool UsbTools::load_document() {
    const auto path = expanded_path();
    if (path.isEmpty()) { message_ = "Enter a file path first."; return false; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { message_ = "Could not open " + display_path(path) + ": " + file.errorString(); return false; }
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString value = decoder.decode(file.readAll());
    if (decoder.hasError()) { message_ = "The file is not valid UTF-8."; return false; }
    if (value.startsWith(QChar(0xfeff))) value.remove(0, 1);
    value.replace("\r\n", "\n"); value.replace('\r', '\n');
    document_ = value; file_path_ = path; path_entry_ = path; cursor_ = anchor_ = 0; scroll_y_ = 0; dirty_ = false;
    remove_draft_on_exit_ = QFile::exists(draft_path_);
    message_ = "Loaded " + display_path(path) + ". No file was changed."; update_layout(); return true;
}
bool UsbTools::save_document(const QString &path, bool explicit_save) {
    if (path.isEmpty()) { message_ = "Enter a file path first."; return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { message_ = "Could not save " + display_path(path) + ": " + file.errorString(); return false; }
    const auto bytes = document_.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) { message_ = "Could not finish saving " + display_path(path) + "."; return false; }
    dirty_ = false;
    if (explicit_save) {
        file_path_ = path; path_entry_ = path;
        if (QFileInfo(path).absoluteFilePath() != QFileInfo(draft_path_).absoluteFilePath()) QFile::remove(draft_path_);
        remove_draft_on_exit_ = false;
        message_ = "Saved " + display_path(path) + ".";
    }
    return true;
}
void UsbTools::new_document() {
    if (bulk_) pause();
    document_.clear(); file_path_.clear(); path_entry_ = QDir::home().filePath("typewriter.txt"); remove_draft_on_exit_ = false;
    cursor_ = anchor_ = 0; scroll_y_ = 0; dirty_ = true; ime_.reset();
    message_ = "New unsaved document. Inkline will use a hidden recovery name only when it exits."; update_layout();
}

void UsbTools::clipboard(InputAction action) {
    if (files_) {
        if (action == InputAction::Paste) path_entry_ += QString::fromUtf8(clipboard_.text());
        else if (action == InputAction::Copy) clipboard_.set(path_entry_.toUtf8());
        else path_entry_.clear();
        changed_(); return;
    }
    const int first = std::min(cursor_, anchor_), last = std::max(cursor_, anchor_);
    if (action == InputAction::Paste) insert_document(QString::fromUtf8(clipboard_.text()));
    else if (first == last) { message_ = "Select editor text first."; changed_(); }
    else {
        clipboard_.set(document_.mid(first, last - first).toUtf8());
        if (action == InputAction::Cut) {
            remove_selection(); update_layout(); ensure_cursor_visible();
            if (ready_) enqueue(request("key", "ctrl+x"));
        }
        message_ = action == InputAction::Cut ? "Cut to the RAM clipboard." : "Copied to the RAM clipboard.";
        changed_();
    }
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
QRectF UsbTools::file_control(int index) const {
    const qreal x = panel_.left() + 22, width = panel_.width() - 44;
    if (index == 0) return {x, panel_.top() + 126, width, 54};
    if (index >= 1 && index <= 3) { const qreal w = (width - 20) / 3; return {x + (index - 1) * (w + 10), panel_.top() + 214, w, 54}; }
    if (index >= 4 && index <= 8) { const qreal w = (width - 32) / 5; return {x + (index - 4) * (w + 8), panel_.top() + 330, w, 52}; }
    if (index == 9) return {x, panel_.top() + 416, width, 60};
    return {panel_.right() - 190, panel_.bottom() - 74, 168, 52};
}
QRectF UsbTools::candidate_box(int index) const {
    const qreal w = (panel_.width() - 44) / 9;
    return {panel_.left() + 22 + index * w, panel_.bottom() - 184, w, 42};
}

void UsbTools::update_layout() {
    if (editor_box_.isEmpty()) return;
    text_layout_.setDefaultFont(document_font(27));
    text_layout_.setTextWidth(std::max<qreal>(20, editor_box_.width() - 20));
    text_layout_.setPlainText(document_);
    text_layout_.documentLayout()->documentSize();
}
void UsbTools::ensure_cursor_visible() {
    if (editor_box_.isEmpty()) return;
    update_layout();
    const auto block = text_layout_.findBlock(cursor_);
    if (!block.isValid() || !block.layout()) return;
    const auto bounds = text_layout_.documentLayout()->blockBoundingRect(block);
    const auto line = block.layout()->lineForTextPosition(std::max(0, cursor_ - block.position()));
    const qreal top = bounds.top() + (line.isValid() ? line.y() : 0);
    const qreal bottom = top + (line.isValid() ? line.height() : 32);
    const qreal height = editor_box_.height() - 18;
    if (top < scroll_y_) scroll_y_ = top;
    else if (bottom > scroll_y_ + height) scroll_y_ = bottom - height;
    const qreal maximum = std::max<qreal>(0, text_layout_.size().height() - height);
    scroll_y_ = std::clamp(scroll_y_, qreal(0), maximum);
}
int UsbTools::hit_test(const QPointF &point) {
    update_layout();
    const auto local = point - editor_box_.topLeft() - QPointF(10, 8) + QPointF(0, scroll_y_);
    return std::clamp(text_layout_.documentLayout()->hitTest(local, Qt::FuzzyHit), 0, int(document_.size()));
}
bool UsbTools::begin_drag(const QPointF &point) {
    if (!editor_page() || !editor_box_.contains(point) || bulk_) return false;
    selecting_ = true; cursor_ = anchor_ = hit_test(point); ensure_cursor_visible(); changed_(); return true;
}
void UsbTools::drag(const QPointF &point) {
    if (!selecting_) return;
    cursor_ = hit_test(point); ensure_cursor_visible(); changed_();
}
void UsbTools::end_drag(const QPointF &point) { if (selecting_) { drag(point); selecting_ = false; } }
void UsbTools::scroll(qreal pixels) {
    if (!editor_page()) return;
    update_layout();
    const qreal maximum = std::max<qreal>(0, text_layout_.size().height() - editor_box_.height() + 18);
    scroll_y_ = std::clamp(scroll_y_ + pixels, qreal(0), maximum); changed_();
}

void UsbTools::paint(QPainter &p, const QRectF &panel) {
    panel_ = panel; p.fillRect(panel, Qt::white); p.setPen(QPen(Qt::black, 2)); p.drawRect(panel);
    if (typewriter_ && files_) {
        font(p, 30, true); p.drawText(QRectF(panel.left() + 22, panel.top() + 18, panel.width() - 44, 48), Qt::AlignVCenter, "Typewriter · Files and send");
        font(p, 20); p.drawText(QRectF(panel.left() + 22, panel.top() + 72, panel.width() - 44, 42), Qt::TextWordWrap,
            "Load or save UTF-8 text under any filename. Ordinary editing stays in RAM.");
        font(p, 19, true); p.drawText(QRectF(panel.left() + 22, panel.top() + 104, panel.width() - 44, 24), "Path");
        p.fillRect(file_control(0), Qt::white); p.setPen(Qt::black); p.drawRect(file_control(0));
        font(p, 21); p.drawText(file_control(0).adjusted(10, 0, -10, 0), Qt::AlignVCenter,
            QFontMetrics(p.font()).elidedText(display_path(path_entry_), Qt::ElideMiddle, int(file_control(0).width() - 20)));
        if (focus_ == 0) { p.setPen(QPen(Qt::black, 2, Qt::DashLine)); p.drawRect(file_control(0).adjusted(4, 4, -4, -4)); }
        const QString actions[] = {"Load", "Save", "New"};
        for (int i = 0; i < 3; ++i) button(p, file_control(i + 1), actions[i], false, focus_ == i + 1);
        font(p, 21, true); p.drawText(QRectF(panel.left() + 22, panel.top() + 286, panel.width() - 44, 30), "Whole-document speed");
        for (int i = 0; i < 5; ++i) button(p, file_control(i + 4), QString("%1 cps").arg(speeds[i]), speed_ == speeds[i], focus_ == i + 4);
        button(p, file_control(9), bulk_ ? "Stop sending document" : "Send entire document", bulk_, focus_ == 9);
        font(p, 19); p.drawText(QRectF(panel.left() + 22, panel.top() + 502, panel.width() - 44, std::max<qreal>(50, panel.height() - 600)), Qt::TextWordWrap, message_);
        button(p, file_control(10), "Back to editor", false, focus_ == 10);
        return;
    }
    font(p, 30, true);
    if (typewriter_) {
        p.drawText(QRectF(panel.left() + 22, panel.top() + 18, panel.width() - 390, 46), Qt::AlignVCenter, "Typewriter · Editor");
        const QRectF state(panel.right() - 352, panel.top() + 18, 330, 46);
        const bool paused = !ready_ && !live_requested_;
        p.fillRect(state, paused ? Qt::black : Qt::white);
        p.setPen(QPen(Qt::black, 2)); p.drawRect(state);
        p.setPen(paused ? Qt::white : Qt::black); font(p, 18, true);
        const QString state_text = bulk_ ? "SENDING DOCUMENT" : ready_ ? "LIVE · USB OUTPUT ON"
            : live_requested_ ? "CONNECTING…" : "PAUSED · USB OUTPUT OFF";
        p.drawText(state.adjusted(8, 0, -8, 0), Qt::AlignCenter, state_text);
        p.setPen(Qt::black);
    } else {
        p.drawText(panel.adjusted(22, 18, -22, -panel.height() + 64), Qt::AlignVCenter, "USB and typewriter · 3/3");
    }
    if (typewriter_) {
        const QString name = file_path_.isEmpty() ? "Untitled (hidden recovery name on exit)" : display_path(file_path_);
        font(p, 18); p.drawText(QRectF(panel.left() + 22, panel.top() + 66, panel.width() - 44, 36), Qt::AlignVCenter,
            QString("%1%2 · %3 · %4").arg(name, dirty_ ? " *" : "", profile_names[prefs_.usb_profile()], InputMethod::name(ime_.method())));
        editor_box_ = {panel.left() + 22, panel.top() + 104, panel.width() - 44, std::max<qreal>(100, panel.height() - 338)};
        p.setPen(QPen(Qt::black, 2)); p.drawRect(editor_box_); update_layout(); ensure_cursor_visible();
        QAbstractTextDocumentLayout::PaintContext context;
        if (cursor_ != anchor_) {
            QAbstractTextDocumentLayout::Selection selection;
            selection.cursor = QTextCursor(&text_layout_); selection.cursor.setPosition(anchor_); selection.cursor.setPosition(cursor_, QTextCursor::KeepAnchor);
            selection.format.setBackground(Qt::black); selection.format.setForeground(Qt::white); context.selections.append(selection);
        }
        p.save(); p.setClipRect(editor_box_.adjusted(2, 2, -2, -2));
        p.translate(editor_box_.left() + 10, editor_box_.top() + 8 - scroll_y_);
        text_layout_.documentLayout()->draw(&p, context);
        const auto block = text_layout_.findBlock(cursor_);
        if (block.isValid() && block.layout()) {
            const auto bounds = text_layout_.documentLayout()->blockBoundingRect(block);
            const auto line = block.layout()->lineForTextPosition(std::max(0, cursor_ - block.position()));
            const qreal x = line.isValid() ? line.cursorToX(std::max(0, cursor_ - block.position())) : 0;
            const qreal y = bounds.top() + (line.isValid() ? line.y() : 0);
            const qreal h = line.isValid() ? line.height() : 30;
            p.setPen(QPen(Qt::black, 2)); p.drawLine(QPointF(x, y), QPointF(x, y + h));
        }
        p.restore();
        font(p, 19, true); p.drawText(QRectF(panel.left() + 22, panel.bottom() - 225, panel.width() - 44, 34), Qt::AlignVCenter,
            QString("%1  %2").arg(InputMethod::name(ime_.method()), ime_.preedit()));
        const auto candidates = ime_.candidates();
        for (int i = 0; i < candidates.size(); ++i) button(p, candidate_box(i), QString("%1 %2").arg(i + 1).arg(candidates[i]), false, false);
        font(p, 17); p.drawText(QRectF(panel.left() + 22, panel.bottom() - 137, panel.width() - 44, 58), Qt::TextWordWrap, message_);
        const QString labels[] = {"Files & send", ready_ ? "Pause live" : "Start live", "Input method", "Unicode", "Exit typewriter"};
        for (int i = 0; i < 5; ++i) button(p, control(i), labels[i], i == 1 && ready_, false);
        return;
    }
    font(p, 21); p.drawText(panel.adjusted(22, 74, -22, -panel.height() + 130), Qt::TextWordWrap,
        QString("USB: %1\nLeaving Inkline restores USB networking. Mode changes take effect immediately.").arg(mode_));
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

UsbTools::Result UsbTools::activate_file(int index) {
    if (index == 1) load_document();
    else if (index == 2) save_document(expanded_path(), true);
    else if (index == 3) new_document();
    else if (index >= 4 && index <= 8) { speed_ = speeds[index - 4]; message_ = QString("Whole-document speed: %1 characters/second.").arg(speed_); }
    else if (index == 9) { if (bulk_) pause(); else send_document(); }
    else if (index == 10) { files_ = false; focus_ = 0; ensure_cursor_visible(); }
    changed_(); return Stay;
}
UsbTools::Result UsbTools::activate(int index) {
    if (typewriter_) {
        if (files_) return activate_file(index);
        if (index == 0) { files_ = true; focus_ = 0; }
        else if (index == 1) { if (ready_ || writer_.state() != QProcess::NotRunning) pause(); else start_writer(); }
        else if (index == 2) { pause(); return Methods; }
        else if (index == 3) return Unicode;
        else if (index == 4) { pause(); typewriter_ = false; status(); }
    } else {
        if (index < 3) { const QString modes[] = {"network", "send-keyboard", "receive-keyboard"}; change_mode(modes[index]); }
        else if (index <= 6) { prefs_.set_usb_profile(index - 3); }
        else if (index == 7) return Methods;
        else if (index == 8) {
            typewriter_ = true; files_ = false; focus_ = 0;
            message_ = "Editing is in RAM. Live typing starts automatically when Send keyboard is active.";
            if (mode_ == "keyboard") start_writer();
        }
        else if (index == 9) { close(); return Back; }
        else if (index == 10) { close(); return Done; }
    }
    changed_(); return Stay;
}
UsbTools::Result UsbTools::click(const QPointF &point) {
    if (typewriter_ && files_) {
        for (int i = 0; i < 11; ++i) if (file_control(i).contains(point)) { focus_ = i; return activate_file(i); }
        return Stay;
    }
    for (int i = 0; i < (typewriter_ ? 5 : 11); ++i) if (control(i).contains(point)) { focus_ = i; return activate(i); }
    if (typewriter_) {
        for (int i = 0; i < ime_.candidates().size(); ++i) if (candidate_box(i).contains(point)) { insert_document(QString::fromUtf8(ime_.candidate(i))); break; }
    }
    return Stay;
}

UsbTools::Result UsbTools::key(const MappedInput &event) {
    if (event.type != QEvent::KeyPress) { if (typewriter_ && !files_) ime_.key(event); return Stay; }
    if (!typewriter_) {
        if (event.key == Qt::Key_Escape) { close(); return Done; }
        if (event.key == Qt::Key_PageUp) { close(); return Back; }
        if (event.key == Qt::Key_Tab || event.key == Qt::Key_Down) focus_ = (focus_ + 1) % 11;
        else if (event.key == Qt::Key_Backtab || event.key == Qt::Key_Up) focus_ = (focus_ + 10) % 11;
        else if ((event.key == Qt::Key_Return || event.key == Qt::Key_Enter || event.key == Qt::Key_Space) && !event.repeat) return activate(focus_);
        changed_(); return Stay;
    }
    if (files_) {
        if (event.key == Qt::Key_Escape) { files_ = false; focus_ = 0; changed_(); return Stay; }
        if (event.key == Qt::Key_Tab || event.key == Qt::Key_Down) focus_ = (focus_ + 1) % 11;
        else if (event.key == Qt::Key_Backtab || event.key == Qt::Key_Up) focus_ = (focus_ + 10) % 11;
        else if ((event.key == Qt::Key_Return || event.key == Qt::Key_Enter) && !event.repeat) return activate_file(focus_);
        else if (focus_ == 0 && event.key == Qt::Key_Backspace && !path_entry_.isEmpty()) path_entry_.chop(1);
        else if (focus_ == 0 && event.key == Qt::Key_U && (event.modifiers & Qt::ControlModifier)) path_entry_.clear();
        else if (focus_ == 0 && !(event.modifiers & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier))) path_entry_ += event.text;
        changed_(); return Stay;
    }
    if (event.key == Qt::Key_Escape) { ime_.reset(); pause(); return Stay; }
    if (bulk_) { message_ = "Stop document sending before editing."; changed_(); return Stay; }
    if (event.key == Qt::Key_A && (event.modifiers & Qt::ControlModifier)) { anchor_ = 0; cursor_ = document_.size(); ensure_cursor_visible(); changed_(); return Stay; }
    const auto composition = ime_.key(event);
    if (!composition.commit.isEmpty()) insert_document(QString::fromUtf8(composition.commit));
    if (composition.consumed) { changed_(); return Stay; }
    const bool select = event.modifiers & Qt::ShiftModifier;
    update_layout();
    QTextCursor navigation(&text_layout_); navigation.setPosition(cursor_);
    QTextCursor::MoveOperation movement = QTextCursor::NoMove;
    switch (event.key) {
    case Qt::Key_Left: movement = QTextCursor::PreviousCharacter; break;
    case Qt::Key_Right: movement = QTextCursor::NextCharacter; break;
    case Qt::Key_Up: movement = QTextCursor::Up; break;
    case Qt::Key_Down: movement = QTextCursor::Down; break;
    case Qt::Key_Home: movement = QTextCursor::StartOfLine; break;
    case Qt::Key_End: movement = QTextCursor::EndOfLine; break;
    case Qt::Key_PageUp: movement = QTextCursor::PreviousBlock; break;
    case Qt::Key_PageDown: movement = QTextCursor::NextBlock; break;
    default: break;
    }
    if (movement != QTextCursor::NoMove) {
        navigation.movePosition(movement); move_cursor(navigation.position(), select);
    } else if (event.key == Qt::Key_Backspace) {
        if (cursor_ != anchor_) remove_selection();
        else if (cursor_ > 0) {
            QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, document_); boundary.setPosition(cursor_);
            int previous = int(boundary.toPreviousBoundary()); if (previous < 0) previous = cursor_ - 1;
            document_.remove(previous, cursor_ - previous); cursor_ = anchor_ = previous; dirty_ = true;
        }
        update_layout(); ensure_cursor_visible(); changed_();
    } else if (event.key == Qt::Key_Delete) {
        if (cursor_ != anchor_) remove_selection();
        else if (cursor_ < document_.size()) {
            QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, document_); boundary.setPosition(cursor_);
            int next = int(boundary.toNextBoundary()); if (next < 0) next = cursor_ + 1;
            document_.remove(cursor_, next - cursor_); dirty_ = true;
        }
        update_layout(); ensure_cursor_visible(); changed_();
    } else if (event.key == Qt::Key_Return || event.key == Qt::Key_Enter) insert_document("\n");
    else if (event.key == Qt::Key_Tab || event.key == Qt::Key_Backtab) insert_document("\t");
    else if (event.modifiers & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier)) {
        QString key;
        if ((event.key >= Qt::Key_A && event.key <= Qt::Key_Z) || (event.key >= Qt::Key_0 && event.key <= Qt::Key_9)) key = QString(QChar(ushort(event.key))).toLower();
        else if (event.key >= Qt::Key_F1 && event.key <= Qt::Key_F12) key = QString("f%1").arg(event.key - Qt::Key_F1 + 1);
        if (!key.isEmpty() && ready_) {
            QString prefix; if (event.modifiers & Qt::ControlModifier) prefix += "ctrl+";
            if (event.modifiers & Qt::ShiftModifier) prefix += "shift+";
            if (event.modifiers & Qt::AltModifier) prefix += "alt+";
            if (event.modifiers & Qt::MetaModifier) prefix += "super+";
            enqueue(request("key", prefix + key));
        }
    } else {
        QString text = event.text;
        if (event.caps_locked) for (auto &c : text) if (c.isLetter()) c = c.isLower() ? c.toUpper() : c.toLower();
        insert_document(text);
    }
    if (ready_ && (movement != QTextCursor::NoMove || event.key == Qt::Key_Backspace || event.key == Qt::Key_Delete)) {
        QString key;
        switch (event.key) {
        case Qt::Key_Backspace: key = "backspace"; break; case Qt::Key_Delete: key = "delete"; break;
        case Qt::Key_Left: key = "left"; break; case Qt::Key_Right: key = "right"; break;
        case Qt::Key_Up: key = "up"; break; case Qt::Key_Down: key = "down"; break;
        case Qt::Key_Home: key = "home"; break; case Qt::Key_End: key = "end"; break;
        case Qt::Key_PageUp: key = "pageup"; break; case Qt::Key_PageDown: key = "pagedown"; break;
        default: break;
        }
        if (!key.isEmpty()) enqueue(request("key", select ? "shift+" + key : key));
    }
    return Stay;
}
}
