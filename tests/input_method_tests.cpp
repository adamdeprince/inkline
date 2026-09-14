#include "rmt/input_method.hpp"
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
rmt::InputMethod::Result key(rmt::InputMethod &im, int code, const QString &text = {}, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    rmt::MappedInput event; event.key = code; event.text = text; event.modifiers = mods;
    auto result = im.key(event); event.type = QEvent::KeyRelease;
    CHECK(im.key(event).consumed == result.consumed); return result;
}
QByteArray type(rmt::InputMethod &im, const QString &text) {
    QByteArray result;
    for (const auto ch : text) { auto out = key(im, ch.toUpper().unicode(), ch); result += out.commit; if (!out.consumed) result += QString(ch).toUtf8(); }
    return result;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); rmt::InputMethod im;
    CHECK(type(im, "normal") == "normal");
    im.set_method(rmt::InputMethod::Romaji);
    CHECK(type(im, "nihongo") == "にほんご");
    CHECK(type(im, "nyakko") == "にゃっこ");
    CHECK(type(im, "n ") == "ん ");
    im.set_method(rmt::InputMethod::USInternational);
    CHECK(type(im, "'e~n\"u") == "éñü");
    CHECK(type(im, "'").isEmpty()); CHECK(key(im, Qt::Key_Backspace).consumed); CHECK(!im.pending());
    im.set_method(rmt::InputMethod::Pinyin);
    CHECK(type(im, "ni").isEmpty()); CHECK(im.preedit() == "ni"); CHECK(im.candidates().first() == "你");
    CHECK(key(im, Qt::Key_Return).commit == "你"); CHECK(!im.pending());
    CHECK(type(im, "hao ") == "好");
    type(im, "wo"); const auto candidate = im.candidates().at(1); CHECK(type(im, "2") == candidate.toUtf8());
    type(im, "ni"); type(im, "]"); CHECK(im.page() == 1); type(im, "["); CHECK(im.page() == 0);
    CHECK(key(im, Qt::Key_Escape).consumed); CHECK(!im.pending());
    type(im, "ni"); CHECK(!key(im, Qt::Key_C, "c", Qt::ControlModifier).consumed); CHECK(!im.pending());
    im.set_method(rmt::InputMethod::Zhuyin); CHECK(type(im, "su ") == "你");
    im.set_method(rmt::InputMethod::Wubi); CHECK(type(im, "a ") == "工");
    CHECK(type(im, "nawt ") == "以其人之道还至其人之身");
    type(im, "a"); CHECK(key(im, Qt::Key_Backspace).consumed); CHECK(!im.pending());
    im.set_method(rmt::InputMethod::Off); CHECK(type(im, "'hello") == "'hello");
    std::puts("Input methods: direct, Romaji, US-International, Pinyin, Zhuyin, Wubi, candidates and key releases passed.");
}
