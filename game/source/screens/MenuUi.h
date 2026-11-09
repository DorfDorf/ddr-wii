// SPDX-License-Identifier: GPL-3.0-or-later
// MenuUi - pieces every menu screen shares (doc 06 §7, target menu flow), placed as in the
// menu prototype:
//   - the help strip: each screen's and popup's controls, two grey lines at x 48, y 400 / 416;
//   - the hint: a one-line note in a box at the bottom (y 440) for 1.6 s, with the error sound;
//   - the exit chord: START+Z held on any pad for 2 s leaves the game from anywhere.
// Menu sounds (platform/wii/MenuSound) play only when a press did something.
#pragma once

#include <gctypes.h>

#include <string>

namespace wii { class Input; }
namespace render { class Font; }

namespace screens {
namespace ui {

constexpr u32 kHelpGrey = 0x9AA4ABFF;

// Draws the help strip; line2 may be null. Call with antialiasing off (the bitmap font).
void help(const render::Font& font, const char* line1, const char* line2 = nullptr);

// Text centred on x.
void printCentred(const render::Font& font, float cx, float y, u32 colour, float scale, const char* s);

class Hint {
public:
    static constexpr u64 kShowUs = 1600000;
    void say(const std::string& text);   // shows it and plays the error sound
    void note(const std::string& text);  // shows it without a sound (a saved setting, say)
    void draw(const render::Font& font) const;   // antialiasing is restored on return

private:
    std::string text_;
    u64 since_us_ = 0;
};

// START+Z held together on one pad for 2 s (120 frames at 60 Hz).
class ExitChord {
public:
    bool update(const wii::Input& input);   // true once held long enough
private:
    u32 frames_ = 0;
};

// START+Z is the exit chord, so a START or Z press with the other held isn't a press.
bool startPressed(const wii::Input& input, int port);
bool zPressed(const wii::Input& input, int port);

}  // namespace ui
}  // namespace screens
