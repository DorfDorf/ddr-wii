// SPDX-License-Identifier: GPL-3.0-or-later
// SongClock - smooth song time from coarse audio-position reports (doc 06 §3.1).
//
// The audio driver reports (cpu time, microseconds of song audio played) whenever it learns
// something, e.g. from the ASND voice callback when a buffer finishes. Those reports are coarse
// (ASND works in 21.3 ms ticks) and jittery, so SongClock keeps the map
//     song_us = cpu_us + offset
// and nudges the offset a little towards each report (a low-pass). now() reads the CPU timer
// through the map, so it has microsecond resolution between reports, never runs backwards,
// and follows the audio over the long run (no drift). A report more than kSnapUs off (start,
// seek, underrun) snaps the offset instead.
// Portable C++17, no libogc: tested on the PC (game/tests).
#pragma once

#include <cstdint>

namespace timing {

class SongClock {
public:
    static constexpr int64_t kSnapUs = 50000;    // report this far off -> jump, don't smooth
    static constexpr int64_t kHoldUs = 200000;   // no report for this long -> clock stops
    static constexpr double kGain = 1.0 / 64;    // share of each report's error applied

    void reset() { *this = SongClock{}; }

    // At CPU time cpu_us, audio_us microseconds of the song had been played.
    void observe(uint64_t cpu_us, int64_t audio_us);

    // Song time at CPU time cpu_us. 0 before the first report. Never smaller than the
    // previous now() result.
    int64_t now(uint64_t cpu_us);

    // The audio has ended: from now on the clock runs on the CPU clock instead of stopping
    // kHoldUs after the last report, so a chart that outlasts its audio still plays out.
    void freeRun() { free_run_ = true; }

    bool started() const { return started_; }
    uint32_t snaps() const { return snaps_; }      // offset jumps after the first report
    double offsetUs() const { return offset_us_; }

private:
    bool started_ = false;
    bool free_run_ = false;
    double offset_us_ = 0;
    uint64_t last_report_cpu_ = 0;
    int64_t last_now_ = 0;
    uint32_t snaps_ = 0;
};

}  // namespace timing
