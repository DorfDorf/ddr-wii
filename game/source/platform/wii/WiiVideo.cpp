// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/wii/WiiVideo.h"

#include <grrlib.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>

namespace wii {

bool Video::init() {
    if (GRRLIB_Init() != 0) return false;

    // Describe the mode GRRLIB picked (VIDEO_GetPreferredMode, from the Wii's settings).
    const u32 tv = VIDEO_GetCurrentTvMode();
    const u32 scan = rmode->viTVMode & 3;   // VI_INTERLACE / VI_NON_INTERLACE / VI_PROGRESSIVE
    const char* std = "NTSC";
    refresh_us_ = 16683;
    switch (tv) {
        case VI_PAL:     std = "PAL50"; refresh_us_ = 20000; break;
        case VI_EURGB60: std = "PAL60"; break;
        case VI_MPAL:    std = "MPAL";  break;
        default: break;
    }
    snprintf(mode_name_, sizeof(mode_name_), "%s %u%c", std, (unsigned)rmode->xfbHeight,
             scan == VI_PROGRESSIVE ? 'p' : 'i');

    resetStats();
    return true;
}

void Video::shutdown() { GRRLIB_Exit(); }

int Video::width() const { return rmode->fbWidth; }
int Video::height() const { return rmode->efbHeight; }

void Video::resetStats() {
    stats_ = FrameStats{};
    hist_pos_ = hist_len_ = 0;
    last_present_ = 0;
}

void Video::beginFrame() { frame_start_ = gettime(); }

void Video::endFrame() {
    const u64 submit = gettime();
    GRRLIB_Render();   // GX_DrawDone + copy to XFB + VIDEO_WaitVSync
    const u64 now = gettime();

    stats_.busy_us = (u32)diff_usec(frame_start_, submit);
    if (last_present_ != 0) {
        stats_.interval_us = (u32)diff_usec(last_present_, now);
        if (stats_.interval_us > refresh_us_ + refresh_us_ / 2) stats_.dropped_total++;

        interval_hist_[hist_pos_] = stats_.interval_us;
        busy_hist_[hist_pos_] = stats_.busy_us;
        hist_pos_ = (hist_pos_ + 1) % kWindow;
        if (hist_len_ < kWindow) hist_len_++;

        u64 isum = 0, bsum = 0;
        u32 imax = 0, bmax = 0;
        for (int i = 0; i < hist_len_; i++) {
            isum += interval_hist_[i];
            bsum += busy_hist_[i];
            if (interval_hist_[i] > imax) imax = interval_hist_[i];
            if (busy_hist_[i] > bmax) bmax = busy_hist_[i];
        }
        stats_.avg_interval_us = (u32)(isum / hist_len_);
        stats_.avg_busy_us = (u32)(bsum / hist_len_);
        stats_.max_interval_us = imax;
        stats_.max_busy_us = bmax;
    }
    last_present_ = now;
    stats_.frame_count++;
}

}  // namespace wii
