#ifndef RMT_UNICODE_KEYBOARD_HPP
#define RMT_UNICODE_KEYBOARD_HPP

#include <QByteArray>
#include <QString>
#include <array>
#include <cstdint>
#include <vector>

namespace rmt {

/*
 * A complete picker for the text scalar values known to the tablet's Qt.
 * Unicode General_Category is used instead of a hand-maintained character
 * list, so supplementary-plane characters and future firmware additions do
 * not silently disappear. C0/C1 controls, surrogates, noncharacters and
 * unassigned values are deliberately omitted: sending those through a VT is
 * a control operation or invalid text rather than keyboard text.
 */
class UnicodeKeyboard {
public:
    enum Category {
        Letters,
        Marks,
        Numbers,
        Punctuation,
        MathCurrency,
        Symbols,
        Spaces,
        Format,
        PrivateUse,
        CategoryCount
    };

    static QString category_name(int category);
    static const std::vector<char32_t> &characters(int category);
    static int category_for(char32_t codepoint);
    static int index_of(int category, char32_t codepoint);
    static QString code_label(char32_t codepoint);
    static QString preview(char32_t codepoint);
    static QByteArray utf8(char32_t codepoint);
    static bool selectable(char32_t codepoint);
};

}
#endif
