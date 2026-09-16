#include "rmt/power_key.hpp"
#undef NDEBUG // These checks must also run in the release build used on tablets.
#include <cassert>
#include <cstdio>

int main() {
    rmt::PowerKey key;
    assert(!key.event(0, true)); // Already held when monitoring started.
    assert(!key.event(1, true));
    assert(!key.event(2, true)); // Kernel autorepeat.
    assert(key.event(0, true));
    assert(!key.event(0, true));
    assert(!key.event(1, true));
    key.reset(); // SYN_DROPPED: the matching press cannot be trusted.
    assert(!key.event(0, true));

    rmt::ResumeGuard guard;
    assert(!guard.observe(10000, 10000));
    assert(!guard.observe(11001, 11000)); // Small clock sampling skew.
    guard.block(11000);
    assert(guard.blocked(12000));
    assert(!guard.blocked(13000));
    assert(guard.observe(71000, 11010)); // One minute suspended.
    assert(guard.blocked(11010));
    key.reset();
    assert(!key.event(1, !guard.blocked(11010))); // Wake press.
    assert(!key.event(2, !guard.blocked(13020))); // Held through debounce.
    assert(!key.event(0, !guard.blocked(13020))); // Wake release stays ignored.
    assert(!guard.observe(73010, 13020));
    assert(!key.event(1, !guard.blocked(13020)));
    assert(key.event(0, !guard.blocked(13100))); // A fresh deliberate press.
    assert(guard.observe(74010, 13030)); // Another resume rearms the guard.
    std::puts("Power release, autorepeat, dropped events and wake suppression passed.");
}
