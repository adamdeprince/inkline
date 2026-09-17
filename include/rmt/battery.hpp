#ifndef RMT_BATTERY_HPP
#define RMT_BATTERY_HPP
#include <QString>

namespace rmt {
struct BatteryInfo {
    int percent = -1;
    QString status;
    bool available() const { return percent >= 0; }
    QString percentage() const;
    QString compact() const;
    QString report() const;
};

// Linux power-supply sysfs is read-only and memory backed. Tests can supply a
// fixture root; production uses /sys/class/power_supply.
BatteryInfo read_battery(const QString &root = {});
}
#endif
