// SPDX-License-Identifier: GPL-3.0-or-later
// MenuRepeat - auto-repeat for a held menu button (doc 06 §7, target menu flow).
//
// Fires once on the press, then after 0.4 s every 75 ms, and every 35 ms once the button has
// been held for 2 s (the same schedule as the menu prototype). At most one fire per update(),
// so a slow frame doesn't burst. Original to ddr-wii. Portable C++17: tested on the PC.
#pragma once

#include <cstdint>

namespace input {

class MenuRepeat {
public:
    static constexpr uint64_t kDelayUs = 400000;      // first repeat
    static constexpr uint64_t kSlowUs = 75000;        // then every 75 ms
    static constexpr uint64_t kFastAfterUs = 2000000; // held this long: every 35 ms
    static constexpr uint64_t kFastUs = 35000;

    // pressed: a new press this frame (Input::down); held: still down (Input::held).
    // Returns true when the action should happen this frame.
    bool update(bool pressed, bool held, uint64_t now_us) {
        if (pressed || (held && !active_)) {
            active_ = true;
            since_us_ = now_us;
            next_us_ = now_us + kDelayUs;
            return true;
        }
        if (!held) {
            active_ = false;
            return false;
        }
        if (now_us < next_us_) return false;
        const uint64_t step = now_us - since_us_ >= kFastAfterUs ? kFastUs : kSlowUs;
        next_us_ += step;
        if (next_us_ <= now_us) next_us_ = now_us + step;   // fell behind: no burst
        return true;
    }
    void reset() { active_ = false; }

private:
    bool active_ = false;
    uint64_t since_us_ = 0;
    uint64_t next_us_ = 0;
};

}  // namespace input
