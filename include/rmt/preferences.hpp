#ifndef RMT_PREFERENCES_HPP
#define RMT_PREFERENCES_HPP
#include <QVariantMap>
namespace rmt {
class Preferences {
public:
    static constexpr int MIN_FONT = 6, MAX_FONT = 48, DEFAULT_FONT = 26;
    static constexpr int DEFAULT_DARKNESS = 50, DEFAULT_MINIMUM_CONTRAST = 35;
    static constexpr int DEFAULT_UPDATE_PROFILE = 2, UPDATE_PROFILE_COUNT = 5;
    explicit Preferences(const QString &path = {});
    bool caps_control() const { return caps_control_; }
    bool bottom_bar() const { return bottom_bar_; }
    int input_method() const { return input_method_; }
    int font_pixels() const { return font_pixels_; }
    int text_darkness() const { return darkness_; }
    int minimum_contrast() const { return contrast_; }
    int update_profile() const { return update_profile_; }
    int usb_profile() const { return usb_profile_; }
    void set_caps_control(bool enabled) { caps_control_ = enabled; }
    void set_bottom_bar(bool visible) { bottom_bar_ = visible; }
    void set_input_method(int method);
    void set_font_pixels(int pixels);
    void set_text_darkness(int value);
    void set_minimum_contrast(int value);
    void set_update_profile(int value);
    void set_usb_profile(int value);
    // Setters only change RAM. Call once when the terminal application exits.
    // An unchanged session (including changes reverted before exit) writes nothing.
    void persist();
private:
    QString path_;
    bool caps_control_, bottom_bar_;
    int font_pixels_, input_method_, darkness_, contrast_, update_profile_, usb_profile_;
    QVariantMap initial_;
    QVariantMap values() const;
};
}
#endif
