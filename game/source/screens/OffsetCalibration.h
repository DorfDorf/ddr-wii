// SPDX-License-Identifier: GPL-3.0-or-later
// OffsetCalibration - sets the global offset (doc 06 §3.5; M5 step 3). Plays the Click 120
// track (120 BPM, beat 0 at sample 0) in a loop; the player steps on any arrow with each
// click, and core/game/Calibration averages how late the steps land. The arrows flash on
// each beat as a guide, but the player should follow the sound, since the flashes carry the
// TV's video delay as well.
//
// Opens from Options and with R in song select, and goes back there. A or START saves the
// suggested offset (or the current one, if there aren't enough steps yet) to config.ini.
// L/R nudge the current offset by 5 ms, Z clears the steps, Y steps exactly on the beat by
// itself (to check the screen in Dolphin), B (any pad) goes back without saving. No menu
// sounds here: they would land on the clicks.
#pragma once

#include <gctypes.h>

#include "core/game/Calibration.h"
#include "core/timing/SongClock.h"
#include "platform/wii/WiiAudio.h"
#include "screens/Session.h"

namespace wii { class Video; class Input; }
namespace render { struct ArrowArt; class Font; }

namespace screens {

class OffsetCalibration {
public:
    static constexpr const char* kTrack = "sd:/ddrwii/songs/ddr-wii tests/Click 120/click120-44k.ogg";
    static constexpr const char* kConfigPath = "sd:/ddrwii/config.ini";

    OffsetCalibration(wii::Video& video, const wii::Input& input, const render::ArrowArt& art,
                      const render::Font& font, Session& session);

    // One frame of logic + drawing. Returns false to go back.
    bool frame();

private:
    void startTrack();
    void save();

    wii::Video& video_;
    const wii::Input& input_;
    const render::ArrowArt& art_;
    const render::Font& font_;
    Session& session_;

    wii::AudioStream stream_;
    timing::SongClock clock_;
    u32 last_tick_report_ = 0;
    game::Calibration cal_;
    double offset_s_ = 0;        // the global offset being set (saved on START)
    bool auto_ = false;
    long next_auto_beat_ = 0;
    u64 flash_us_[4] = {};       // last press per lane, for the receptor glow
    const char* error_ = nullptr;
};

}  // namespace screens
