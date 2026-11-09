// SPDX-License-Identifier: GPL-3.0-or-later
// WiiInput - GameCube pad input with per-sample timestamps (doc 04 §5, §8; doc 06 §3.4).
//
// The SI polling interrupt reads all four ports (PAD_Read) every time the hardware polls
// them and queues {gettime(), buttons} in a lock-free ring. update() drains it once per
// frame, debounces each port (core/input/InputFilter) and produces timestamped LaneEvents,
// plus held/down button masks for menus.
//
// Use this instead of PAD_ScanPads/PAD_ButtonsHeld/PAD_ButtonsDown: SI hands each new sample
// to one reader only, so both can't be used at once.
//
// Note: libogc 3.2.1's PAD_SetSamplingCallback never calls its callback (doc 04 §5), so the
// handler is registered with SI_RegisterPollingHandler directly. If no samples arrive at all,
// update() falls back to one PAD_Read per frame (frame-time timestamps).
#pragma once

#include <gctypes.h>

#include "core/input/InputFilter.h"

namespace wii {

class Input {
public:
    static constexpr int kPorts = 4;
    static constexpr int kMaxEvents = 64;   // lane events per update() (way above 4 panels x 4 ports)

    struct Stats {
        u32 samples_total = 0;
        u32 samples_per_sec = 0;   // over the last full second
        u32 interval_min_us = 0;   // between samples, over the last full second
        u32 interval_max_us = 0;
        u32 overflows = 0;         // samples lost because the ring was full
        bool fallback = false;     // no interrupt samples: reading once per frame
        u32 last_drain = 0;        // samples handled by the last update()
    };

    bool init(u32 debounce_us = input::InputFilter::kDefaultWindowUs);
    void shutdown();
    // The Options screen's "Ignore double presses within N ms": applies from the next update().
    void setDebounceUs(u32 us) {
        for (auto& f : filters_) f.setWindow(us);
    }

    // Call once per frame, before anything reads input.
    void update();

    u16 held(int port) const { return held_[port]; }   // debounced button bits (PAD_BUTTON_*)
    u16 down(int port) const { return down_[port]; }   // pressed since the previous update()
    u32 connected() const { return connected_; }       // bit per port that returned data

    // Lane events from the last update(), in sample order.
    int eventCount() const { return event_count_; }
    const input::LaneEvent& event(int i) const { return events_[i]; }

    const Stats& stats() const { return stats_; }

    // Current time on the same clock as LaneEvent::time_us.
    static u64 nowUs();

private:
    void handleSample(u64 tick, const u16* buttons, u8 ok_mask, u8 missing_mask);

    input::InputFilter filters_[kPorts];
    bool primed_[kPorts] = {};
    u16 held_[kPorts] = {};
    u16 down_[kPorts] = {};
    u32 connected_ = 0;
    input::LaneEvent events_[kMaxEvents];
    int event_count_ = 0;
    Stats stats_;
    u32 updates_ = 0;
    u64 window_start_tick_ = 0;
    u32 window_samples_ = 0;
    u32 window_min_us_ = 0xFFFFFFFF, window_max_us_ = 0;
    u64 last_sample_tick_ = 0;
};

}  // namespace wii
