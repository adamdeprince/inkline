#ifndef RMT_CAPS_LEDS_HPP
#define RMT_CAPS_LEDS_HPP
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <vector>
namespace rmt {
// Qt's native Caps Lock handler also changes keyboard LEDs. Keep them aligned
// with Inkline's application-level lock and restore their prior state on exit.
class CapsLeds final : public QObject {
public:
    explicit CapsLeds(QObject *parent = nullptr);
    ~CapsLeds();
    void set_locked(bool enabled);
private:
    struct Device { QString path; int fd; bool original; };
    std::vector<Device> devices_;
    QFileSystemWatcher watcher_;
    bool enabled_ = false, locked_ = false, pending_ = false;
    void sync();
};
}
#endif
