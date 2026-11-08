// SPDX-License-Identifier: GPL-3.0-or-later
// RenderTest - Milestone M2 scene: background, four receptors, a time-driven arrow
// stream, text, a frame-time overlay and an automatic sprite stress test.
#pragma once

#include <gctypes.h>

namespace wii { class Video; }
namespace render { struct ArrowArt; class Font; }

namespace screens {

class RenderTest {
public:
    RenderTest(wii::Video& video, const render::ArrowArt& art, const render::Font& font);

    // Runs one frame of logic + drawing (does not present). Returns false to quit.
    bool frame();

private:
    struct StepResult {
        u32 frames = 0;
        u64 interval_sum = 0;
        u32 interval_max = 0;
        u64 busy_sum = 0;
        u32 busy_max = 0;
        u32 dropped = 0;
    };
    static constexpr int kSteps = 7;
    static constexpr int kStepSprites[kSteps] = {0, 50, 100, 200, 400, 800, 1600};

    void handleInput();
    void updateStress(u64 now_us);
    void drawBackground();
    void drawStressSprites(float t);
    void drawLanes(float song_beat);
    void drawOverlay();

    wii::Video& video_;
    const render::ArrowArt& art_;
    const render::Font& font_;

    u64 start_ticks_ = 0;
    u16 held_ = 0;
    u32 quit_frames_ = 0;

    bool auto_ = true;
    int step_ = 0;
    u64 step_start_us_ = 0;
    u32 step_drop_base_ = 0;
    StepResult results_[kSteps];
    bool have_result_[kSteps] = {};
};

}  // namespace screens
