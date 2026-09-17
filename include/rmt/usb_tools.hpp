#ifndef RMT_USB_TOOLS_HPP
#define RMT_USB_TOOLS_HPP
#include "rmt/clipboard.hpp"
#include "rmt/input_method.hpp"
#include "rmt/preferences.hpp"
#include <QPainter>
#include <QProcess>
#include <QQueue>
#include <QTextDocument>
#include <QTimer>
#include <functional>

namespace rmt {
// Settings page three and the USB typewriter. The editor, composition and
// pending output remain in RAM. A document is written only by Save or persist(),
// which TerminalView calls during its normal shutdown.
class UsbTools : public QObject {
public:
    enum Result { Stay, Back, Done, Methods, Unicode };
    UsbTools(QObject &owner, Preferences &prefs, Clipboard &clipboard,
             std::function<void()> changed, const QString &directory = {},
             const QString &draft_path = {});
    ~UsbTools() override;
    void open();
    void close();
    void pause();
    void quiesce();
    void persist();
    void paint(QPainter &p, const QRectF &panel);
    Result key(const MappedInput &input);
    Result click(const QPointF &point);
    void insert(const QString &text);
    void clipboard(InputAction action);
    void set_method(int method);
    bool begin_drag(const QPointF &point);
    void drag(const QPointF &point);
    void end_drag(const QPointF &point);
    void scroll(qreal pixels);
    bool typewriter() const { return typewriter_; }
    bool editor_page() const { return typewriter_ && !files_; }
    bool transmitting() const { return ready_; }
    bool bulk_sending() const { return bulk_; }
    QString preview() const { return document_; }
    QString document() const { return document_; }
    QString file_path() const { return file_path_; }
    bool dirty() const { return dirty_; }
private:
    struct Request { QByteArray wire; QString preview; int bulk_end = -1; };
    Preferences &prefs_;
    Clipboard &clipboard_;
    std::function<void()> changed_;
    QString directory_, draft_path_, mode_ = "checking", message_;
    QString document_, file_path_, path_entry_;
    InputMethod ime_;
    QTextDocument text_layout_;
    QProcess action_, status_, writer_;
    QTimer polling_, action_timeout_, writer_timeout_, kill_writer_;
    QRectF panel_, editor_box_;
    QQueue<Request> queue_;
    QByteArray replies_;
    int focus_ = 0, pending_bytes_ = 0, cursor_ = 0, anchor_ = 0;
    int bulk_offset_ = 0, speed_ = 20, writer_speed_ = 0;
    qreal scroll_y_ = 0;
    bool typewriter_ = false, files_ = false, ready_ = false, inflight_ = false;
    bool stopping_ = false, dirty_ = false, bulk_ = false, restart_bulk_ = false;
    bool selecting_ = false, live_requested_ = false, remove_draft_on_exit_ = false;
    QRectF control(int index) const;
    QRectF file_control(int index) const;
    QRectF candidate_box(int index) const;
    void status();
    void change_mode(const QString &mode);
    void start_writer();
    void stop_writer(bool cancel_bulk = true);
    void fail_writer(const QString &message);
    void enqueue(const QByteArray &wire, const QString &preview = {}, int bulk_end = -1);
    void pump();
    void receive();
    void next_bulk();
    void send_document();
    QString validate_document() const;
    Result activate(int index);
    Result activate_file(int index);
    void insert_document(const QString &text, bool transmit = true);
    void remove_selection();
    void move_cursor(int position, bool selecting);
    void update_layout();
    void ensure_cursor_visible();
    int hit_test(const QPointF &point);
    QString expanded_path() const;
    bool load_document();
    bool save_document(const QString &path, bool explicit_save);
    void new_document();
};
}
#endif
