#ifndef RMT_USB_TOOLS_HPP
#define RMT_USB_TOOLS_HPP
#include "rmt/clipboard.hpp"
#include "rmt/input_method.hpp"
#include "rmt/preferences.hpp"
#include <QPainter>
#include <QProcess>
#include <QQueue>
#include <QTimer>
#include <functional>
namespace rmt {
// Settings page three and the live USB typewriter. Text, composition, preview
// and pending output remain in RAM. The helper owns encoding and endpoint I/O.
class UsbTools : public QObject {
public:
    enum Result { Stay, Back, Done, Methods, Unicode };
    UsbTools(QObject &owner, Preferences &prefs, Clipboard &clipboard,
             std::function<void()> changed, const QString &directory = {});
    ~UsbTools() override;
    void open();
    void close();
    void pause();
    void quiesce();
    void paint(QPainter &p, const QRectF &panel);
    Result key(const MappedInput &input);
    Result click(const QPointF &point);
    void insert(const QString &text);
    void clipboard(InputAction action);
    void set_method(int method);
    bool typewriter() const { return typewriter_; }
    bool transmitting() const { return ready_; }
    QString preview() const { return preview_; }
private:
    struct Request { QByteArray wire; QString preview; };
    Preferences &prefs_;
    Clipboard &clipboard_;
    std::function<void()> changed_;
    QString directory_, mode_ = "checking", message_, preview_;
    InputMethod ime_;
    QProcess action_, status_, writer_;
    QTimer polling_, action_timeout_, writer_timeout_, kill_writer_;
    QRectF panel_;
    QQueue<Request> queue_;
    QByteArray replies_;
    int focus_ = 0, pending_bytes_ = 0;
    bool typewriter_ = false, ready_ = false, inflight_ = false, stopping_ = false;
    QRectF control(int index) const;
    QRectF candidate_box(int index) const;
    void status();
    void change_mode(const QString &mode);
    void start_writer();
    void stop_writer();
    void fail_writer(const QString &message);
    void enqueue(const QByteArray &wire, const QString &preview);
    void pump();
    void receive();
    Result activate(int index);
};
}
#endif
