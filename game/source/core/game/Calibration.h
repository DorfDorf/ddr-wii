// SPDX-License-Identifier: GPL-3.0-or-later
// Calibration - measures how late a player steps against a steady beat, to set the global
// offset (doc 06 §3.5; M5 step 3). The idea is StepMania's machine sync: step along with a
// click track, average the error. Re-implemented.
//
// Times are song seconds (audio time, no global offset). Each press is matched to the
// nearest beat of the grid first_beat + k * period. Presses more than kMaxErrorS from a beat
// (a missed or extra step) and presses within kJumpS of the last counted one (the other
// foot of a jump) are ignored. Mean and spread are over the last kWindow presses.
// Portable C++17, no libogc: tested on the PC (game/tests).
#pragma once

#include <cstddef>
#include <vector>

namespace game {

class Calibration {
public:
    static constexpr double kMaxErrorS = 0.150;
    static constexpr double kJumpS = 0.060;
    static constexpr size_t kWindow = 32;
    static constexpr size_t kReady = 24;    // presses before a suggestion is offered

    explicit Calibration(double period_s = 0.5, double first_beat_s = 0)
        : period_(period_s), first_(first_beat_s) {}

    void reset() { errors_.clear(); last_press_ = -1e9; }
    // The track restarted from 0: keep the errors, forget the last press time.
    void restart() { last_press_ = -1e9; }
    // Returns true if the press was counted.
    bool press(double song_s);

    size_t count() const { return errors_.size() < kWindow ? errors_.size() : kWindow; }
    bool ready() const { return count() >= kReady; }
    double lastError() const { return errors_.empty() ? 0 : errors_.back(); }
    double mean() const;      // seconds; positive = steps land after the beat
    double stdDev() const;
    // The global offset that would put these steps on the beat (chart time = song time +
    // offset, so a step that is `mean` late needs the offset lowered by `mean`).
    double suggestedOffset() const { return 0.0 - mean(); }   // 0 - x: no "-0.0"

private:
    double period_, first_;
    double last_press_ = -1e9;
    std::vector<double> errors_;
};

}  // namespace game
