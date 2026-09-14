#include "rmt/caps_leds.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>
#ifdef __linux__
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace rmt {
#ifdef __linux__
namespace {
void set_led(int fd, bool value) {
    unsigned long current = 0;
    if (ioctl(fd, EVIOCGLED(sizeof(current)), &current) < 0 || bool(current & (1ul << LED_CAPSL)) == value) return;
    input_event events[2]{};
    events[0].type = EV_LED; events[0].code = LED_CAPSL; events[0].value = value;
    events[1].type = EV_SYN; events[1].code = SYN_REPORT;
    (void)::write(fd, events, sizeof(events));
}
}
#endif
CapsLeds::CapsLeds(QObject *parent) : QObject(parent) {
#ifdef __linux__
    // Offscreen device tests must never alter the physical keyboard.
    enabled_ = QGuiApplication::platformName() == "epaper";
    if (enabled_) {
        watcher_.addPath("/dev/input");
        connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { set_locked(locked_); });
        sync();
    }
#endif
}
CapsLeds::~CapsLeds() {
#ifdef __linux__
    for (const auto &device : devices_) { set_led(device.fd, device.original); ::close(device.fd); }
#endif
}
void CapsLeds::set_locked(bool locked) {
    locked_ = locked;
    if (!enabled_ || pending_) return;
    pending_ = true;
    // Run after the native handler finishes its own Caps Lock LED update.
    QTimer::singleShot(0, this, [this] { pending_ = false; sync(); });
}
void CapsLeds::sync() {
#ifdef __linux__
    if (!enabled_) return;
    QDir directory("/dev/input");
    const auto nodes = directory.entryList({"event*"}, QDir::Files | QDir::System);
    for (auto it = devices_.begin(); it != devices_.end();) {
        unsigned long leds = 0;
        if (!nodes.contains(QFileInfo(it->path).fileName()) || ioctl(it->fd, EVIOCGLED(sizeof(leds)), &leds) < 0) {
            ::close(it->fd); it = devices_.erase(it);
        } else ++it;
    }
    for (const auto &name : nodes) {
        const auto path = directory.filePath(name);
        bool known = false;
        for (const auto &device : devices_) if (device.path == path) { known = true; break; }
        if (known) continue;
        const int fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long supported = 0, current = 0;
        if (ioctl(fd, EVIOCGBIT(EV_LED, sizeof(supported)), &supported) < 0 || !(supported & (1ul << LED_CAPSL)) ||
            ioctl(fd, EVIOCGLED(sizeof(current)), &current) < 0) { ::close(fd); continue; }
        devices_.push_back({path, fd, bool(current & (1ul << LED_CAPSL))});
    }
    for (const auto &device : devices_) set_led(device.fd, locked_);
#endif
}
}
