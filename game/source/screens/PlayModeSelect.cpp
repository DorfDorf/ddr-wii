// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/PlayModeSelect.h"

#include <gccore.h>
#include <grrlib.h>

#include <cmath>
#include <cstring>

#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/Draw.h"
#include "render/Font.h"

namespace screens {
namespace {

struct Choice {
    ddr::PlayMode mode;
    const char* name;
    const char* lines[6];
};

const Choice kChoices[] = {
    {ddr::PlayMode::Casual, "Casual",
     {"Just play.", "", "No life bar, and nobody", "fails a song.", "", ""}},
    {ddr::PlayMode::Competitive, "Competitive",
     {"ITG rules.", "", "A life bar that fills with good", "steps and drains with misses,",
      "dropped holds and mines.", "Empty it and you fail the song."}},
};
constexpr int kCount = sizeof kChoices / sizeof kChoices[0];

// Simply Love colours (default colour index 3, as in SongSelect).
constexpr u32 kChoiceColour[kCount] = {0xFF7D00FF, 0xFF47B3FF};
constexpr u32 kBg = 0x101828FF, kBox = 0x1E282FFF, kWhite = 0xFFFFFFFF, kGrey = 0x9AA4ABFF;

}  // namespace

PlayModeSelect::PlayModeSelect(wii::Video& video, const wii::Input& input, const render::Font& font,
                               Session& session)
    : video_(video), input_(input), font_(font), session_(session) {
    for (int i = 0; i < kCount; i++)
        if (kChoices[i].mode == session_.config.playMode) cursor_ = i;
}

bool PlayModeSelect::frame() {
    frames_++;
    for (int port = 0; port < wii::Input::kPorts; port++) {
        const u16 d = input_.down(port), h = input_.held(port);
        if (d & (PAD_BUTTON_UP | PAD_BUTTON_LEFT)) cursor_ = (cursor_ + kCount - 1) % kCount;
        if (d & (PAD_BUTTON_DOWN | PAD_BUTTON_RIGHT)) cursor_ = (cursor_ + 1) % kCount;
        if ((d & (PAD_BUTTON_START | PAD_BUTTON_A)) && !(h & PAD_TRIGGER_Z)) chosen_ = true;
    }
    if (chosen_ && session_.config.playMode != kChoices[cursor_].mode) {
        session_.config.playMode = kChoices[cursor_].mode;
        session_.config_dirty = true;
    }

    render::rect(0, 0, video_.width(), video_.height(), kBg);
    render::rect(0, 0, video_.width(), 46, 0x000000A0);
    // Choices on the left, the cursor's description on the right.
    const float pulse = 0.5f + 0.5f * std::sin(frames_ * 0.1f);
    for (int i = 0; i < kCount; i++) {
        const float y = 120 + i * 72;
        const bool on = i == cursor_;
        render::rect(48, y, 200, 56, on ? kChoiceColour[i] : kBox);
        if (on) render::rect(44, y, 4, 56, 0xFFFFFF00 | (u32)(128 + 127 * pulse));
    }
    render::rect(272, 100, 320, 220, kBox);
    render::rect(272, 100, 320, 4, kChoiceColour[cursor_]);

    GRRLIB_SetAntiAliasing(false);
    font_.print(48, 24, kWhite, 1, "SELECT MODE");
    for (int i = 0; i < kCount; i++) {
        const float y = 120 + i * 72;
        const char* name = kChoices[i].name;
        font_.print(148 - std::strlen(name) * 8, y + 12, i == cursor_ ? 0x000000FF : kWhite, 2, "%s", name);
    }
    const Choice& c = kChoices[cursor_];
    font_.print(288, 120, kChoiceColour[cursor_], 2, "%s", c.name);
    for (int l = 0; l < 6; l++) font_.print(288, 164 + l * 20, kWhite, 1, "%s", c.lines[l]);
    font_.print(288, 292, kGrey, 1, "Timing and scoring are the same.");
    font_.print(48, 400, kGrey, 1, "Up/Down choose   START confirm");
    font_.print(48, 416, kGrey, 1, "B tests   START+Z 2 s exit");
    GRRLIB_SetAntiAliasing(true);

    const u16 held = input_.held(0);
    quit_frames_ = ((held & PAD_BUTTON_START) && (held & PAD_TRIGGER_Z)) ? quit_frames_ + 1 : 0;
    return !chosen_ && quit_frames_ < 120;
}

}  // namespace screens
