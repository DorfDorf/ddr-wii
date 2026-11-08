// SPDX-License-Identifier: GPL-3.0-or-later
// InputFilter - per-port debounce for pad buttons (doc 06 §3.4), and the LaneEvent type.
//
// The rules follow ITGmania's InputFilter idea (re-implemented here, no code copied):
//   - A press or release is reported at once, with the timestamp of the sample that saw it.
//   - After a reported change, further changes on that button are held back for the window.
//   - A change that reverts inside the window (a bounce: release then re-press) is dropped.
//   - A change that is still there when the window ends is reported then, with the
//     timestamp of the sample where it first appeared.
// Portable C++17, no libogc: tested on the PC (game/tests).
#pragma once

#include <cstdint>

namespace input {

// Lanes in StepMania order for dance-single: Left, Down, Up, Right.
enum Lane : uint8_t { kLaneLeft = 0, kLaneDown = 1, kLaneUp = 2, kLaneRight = 3, kNumLanes = 4 };

struct LaneEvent {
    uint64_t time_us;   // when the change was sampled (same clock as the song clock)
    uint8_t port;       // GameCube port 0-3
    uint8_t lane;       // Lane
    bool pressed;       // true = press, false = release
};

class InputFilter {
public:
    static constexpr int kButtons = 16;
    static constexpr uint32_t kDefaultWindowUs = 20000;   // ITGmania's pad default

    struct Change {
        uint8_t button;     // bit index 0-15
        bool pressed;
        uint64_t time_us;
    };

    explicit InputFilter(uint32_t window_us = kDefaultWindowUs) : window_us_(window_us) {}

    // Start from a known state without reporting anything (e.g. panels already held at boot).
    void reset(uint16_t raw);

    // Feed one raw sample. Writes up to kButtons reported changes to `out` (in bit order)
    // and returns how many. Samples must arrive in time order.
    int feed(uint64_t time_us, uint16_t raw, Change* out);

    uint16_t state() const { return state_; }   // debounced button bits
    uint32_t window() const { return window_us_; }
    void setWindow(uint32_t window_us) { window_us_ = window_us; }

private:
    struct Button {
        bool changed_before = false;   // has a change been reported yet?
        bool pending = false;          // raw differs from the reported state
        uint64_t last_change_us = 0;   // timestamp of the last reported change
        uint64_t pending_since_us = 0; // first sample where raw differed
    };

    uint32_t window_us_;
    uint16_t state_ = 0;
    Button buttons_[kButtons];
};

}  // namespace input
