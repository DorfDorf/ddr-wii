// SPDX-License-Identifier: GPL-3.0-or-later
// PlayModeSelect - choose Casual or Competitive before song select (M5 step 4). Modelled on
// Simply Love's ScreenSelectPlayMode (choices on the left, what the mode means on the right),
// redrawn with our own placeholder graphics.
//
//   Casual       no life bar, nobody fails
//   Competitive  ITG's life bar (core/game/LifeMeter); empty it and you fail the song
//
// Up/Down (or Left/Right) pick, START confirms; the choice is saved to config.ini. B on
// port 1 goes on to the test scenes, START+Z for 2 s exits.
#pragma once

#include <gctypes.h>

#include "screens/Session.h"

namespace wii { class Video; class Input; }
namespace render { class Font; }

namespace screens {

class PlayModeSelect {
public:
    PlayModeSelect(wii::Video& video, const wii::Input& input, const render::Font& font, Session& session);

    // Returns false when a mode was chosen (chosen() is true) or the exit chord was held.
    bool frame();
    bool chosen() const { return chosen_; }

private:
    wii::Video& video_;
    const wii::Input& input_;
    const render::Font& font_;
    Session& session_;
    int cursor_ = 0;
    u32 frames_ = 0;
    u32 quit_frames_ = 0;
    bool chosen_ = false;
};

}  // namespace screens
