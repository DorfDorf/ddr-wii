// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/Players.h"

#include <gccore.h>
#include <grrlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "platform/wii/MenuSound.h"
#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/Draw.h"
#include "render/Font.h"
#include "screens/MenuUi.h"

namespace screens {
namespace {

// Simply Love colours, as in SongSelect (P1 pink, P2 red-orange).
constexpr u32 kBg = 0x101828FF, kBox = 0x1E282FFF, kWhite = 0xFFFFFFFF, kGrey = 0x9AA4ABFF;
constexpr u32 kBlack = 0x000000FF, kGone = 0x141C22FF, kGoneText = 0x4C565DFF;
constexpr u32 kPlayerColour[2] = {0xFF47B3FF, 0xFF5D47FF};
constexpr u16 kArrows = PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_UP | PAD_BUTTON_DOWN;
constexpr u64 kMessageUs = 2000000;
constexpr float kGlyph = render::Font::kGlyphW;

void printCentred(const render::Font& font, float cx, float y, u32 colour, float scale, const char* s) {
    font.print(cx - std::strlen(s) * kGlyph * scale / 2, y, colour, scale, "%s", s);
}

// A 3x3 pad, the four arrow panels lit (the menu prototype's icon).
void padIcon(float x, float y, float size, u32 lit, u32 unlit) {
    const float g = 3, c = (size - 2 * g) / 3;
    for (int r = 0; r < 3; r++)
        for (int q = 0; q < 3; q++)
            render::rect(x + q * (c + g), y + r * (c + g), c, c, (r * 3 + q) % 2 == 1 ? lit : unlit);
}

}  // namespace

Players::Players(wii::Video& video, const wii::Input& input, const render::Font& font, Session& session,
                 bool from_title)
    : video_(video), input_(input), font_(font), session_(session) {
    // The lead is whoever pressed Play on the title. Back from song select: the same lead, on
    // the same choice; P2 joins again.
    lead_ = session_.lead_port;
    choice_ = !from_title && session_.players.size() > 1 ? 1 : 0;
}

SessionPlayer Players::makePlayer(int port) const {
    SessionPlayer p;
    p.port = port;
    p.speed = session_.config.ports[port].scrollSpeed;
    p.life_bar = session_.config.ports[port].lifeBar;
    // Keep a returning player's difficulty (and autoplay) from the last session.
    for (const SessionPlayer& old : session_.players)
        if (old.port == port && !old.bot) {
            p.preferred = old.preferred;
            p.autoplay = old.autoplay;
        }
    return p;
}

bool Players::secondPad() const {
    const u32 c = input_.connected();
    int n = 0;
    for (int port = 0; port < wii::Input::kPorts; port++)
        if ((c & (1u << port)) && port != lead_) n++;
    return lead_ >= 0 ? n >= 1 : n >= 2;
}

void Players::say(const char* text) {
    message_ = text;
    message_us_ = wii::Input::nowUs();
    wii::sfx::error();
}

void Players::finish() {
    std::vector<SessionPlayer> list{makePlayer(lead_)};
    if (choice_ == 1 && has_p2_) list.push_back(p2_);
    std::sort(list.begin(), list.end(), [](const SessionPlayer& a, const SessionPlayer& b) { return a.port < b.port; });
    session_.players = list;
    session_.lead_port = lead_;
    chosen_ = true;
}

void Players::handleInput() {
    for (int port = 0; port < wii::Input::kPorts && !chosen_ && !back_; port++) {
        const u16 d = input_.down(port);
        if (!d) continue;
        if (lead_ < 0) {
            if (d & PAD_BUTTON_B) {
                back_ = true;
                wii::sfx::back();
                return;
            }
            if (!(d & (kArrows | PAD_BUTTON_A | PAD_BUTTON_Y))) continue;
            lead_ = port;   // the first press claims the lead, and counts
        }
        if (port != lead_) {
            // P2 joins with A on their own pad, only while 2 Players is highlighted.
            if ((d & PAD_BUTTON_A) && choice_ == 1 && !has_p2_) {
                p2_ = makePlayer(port);
                has_p2_ = true;
                wii::sfx::select();
                if (waiting_) finish();
            }
            continue;
        }
        if (d & kArrows) {
            if (choice_ == 0 && !has_p2_ && !secondPad()) {
                say("No second pad found");
            } else {
                choice_ = 1 - choice_;
                waiting_ = false;
                if (choice_ == 0) has_p2_ = false;
                wii::sfx::move();
            }
        }
        if (d & PAD_BUTTON_B) {
            if (waiting_) waiting_ = false;
            else back_ = true;
            wii::sfx::back();
        }
        if ((d & PAD_BUTTON_Y) && !has_p2_) {   // an autoplay P2 on the first free port
            choice_ = 1;
            p2_ = makePlayer(lead_ == 0 ? 1 : 0);
            p2_.bot = p2_.autoplay = true;
            p2_.life_bar = ddr::LifeBar::Off;
            has_p2_ = true;
            if (waiting_) finish();
        }
        if ((d & PAD_BUTTON_A) && !chosen_) {
            wii::sfx::select();
            if (choice_ == 0 || has_p2_) finish();
            else waiting_ = true;
        }
    }
    // The second pad was unplugged before P2 joined.
    if (!chosen_ && choice_ == 1 && !has_p2_ && !secondPad()) {
        choice_ = 0;
        waiting_ = false;
    }
}

bool Players::frame() {
    frames_++;
    handleInput();
    draw();
    const bool quit = exit_.update(input_);
    return !chosen_ && !back_ && !quit;
}

void Players::draw() {
    render::rect(0, 0, video_.width(), video_.height(), kBg);
    render::rect(0, 0, video_.width(), 46, 0x000000A0);
    const bool one = choice_ == 0, two = choice_ == 1, gone = !has_p2_ && !secondPad();
    const float pulse = 0.6f + 0.4f * std::sin(frames_ * 0.1f);
    const u32 edge = 0xFFFFFF00 | (u32)(255 * pulse);
    const u32 lit_bg = 0x10182873;   // the dark squares on a highlighted choice
    render::rect(104, 104, 200, 200, one ? kPlayerColour[0] : kBox);
    if (one) render::rect(100, 104, 4, 200, edge);
    padIcon(156, 128, 96, one ? kWhite : kGrey, one ? lit_bg : kBg);
    render::rect(336, 104, 200, 200, two ? kPlayerColour[0] : gone ? kGone : kBox);
    if (two) render::rect(332, 104, 4, 200, edge);
    const u32 lit2 = two ? kWhite : gone ? 0x26303AFF : kGrey;
    padIcon(358, 140, 72, lit2, two ? lit_bg : kBg);
    padIcon(442, 140, 72, lit2, two ? lit_bg : kBg);

    GRRLIB_SetAntiAliasing(false);
    font_.print(48, 24, kWhite, 1, "PLAYERS");
    printCentred(font_, 204, 256, one ? kBlack : kWhite, 2, "1 Player");
    printCentred(font_, 436, 256, two ? kBlack : gone ? kGoneText : kWhite, 2, "2 Players");
    if (gone) printCentred(font_, 436, 312, kGrey, 1, "no second pad found");

    char buf[64];
    if (lead_ < 0) {
        font_.print(104, 328, kGrey, 1, "Press A or an arrow on your pad");
    } else if (one || !has_p2_) {
        font_.print(104, 328, kPlayerColour[0], 1, "P1");
        std::snprintf(buf, sizeof buf, "port %d", lead_ + 1);
        font_.print(136, 328, kWhite, 1, "%s", buf);
        if (two) {
            font_.print(104, 348, kPlayerColour[1], 1, "P2");
            const bool blink = waiting_ && frames_ / 10 % 2;
            font_.print(136, 348, blink ? kWhite : kGrey, 1, "press A on your pad");
            font_.print(136, 368, kGrey, 1, "the lower port plays on the left");
        }
    } else {
        const int ports[2] = {std::min(lead_, p2_.port), std::max(lead_, p2_.port)};
        for (int n = 0; n < 2; n++) {
            font_.print(104, 328 + n * 20, kPlayerColour[n], 1, "P%d", n + 1);
            std::snprintf(buf, sizeof buf, "%s   port %d%s", n == 0 ? "left " : "right", ports[n] + 1,
                          ports[n] == p2_.port && p2_.bot ? "   autoplay" : "");
            font_.print(136, 328 + n * 20, kWhite, 1, "%s", buf);
        }
    }
    if (!message_.empty() && wii::Input::nowUs() - message_us_ < kMessageUs)
        printCentred(font_, 320, 72, 0xFFC040FF, 1, message_.c_str());
    ui::help(font_, "Left/Right choose   A select   B back", "START+Z 2 s exit");
    GRRLIB_SetAntiAliasing(true);
}

}  // namespace screens
