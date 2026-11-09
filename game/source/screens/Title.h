// SPDX-License-Identifier: GPL-3.0-or-later
// Title - the boot scene (doc 06 §7 target menu flow): Play / Options / Exit, laid out as in
// the menu prototype. Up/Down choose, A selects, on any pad. Whoever presses A on Play leads
// the Players screen.
//
// Exit opens a prompt over the title that defaults to No. Left/Right choose; START on Yes
// exits, A on Yes only shows "Press START to exit ddr-wii" (exiting is irreversible, so it
// takes START), A or START on No and B cancel. START+Z held for 2 s exits from anywhere.
#pragma once

#include <gctypes.h>

#include "screens/MenuUi.h"
#include "screens/Session.h"

namespace wii { class Video; class Input; }
namespace render { struct ArrowArt; class Font; }

namespace screens {

class Title {
public:
    enum class Choice { None, Play, Options, Exit };
    enum Row { kPlay, kOptions, kExit };

    // start_row: the row the cursor starts on (where the player came back from).
    Title(wii::Video& video, const wii::Input& input, const render::ArrowArt& art, const render::Font& font,
          Session& session, int start_row);

    // Returns false once something was chosen (choice()) or the exit chord was held (Exit).
    bool frame();
    Choice choice() const { return choice_; }

private:
    void handleMenu();
    void handlePrompt();
    void draw();

    wii::Video& video_;
    const wii::Input& input_;
    const render::ArrowArt& art_;
    const render::Font& font_;
    Session& session_;

    int row_ = kPlay;
    bool prompt_ = false;
    int prompt_yes_ = 0;   // 1: Yes highlighted (the prompt opens on No)
    Choice choice_ = Choice::None;
    ui::Hint hint_;
    ui::ExitChord exit_;
    u32 frames_ = 0;
};

}  // namespace screens
