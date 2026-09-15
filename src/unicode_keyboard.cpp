#include "rmt/unicode_keyboard.hpp"
#include <QChar>
#include <QStringList>
#include <algorithm>

namespace rmt {
namespace {
using Catalog = std::array<std::vector<char32_t>, UnicodeKeyboard::CategoryCount>;

int category_for_qt(QChar::Category category) {
    switch (category) {
    case QChar::Letter_Uppercase:
    case QChar::Letter_Lowercase:
    case QChar::Letter_Titlecase:
    case QChar::Letter_Modifier:
    case QChar::Letter_Other:
        return UnicodeKeyboard::Letters;
    case QChar::Mark_NonSpacing:
    case QChar::Mark_SpacingCombining:
    case QChar::Mark_Enclosing:
        return UnicodeKeyboard::Marks;
    case QChar::Number_DecimalDigit:
    case QChar::Number_Letter:
    case QChar::Number_Other:
        return UnicodeKeyboard::Numbers;
    case QChar::Punctuation_Connector:
    case QChar::Punctuation_Dash:
    case QChar::Punctuation_Open:
    case QChar::Punctuation_Close:
    case QChar::Punctuation_InitialQuote:
    case QChar::Punctuation_FinalQuote:
    case QChar::Punctuation_Other:
        return UnicodeKeyboard::Punctuation;
    case QChar::Symbol_Math:
    case QChar::Symbol_Currency:
        return UnicodeKeyboard::MathCurrency;
    case QChar::Symbol_Modifier:
    case QChar::Symbol_Other:
        return UnicodeKeyboard::Symbols;
    case QChar::Separator_Space:
    case QChar::Separator_Line:
    case QChar::Separator_Paragraph:
        return UnicodeKeyboard::Spaces;
    case QChar::Other_Format:
        return UnicodeKeyboard::Format;
    case QChar::Other_PrivateUse:
        return UnicodeKeyboard::PrivateUse;
    default:
        return -1;
    }
}

const Catalog &catalog() {
    static const Catalog value = [] {
        Catalog result;
        // The large buckets avoid repeated reallocations on the Cortex-A7.
        result[UnicodeKeyboard::Letters].reserve(140000);
        result[UnicodeKeyboard::PrivateUse].reserve(138000);
        for (char32_t codepoint = 0; codepoint <= 0x10ffff; ++codepoint) {
            if (QChar::isSurrogate(codepoint) || QChar::isNonCharacter(codepoint)) continue;
            const int category = category_for_qt(QChar::category(codepoint));
            if (category >= 0) result[size_t(category)].push_back(codepoint);
        }
        return result;
    }();
    return value;
}
}

QString UnicodeKeyboard::category_name(int category) {
    static const std::array<const char *, CategoryCount> names{{
        "Letters", "Marks", "Numbers", "Punctuation", "Math / currency",
        "Symbols", "Spaces", "Format", "Private use"
    }};
    return category >= 0 && category < CategoryCount ? QString::fromLatin1(names[size_t(category)]) : QString();
}

const std::vector<char32_t> &UnicodeKeyboard::characters(int category) {
    static const std::vector<char32_t> empty;
    return category >= 0 && category < CategoryCount ? catalog()[size_t(category)] : empty;
}

int UnicodeKeyboard::category_for(char32_t codepoint) {
    if (codepoint > 0x10ffff || QChar::isSurrogate(codepoint) || QChar::isNonCharacter(codepoint)) return -1;
    return category_for_qt(QChar::category(codepoint));
}

int UnicodeKeyboard::index_of(int category, char32_t codepoint) {
    const auto &values = characters(category);
    const auto found = std::lower_bound(values.begin(), values.end(), codepoint);
    return found != values.end() && *found == codepoint ? int(found - values.begin()) : -1;
}

QString UnicodeKeyboard::code_label(char32_t codepoint) {
    return QString("U+%1").arg(quint32(codepoint), codepoint > 0xffff ? 6 : 4, 16, QChar('0')).toUpper();
}

QString UnicodeKeyboard::preview(char32_t codepoint) {
    const auto category = QChar::category(codepoint);
    if (category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining || category == QChar::Mark_Enclosing)
        return QString(QChar(0x25cc)) + QString::fromUcs4(&codepoint, 1);
    if (category == QChar::Separator_Space) return QString(QChar(0x2423));
    if (category == QChar::Separator_Line) return QString::fromUtf8("ZL");
    if (category == QChar::Separator_Paragraph) return QString::fromUtf8("ZP");
    if (category == QChar::Other_Format) return QString::fromUtf8("Cf");
    return QString::fromUcs4(&codepoint, 1);
}

QByteArray UnicodeKeyboard::utf8(char32_t codepoint) {
    return selectable(codepoint) ? QString::fromUcs4(&codepoint, 1).toUtf8() : QByteArray();
}

bool UnicodeKeyboard::selectable(char32_t codepoint) {
    return category_for(codepoint) >= 0;
}
}
