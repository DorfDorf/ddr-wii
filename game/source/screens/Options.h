// SPDX-License-Identifier: GPL-3.0-or-later
// Options - the settings, from the title (doc 06 §7 target menu flow), laid out as in the menu
// prototype: one list under three headings, so nothing is more than a level deeper.
//   PLAY      Calibrate offset: Left/Right nudge GlobalOffset by 1 ms, A opens the calibration
//   PADS      Ignore double presses within N ms (InputDebounceMs, applies at once)
//             Port offsets and test input: the Pads page
//   ADVANCED  Default pack sync (None / ITG, for packs whose Pack.ini doesn't set one; applies
//             at once), Test scenes (the audio clock, playfield stress and render tests)
// The Pads page has each port's InputOffset (Left/Right, 1 ms) and Test input, which lights up
// every button each pad holds; hold B for 1 s there to leave (B is one of the buttons shown).
// Any pad drives the screen. B saves config.ini (main.cpp writes it if something changed) and
// goes back to the title. Settings that do nothing yet (VisualOffset, a TV safe area) aren't
// shown.
#pragma once

#include <gctypes.h>

#include "core/input/MenuRepeat.h"
#include "screens/MenuUi.h"
#include "screens/Session.h"

namespace wii { class Video; class Input; }
namespace render { class Font; }

namespace screens {

class Options {
public:
    enum class Next { None, Back, Calibrate, Tests };

    // Comes back to the row (and page) in session.options_row / options_pads.
    Options(wii::Video& video, wii::Input& input, const render::Font& font, Session& session);

    // Returns false when it's left (next()) or the exit chord was held (Next::None).
    bool frame();
    Next next() const { return next_; }

private:
    enum Page { kList, kPads, kTestInput };

    void handleList(u16 down, int dir);
    void handlePads(u16 down, int dir);
    void handleTestInput();
    void draw();
    void drawList();
    void drawPads();
    void drawTestInput();

    wii::Video& video_;
    wii::Input& input_;
    const render::Font& font_;
    Session& session_;

    Page page_ = kList;
    int row_ = 1;              // into the list's rows (headings are skipped)
    int pads_row_ = 0;         // 0-3 the ports' offsets, 4 Test input
    input::MenuRepeat up_, down_, left_, right_;
    u64 b_held_since_us_ = 0;  // Test input: B held on some pad since
    Next next_ = Next::None;
    bool quit_ = false;
    ui::Hint hint_;
    ui::ExitChord exit_;
    u32 frames_ = 0;
};

}  // namespace screens
