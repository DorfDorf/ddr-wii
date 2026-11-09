// SPDX-License-Identifier: GPL-3.0-or-later
#include "SongClock.h"

namespace timing {

void SongClock::observe(uint64_t cpu_us, int64_t audio_us) {
    if (paused_) return;
    const double target = (double)audio_us - (double)cpu_us;
    if (resync_) {   // first report after a resume: take it as the map, keep now() monotonic
        if (cpu_us < resume_cpu_) return;   // mixed before the resume
        offset_us_ = target;
        last_report_cpu_ = cpu_us;
        resync_ = false;
        return;
    }
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
    if (paused_ || (resync_ && !free_run_)) return last_now_;
    // Stop at kHoldUs past the last report: if the audio stalls, so does the song.
    uint64_t t = cpu_us;
    if (!free_run_ && t > last_report_cpu_ + kHoldUs) t = last_report_cpu_ + kHoldUs;
    const int64_t v = (int64_t)((double)t + offset_us_);
    if (v > last_now_) last_now_ = v;
    return last_now_;
}

void SongClock::pause(uint64_t cpu_us) {
    if (paused_) return;
    now(cpu_us);   // last_now_ is the frozen value
    paused_ = true;
}

void SongClock::resume(uint64_t cpu_us) {
    if (!paused_) return;
    paused_ = false;
    if (!started_) return;   // paused before the first report: it will start the clock
    // Until the next report (or for good when free-running) the map continues from the
    // frozen value; lane events in that gap convert through it.
    offset_us_ = (double)last_now_ - (double)cpu_us;
    last_report_cpu_ = cpu_us;
    resume_cpu_ = cpu_us;
    resync_ = !free_run_;
}

}  // namespace timing
