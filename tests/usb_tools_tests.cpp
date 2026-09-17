#include "rmt/usb_tools.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
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
rmt::MappedInput event(int key, const QString &text = {}, Qt::KeyboardModifiers modifiers = {}) {
    rmt::MappedInput e; e.action = rmt::InputAction::Send; e.type = QEvent::KeyPress; e.key = key; e.text = text; e.modifiers = modifiers; return e;
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir temp; CHECK(temp.isValid()); QObject owner;
    rmt::Preferences prefs(temp.filePath("settings.ini")); rmt::Clipboard clipboard;
    write(temp.filePath("inkline-usb"), "#!/bin/sh\nprintf 'keyboard\\n'\n");
    write(temp.filePath("keyboard-send"), "#!/bin/sh\ncd -- \"$(dirname -- \"$0\")\"\nprintf '%s\\n' \"$*\" >> arguments\nexec python3 -B fake.py \"$@\"\n");
    write(temp.filePath("fake.py"),
          "import json,sys\nprint('{\"ready\":true}',flush=True)\n"
          "for line in sys.stdin:\n obj=json.loads(line)\n"
          " with open('traffic','a',encoding='utf8') as f:f.write(line)\n print('{\"ok\":true}',flush=True)\n");
    const auto draft = temp.filePath(".draft");
    int changes = 0;
    rmt::UsbTools usb(owner, prefs, clipboard, [&] { ++changes; }, temp.path(), draft);
    QImage image(1000, 750, QImage::Format_RGB32); image.fill(Qt::white);
    auto paint = [&] { QPainter p(&image); usb.paint(p, QRectF(0, 0, 1000, 750)); };

    usb.open(); pump(); paint();
    const auto screenshots = qEnvironmentVariable("INKLINE_TEST_IMAGES");
    if (!screenshots.isEmpty()) CHECK(image.save(screenshots + "/usb-settings.png"));
    for (int i = 0; i < 6; ++i) usb.key(event(Qt::Key_Tab));
    usb.key(event(Qt::Key_Return)); CHECK(prefs.usb_profile() == 3);
    CHECK(!QFile::exists(temp.filePath("settings.ini")));
    usb.key(event(Qt::Key_Tab)); usb.key(event(Qt::Key_Tab)); usb.key(event(Qt::Key_Return));
    CHECK(usb.typewriter());
    for (int i = 0; i < 50 && !usb.transmitting(); ++i) pump();
    CHECK(usb.transmitting());

    usb.set_method(rmt::InputMethod::Romaji);
    usb.key(event(Qt::Key_K, "k")); pump(); CHECK(usb.document().isEmpty());
    usb.key(event(Qt::Key_A, "a")); pump(); CHECK(usb.document() == QString::fromUtf8("か"));
    usb.set_method(rmt::InputMethod::Off);
    clipboard.set("你好😀"); usb.clipboard(rmt::InputAction::Paste); pump();
    CHECK(usb.document() == QString::fromUtf8("か你好😀"));
    CHECK(!QFile::exists(draft)); // Editing and USB acknowledgements do not touch the document store.
    paint();
    if (!screenshots.isEmpty()) CHECK(image.save(screenshots + "/typewriter.png"));

    usb.key(event(Qt::Key_Backspace)); pump(); CHECK(usb.document() == QString::fromUtf8("か你好"));
    usb.key(event(Qt::Key_A, "", Qt::ControlModifier));
    usb.clipboard(rmt::InputAction::Copy); CHECK(clipboard.text() == QByteArray("か你好"));
    usb.key(event(Qt::Key_Escape)); pump(); CHECK(!usb.transmitting());
    const auto sent = read(temp.filePath("traffic"));
    usb.insert(" stays in RAM"); pump();
    CHECK(usb.document().endsWith(" stays in RAM"));
    CHECK(read(temp.filePath("traffic")) == sent);
    CHECK(!QFile::exists(draft));

    // Files & send: arbitrary extensions, explicit Save, selectable bulk speed.
    paint(); CHECK(usb.click(QPointF(70, 700)) == rmt::UsbTools::Stay);
    paint(); if (!screenshots.isEmpty()) CHECK(image.save(screenshots + "/typewriter-files.png"));
    const auto named = temp.filePath("notes.any-name");
    usb.key(event(Qt::Key_U, "", Qt::ControlModifier));
    usb.key(event(Qt::Key_X, named));
    usb.key(event(Qt::Key_Tab)); usb.key(event(Qt::Key_Tab)); usb.key(event(Qt::Key_Return));
    CHECK(read(named) == usb.document().toUtf8());
    const auto saved = read(named);
    usb.click(QPointF(900, 700)); // Back to editor.
    usb.insert(" unsaved"); CHECK(read(named) == saved);
    usb.persist(); CHECK(read(named) == usb.document().toUtf8());

    paint(); usb.click(QPointF(70, 700));
    usb.click(QPointF(70, 350)); // 5 cps.
    usb.click(QPointF(500, 445));
    for (int i = 0; i < 100 && usb.bulk_sending(); ++i) pump(20);
    CHECK(!usb.bulk_sending()); CHECK(usb.transmitting());
    CHECK(read(temp.filePath("arguments")).contains("--cps 5"));

    // Loading reads UTF-8 without filtering the filename extension.
    const auto source = temp.filePath("loaded.binary-looking"); write(source, "line one\nline two\n");
    usb.click(QPointF(100, 150));
    usb.key(event(Qt::Key_U, "", Qt::ControlModifier)); usb.key(event(Qt::Key_X, source));
    usb.click(QPointF(100, 240));
    CHECK(usb.document() == "line one\nline two\n"); CHECK(!usb.dirty());

    usb.click(QPointF(900, 700)); usb.insert("exit save");
    CHECK(read(source) == QByteArray("line one\nline two\n"));
    usb.persist(); CHECK(read(source) == usb.document().toUtf8());
    usb.quiesce(); usb.close(); CHECK(!usb.typewriter()); CHECK(changes > 0);
    CHECK(!QFile::exists(temp.filePath("settings.ini")));
    prefs.persist(); CHECK(rmt::Preferences(temp.filePath("settings.ini")).usb_profile() == 3);

    // A new untitled document gets its hidden name only at normal shutdown.
    rmt::UsbTools recovery(owner, prefs, clipboard, [] {}, temp.path(), draft);
    recovery.open(); pump();
    for (int i = 0; i < 8; ++i) recovery.key(event(Qt::Key_Tab));
    recovery.key(event(Qt::Key_Return)); recovery.pause(); recovery.insert("hidden recovery");
    CHECK(!QFile::exists(draft)); recovery.persist(); CHECK(read(draft) == QByteArray("hidden recovery"));
    rmt::UsbTools restored(owner, prefs, clipboard, [] {}, temp.path(), draft);
    CHECK(restored.document() == "hidden recovery"); CHECK(restored.dirty());
    std::puts("USB Typewriter: RAM editor, Unicode input, explicit files, shutdown persistence, speed and bulk send passed.");
}
