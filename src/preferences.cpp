#include "rmt/preferences.hpp"
#include "rmt/input_method.hpp"
#include <QStandardPaths>
#include <algorithm>
#include <stdexcept>
namespace rmt {
Preferences::Preferences(const QString &path)
    : settings_(path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/inkline/settings.ini" : path, QSettings::IniFormat),
      caps_control_(settings_.value("keyboard/capsControl", true).toBool()),
      bottom_bar_(settings_.value("display/bottomBar", true).toBool()),
      font_pixels_(std::clamp(settings_.value("display/fontPixels", DEFAULT_FONT).toInt(), MIN_FONT, MAX_FONT)),
      input_method_(std::clamp(settings_.value("keyboard/inputMethod", 0).toInt(), 0, int(InputMethod::COUNT) - 1)) {}
void Preferences::save(const QString &name, const QVariant &value) {
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
void Preferences::set_input_method(int method) {
    if (method < 0 || method >= InputMethod::COUNT) throw std::out_of_range("Input method");
    if (input_method_ == method) return;
    save("keyboard/inputMethod", method); input_method_ = method;
}
void Preferences::set_font_pixels(int pixels) {
    pixels = std::clamp(pixels, MIN_FONT, MAX_FONT);
    if (font_pixels_ == pixels) return;
    save("display/fontPixels", pixels);
    font_pixels_ = pixels;
}
}
