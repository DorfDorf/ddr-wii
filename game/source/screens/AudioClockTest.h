// SPDX-License-Identifier: GPL-3.0-or-later
// AudioClockTest - Milestone M4b scene: plays a 120 BPM click-track WAV from the SD card
// through ASND, flashes on every beat from SongClock, and shows SongClock against ASND's
// own voice timer (jitter, drift, underruns). Click tracks: tools/make-click-track.py,
// staged by tools/stage-sd.sh.
#pragma once

#include <gctypes.h>

#include "core/timing/SongClock.h"
#include "platform/wii/WiiAudio.h"

namespace wii { class Video; class Input; }
namespace render { class Font; }

namespace screens {

class AudioClockTest {
public:
    AudioClockTest(wii::Video& video, const wii::Input& input, const render::Font& font);

    // Runs one frame of logic + drawing (does not present). Returns false to quit.
    bool frame();

private:
    void start();
    void measure(u64 cpu_us, s64 song_us);
    void draw(s64 song_us);

    wii::Video& video_;
    const wii::Input& input_;
    const render::Font& font_;

    int file_ = 0;   // index into the click-track list (A switches)
    wii::AudioStream stream_;
    timing::SongClock clock_;
    u32 last_report_ = 0;
    u64 start_cpu_us_ = 0;

    // Measurements. "diff" = SongClock - ASND_GetTickCounterVoice(0), in microseconds.
    s64 prev_song_us_ = -1;
    u64 prev_cpu_us_ = 0;
    u32 jitter_max_us_ = 0;      // since settling: |song step - cpu step| per frame
    u32 jitter_sec_us_ = 0;      // over the last full second
    u32 jitter_acc_us_ = 0;
    s64 diff_us_ = 0;
    s64 diff_sum_ = 0;           // this second
    u32 diff_n_ = 0;
    s64 diff_avg_us_ = 0;        // last full second
    s64 diff_ref_us_ = 0;        // average over seconds 5-15: the reference for drift
    s64 diff_ref_sum_ = 0;
    u32 diff_ref_n_ = 0;
    s64 drift_min_us_ = 0, drift_max_us_ = 0;   // drift from the reference, since it was set
    u64 second_start_us_ = 0;
    u32 quit_frames_ = 0;
};

}  // namespace screens
