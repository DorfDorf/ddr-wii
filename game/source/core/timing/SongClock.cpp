// SPDX-License-Identifier: GPL-3.0-or-later
#include "SongClock.h"

namespace timing {

void SongClock::observe(uint64_t cpu_us, int64_t audio_us) {
    const double target = (double)audio_us - (double)cpu_us;
    const double err = target - offset_us_;
    if (!started_ || err > kSnapUs || err < -kSnapUs) {
        if (started_) snaps_++;
        offset_us_ = target;
        if (!started_ || audio_us < last_now_) last_now_ = audio_us;   // allow a jump back (seek, restart)
        started_ = true;
    } else {
        offset_us_ += err * kGain;
    }
    last_report_cpu_ = cpu_us;
}

int64_t SongClock::now(uint64_t cpu_us) {
    if (!started_) return 0;
    // Stop at kHoldUs past the last report: if the audio stalls, so does the song.
    uint64_t t = cpu_us;
    if (!free_run_ && t > last_report_cpu_ + kHoldUs) t = last_report_cpu_ + kHoldUs;
    const int64_t v = (int64_t)((double)t + offset_us_);
    if (v > last_now_) last_now_ = v;
    return last_now_;
}

}  // namespace timing
