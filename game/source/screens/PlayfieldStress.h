// SPDX-License-Identifier: GPL-3.0-or-later
// PlayfieldStress - Milestone M2b: draws 1-4 playfields from the doc 06 §11.3 layout table,
// each with a worst-case chart (16th-note jumps at 1.5x, hold bodies, receptor glows, hit
// explosions, per-player text), and records frame time, CPU time, GPU wait, draw calls,
// texture switches and drawn area for each player count.
#pragma once

#include <gctypes.h>

#include "screens/MenuUi.h"

namespace wii { class Video; class Input; }
namespace render { struct ArrowArt; class Font; }

namespace screens {

class PlayfieldStress {
public:
    PlayfieldStress(wii::Video& video, const wii::Input& input, const render::ArrowArt& art,
                    const render::Font& font);

    // Runs one frame of logic + drawing (does not present). Pads must already be scanned.
    // Returns false to quit.
    bool frame();

private:
    struct Layout {
        float centre_x;   // playfield centre
        float spacing;    // lane spacing in px
        float scale;      // sprite scale (1 = 64 px art)
    };
    struct StepResult {
        u32 frames = 0;
        u64 interval_sum = 0, busy_sum = 0, gpu_sum = 0;
        u32 interval_max = 0, busy_max = 0, gpu_max = 0;
        u32 dropped = 0;
        u64 draws_sum = 0, switches_sum = 0;
        double pixels_sum = 0;
    };
    static constexpr int kSteps = 4;   // 1, 2, 3, 4 players

    void handleInput();
    void updateStats(u64 now_us);
    static Layout layoutFor(int players, int index);
    void drawBackground();
    void drawPlayfield(int player, const Layout& l, float beat);
    void drawOverlay();

    wii::Video& video_;
    const wii::Input& input_;
    const render::ArrowArt& art_;
    const render::Font& font_;

    u64 start_ticks_ = 0;
    u16 held_ = 0;
    ui::ExitChord exit_;

    bool auto_ = true;
    int step_ = 0;   // players - 1
    u64 step_start_us_ = 0;
    u32 step_drop_base_ = 0;
    StepResult results_[kSteps];

    // Scene-only draw counts of the previous frame (the overlay isn't included).
    u32 scene_draws_ = 0, scene_switches_ = 0;
    float scene_pixels_ = 0;
};

}  // namespace screens
