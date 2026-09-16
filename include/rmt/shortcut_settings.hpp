#ifndef RMT_SHORTCUT_SETTINGS_HPP
#define RMT_SHORTCUT_SETTINGS_HPP
#include "rmt/clipboard.hpp"
#include "rmt/input.hpp"
#include "rmt/hotkey_config.hpp"
#include <QPainter>
#include <QObject>
namespace rmt {
// Page two of Settings. Draft text stays in RAM; only Save/Remove write files.
class ShortcutSettings {
public:
    enum Result { Stay, Back, Done, Next };
    ShortcutSettings(QObject &owner, Clipboard &clipboard);
    void open();
    void paint(QPainter &p, const QRectF &panel);
    Result key(const MappedInput &event);
    Result click(const QPointF &point);
    void insert(const QString &text);
    void clipboard(InputAction action);
private:
    QObject &owner_;
    Clipboard &clipboard_;
    std::string directory_;
    std::vector<hotkeys::Binding> bindings_;
    QRectF panel_;
    QString command_, saved_, message_;
    int letter_ = 0, focus_ = 0, cursor_ = 0, anchor_ = 0;
    bool epaper_ = false, saved_epaper_ = false, removing_ = false;
    qreal scroll_ = 0;
    QRectF letter_box(int index) const;
    QRectF command_box() const;
    QRectF control(int index) const;
    bool locked() const { return letter_ == 't' - 'a'; }
    bool dirty() const { return command_ != saved_ || epaper_ != saved_epaper_; }
    const hotkeys::Binding *binding() const;
    void load();
    void choose(int letter);
    void reset();
    void save();
    void remove();
    void notify();
    Result activate();
    Result leave(Result result);
    void move_cursor(int position, bool select);
};
}
#endif
