#ifndef RMT_SHORTCUT_CHORD_HPP
#define RMT_SHORTCUT_CHORD_HPP
#include <array>
namespace rmt {
// Linux input ABI codes. On the Type Folio only, its separate Opt key is
// reported as KEY_END (107). USB keyboards use their Super/Windows/Command
// keys (125/126); an ordinary USB End key must never act as a modifier.
class ShortcutChord {
public:
    explicit ShortcutChord(bool folio = false) : folio_(folio) {}
    void update(unsigned code, int value) { if (code < held_.size()) held_[code] = value != 0; }
    void reset() { held_.fill(false); }
    bool active() const {
        return (held_[29] || held_[97]) && (held_[56] || held_[100]) &&
               (folio_ ? held_[107] : held_[125] || held_[126]);
    }
    bool recovery_held() const { return active() && held_[14]; }
private:
    bool folio_;
    std::array<bool, 128> held_{};
};
}
#endif
