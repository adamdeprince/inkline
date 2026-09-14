#ifndef RMT_INPUT_METHOD_HPP
#define RMT_INPUT_METHOD_HPP
#include "rmt/input.hpp"
#include <QByteArray>
#include <QStringList>
#include <memory>
namespace rmt {
class InputMethod {
public:
    enum { Off, Romaji, USInternational, Pinyin, Zhuyin, Wubi, COUNT };
    struct Result { bool consumed = false; QByteArray commit; };
    InputMethod();
    ~InputMethod();
    int method() const;
    static QString name(int method);
    void set_method(int method);
    void reset();
    bool pending() const;
    QString preedit() const;
    QStringList candidates() const;
    int page() const;
    QByteArray candidate(int index);
    Result key(const MappedInput &event);
private:
    class Private;
    std::unique_ptr<Private> d_;
};
}
#endif
