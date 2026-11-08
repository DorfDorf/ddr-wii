// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/PlayfieldStress.h"

#include <grrlib.h>
#include <math.h>
#include <ogc/lwp_watchdog.h>

#include "platform/wii/WiiVideo.h"
#include "render/ArrowArt.h"
#include "render/Draw.h"
#include "render/Font.h"

namespace screens {
namespace {

constexpr float kBpm = 150.0f;
constexpr float kSpeedMod = 1.5f;           // scroll: 1.5 lane widths per beat (low speed = most notes)
constexpr float kReceptorY = 76.0f;
constexpr u64 kStepUs = 5000000;            // auto: 5 s per player count
constexpr u64 kWarmupUs = 500000;           // ignore the first 0.5 s of each step
constexpr float kArt = render::ArrowArt::kSize;

constexpr float kLaneDegrees[4] = {-90.0f, 180.0f, 0.0f, 90.0f};
constexpr u16 kLaneButton[4] = {PAD_BUTTON_LEFT, PAD_BUTTON_DOWN, PAD_BUTTON_UP, PAD_BUTTON_RIGHT};

// Every 16th row is a jump (two notes): the densest plausible stream, as a worst case.
constexpr int kJumps[6][2] = {{0, 3}, {1, 2}, {0, 1}, {2, 3}, {0, 2}, {1, 3}};

constexpr u32 kColour4th = 0xFF5050FF, kColour8th = 0x5090FFFF, kColour16th = 0xFFE040FF;
constexpr u32 kPlayerColour[4] = {0x60A0FFFF, 0xFF7070FF, 0x70E070FF, 0xFFD050FF};

const char* const kJudgements[] = {"MARVELOUS", "PERFECT", "GREAT", "PERFECT"};

u32 withAlpha(u32 rgba, float a) {
    const u32 aa = (u32)((a < 0 ? 0 : (a > 1 ? 1 : a)) * 255.0f + 0.5f);
    return (rgba & 0xFFFFFF00) | aa;
}

}  // namespace

PlayfieldStress::PlayfieldStress(wii::Video& video, const render::ArrowArt& art,
                                 const render::Font& font)
    : video_(video), art_(art), font_(font) {
    start_ticks_ = gettime();
}

// doc 06 §11.3: 1P centred and 2P versus at 64 px lanes; 3-4P in 160 px columns at 36 px.
PlayfieldStress::Layout PlayfieldStress::layoutFor(int players, int index) {
    if (players == 1) return {320.0f, 64.0f, 1.0f};
    if (players == 2) return {index == 0 ? 160.0f : 480.0f, 64.0f, 1.0f};
    const float centre = 320.0f + (index - (players - 1) / 2.0f) * 160.0f;
    return {centre, 36.0f, 36.0f / kArt};
}

bool PlayfieldStress::frame() {
    const u64 now_us = ticks_to_microsecs(gettime() - start_ticks_);
    const float beat = now_us / 1e6f * kBpm / 60.0f;

    handleInput();
    updateStats(now_us);

    render::resetCounts();
    drawBackground();
    const int players = step_ + 1;
    for (int p = 0; p < players; p++) drawPlayfield(p, layoutFor(players, p), beat);
    const render::DrawCounts& c = render::counts();
    scene_draws_ = c.draws;
    scene_switches_ = c.tex_switches;
    scene_pixels_ = c.pixels;
    drawOverlay();

    quit_frames_ = ((held_ & PAD_BUTTON_START) && (held_ & PAD_TRIGGER_Z)) ? quit_frames_ + 1 : 0;
    return quit_frames_ < 120;
}

void PlayfieldStress::handleInput() {
    held_ = PAD_ButtonsHeld(0);
    const u16 down = PAD_ButtonsDown(0);
    if (down & PAD_BUTTON_A) auto_ = !auto_;
    int manual = 0;
    if (down & PAD_TRIGGER_R) manual = 1;
    if (down & PAD_TRIGGER_L) manual = -1;
    if (manual) {
        auto_ = false;
        step_ = (step_ + manual + kSteps) % kSteps;
        step_start_us_ = ticks_to_microsecs(gettime() - start_ticks_);
        step_drop_base_ = video_.stats().dropped_total;
        results_[step_] = StepResult{};
    }
}

void PlayfieldStress::updateStats(u64 now_us) {
    const u64 in_step = now_us - step_start_us_;
    const wii::FrameStats& s = video_.stats();
    if (in_step >= kWarmupUs && s.frame_count > 1) {
        StepResult& r = results_[step_];
        r.frames++;
        r.interval_sum += s.interval_us;
        r.busy_sum += s.busy_us;
        r.gpu_sum += s.gpu_us;
        if (s.interval_us > r.interval_max) r.interval_max = s.interval_us;
        if (s.busy_us > r.busy_max) r.busy_max = s.busy_us;
        if (s.gpu_us > r.gpu_max) r.gpu_max = s.gpu_us;
        r.dropped = s.dropped_total - step_drop_base_;
        // Counts from the previous frame, which is the one these timings measured.
        r.draws_sum += scene_draws_;
        r.switches_sum += scene_switches_;
        r.pixels_sum += scene_pixels_;
    } else if (in_step < kWarmupUs) {
        step_drop_base_ = s.dropped_total;
    }
    if (auto_ && in_step >= kStepUs) {
        step_ = (step_ + 1) % kSteps;
        step_start_us_ = now_us;
        step_drop_base_ = s.dropped_total;
        results_[step_] = StepResult{};
    }
}

void PlayfieldStress::drawBackground() {
    const int h = video_.height(), w = video_.width();
    constexpr int kBands = 24;
    for (int i = 0; i < kBands; i++) {
        const float t = (float)i / kBands;
        const u32 r = 0x10 + (u32)(0x20 * t), b = 0x30 + (u32)(0x10 * t);
        render::rect(0, (float)h * i / kBands, w, (float)h / kBands + 1, (r << 24) | (0x18 << 16) | (b << 8) | 0xFF);
    }
}

void PlayfieldStress::drawPlayfield(int player, const Layout& l, float beat) {
    const float h = video_.height();
    const float half = kArt / 2.0f;          // GRRLIB positions by the unscaled top-left
    const float size = kArt * l.scale;        // on-screen sprite size
    const float ppb = kSpeedMod * l.spacing;  // pixels per beat
    auto laneX = [&](int lane) { return l.centre_x + (lane - 1.5f) * l.spacing; };
    auto beatY = [&](float b) { return kReceptorY + (b - beat) * ppb; };
    const float frac = beat - floorf(beat);
    const float pulse = (1.0f - frac) * (1.0f - frac);

    // Lane channel and life bar.
    const float field_w = 4 * l.spacing + l.spacing * 0.5f;
    render::rect(l.centre_x - field_w / 2, 0, field_w, h, 0x00000070);
    render::rect(l.centre_x - field_w / 2, 8, field_w, 10, 0x202020FF);
    const float life = 0.6f + 0.35f * sinf(beat * 0.3f + player);
    render::rect(l.centre_x - field_w / 2, 8, field_w * life, 10, kPlayerColour[player]);

    // Receptors with a beat glow behind them (large translucent quads).
    for (int lane = 0; lane < 4; lane++) {
        const float x = laneX(lane) - half, y = kReceptorY - half;
        render::img(x, y, art_.glow, 0, 1.6f * l.scale, 1.6f * l.scale, withAlpha(0xFFFFFFFF, 0.25f + 0.35f * pulse));
        const bool pressed = player == 0 && (held_ & kLaneButton[lane]);
        const float s = l.scale * (pressed ? 0.9f : 1.0f + 0.10f * pulse);
        render::img(x, y, art_.receptor, kLaneDegrees[lane], s, s, pressed ? 0xFFFFFFFF : 0xC0C0C0FF);
    }

    const float beat_min = beat - (kReceptorY + size) / ppb;
    const float beat_max = beat + (h - kReceptorY + size) / ppb;

    // Holds: one every 2 beats, 1.5 beats long, rotating lanes. Body below the notes.
    for (int k = (int)floorf((beat_min - 2.0f) / 2.0f); 2.0f * k + 0.5f <= beat_max; k++) {
        if (k < 0) continue;
        const float b0 = 2.0f * k + 0.5f, b1 = b0 + 1.5f;
        if (b1 < beat) continue;   // finished
        const int lane = (k + player) & 3;
        const float y0 = beatY(b0 < beat ? beat : b0), y1 = beatY(b1);
        const float cy = (y0 + y1) / 2.0f;
        render::img(laneX(lane) - half, cy - half, art_.hold, 0, l.scale, (y1 - y0) / kArt, 0x80FF80FF);
        render::img(laneX(lane) - half, y1 - half, art_.glow, 0, 0.6f * l.scale, 0.6f * l.scale, 0x80FF80C0);
    }

    // Notes: a jump on every 16th row.
    for (int row = (int)ceilf(beat_min * 4.0f); row <= (int)floorf(beat_max * 4.0f); row++) {
        if (row < 0) continue;
        const float b = row / 4.0f;
        if (b < beat - 0.02f) continue;   // already hit (judged on arrival)
        const u32 colour = (row % 4 == 0) ? kColour4th : (row % 2 == 0) ? kColour8th : kColour16th;
        const float y = beatY(b) - half;
        for (int j = 0; j < 2; j++) {
            const int lane = kJumps[(row + player) % 6][j];
            render::img(laneX(lane) - half, y, art_.note, kLaneDegrees[lane], l.scale, l.scale, colour);
        }
    }

    // Hit explosions on all four lanes, fading over each 16th (always on screen: worst case).
    const float row_frac = beat * 4.0f - floorf(beat * 4.0f);
    for (int lane = 0; lane < 4; lane++) {
        const float s = l.scale * (1.2f + 0.8f * row_frac);
        render::img(laneX(lane) - half, kReceptorY - half, art_.glow, 0, s, s,
                    withAlpha(0xFFF0A0FF, 0.8f * (1.0f - row_frac)));
    }

    // Judgement, combo and score text (bitmap font).
    const float ts = l.scale < 1.0f ? 1.5f : 2.0f;
    const int combo = (int)(beat * 8.0f);
    const char* judge = kJudgements[(combo / 8 + player) % 4];
    const float jw = 9 * 8 * ts;   // centre on the widest word
    font_.print(l.centre_x - jw / 2, 200, 0xFFFFFFFF, ts, "%9s", judge);
    font_.print(l.centre_x - 4 * 8 * ts, 200 + 18 * ts, 0xFFFFFFFF, ts, "%4d", combo);
    font_.print(l.centre_x - 5 * 4, 200 + 34 * ts + 2, 0xD0D0D0FF, 1, "COMBO");
    font_.print(l.centre_x - field_w / 2 + 4, 22, kPlayerColour[player], 1, "P%d %7d", player + 1, combo * 977 % 10000000);
}

void PlayfieldStress::drawOverlay() {
    const wii::FrameStats& s = video_.stats();
    const u32 white = 0xFFFFFFFF, grey = 0xB0B0B0FF, warn = 0xFFC040FF, live = 0x80FF80FF;
    GRRLIB_SetAntiAliasing(false);

    render::rect(0, 360, 640, 120, 0x000000C8);
    font_.print(8, 364, white, 1, "M2b playfield stress  %s  %s  now %dP  draws %u tex %u %.2f Mpx",
                video_.modeName(), auto_ ? "auto" : "manual", step_ + 1, (unsigned)scene_draws_,
                (unsigned)scene_switches_, scene_pixels_ / 1e6f);
    font_.print(8, 382, grey, 1, "   frame avg  max   cpu avg  max   gpu avg  max  drop draws  tex   Mpx");
    for (int i = 0; i < kSteps; i++) {
        const float y = 398.0f + 16.0f * i;
        const StepResult& r = results_[i];
        const u32 c = (i == step_) ? live : white;
        if (r.frames == 0) {
            font_.print(8, y, c, 1, "%dP        -", i + 1);
            continue;
        }
        const float n = (float)r.frames;
        font_.print(8, y, r.dropped ? warn : c, 1,
                    "%dP     %5.2f %5.2f    %5.2f %5.2f    %5.2f %5.2f  %3lu  %4u %4u  %4.2f", i + 1,
                    r.interval_sum / n / 1000.0f, r.interval_max / 1000.0f, r.busy_sum / n / 1000.0f,
                    r.busy_max / 1000.0f, r.gpu_sum / n / 1000.0f, r.gpu_max / 1000.0f,
                    (unsigned long)r.dropped, (unsigned)(r.draws_sum / r.frames),
                    (unsigned)(r.switches_sum / r.frames), r.pixels_sum / r.frames / 1e6);
    }
    const bool over = s.max_interval_us > video_.refreshPeriodUs() * 3 / 2;
    font_.print(8, 462, over ? warn : grey, 1, "A auto  L/R players  B M2 scene  START+Z 2s exit  (ms; gpu = HW only)");
    GRRLIB_SetAntiAliasing(true);
}

}  // namespace screens
