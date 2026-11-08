// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/OffsetCalibration.h"

#include <gccore.h>
#include <grrlib.h>
#include <ogc/lwp_watchdog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/ArrowArt.h"
#include "render/Draw.h"
#include "render/Font.h"

namespace screens {
namespace {

constexpr double kBeatS = 0.5;                 // Click 120
constexpr double kStepS = 0.005;               // L/R nudge
constexpr float kLane = 64, kArrowY = 150;
constexpr float kLaneDegrees[4] = {-90.0f, 180.0f, 0.0f, 90.0f};   // art points up
constexpr u64 kFlashUs = 120000;

u64 cpuUs() { return ticks_to_microsecs(gettime()); }

void printCentred(const render::Font& font, float y, u32 colour, float scale, const char* text) {
    font.print(320 - std::strlen(text) * render::Font::kGlyphW * scale / 2, y, colour, scale, "%s", text);
}

}  // namespace

OffsetCalibration::OffsetCalibration(wii::Video& video, const wii::Input& input, const render::ArrowArt& art,
                                     const render::Font& font, Session& session)
    : video_(video), input_(input), art_(art), font_(font), session_(session), cal_(kBeatS, 0.0),
      offset_s_(session.config.globalOffsetS) {
    startTrack();
}

void OffsetCalibration::startTrack() {
    clock_.reset();
    last_tick_report_ = 0;
    next_auto_beat_ = 0;
    cal_.restart();
    if (!stream_.open(kTrack)) {
        error_ = stream_.error();
        return;
    }
    stream_.play();
    video_.resetStats();
}

void OffsetCalibration::save() {
    session_.config.globalOffsetS = offset_s_;
    if (ddr::SaveConfig(kConfigPath, session_.config)) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "Offset %+.1f ms saved to config.ini", offset_s_ * 1000);
        session_.message = buf;
    } else {
        session_.message = "Couldn't write sd:/ddrwii/config.ini (offset kept until power off)";
    }
}

