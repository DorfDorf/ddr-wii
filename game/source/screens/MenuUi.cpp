// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/MenuUi.h"

#include <gccore.h>
#include <grrlib.h>

#include <cstring>

#include "platform/wii/MenuSound.h"
#include "platform/wii/WiiInput.h"
#include "render/Draw.h"
#include "render/Font.h"

namespace screens {
namespace ui {

void help(const render::Font& font, const char* line1, const char* line2) {
    if (line1 && *line1) font.print(48, 400, kHelpGrey, 1, "%s", line1);
    if (line2 && *line2) font.print(48, 416, kHelpGrey, 1, "%s", line2);
}

void printCentred(const render::Font& font, float cx, float y, u32 colour, float scale, const char* s) {
    font.print(cx - std::strlen(s) * render::Font::kGlyphW * scale / 2, y, colour, scale, "%s", s);
}

void Hint::say(const std::string& text) {
    note(text);
    wii::sfx::error();
}

void Hint::note(const std::string& text) {
    text_ = text;
    since_us_ = wii::Input::nowUs();
}

void Hint::draw(const render::Font& font) const {
    if (text_.empty() || wii::Input::nowUs() - since_us_ >= kShowUs) return;
    GRRLIB_SetAntiAliasing(true);
    render::rect(112, 440, 416, 28, 0x000000E0);
    render::rect(112, 440, 4, 28, 0xFFBE00FF);
    GRRLIB_SetAntiAliasing(false);
    printCentred(font, 320, 446, 0xFFFFFFFF, 1, text_.c_str());
    GRRLIB_SetAntiAliasing(true);
}

bool ExitChord::update(const wii::Input& input) {
    bool any = false;
    for (int port = 0; port < wii::Input::kPorts; port++) {
        const u16 h = input.held(port);
        any = any || ((h & PAD_BUTTON_START) && (h & PAD_TRIGGER_Z));
    }
    frames_ = any ? frames_ + 1 : 0;
    return frames_ >= 120;
}

bool startPressed(const wii::Input& input, int port) {
    return (input.down(port) & PAD_BUTTON_START) && !(input.held(port) & PAD_TRIGGER_Z);
}

bool zPressed(const wii::Input& input, int port) {
    return (input.down(port) & PAD_TRIGGER_Z) && !(input.held(port) & PAD_BUTTON_START);
}

}  // namespace ui
}  // namespace screens
