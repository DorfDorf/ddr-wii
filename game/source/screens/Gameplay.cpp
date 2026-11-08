// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/Gameplay.h"

#include <gccore.h>
#include <grrlib.h>
#include <ogc/lwp_watchdog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/chart/SmLoader.h"
#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/ArrowArt.h"
#include "render/Draw.h"
#include "render/Font.h"

namespace screens {
namespace {

constexpr const char* kSongPath = "sd:/ddrwii/songs/ddr-wii tests/Click 120/click120.sm";

constexpr float kArt = render::ArrowArt::kSize;
constexpr float kLane = 64.0f;          // lane spacing, 1-2 players (doc 06 §11.3)
constexpr float kReceptorY = 80.0f;     // inside the title-safe area (y >= 48)
constexpr float kSpeed = 2.0f;          // scroll: lane widths per beat
constexpr double kEndPadS = 2.0;        // stay on the playfield after the last note
constexpr u64 kFlashUs = 150000;        // receptor glow after a hit
constexpr u64 kGradeShowUs = 900000;    // judgement text
constexpr double kOffsetStepS = 0.005;

constexpr float kLaneDegrees[4] = {-90.0f, 180.0f, 0.0f, 90.0f};   // art points up
constexpr u16 kLaneButton[4] = {PAD_BUTTON_LEFT, PAD_BUTTON_DOWN, PAD_BUTTON_UP, PAD_BUTTON_RIGHT};

u32 gradeColour(game::Grade g) {
    switch (g) {
        case game::Grade::Marvelous: return 0xB0F4FFFF;
        case game::Grade::Perfect: return 0xFFE060FF;
        case game::Grade::Great: return 0x60F060FF;
        case game::Grade::Good: return 0x60A0FFFF;
        case game::Grade::Boo: return 0xC070FFFF;
        case game::Grade::Miss: return 0xFF4040FF;
        default: return 0xFFFFFFFF;
    }
}

// Note colour by quantisation: 4ths red, 8ths blue, 12ths purple, 16ths yellow, rest green.
u32 quantColour(int row) {
    if (row % 48 == 0) return 0xFF5050FF;
    if (row % 24 == 0) return 0x5090FFFF;
    if (row % 16 == 0) return 0xC060FFFF;
    if (row % 12 == 0) return 0xFFE040FF;
    return 0x60D080FF;
}

u32 withAlpha(u32 rgba, float a) {
    const u32 aa = (u32)((a < 0 ? 0 : (a > 1 ? 1 : a)) * 255.0f + 0.5f);
    return (rgba & 0xFFFFFF00) | aa;
}

u64 cpuUs() { return ticks_to_microsecs(gettime()); }

void printCentred(const render::Font& font, float cx, float y, u32 colour, float scale, const char* text) {
    const float w = std::strlen(text) * render::Font::kGlyphW * scale;
    font.print(cx - w / 2, y, colour, scale, "%s", text);
}

}  // namespace

Gameplay::Gameplay(wii::Video& video, const wii::Input& input, const render::ArrowArt& art,
                   const render::Font& font)
    : video_(video), input_(input), art_(art), font_(font) {
    loadSong();
}

void Gameplay::loadSong() {
    const std::string path = kSongPath;
    song_dir_ = path.substr(0, path.rfind('/') + 1);
    if (!ddr::LoadSongFile(path, song_)) {
        error_ = std::string("can't load ") + path;
        state_ = State::Error;
        return;
    }
    for (size_t i = 0; i < song_.charts.size(); i++)
        if (song_.charts[i].stepsType == "dance-single" && !song_.charts[i].notes.empty())
            charts_.push_back((int)i);
    if (charts_.empty()) {
        error_ = "no dance-single charts in " + path;
        state_ = State::Error;
        return;
    }
    // One player for now, on port 1. 2P adds a second entry at x = 160 / 480 (doc 06 §11.3).
    players_.resize(1);
    players_[0].port = 0;
    players_[0].centre_x = 320;
    state_ = State::Ready;
}

void Gameplay::start() {
    end_s_ = 0;
    for (Player& p : players_) {
        const ddr::Chart& c = song_.charts[charts_[p.chart]];
        p.judge.load(c.notes, c.numTracks);
        p.steps = game::MakeAutoplay(p.judge);
        p.next_step = 0;
        p.first_visible = 0;
        p.row_grade = game::Grade::None;
        p.note_offset_us = p.row_grade_us = 0;
        std::fill(std::begin(p.lane_flash_us), std::end(p.lane_flash_us), 0);
        for (const game::Judge::NoteState& n : p.judge.notes()) end_s_ = std::max(end_s_, n.end_time);
    }
    end_s_ += kEndPadS;

    stream_.close();
    clock_.reset();
    last_tick_report_ = 0;
    chart_s_ = 0;
    if (!stream_.open((song_dir_ + song_.music).c_str())) {
        error_ = std::string(song_.music) + ": " + stream_.error();
        state_ = State::Error;
        return;
    }
    stream_.play();
    state_ = State::Playing;
    video_.resetStats();   // the preload may have cost a frame
}

void Gameplay::stop(State next) {
    stream_.close();
    state_ = next;
}

bool Gameplay::frame() {
    const u16 down = input_.down(0), held = input_.held(0);
    const u64 now_us = cpuUs();

    if (state_ == State::Playing) {
        if ((down & PAD_BUTTON_START) && !(held & PAD_TRIGGER_Z)) {
            stop(State::Ready);
        } else {
            const wii::AudioStream::TickReport t = stream_.tickReport();
            if (t.count != last_tick_report_) {
                last_tick_report_ = t.count;
                clock_.observe(ticks_to_microsecs(t.tick),
                               (s64)t.ticks * wii::AudioStream::kTickUsNum / wii::AudioStream::kTickUsDen);
            }
            if (clock_.started()) {
                // Steps first (they happened before now), then everything up to now.
                handleEvents(clock_.offsetUs());
                chart_s_ = clock_.now(now_us) / 1e6 + offset_s_;
                bool all_done = true;
                for (Player& p : players_) {
                    if (p.autoplay) {
                        while (p.next_step < p.steps.size() && p.steps[p.next_step].time <= chart_s_) {
                            const game::AutoStep& s = p.steps[p.next_step++];
                            if (s.pressed)
                                p.judge.press(s.lane, s.time);
                            else
                                p.judge.release(s.lane, s.time);
                        }
                    }
                    p.judge.update(chart_s_);
                    drainJudge(p, now_us);
                    all_done = all_done && p.judge.finished();
                }
                if ((all_done && chart_s_ >= end_s_) || stream_.stats().finished) stop(State::Done);
            }
        }
    } else {
        handleMenu(down);
    }

    render::rect(0, 0, video_.width(), video_.height(), 0x101828FF);
    if (state_ != State::Error) {
        for (Player& p : players_) {
            const ddr::TimingData& td = song_.TimingFor(song_.charts[charts_[p.chart]]);
            drawPlayer(p, td.GetBeatFromElapsedTime(chart_s_), now_us);
        }
    }
    drawHud(chart_s_);

    quit_frames_ = ((held & PAD_BUTTON_START) && (held & PAD_TRIGGER_Z)) ? quit_frames_ + 1 : 0;
    return quit_frames_ < 120;
}

void Gameplay::handleMenu(u16 down) {
    if (state_ == State::Error) return;
    Player& p = players_[0];
    if (state_ == State::Done) {
        if (down & (PAD_BUTTON_START | PAD_BUTTON_A)) state_ = State::Ready;
        return;
    }
    const int n = (int)charts_.size();
    if (down & PAD_BUTTON_UP) p.chart = (p.chart + n - 1) % n;
    if (down & PAD_BUTTON_DOWN) p.chart = (p.chart + 1) % n;
    if (down & PAD_BUTTON_LEFT) offset_s_ -= kOffsetStepS;
    if (down & PAD_BUTTON_RIGHT) offset_s_ += kOffsetStepS;
    if (down & PAD_TRIGGER_Z) p.autoplay = !p.autoplay;
    if (down & (PAD_BUTTON_A | PAD_BUTTON_START)) start();
}

// Lane events carry the CPU time they were sampled at; SongClock's map turns that into
// song time, then the global offset into chart time (doc 06 §3.2).
void Gameplay::handleEvents(double offset_us) {
    for (int i = 0; i < input_.eventCount(); i++) {
        const input::LaneEvent& e = input_.event(i);
        const double t = (e.time_us + offset_us) / 1e6 + offset_s_;
        for (Player& p : players_) {
            if (p.autoplay || p.port != e.port) continue;
            if (e.pressed)
                p.judge.press(e.lane, t);
            else
                p.judge.release(e.lane, t);
        }
    }
}

void Gameplay::drainJudge(Player& p, u64 now_us) {
    for (const game::JudgeEvent& e : p.judge.events()) {
        switch (e.kind) {
            case game::JudgeEvent::kNote:
            case game::JudgeEvent::kEarly:
                if (e.grade == game::Grade::Miss) break;
                p.note_offset = e.offset;
                p.note_early_only = e.kind == game::JudgeEvent::kEarly;
                p.note_offset_us = now_us;
                if (e.lane >= 0 && e.lane < game::Judge::kMaxLanes) {
                    p.lane_flash[e.lane] = e.grade;
                    p.lane_flash_us[e.lane] = now_us;
                }
                break;
            case game::JudgeEvent::kRow:
                p.row_grade = e.grade;
                p.row_grade_us = now_us;
                break;
            case game::JudgeEvent::kHold:
                break;
        }
    }
    p.judge.events().clear();
}

void Gameplay::drawPlayer(Player& p, double beat, u64 now_us) {
    const float h = video_.height();
    const float half = kArt / 2.0f;   // GRRLIB positions by the unscaled top-left
    const float ppb = kSpeed * kLane;
    auto laneX = [&](int lane) { return p.centre_x + (lane - 1.5f) * kLane; };
    auto beatY = [&](double b) { return kReceptorY + (float)((b - beat) * ppb); };
    const u16 held = input_.held(p.port);

    // Lane channel.
    const float field_w = 4.5f * kLane;
    render::rect(p.centre_x - field_w / 2, 0, field_w, h, 0x00000070);

    // Receptors, lit while the lane is down, with a glow in the grade's colour after a hit.
    for (int lane = 0; lane < 4; lane++) {
        const float x = laneX(lane) - half, y = kReceptorY - half;
        const bool pressed = p.autoplay ? p.judge.laneDown(lane) : (held & kLaneButton[lane]) != 0;
        const float s = pressed ? 0.9f : 1.0f;
        render::img(x, y, art_.receptor, kLaneDegrees[lane], s, s, pressed ? 0xFFFFFFFF : 0xB0B0B0FF);
        const u64 since = now_us - p.lane_flash_us[lane];
        if (p.lane_flash_us[lane] && since < kFlashUs) {
            const float k = 1.0f - (float)since / kFlashUs;
            render::img(x, y, art_.glow, 0, 1.2f + 0.4f * (1 - k), 1.2f + 0.4f * (1 - k),
                        withAlpha(gradeColour(p.lane_flash[lane]), 0.8f * k));
        }
    }

    // Notes, scrolling up by beat (doc 06 §3.2). Notes are in row order; holds can reach
    // further, so first_visible only moves past notes whose end is off the top.
    const std::vector<game::Judge::NoteState>& notes = p.judge.notes();
    const double beat_min = beat - (kReceptorY + kArt) / ppb;
    while (p.first_visible < notes.size() && notes[p.first_visible].end_row / 48.0 < beat_min)
        p.first_visible++;
    for (size_t i = p.first_visible; i < notes.size(); i++) {
        const game::Judge::NoteState& n = notes[i];
        if (n.lane < 0 || n.lane >= 4) continue;
        const double b0 = n.row / 48.0;
        if (beatY(b0) > h + kArt) break;
        const bool missed = n.grade == game::Grade::Miss;
        const float x = laneX(n.lane) - half;
        if (!n.hold) {
            if (n.grade != game::Grade::None && !missed) continue;   // hit: gone
            render::img(x, beatY(b0) - half, art_.note, kLaneDegrees[n.lane], 1, 1,
                        missed ? 0x707070A0 : quantColour(n.row));
            continue;
        }
        if (n.hold_result == game::HoldResult::Held) continue;
        const bool dead = missed || n.hold_result == game::HoldResult::LetGo;
        const bool active = !dead && n.grade != game::Grade::None;   // head hit, still going
        const double hb = active ? std::max(b0, beat) : b0;
        const float yh = beatY(hb), yt = beatY(n.end_row / 48.0);
        if (yt < -kArt) continue;
        if (yt > yh) {
            const u32 body = dead ? 0x606060A0 : (active && p.judge.laneDown(n.lane)) ? 0x90FF90FF : 0x50B050FF;
            render::img(x, (yh + yt) / 2 - half, art_.hold, 0, 1, (yt - yh) / kArt, body);
        }
        render::img(x, yt - half, art_.glow, 0, 0.6f, 0.6f, dead ? 0x606060A0 : 0x80FF80C0);
        render::img(x, yh - half, art_.note, kLaneDegrees[n.lane], 1, 1,
                    dead ? 0x707070A0 : quantColour(n.row));
    }

    // Judgement, ms error and combo.
    char buf[48];
    if (p.row_grade != game::Grade::None && now_us - p.row_grade_us < kGradeShowUs)
        printCentred(font_, p.centre_x, 196, gradeColour(p.row_grade), 2, game::GradeName(p.row_grade));
    if (p.note_offset_us && now_us - p.note_offset_us < kGradeShowUs) {
        const double ms = p.note_offset * 1000;
        std::snprintf(buf, sizeof buf, "%+.1f ms %s%s", ms, ms < 0 ? "early" : "late",
                      p.note_early_only ? " (kept)" : "");
        printCentred(font_, p.centre_x, 232, 0xD0D0D0FF, 1, buf);
    }
    if (state_ != State::Ready && p.judge.score().combo >= 2) {
        std::snprintf(buf, sizeof buf, "%d combo", p.judge.score().combo);
        printCentred(font_, p.centre_x, 252, 0xFFFFFFFF, 2, buf);
    }
}

void Gameplay::drawHud(double chart_s) {
    GRRLIB_SetAntiAliasing(false);
    const u32 white = 0xFFFFFFFF, grey = 0xB0B0B0FF, warn = 0xFFC040FF, bad = 0xFF5050FF;
    const u32 dropped = video_.stats().dropped_total;
    // Text stays in the title-safe area (x 64-576, y 48-432; doc 06 §11.3).
    if (state_ == State::Error) {
        font_.print(64, 200, bad, 1, "%s", error_.c_str());
        font_.print(64, 220, grey, 1, "Run tools/stage-sd.sh and copy sd/ to the card.");
        GRRLIB_SetAntiAliasing(true);
        return;
    }
    const Player& p = players_[0];
    const ddr::Chart& c = song_.charts[charts_[p.chart]];
    const game::Score& s = p.judge.score();

    font_.print(64, 48, white, 1, "%s", song_.title.c_str());
    font_.print(64, 64, grey, 1, "%s %d", ddr::DifficultyToString(c.difficulty), c.meter);
    font_.print(64, 80, p.autoplay ? warn : grey, 1, "%s", p.autoplay ? "AUTOPLAY" : "P1 port 1");
    if (state_ != State::Ready) font_.print(472, 48, white, 2, "%6.2f%%", s.percent());

    // Judgement counts (rows), holds, and the timing stats for calibration.
    static const game::Grade kShown[] = {game::Grade::Marvelous, game::Grade::Perfect, game::Grade::Great,
                                         game::Grade::Good, game::Grade::Boo, game::Grade::Miss};
    float y = 120;
    if (state_ != State::Ready) {   // the ready screen would show the previous run's
        for (game::Grade g : kShown) {
            font_.print(472, y, gradeColour(g), 1, "%-9s%4d", game::GradeName(g), s.rows[(int)g]);
            y += 16;
        }
        font_.print(472, y + 8, white, 1, "OK %d NG %d", s.holds_held, s.holds_let_go);
        font_.print(472, y + 24, white, 1, "max combo %d", s.max_combo);
        font_.print(472, y + 48, grey, 1, "mean %+.1f ms", s.meanOffset() * 1000);
        font_.print(472, y + 64, grey, 1, "sd   %.1f ms", s.offsetStdDev() * 1000);
        if (s.held_misses) font_.print(472, y + 80, warn, 1, "held miss %d", s.held_misses);
    }

    if (state_ == State::Ready) {
        font_.print(64, 120, white, 1, "Charts:");
        for (size_t i = 0; i < charts_.size(); i++) {
            const ddr::Chart& ci = song_.charts[charts_[i]];
            font_.print(64, 140 + 16 * i, (int)i == p.chart ? warn : grey, 1, "%c %s %d",
                        (int)i == p.chart ? '>' : ' ', ddr::DifficultyToString(ci.difficulty), ci.meter);
        }
        font_.print(64, 384, white, 1, "A/START play  Up/Down chart  Z autoplay");
        font_.print(64, 400, white, 1, "Left/Right offset %+.0f ms (lower it if steps are late)",
                    offset_s_ * 1000);
    } else if (state_ == State::Done) {
        font_.print(64, 384, white, 1, "Done: %.2f%%, max combo %d, mean %+.1f ms, sd %.1f ms",
                    s.percent(), s.max_combo, s.meanOffset() * 1000, s.offsetStdDev() * 1000);
        font_.print(64, 400, grey, 1, "A/START back");
    } else {
        const wii::AudioStream::Stats st = stream_.stats();
        font_.print(64, 400, st.underruns ? bad : grey, 1, "%6.2f s  offset %+.0f ms  underruns %lu",
                    chart_s, offset_s_ * 1000, (unsigned long)st.underruns);
    }
    font_.print(64, 416, dropped ? warn : grey, 1, "%s  dropped %lu  B next test  START+Z 2 s exit",
                video_.modeName(), (unsigned long)dropped);
    GRRLIB_SetAntiAliasing(true);
}

}  // namespace screens