bool OffsetCalibration::frame() {
    const u64 now_us = cpuUs();
    const u16 down0 = input_.down(0);
    if (down0 & PAD_BUTTON_B) return false;   // back, nothing saved

    if (!error_) {
        const wii::AudioStream::TickReport t = stream_.tickReport();
        if (t.count != last_tick_report_) {
            last_tick_report_ = t.count;
            clock_.observe(ticks_to_microsecs(t.tick),
                           (s64)t.ticks * wii::AudioStream::kTickUsNum / wii::AudioStream::kTickUsDen);
        }
        if (stream_.stats().finished) startTrack();   // loop the 5-minute track
    }

    // Steps: any lane on any port, in song time plus that port's InputOffset. The global
    // offset is left out: it's what we're measuring.
    if (clock_.started()) {
        for (int i = 0; i < input_.eventCount(); i++) {
            const input::LaneEvent& e = input_.event(i);
            if (!e.pressed || e.lane >= 4) continue;
            const double t = (e.time_us + clock_.offsetUs()) / 1e6 + session_.config.ports[e.port].inputOffsetS;
            cal_.press(t);
            flash_us_[e.lane] = now_us;
        }
    }
    const double song_s = clock_.started() ? clock_.now(now_us) / 1e6 : 0;
    if (auto_) {
        while ((next_auto_beat_ + 1) * kBeatS <= song_s) {
            next_auto_beat_++;
            cal_.press(next_auto_beat_ * kBeatS);
            flash_us_[next_auto_beat_ % 4] = now_us;
        }
    } else {
        next_auto_beat_ = (long)(song_s / kBeatS);
    }

    if (down0 & PAD_TRIGGER_L) offset_s_ -= kStepS;
    if (down0 & PAD_TRIGGER_R) offset_s_ += kStepS;
    offset_s_ = std::max(-ddr::Config::kMaxOffsetS, std::min(ddr::Config::kMaxOffsetS, offset_s_));
    if (down0 & PAD_TRIGGER_Z) cal_.reset();
    if (down0 & PAD_BUTTON_Y) auto_ = !auto_;
    for (int port = 0; port < wii::Input::kPorts; port++) {
        if (input_.down(port) & (PAD_BUTTON_START | PAD_BUTTON_A)) {
            if (cal_.ready()) offset_s_ = cal_.suggestedOffset();
            save();
            return false;
        }
    }

    // Drawing: four arrows that flash on each beat (brighter on the downbeat), glowing where
    // a step landed; then the numbers.
    render::rect(0, 0, video_.width(), video_.height(), 0x101828FF);
    const double beat_pos = song_s / kBeatS;
    const long beat = (long)std::floor(beat_pos);
    const float since = (float)(beat_pos - beat);   // 0 at the click
    const float k = clock_.started() ? std::max(0.0f, 1.0f - since * 4) : 0;
    const bool down_beat = beat % 4 == 0;
    for (int lane = 0; lane < 4; lane++) {
        const float x = 320 + (lane - 1.5f) * kLane;
        const float s = 1.0f + 0.15f * k;
        const u8 v = (u8)(0x90 + 0x6F * k);
        render::img(x, kArrowY, art_.receptor, kLaneDegrees[lane], s, s,
                    ((u32)v << 24) | ((u32)v << 16) | ((u32)v << 8) | 0xFF);
        if (k > 0 && down_beat) render::img(x, kArrowY, art_.glow, 0, 1.5f, 1.5f, 0xFF5D4700 | (u32)(k * 160));
        if (flash_us_[lane] && now_us - flash_us_[lane] < kFlashUs) {
            const float f = 1.0f - (float)(now_us - flash_us_[lane]) / kFlashUs;
            render::img(x, kArrowY, art_.glow, 0, 1.2f, 1.2f, 0xB0F4FF00 | (u32)(f * 200));
        }
    }

    GRRLIB_SetAntiAliasing(false);
    const u32 white = 0xFFFFFFFF, grey = 0x9AA4ABFF, warn = 0xFFC040FF;
    font_.print(48, 24, white, 1, "OFFSET CALIBRATION");
    printCentred(font_, 64, grey, 1, "Step on any arrow with each click.");
    printCentred(font_, 82, grey, 1, "Follow the sound; the flashes are only a guide.");
    char buf[96];
    if (error_) {
        std::snprintf(buf, sizeof buf, "Click track: %s", error_);
        printCentred(font_, 210, 0xFF5050FF, 1, buf);
        printCentred(font_, 228, grey, 1, "Run tools/stage-sd.sh and copy sd/ to the card.");
    } else {
        const size_t n = cal_.count();
        if (cal_.ready())
            std::snprintf(buf, sizeof buf, "steps %u%s", (unsigned)n, auto_ ? "   (auto)" : "");
        else
            std::snprintf(buf, sizeof buf, "steps %u / %u%s", (unsigned)n, (unsigned)game::Calibration::kReady,
                          auto_ ? "   (auto)" : "");
        printCentred(font_, 210, white, 1, buf);
        if (n) {
            const double last = cal_.lastError() * 1000;
            std::snprintf(buf, sizeof buf, "last step %+.1f ms %s", last, last < 0 ? "early" : "late");
            printCentred(font_, 232, grey, 1, buf);
            std::snprintf(buf, sizeof buf, "average %+.1f ms   spread %.1f ms", cal_.mean() * 1000,
                          cal_.stdDev() * 1000);
            printCentred(font_, 256, white, 1, buf);
        }
    }
    std::snprintf(buf, sizeof buf, "current offset %+.1f ms", offset_s_ * 1000);
    printCentred(font_, 310, white, 1, buf);
    if (cal_.ready()) {
        std::snprintf(buf, sizeof buf, "suggested offset %+.1f ms", cal_.suggestedOffset() * 1000);
        printCentred(font_, 330, warn, 2, buf);
    }
    font_.print(64, 384, grey, 1, cal_.ready() ? "START save the suggested offset" : "START save the current offset");
    font_.print(64, 400, grey, 1, "L/R adjust 5 ms   Z clear steps   Y auto steps");
    font_.print(64, 416, grey, 1, "B back without saving");
    GRRLIB_SetAntiAliasing(true);
    return true;
}

}  // namespace screens
