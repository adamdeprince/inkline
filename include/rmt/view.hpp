#ifndef RMT_VIEW_HPP
#define RMT_VIEW_HPP
#include <QQuickPaintedItem>
#include <memory>
#include <string_view>
#include <vector>

namespace rmt {
class TerminalView final : public QQuickPaintedItem {
public:
    static constexpr int TERMINALS = 6;
    TerminalView(QQuickItem *parent, int font_pixels, bool demo,
                 const QString &settings_path = {}, std::vector<std::string> shell = {});
    ~TerminalView();
    void start();
    void layout();
    void paint(QPainter *painter) override;
    QImage snapshot();
    void settings();
    void select_terminal(int index);
    void send_text(std::string_view text);
    int active_terminal() const;
    int terminal_count() const;
    bool bottom_bar() const;
    bool settings_open() const;
    bool quit_confirmation_open() const;
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
private:
    class Private;
    std::unique_ptr<Private> d_;
};
}
#endif
