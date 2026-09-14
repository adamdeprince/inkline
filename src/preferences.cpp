#include "rmt/preferences.hpp"
#include <QStandardPaths>
#include <stdexcept>
namespace rmt {
Preferences::Preferences(const QString &path)
    : settings_(path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/inkline/settings.ini" : path, QSettings::IniFormat),
      caps_control_(settings_.value("keyboard/capsControl", true).toBool()),
      bottom_bar_(settings_.value("display/bottomBar", true).toBool()) {}
void Preferences::save(const QString &name, bool value) {
    settings_.setValue(name, value);
    settings_.sync();
    if (settings_.status() != QSettings::NoError) throw std::runtime_error("Could not save Inkline settings");
}
void Preferences::set_caps_control(bool enabled) {
    if (caps_control_ == enabled) return;
    save("keyboard/capsControl", enabled);
    caps_control_ = enabled;
}
void Preferences::set_bottom_bar(bool visible) {
    if (bottom_bar_ == visible) return;
    save("display/bottomBar", visible);
    bottom_bar_ = visible;
}
}
