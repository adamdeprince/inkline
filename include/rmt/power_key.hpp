#ifndef RMT_POWER_KEY_HPP
#define RMT_POWER_KEY_HPP

#include <cstdint>
#include <utility>

namespace rmt {

// Act on release, after a press we actually observed. A held key at startup,
// a lost evdev sequence, and autorepeat must never request another suspend.
class PowerKey {
public:
    bool event(int value, bool enabled) {
        if (!enabled) { reset(); return false; }
        if (value == 1) down_ = true;
        return value == 0 && std::exchange(down_, false);
    }
    void reset() { down_ = false; }
private:
    bool down_ = false;
};

// CLOCK_BOOTTIME advances during suspend; CLOCK_MONOTONIC does not. Detect
// resume before reading queued input so the wake button cannot suspend again.
// Caller-supplied clocks keep the behavior testable without sleeping hardware.
class ResumeGuard {
public:
    bool observe(std::int64_t boot_ms, std::int64_t monotonic_ms) {
        const auto offset = boot_ms - monotonic_ms;
        const bool resumed = initialized_ && offset - offset_ >= 100;
        initialized_ = true;
        offset_ = offset;
        if (resumed) block(monotonic_ms);
        return resumed;
    }
    void block(std::int64_t monotonic_ms) { until_ = monotonic_ms + 2000; }
    bool blocked(std::int64_t monotonic_ms) const { return monotonic_ms < until_; }
private:
    bool initialized_ = false;
    std::int64_t offset_ = 0, until_ = 0;
};

}
#endif
