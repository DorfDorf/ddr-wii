// SPDX-License-Identifier: GPL-3.0-or-later
#include "InputFilter.h"

namespace input {

void InputFilter::reset(uint16_t raw) {
    state_ = raw;
    for (Button& b : buttons_) b = Button{};
}

int InputFilter::feed(uint64_t time_us, uint16_t raw, Change* out) {
    int n = 0;
    for (int i = 0; i < kButtons; i++) {
        Button& b = buttons_[i];
        const uint16_t bit = (uint16_t)(1u << i);
        const bool want = raw & bit;
        const bool have = state_ & bit;

        if (want == have) {
            b.pending = false;   // nothing new, or a bounce that came back inside the window
            continue;
        }
        if (!b.pending) {
            b.pending = true;
            b.pending_since_us = time_us;
        }
        const bool window_over = !b.changed_before || time_us - b.last_change_us >= window_us_;
        if (!window_over) continue;   // hold it back; dropped if it reverts before the window ends

        // Report with the time it was first seen. If it appeared inside the window, that is
        // still the best estimate of when the foot moved.
        const uint64_t t = b.pending_since_us;
        state_ = (uint16_t)(want ? (state_ | bit) : (state_ & ~bit));
        b.pending = false;
        b.changed_before = true;
        b.last_change_us = t;
        out[n++] = Change{(uint8_t)i, want, t};
    }
    return n;
}

}  // namespace input
