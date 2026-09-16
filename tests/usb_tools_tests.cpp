#include "rmt/usb_tools.hpp"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
void pump(int ms = 100) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < ms) { QCoreApplication::processEvents(); QThread::msleep(5); }
}
void write(const QString &path, const QByteArray &data) {
    QFile f(path); CHECK(f.open(QIODevice::WriteOnly)); CHECK(f.write(data) == data.size());
}
QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
rmt::MappedInput event(int key, const QString &text = {}) {
    rmt::MappedInput e; e.action = rmt::InputAction::Send; e.type = QEvent::KeyPress; e.key = key; e.text = text; return e;
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir temp; CHECK(temp.isValid()); QObject owner;
    rmt::Preferences prefs(temp.filePath("settings.ini")); rmt::Clipboard clipboard;
    write(temp.filePath("inkline-usb"), "#!/bin/sh\nprintf 'keyboard\\n'\n");
    write(temp.filePath("inkline-type"), "#!/bin/sh\ncd -- \"$(dirname -- \"$0\")\"\nexec python3 -B fake.py \"$@\"\n");
    write(temp.filePath("fake.py"),
          "import json,sys\nprint('{\"ready\":true}',flush=True)\n"
          "for line in sys.stdin:\n obj=json.loads(line)\n"
          " with open('traffic','a',encoding='utf8') as f:f.write(line)\n print('{\"ok\":true}',flush=True)\n");
    int changes = 0;
    rmt::UsbTools usb(owner, prefs, clipboard, [&] { ++changes; }, temp.path());
    QImage image(1000, 750, QImage::Format_RGB32); image.fill(Qt::white);
    auto paint = [&] { QPainter p(&image); usb.paint(p, QRectF(0, 0, 1000, 750)); };
    usb.open(); pump(); paint();
    const auto screenshots = qEnvironmentVariable("INKLINE_TEST_IMAGES");
    if (!screenshots.isEmpty()) CHECK(image.save(screenshots + "/usb-settings.png"));
    for (int i = 0; i < 6; ++i) usb.key(event(Qt::Key_Tab));
    usb.key(event(Qt::Key_Return)); CHECK(prefs.usb_profile() == 3); // Windows selected, still only RAM.
    CHECK(!QFile::exists(temp.filePath("settings.ini")));
    usb.key(event(Qt::Key_Tab)); usb.key(event(Qt::Key_Tab)); usb.key(event(Qt::Key_Return));
    CHECK(usb.typewriter() && !usb.transmitting()); paint();
    usb.insert("not sent"); CHECK(!QFile::exists(temp.filePath("traffic")));
    usb.click(QPointF(70, 700));
    for (int i = 0; i < 30 && !usb.transmitting(); ++i) pump();
    CHECK(usb.transmitting());
    usb.set_method(rmt::InputMethod::Romaji);
    usb.key(event(Qt::Key_K, "k")); pump(); CHECK(read(temp.filePath("traffic")).isEmpty());
    usb.key(event(Qt::Key_A, "a")); pump(); CHECK(usb.preview() == QString::fromUtf8("か"));
    usb.set_method(rmt::InputMethod::Off);
    clipboard.set("你好😀"); usb.clipboard(rmt::InputAction::Paste); pump();
    CHECK(usb.preview() == QString::fromUtf8("か你好😀"));
    paint();
    if (!screenshots.isEmpty()) CHECK(image.save(screenshots + "/typewriter.png"));
    usb.key(event(Qt::Key_Backspace)); pump(); CHECK(usb.preview() == QString::fromUtf8("か你好"));
    auto ctrl = event(Qt::Key_A, "\1"); ctrl.modifiers = Qt::ControlModifier; usb.key(ctrl); pump();
    CHECK(read(temp.filePath("traffic")).contains("ctrl+a"));
    usb.clipboard(rmt::InputAction::Copy); CHECK(clipboard.text() == QByteArray("か你好"));
    usb.key(event(Qt::Key_Escape)); pump(); CHECK(!usb.transmitting());
    const auto sent = read(temp.filePath("traffic")); usb.insert("ignored while paused"); pump();
    CHECK(read(temp.filePath("traffic")) == sent);
    paint(); CHECK(usb.click(QPointF(70, 700)) == rmt::UsbTools::Stay);
    for (int i = 0; i < 50 && !usb.transmitting(); ++i) pump();
    CHECK(usb.transmitting()); // Python can take longer than 250 ms to restart on ARM.
    usb.quiesce(); CHECK(!usb.transmitting());
    usb.close(); CHECK(!usb.typewriter()); CHECK(changes > 0);
    CHECK(!QFile::exists(temp.filePath("settings.ini")));
    prefs.persist(); CHECK(rmt::Preferences(temp.filePath("settings.ini")).usb_profile() == 3);
    std::puts("USB UI: profiles, explicit start, IME, clipboard, host chords, Escape, quiesce and RAM preferences passed.");
}
