#include "rmt/preferences.hpp"
#include "rmt/input_method.hpp"
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <stdexcept>
namespace rmt {
Preferences::Preferences(const QString &path)
    : path_(path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/inkline/settings.ini" : path) {
    QSettings settings(path_, QSettings::IniFormat);
    caps_control_ = settings.value("keyboard/capsControl", true).toBool();
    bottom_bar_ = settings.value("display/bottomBar", true).toBool();
    font_pixels_ = std::clamp(settings.value("display/fontPixels", DEFAULT_FONT).toInt(), MIN_FONT, MAX_FONT);
    input_method_ = std::clamp(settings.value("keyboard/inputMethod", 0).toInt(), 0, int(InputMethod::COUNT) - 1);
    darkness_ = std::clamp(settings.value("display/textDarkness", DEFAULT_DARKNESS).toInt(), 0, 100);
    contrast_ = std::clamp(settings.value("display/minimumContrast", DEFAULT_MINIMUM_CONTRAST).toInt(), 0, 100);
    update_profile_ = settings.value("display/updateProfile", DEFAULT_UPDATE_PROFILE).toInt();
    if (update_profile_ < 0 || update_profile_ >= UPDATE_PROFILE_COUNT) update_profile_ = DEFAULT_UPDATE_PROFILE;
    usb_profile_ = std::clamp(settings.value("usb/hostProfile", 0).toInt(), 0, 3);
    initial_ = values();
}
QVariantMap Preferences::values() const {
    return {{"keyboard/capsControl", caps_control_}, {"display/bottomBar", bottom_bar_},
            {"display/fontPixels", font_pixels_}, {"keyboard/inputMethod", input_method_},
            {"display/textDarkness", darkness_}, {"display/minimumContrast", contrast_},
            {"display/updateProfile", update_profile_}, {"usb/hostProfile", usb_profile_}};
}
void Preferences::persist() {
    const auto current = values();
    if (current == initial_) return;
    QSettings settings(path_, QSettings::IniFormat);
    for (auto i = current.cbegin(); i != current.cend(); ++i)
        if (i.value() != initial_.value(i.key())) settings.setValue(i.key(), i.value());
    settings.sync();
    if (settings.status() != QSettings::NoError) throw std::runtime_error("Could not save Inkline settings");
    initial_ = current;
}
void Preferences::set_input_method(int method) {
    if (method < 0 || method >= InputMethod::COUNT) throw std::out_of_range("Input method");
    input_method_ = method;
}
void Preferences::set_font_pixels(int pixels) { font_pixels_ = std::clamp(pixels, MIN_FONT, MAX_FONT); }
void Preferences::set_text_darkness(int value) { darkness_ = std::clamp(value, 0, 100); }
void Preferences::set_minimum_contrast(int value) { contrast_ = std::clamp(value, 0, 100); }
void Preferences::set_update_profile(int value) {
    if (value < 0 || value >= UPDATE_PROFILE_COUNT) throw std::out_of_range("Update profile");
    update_profile_ = value;
}
void Preferences::set_usb_profile(int value) {
    if (value < 0 || value > 3) throw std::out_of_range("USB host profile");
    usb_profile_ = value;
}
}
