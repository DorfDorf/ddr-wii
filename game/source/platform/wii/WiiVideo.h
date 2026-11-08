// SPDX-License-Identifier: GPL-3.0-or-later
// WiiVideo - GRRLIB video setup, frame submission and frame-time statistics.
#pragma once

#include <gctypes.h>

namespace wii {

struct FrameStats {
    // Last completed frame, microseconds.
    u32 interval_us = 0;     // Render-return to Render-return (includes vsync wait)
    u32 busy_us = 0;         // frame start to Render call (CPU work, before GX flush)
    // Rolling window over the last kWindow frames.
    u32 avg_interval_us = 0;
    u32 max_interval_us = 0;
    u32 avg_busy_us = 0;
    u32 max_busy_us = 0;
    u32 dropped_total = 0;   // frames whose interval exceeded 1.5x the refresh period
    u32 frame_count = 0;
};

class Video {
public:
    static constexpr int kWindow = 60;

    bool init();
    void shutdown();

    // Call at the start of each frame's game logic.
    void beginFrame();
    // Flushes GX and waits for vsync (GRRLIB_Render), then updates stats.
    void endFrame();

    const FrameStats& stats() const { return stats_; }

    // Nominal refresh period for the current TV mode (16683 us at 60 Hz, 20000 at 50 Hz).
    u32 refreshPeriodUs() const { return refresh_us_; }
    // e.g. "NTSC 480i", "PAL60 480i", "PAL50 576i", "NTSC 480p"
    const char* modeName() const { return mode_name_; }
    int width() const;
    int height() const;

    void resetStats();

private:
    FrameStats stats_;
    u32 interval_hist_[kWindow] = {};
    u32 busy_hist_[kWindow] = {};
    int hist_pos_ = 0;
    int hist_len_ = 0;
    u64 frame_start_ = 0;
    u64 last_present_ = 0;
    u32 refresh_us_ = 16683;
    char mode_name_[24] = "?";
};

}  // namespace wii
