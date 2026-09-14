#ifndef RMT_PREFERENCES_HPP
#define RMT_PREFERENCES_HPP
#include <QSettings>
namespace rmt {
class Preferences {
public:
    static constexpr int MIN_FONT = 16, MAX_FONT = 48, DEFAULT_FONT = 26;
    explicit Preferences(const QString &path = {});
    bool caps_control() const { return caps_control_; }
    bool bottom_bar() const { return bottom_bar_; }
    int input_method() const { return input_method_; }
    void set_input_method(int method);
    int font_pixels() const { return font_pixels_; }
    void set_caps_control(bool enabled);
    void set_bottom_bar(bool visible);
    void set_font_pixels(int pixels);
private:
    QSettings settings_;
    bool caps_control_, bottom_bar_;
    int font_pixels_, input_method_;
    void save(const QString &name, const QVariant &value);
};
}
#endif
