// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/RenderTest.h"

#include <grrlib.h>
#include <math.h>
#include <ogc/lwp_watchdog.h>

#include "platform/wii/WiiVideo.h"
#include "render/ArrowArt.h"
#include "render/Font.h"

namespace screens {
namespace {

constexpr float kBpm = 120.0f;
constexpr float kPixelsPerBeat = 128.0f;  // scroll speed
constexpr float kReceptorY = 80.0f;       // receptor centre line
constexpr float kLaneSpacing = 68.0f;
constexpr u64 kStepUs = 4000000;          // auto stress: 4 s per step
constexpr u64 kWarmupUs = 500000;         // ignore the first 0.5 s of each step

// Lane order L, D, U, R. The art points up, so rotate per lane (GRRLIB degrees, clockwise).
constexpr float kLaneDegrees[4] = {-90.0f, 180.0f, 0.0f, 90.0f};
constexpr u16 kLaneButton[4] = {PAD_BUTTON_LEFT, PAD_BUTTON_DOWN, PAD_BUTTON_UP, PAD_BUTTON_RIGHT};

// Demo stream: one note every 8th, lanes from this repeating pattern.
constexpr int kPattern[] = {0, 1, 2, 3, 2, 1, 0, 3, 1, 2, 0, 3, 3, 2, 1, 0};
constexpr int kPatternN = sizeof(kPattern) / sizeof(kPattern[0]);

constexpr u32 kColour4th = 0xFF5050FF;
constexpr u32 kColour8th = 0x5090FFFF;

float laneX(int lane) { return 320.0f + (lane - 1.5f) * kLaneSpacing; }

u32 lerpColour(u32 a, u32 b, float t) {
    auto ch = [&](int s) {
        const float x = ((a >> s) & 0xFF) * (1 - t) + ((b >> s) & 0xFF) * t;
        return (u32)(x + 0.5f) << s;
    };
    return ch(24) | ch(16) | ch(8) | ch(0);
}

}  // namespace

constexpr int RenderTest::kStepSprites[RenderTest::kSteps];

RenderTest::RenderTest(wii::Video& video, const render::ArrowArt& art, const render::Font& font)
    : video_(video), art_(art), font_(font) {
    start_ticks_ = gettime();
    step_start_us_ = 0;
}

bool RenderTest::frame() {
    const u64 now_us = ticks_to_microsecs(gettime() - start_ticks_);
    const float t = now_us / 1e6f;
    const float song_beat = t * kBpm / 60.0f;

    handleInput();
    updateStress(now_us);

    drawBackground();
    drawStressSprites(t);
    drawLanes(song_beat);
    drawOverlay();

    // START+Z held for 2 s on port 1 exits (same as padtest).
    quit_frames_ = ((held_ & PAD_BUTTON_START) && (held_ & PAD_TRIGGER_Z)) ? quit_frames_ + 1 : 0;
    return quit_frames_ < 120;
}

void RenderTest::handleInput() {
    PAD_ScanPads();
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

void RenderTest::updateStress(u64 now_us) {
    const u64 in_step = now_us - step_start_us_;
    const wii::FrameStats& s = video_.stats();
    if (in_step >= kWarmupUs && s.frame_count > 1) {
        StepResult& r = results_[step_];
        r.frames++;
        r.interval_sum += s.interval_us;
        if (s.interval_us > r.interval_max) r.interval_max = s.interval_us;
        r.busy_sum += s.busy_us;
        if (s.busy_us > r.busy_max) r.busy_max = s.busy_us;
        r.dropped = s.dropped_total - step_drop_base_;
        have_result_[step_] = true;
    } else if (in_step < kWarmupUs) {
        step_drop_base_ = s.dropped_total;   // drops during warm-up don't count
    }
    if (auto_ && in_step >= kStepUs) {
        step_ = (step_ + 1) % kSteps;
        step_start_us_ = now_us;
        step_drop_base_ = s.dropped_total;
        results_[step_] = StepResult{};
    }
}

void RenderTest::drawBackground() {
    const int h = video_.height(), w = video_.width();
    constexpr int kBands = 24;
    for (int i = 0; i < kBands; i++) {
        const float y0 = (float)h * i / kBands, y1 = (float)h * (i + 1) / kBands;
        GRRLIB_Rectangle(0, y0, w, y1 - y0 + 1, lerpColour(0x101830FF, 0x302040FF, (float)i / kBands), true);
    }
    // Lane channel behind the receptors.
    GRRLIB_Rectangle(laneX(0) - 40, 0, laneX(3) - laneX(0) + 80, h, 0x00000060, true);
}

void RenderTest::drawStressSprites(float t) {
    const int n = kStepSprites[step_];
    const float h = video_.height() + 64.0f;
    for (int i = 0; i < n; i++) {
        const int lane = i & 3;
        const float x = 32.0f + (float)((i * 37) % 576);
        const float y = fmodf(i * 53.0f + t * 120.0f, h) - 32.0f;
        const u32 colour = ((i & 4) ? kColour8th : kColour4th) & 0xFFFFFFA0;
        GRRLIB_DrawImg(x, y, art_.note, kLaneDegrees[lane], 0.75f, 0.75f, colour);
    }
}

void RenderTest::drawLanes(float song_beat) {
    // Receptors pulse on each beat and light up while their panel is held.
    const float frac = song_beat - floorf(song_beat);
    const float pulse = (1.0f - frac) * (1.0f - frac);
    const int half = render::ArrowArt::kSize / 2;
    for (int lane = 0; lane < 4; lane++) {
        const bool pressed = held_ & kLaneButton[lane];
        const float scale = pressed ? 0.9f : 1.0f + 0.10f * pulse;
        const u32 colour = pressed ? 0xFFFFFFFF : lerpColour(0x909090FF, 0xFFFFFFFF, pulse);
        GRRLIB_DrawImg(laneX(lane) - half, kReceptorY - half, art_.receptor, kLaneDegrees[lane],
                       scale, scale, colour);
    }

    // Notes: one every 8th, positioned purely from the song time.
    const float first = floorf((song_beat - 0.75f) * 2.0f) / 2.0f;
    const float last = song_beat + (video_.height() - kReceptorY + half) / kPixelsPerBeat;
    for (float beat = first; beat <= last; beat += 0.5f) {
        const int idx = (int)(beat * 2.0f);
        if (idx < 0) continue;
        const int lane = kPattern[idx % kPatternN];
        const float y = kReceptorY + (beat - song_beat) * kPixelsPerBeat;
        const bool on_beat = (idx % 2) == 0;
        GRRLIB_DrawImg(laneX(lane) - half, y - half, art_.note, kLaneDegrees[lane], 1.0f, 1.0f,
                       on_beat ? kColour4th : kColour8th);
    }
}

void RenderTest::drawOverlay() {
    const wii::FrameStats& s = video_.stats();
    const u32 white = 0xFFFFFFFF, grey = 0xB0B0B0FF, warn = 0xFFC040FF;
    GRRLIB_SetAntiAliasing(false);   // crisp, non-bleeding glyphs

    GRRLIB_Rectangle(0, 384, 640, 96, 0x000000B0, true);
    font_.print(16, 390, white, 1, "ddr-wii M2 render test   %s   %ux%u", video_.modeName(),
                (unsigned)video_.width(), (unsigned)video_.height());
    const float fps = s.avg_interval_us ? 1e6f / s.avg_interval_us : 0.0f;
    font_.print(16, 408, s.max_interval_us > video_.refreshPeriodUs() * 3 / 2 ? warn : white, 1,
                "%5.2f fps  frame avg %5.2f max %5.2f ms  busy avg %5.2f max %5.2f ms", fps,
                s.avg_interval_us / 1000.0f, s.max_interval_us / 1000.0f, s.avg_busy_us / 1000.0f,
                s.max_busy_us / 1000.0f);
    font_.print(16, 426, s.dropped_total ? warn : white, 1, "dropped %lu   stress sprites %d (%s)",
                (unsigned long)s.dropped_total, kStepSprites[step_], auto_ ? "auto" : "manual");
    font_.print(16, 448, grey, 1, "A: auto on/off   L/R: fewer/more sprites   START+Z 2s: exit");

    // Stress table, top right.
    GRRLIB_Rectangle(456, 8, 176, 18 * (kSteps + 1) + 8, 0x000000B0, true);
    font_.print(462, 12, grey, 1, "spr  avg ms  max drp");
    for (int i = 0; i < kSteps; i++) {
        const float y = 30.0f + 18.0f * i;
        const u32 c = (i == step_) ? 0x80FF80FF : white;
        if (!have_result_[i] || results_[i].frames == 0) {
            font_.print(462, y, c, 1, "%4d     -", kStepSprites[i]);
            continue;
        }
        const StepResult& r = results_[i];
        font_.print(462, y, r.dropped ? warn : c, 1, "%4d %6.2f %5.1f %3lu", kStepSprites[i],
                    (float)r.interval_sum / r.frames / 1000.0f, r.interval_max / 1000.0f,
                    (unsigned long)r.dropped);
    }
    GRRLIB_SetAntiAliasing(true);
}

}  // namespace screens
