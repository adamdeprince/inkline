#include "rmt/battery.hpp"
#include <QDir>
#include <QFile>
#include <algorithm>

namespace rmt {
namespace {
QString read_value(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}
BatteryInfo supply(const QString &path) {
    bool valid = false;
    const int value = read_value(path + "/capacity").toInt(&valid);
    if (!valid) return {};
    return {std::clamp(value, 0, 100), read_value(path + "/status")};
}
}

QString BatteryInfo::percentage() const {
    return available() ? QString::number(percent) + '%' : "--";
}
QString BatteryInfo::compact() const {
    QString value = "Battery " + percentage();
    if (status.compare("Charging", Qt::CaseInsensitive) == 0) value += " · charging";
    else if (status.compare("Full", Qt::CaseInsensitive) == 0) value += " · full";
    return value;
}
QString BatteryInfo::report() const {
    if (!available()) return "Battery information is unavailable.";
    QString value = "Battery: " + percentage();
    if (!status.isEmpty() && status.compare("Unknown", Qt::CaseInsensitive) != 0)
        value += "\nStatus: " + status;
    return value;
}

BatteryInfo read_battery(const QString &requested_root) {
    const QString root = requested_root.isEmpty()
        ? qEnvironmentVariable("INKLINE_BATTERY_ROOT", "/sys/class/power_supply")
        : requested_root;
    QDir directory(root);
    BatteryInfo fallback;
    for (const auto &name : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString path = directory.filePath(name);
        if (read_value(path + "/present") == "0") continue;
        const auto info = supply(path);
        if (!info.available()) continue;
        if (read_value(path + "/type").compare("Battery", Qt::CaseInsensitive) == 0) return info;
        if (!fallback.available()) fallback = info;
    }
    return fallback;
}
}
