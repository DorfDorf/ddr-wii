// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/Options.h"

#include <gccore.h>
#include <grrlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/songs/SongLibrary.h"
#include "platform/wii/MenuSound.h"
#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/Draw.h"
#include "render/Font.h"

namespace screens {
namespace {

constexpr u32 kBg = 0x101828FF, kBox = 0x1E282FFF, kDark = 0x0A141BFF, kWhite = 0xFFFFFFFF;
constexpr u32 kGrey = 0x9AA4ABFF, kBlack = 0x000000FF, kPink = 0xFF47B3FF;
constexpr u64 kHoldBUs = 1000000;
constexpr int kMaxDebounceMs = 50;

// The list: headings and rows, in order. Rows that do nothing yet aren't here.
enum RowId { kHeadPlay, kCalibrate, kHeadPads, kDebounce, kPadsPage, kHeadAdvanced, kSync, kTests, kRowCount };
bool isHeading(int r) { return r == kHeadPlay || r == kHeadPads || r == kHeadAdvanced; }

struct RowText { const char* label; const char* help1; const char* help2; };
constexpr RowText kRowText[kRowCount] = {
    {"PLAY", "", ""},
    {"Calibrate offset", "Left/Right nudge the offset by 1 ms. A opens the calibration screen:",
     "step on each click, then A there saves the suggested offset."},
    {"PADS", "", ""},
    {"Ignore double presses within", "A panel that bounces within this time counts once.", "Saved as InputDebounceMs."},
    {"Port offsets and test input", "A timing offset for each port, for two mats that measure",
     "differently, and a screen that lights up what each pad holds."},
    {"ADVANCED", "", ""},
    {"Default pack sync", "The sync used for packs without a Pack.ini.", "ITG takes off ITG's 9 ms bias; None leaves them as written."},
    {"Test scenes", "Audio clock, playfield stress and render tests.", "For checking the build, not for playing."},
};

// ms, kept to 0.1 ms (the calibration's suggestion has a fraction) and to config.ini's +-1 s.
double nudgeMs(double seconds, int dir) {
    const double ms = std::round((seconds * 1000 + dir) * 10) / 10;
    const double lim = ddr::Config::kMaxOffsetS * 1000;
    return std::max(-lim, std::min(lim, ms)) / 1000;
}

void printRight(const render::Font& font, float right, float y, u32 colour, const char* s) {
    font.print(right - std::strlen(s) * render::Font::kGlyphW, y, colour, 1, "%s", s);
}

void choiceRow(float y, bool sel, u32 frames) {
    render::rect(48, y, 544, 32, sel ? kPink : kBox);
    if (sel) {
        const float pulse = 0.6f + 0.4f * std::sin(frames * 0.1f);
        render::rect(44, y, 4, 32, 0xFFFFFF00 | (u32)(255 * pulse));
    }
}

void header(const wii::Video& video, const render::Font& font, const char* title) {
    render::rect(0, 0, video.width(), 46, 0x000000A0);
    GRRLIB_SetAntiAliasing(false);
    font.print(48, 24, kWhite, 1, "%s", title);
    GRRLIB_SetAntiAliasing(true);
}

}  // namespace

Options::Options(wii::Video& video, wii::Input& input, const render::Font& font, Session& session)
    : video_(video), input_(input), font_(font), session_(session) {
    row_ = isHeading(session_.options_row) ? kCalibrate : session_.options_row;
    page_ = session_.options_pads ? kPads : kList;
    pads_row_ = session_.options_pads ? 4 : 0;
    if (!session_.message.empty()) {   // e.g. the calibration's "offset saved"
        hint_.note(session_.message);
        session_.message.clear();
    }
}

bool Options::frame() {
    frames_++;
    const u64 now = wii::Input::nowUs();
    u16 down = 0, held = 0;
    for (int port = 0; port < wii::Input::kPorts; port++) {
        down |= input_.down(port);
        held |= input_.held(port);
    }
    if (page_ == kTestInput) {
        handleTestInput();
    } else {
        const bool up = up_.update(down & PAD_BUTTON_UP, held & PAD_BUTTON_UP, now);
        const bool dn = down_.update(down & PAD_BUTTON_DOWN, held & PAD_BUTTON_DOWN, now);
        const bool lt = left_.update(down & PAD_BUTTON_LEFT, held & PAD_BUTTON_LEFT, now);
        const bool rt = right_.update(down & PAD_BUTTON_RIGHT, held & PAD_BUTTON_RIGHT, now);
        u16 d = down & ~(PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
        if (up) d |= PAD_BUTTON_UP;
        if (dn) d |= PAD_BUTTON_DOWN;
        const int dir = lt ? -1 : rt ? 1 : 0;
        if (page_ == kList)
            handleList(d, dir);
        else
            handlePads(d, dir);
    }
    session_.options_row = row_;
    session_.options_pads = page_ != kList;
    if (exit_.update(input_)) quit_ = true;
    draw();
    return next_ == Next::None && !quit_;
}

void Options::handleList(u16 d, int dir) {
    if (d & (PAD_BUTTON_UP | PAD_BUTTON_DOWN)) {
        const int step = (d & PAD_BUTTON_UP) ? -1 : 1;
        do row_ = (row_ + step + kRowCount) % kRowCount;
        while (isHeading(row_));
        wii::sfx::move();
        return;
    }
    ddr::Config& c = session_.config;
    if (dir) {
        bool changed = false;
        if (row_ == kCalibrate) {
            const double v = nudgeMs(c.globalOffsetS, dir);
            changed = v != c.globalOffsetS;
            c.globalOffsetS = v;
        } else if (row_ == kDebounce) {
            const int v = std::max(0, std::min(kMaxDebounceMs, c.inputDebounceMs + dir));
            changed = v != c.inputDebounceMs;
            c.inputDebounceMs = v;
            input_.setDebounceUs((u32)v * 1000);
        } else if (row_ == kSync && (input_.down(0) | input_.down(1) | input_.down(2) | input_.down(3)) &
                                        (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) {   // not on repeats
            c.defaultSyncOffset = c.defaultSyncOffset == ddr::DefaultSyncOffset::Null ? ddr::DefaultSyncOffset::ITG
                                                                                      : ddr::DefaultSyncOffset::Null;
            ddr::ApplyDefaultSync(session_.library, c.defaultSyncOffset);
            changed = true;
        }
        if (changed) {
            session_.config_dirty = true;
            wii::sfx::move();
        }
        return;
    }
    if (d & PAD_BUTTON_A) {
        if (row_ == kCalibrate) next_ = Next::Calibrate;
        else if (row_ == kTests) next_ = Next::Tests;
        else if (row_ == kPadsPage) page_ = kPads, pads_row_ = 0;
        else return;
        wii::sfx::select();
    } else if (d & PAD_BUTTON_B) {
        next_ = Next::Back;
        wii::sfx::back();
    }
}

void Options::handlePads(u16 d, int dir) {
    if (d & (PAD_BUTTON_UP | PAD_BUTTON_DOWN)) {
        pads_row_ = (pads_row_ + ((d & PAD_BUTTON_UP) ? 4 : 1)) % 5;
        wii::sfx::move();
        return;
    }
    if (dir && pads_row_ < 4) {
        double& off = session_.config.ports[pads_row_].inputOffsetS;
        const double v = nudgeMs(off, dir);
        if (v != off) {
            off = v;
            session_.config_dirty = true;
            wii::sfx::move();
        }
        return;
    }
    if ((d & PAD_BUTTON_A) && pads_row_ == 4) {
        page_ = kTestInput;
        b_held_since_us_ = 0;
        wii::sfx::select();
    } else if (d & PAD_BUTTON_B) {
        page_ = kList;
        wii::sfx::back();
    }
}

// B is one of the buttons this page shows, so leaving takes holding it for 1 s. No sounds.
void Options::handleTestInput() {
    bool b = false;
    for (int port = 0; port < wii::Input::kPorts; port++) b = b || (input_.held(port) & PAD_BUTTON_B);
    const u64 now = wii::Input::nowUs();
    if (!b) {
        b_held_since_us_ = 0;
    } else if (!b_held_since_us_) {
        b_held_since_us_ = now;
    } else if (now - b_held_since_us_ >= kHoldBUs) {
        page_ = kPads;
        up_.reset();
        down_.reset();
    }
}

void Options::draw() {
    render::rect(0, 0, video_.width(), video_.height(), kBg);
    if (page_ == kList)
        drawList();
    else if (page_ == kPads)
        drawPads();
    else
        drawTestInput();
    hint_.draw(font_);
}

void Options::drawList() {
    header(video_, font_, "OPTIONS");
    const ddr::Config& c = session_.config;
    float y = 56, ys[kRowCount];
    for (int r = 0; r < kRowCount; r++) {
        ys[r] = y;
        if (isHeading(r)) {
            y += 22;
            continue;
        }
        choiceRow(y, r == row_, frames_);
        y += 36;
    }
    GRRLIB_SetAntiAliasing(false);
    char buf[48];
    for (int r = 0; r < kRowCount; r++) {
        if (isHeading(r)) {
            font_.print(48, ys[r] + 2, kGrey, 1, "%s", kRowText[r].label);
            continue;
        }
        const u32 col = r == row_ ? kBlack : kWhite;
        font_.print(64, ys[r] + 8, col, 1, "%s", kRowText[r].label);
        switch (r) {
            case kCalibrate: std::snprintf(buf, sizeof buf, "< %+.1f ms >  >", c.globalOffsetS * 1000); break;
            case kDebounce: std::snprintf(buf, sizeof buf, "< %d ms >", c.inputDebounceMs); break;
            case kSync:
                std::snprintf(buf, sizeof buf, "< %s >", c.defaultSyncOffset == ddr::DefaultSyncOffset::ITG ? "ITG" : "None");
                break;
            default: std::snprintf(buf, sizeof buf, ">"); break;
        }
        printRight(font_, 576, ys[r] + 8, col, buf);
    }
    font_.print(64, 352, kGrey, 1, "%s", kRowText[row_].help1);
    font_.print(64, 368, kGrey, 1, "%s", kRowText[row_].help2);
    ui::help(font_, "Up/Down row   Left/Right change   A open", "B back, saving config.ini");
    GRRLIB_SetAntiAliasing(true);
}

void Options::drawPads() {
    header(video_, font_, "OPTIONS  PADS");
    for (int k = 0; k < 5; k++) choiceRow(60 + k * 36, k == pads_row_, frames_);
    GRRLIB_SetAntiAliasing(false);
    char buf[32];
    for (int k = 0; k < 5; k++) {
        const float y = 60 + k * 36 + 8;
        const u32 col = k == pads_row_ ? kBlack : kWhite;
        if (k < 4) {
            font_.print(64, y, col, 1, "Port %d input offset", k + 1);
            std::snprintf(buf, sizeof buf, "< %+.1f ms >", session_.config.ports[k].inputOffsetS * 1000);
        } else {
            font_.print(64, y, col, 1, "Test input");
            std::snprintf(buf, sizeof buf, ">");
        }
        printRight(font_, 576, y, col, buf);
    }
    if (pads_row_ < 4) {
        font_.print(64, 352, kGrey, 1, "Shifts this mat's steps, for two mats that measure");
        font_.print(64, 368, kGrey, 1, "differently. Saved as [PortN] InputOffset.");
    } else {
        font_.print(64, 352, kGrey, 1, "Lights up every button each pad is holding.");
    }
    ui::help(font_, "Up/Down row   Left/Right change   A open", "B back to Options");
    GRRLIB_SetAntiAliasing(true);
}

void Options::drawTestInput() {
    header(video_, font_, "TEST INPUT");
    struct Cell { u16 bit; const char* label; };
    static const Cell kCells[9] = {{PAD_BUTTON_B, "B"},    {PAD_BUTTON_UP, "^"}, {PAD_BUTTON_A, "A"},
                                   {PAD_BUTTON_LEFT, "<"}, {0, ""},              {PAD_BUTTON_RIGHT, ">"},
                                   {0, ""},                {PAD_BUTTON_DOWN, "v"}, {0, ""}};
    static const Cell kBars[2] = {{PAD_BUTTON_START, "START"}, {PAD_TRIGGER_Z, "Z"}};
    const u32 conn = input_.connected();
    for (int pass = 0; pass < 2; pass++) {   // boxes, then the text over them
        if (pass == 1) GRRLIB_SetAntiAliasing(false);
        for (int q = 0; q < wii::Input::kPorts; q++) {
            const float x0 = 64 + q * 136;
            const bool live = conn & (1u << q);
            const u16 h = input_.held(q);
            if (pass == 1) ui::printCentred(font_, x0 + 52, 72, live ? kWhite : kGrey, 1, q == 0 ? "Port 1" : q == 1 ? "Port 2" : q == 2 ? "Port 3" : "Port 4");
            for (int k = 0; k < 9; k++) {
                const float cx = x0 + (k % 3) * 36, cy = 100 + (k / 3) * 36;
                const bool on = kCells[k].bit && (h & kCells[k].bit);
                if (pass == 0) render::rect(cx, cy, 32, 32, !kCells[k].bit ? kDark : on ? kWhite : kBox);
                else if (kCells[k].bit) ui::printCentred(font_, cx + 16, cy + 8, on ? kBlack : kGrey, 1, kCells[k].label);
            }
            for (int k = 0; k < 2; k++) {
                const float y = 220 + k * 28;
                const bool on = h & kBars[k].bit;
                if (pass == 0) render::rect(x0, y, 104, 24, on ? kWhite : kBox);
                else ui::printCentred(font_, x0 + 52, y + 4, on ? kBlack : kGrey, 1, kBars[k].label);
            }
            if (pass == 1 && !live) ui::printCentred(font_, x0 + 52, 284, kGrey, 1, "not connected");
        }
    }
    GRRLIB_SetAntiAliasing(true);
    if (b_held_since_us_) {
        const float f = std::min(1.0f, (float)(wii::Input::nowUs() - b_held_since_us_) / kHoldBUs);
        render::rect(48, 330, 544 * f, 4, kPink);
    }
    GRRLIB_SetAntiAliasing(false);
    font_.print(64, 352, kGrey, 1, "Every pad: press anything to light it up.");
    ui::help(font_, "Hold B for 1 s to go back");
    GRRLIB_SetAntiAliasing(true);
}

}  // namespace screens
