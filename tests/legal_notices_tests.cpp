// SPDX-License-Identifier: GPL-3.0-or-later
#include "rmt/legal_notices.hpp"
#include <QGuiApplication>
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    rmt::LegalNotices notices; notices.open();
    QImage image(1000, 750, QImage::Format_RGB32);
    auto paint = [&] { QPainter p(&image); notices.paint(p, QRectF(80, 24, 840, 702)); };
    auto key = [&](int code, QEvent::Type type = QEvent::KeyPress) {
        rmt::MappedInput event{}; event.type = type; event.key = code;
        return notices.key(event);
    };
    paint();
    CHECK(notices.text(0).contains("WITHOUT"));
    CHECK(notices.text(0).contains("redistribute"));
    CHECK(notices.text(0).contains("inkline-source.tar.gz"));
    // Every notice is readable, including the final line of long licenses.
    bool found_gpl = false, found_lgpl = false, found_ghostty = false, found_adobe = false;
    for (int i = 0; i < notices.count(); ++i) {
        CHECK(notices.current() == i && !notices.title(i).isEmpty());
        const auto text = notices.text(i);
        CHECK(!text.isEmpty());
        found_gpl |= text.contains("END OF TERMS AND CONDITIONS") && text.contains("GNU GENERAL PUBLIC LICENSE");
        found_lgpl |= text.contains("GNU LESSER GENERAL PUBLIC LICENSE");
        found_ghostty |= text.contains("Mitchell Hashimoto, Ghostty contributors");
        found_adobe |= text.contains("2014-2021 Adobe");
        paint(); key(Qt::Key_End); paint();
        const auto end = notices.position();
        notices.scroll(1000000); CHECK(notices.position() == end);
        key(Qt::Key_Home); CHECK(notices.position() == 0);
        if (end > 0) {
            notices.begin_drag(QPointF(300, 400)); notices.drag(QPointF(300, 220));
            CHECK(notices.position() > 0 && notices.position() <= end);
            key(Qt::Key_PageDown); CHECK(notices.position() > 0);
        }
        key(Qt::Key_Right, QEvent::KeyRelease); CHECK(notices.current() == i);
        key(Qt::Key_Right);
    }
    CHECK(found_gpl && found_lgpl && found_ghostty && found_adobe);
    CHECK(notices.current() == 0 && notices.position() == 0);
    CHECK(key(Qt::Key_Escape));
    CHECK(key(Qt::Key_Return)); // Back is focused on first opening.
    std::puts("Offline notices, full text, navigation and scroll bounds passed");
}
