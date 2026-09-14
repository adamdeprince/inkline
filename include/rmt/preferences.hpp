#ifndef RMT_PREFERENCES_HPP
#define RMT_PREFERENCES_HPP
#include <QSettings>
namespace rmt {
class Preferences {
public:
    explicit Preferences(const QString &path = {});
    bool caps_control() const { return caps_control_; }
    bool bottom_bar() const { return bottom_bar_; }
    void set_caps_control(bool enabled);
    void set_bottom_bar(bool visible);
private:
    QSettings settings_;
    bool caps_control_, bottom_bar_;
    void save(const QString &name, bool value);
};
}
#endif
