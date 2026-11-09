// SPDX-License-Identifier: GPL-3.0-or-later
// Players - choose 1 or 2 players before song select (doc 06 §7 target menu flow, §11.2).
// Replaces the play-mode screen; laid out like the menu prototype, which follows Simply Love's
// ScreenSelectStyle, redrawn with our own placeholder graphics.
//
// The lead port (the first port to press an arrow, A or Y here, until the title screen exists
// and does it) moves between 1 Player and 2 Players with Left/Right and confirms with A.
// 2 Players is greyed out when no second pad is connected. P2 joins by pressing A on their
// own pad, before or after the lead's A; Y (controller) adds an autoplay P2 for testing.
// Once set, the lower port is P1 and plays on the left. Nobody joins anywhere else.
// B goes back (to the test scenes, until the title exists); START+Z for 2 s exits.
#pragma once

#include <gctypes.h>

#include <string>

#include "screens/Session.h"

namespace wii { class Video; class Input; }
namespace render { class Font; }

namespace screens {

class Players {
public:
    Players(wii::Video& video, const wii::Input& input, const render::Font& font, Session& session);

    // Returns false when the players are set (chosen()), B went back (back()) or the exit
    // chord was held.
    bool frame();
    bool chosen() const { return chosen_; }
    bool back() const { return back_; }

private:
    void handleInput();
    void finish();
    bool secondPad() const;              // a connected port other than the lead's
    SessionPlayer makePlayer(int port) const;
    void say(const char* text);
    void draw();

    wii::Video& video_;
    const wii::Input& input_;
    const render::Font& font_;
    Session& session_;

    int lead_ = -1;                      // -1: nobody has pressed anything yet
    int choice_ = 0;                     // 0: 1 Player, 1: 2 Players
    bool waiting_ = false;               // the lead pressed A on 2 Players before P2 joined
    bool has_p2_ = false;
    SessionPlayer p2_;
    std::string message_;
    u64 message_us_ = 0;
    u32 frames_ = 0;
    u32 quit_frames_ = 0;
    bool chosen_ = false;
    bool back_ = false;
};

}  // namespace screens
