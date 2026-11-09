// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/Title.h"

#include <gccore.h>
#include <grrlib.h>

#include <cmath>
#include <cstdio>

#include "platform/wii/MenuSound.h"
#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/ArrowArt.h"
#include "render/Draw.h"
#include "render/Font.h"

namespace screens {
namespace {

constexpr u32 kBg = 0x101828FF, kBox = 0x1E282FFF, kWhite = 0xFFFFFFFF, kGrey = 0x9AA4ABFF;
constexpr u32 kBlack = 0x000000FF, kPink = 0xFF47B3FF;
constexpr u16 kArrows = PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_UP | PAD_BUTTON_DOWN;
constexpr float kLaneDegrees[4] = {-90.0f, 180.0f, 0.0f, 90.0f};   // art points up
constexpr const char* kRows[3] = {"Play", "Options", "Exit"};

// A menu choice: highlighted in pink with a pulsing white edge on the left (the prototype's).
void choiceBox(float x, float y, float w, float h, bool sel, u32 frames) {
    render::rect(x, y, w, h, sel ? kPink : kBox);
    if (sel) {
        const float pulse = 0.6f + 0.4f * std::sin(frames * 0.1f);
        render::rect(x - 4, y, 4, h, 0xFFFFFF00 | (u32)(255 * pulse));
    }
}

}  // namespace

Title::Title(wii::Video& video, const wii::Input& input, const render::ArrowArt& art, const render::Font& font,
             Session& session, int start_row)
    : video_(video), input_(input), art_(art), font_(font), session_(session), row_(start_row) {
    if (!session_.message.empty()) {   // e.g. "Couldn't write sd:/ddrwii/config.ini"
        hint_.note(session_.message);
        session_.message.clear();
    }
}

void Title::handleMenu() {
    for (int port = 0; port < wii::Input::kPorts; port++) {
        const u16 d = input_.down(port);
        if (d & PAD_BUTTON_UP) {
            row_ = (row_ + 2) % 3;
            wii::sfx::move();
        } else if (d & PAD_BUTTON_DOWN) {
            row_ = (row_ + 1) % 3;
            wii::sfx::move();
        } else if (d & PAD_BUTTON_A) {
            wii::sfx::select();
            if (row_ == kPlay) {
                session_.lead_port = port;   // whoever pressed Play leads the Players screen
                choice_ = Choice::Play;
            } else if (row_ == kOptions) {
                choice_ = Choice::Options;
            } else {
                prompt_ = true;
                prompt_yes_ = 0;
            }
            return;
        }
    }
}

void Title::handlePrompt() {
    for (int port = 0; port < wii::Input::kPorts; port++) {
        const u16 d = input_.down(port);
        if (d & kArrows) {
            prompt_yes_ = 1 - prompt_yes_;
            wii::sfx::move();
        } else if (d & PAD_BUTTON_B) {
            prompt_ = false;
            wii::sfx::back();
        } else if (ui::startPressed(input_, port) || (d & PAD_BUTTON_A)) {
            const bool start = !(d & PAD_BUTTON_A);
            if (!prompt_yes_) {
                prompt_ = false;
                wii::sfx::select();
            } else if (start) {
                wii::sfx::select();
                choice_ = Choice::Exit;
            } else {
                hint_.say("Press START to exit Dance Dance Wiivolution");
            }
        } else {
            continue;
        }
        return;
    }
}

bool Title::frame() {
    frames_++;
    if (prompt_)
        handlePrompt();
    else
        handleMenu();
    if (exit_.update(input_)) choice_ = Choice::Exit;
    draw();
    return choice_ == Choice::None;
}

void Title::draw() {
    // The title, then (Exit chosen) the prompt over it, dimmed, as in the prototype.
    render::rect(0, 0, video_.width(), video_.height(), kBg);
    for (int lane = 0; lane < 4; lane++)
        render::img(224 + lane * 64.0f, 104, art_.receptor, kLaneDegrees[lane], 0.8f, 0.8f, 0xA0A0A0FF);
    const int row = prompt_ ? (int)kExit : row_;
    for (int k = 0; k < 3; k++) choiceBox(220, 232 + k * 56, 200, 48, k == row, prompt_ ? 0 : frames_);
    GRRLIB_SetAntiAliasing(false);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%d songs in %d packs", (int)session_.library.SongCount(),
                  (int)session_.library.packs.size());
    font_.print(48, 24, kGrey, 1, "%s", buf);
    // The name on two lines: at three times the font size one line would run past the
    // title-safe area (x 64-576).
    ui::printCentred(font_, 320, 140, kGrey, 2, "DANCE DANCE");
    ui::printCentred(font_, 320, 172, kWhite, 3, "WIIVOLUTION");
    for (int k = 0; k < 3; k++) ui::printCentred(font_, 320, 232 + k * 56 + 8, k == row ? kBlack : kWhite, 2, kRows[k]);
    if (!prompt_) ui::help(font_, "Up/Down choose   A select", "START+Z 2 s exit");
    GRRLIB_SetAntiAliasing(true);

    if (prompt_) {
        render::rect(0, 0, video_.width(), video_.height(), 0x00000099);
        render::rect(160, 164, 320, 152, kBox);
        render::rect(160, 164, 320, 4, kPink);
        choiceBox(196, 256, 112, 40, prompt_yes_ == 1, frames_);
        choiceBox(332, 256, 112, 40, prompt_yes_ == 0, frames_);
        GRRLIB_SetAntiAliasing(false);
        ui::printCentred(font_, 320, 180, kWhite, 2, "Exit the game?");
        ui::printCentred(font_, 320, 220, kGrey, 1, "Back to the Homebrew Channel");
        ui::printCentred(font_, 252, 268, prompt_yes_ == 1 ? kBlack : kWhite, 1, "Yes");
        ui::printCentred(font_, 388, 268, prompt_yes_ == 0 ? kBlack : kWhite, 1, "No");
        ui::help(font_, "Left/Right choose   START on Yes exits   B cancel");
        GRRLIB_SetAntiAliasing(true);
    }
    hint_.draw(font_);
}

}  // namespace screens
