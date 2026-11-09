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


constexpr float kArt = render::ArrowArt::kSize;
constexpr float kLane = 64.0f;          // lane spacing, 1-2 players (doc 06 §11.3)
constexpr float kReceptorY = 80.0f;     // inside the title-safe area (y >= 48)
constexpr float kLifeY = 30, kLifeH = 12;   // life bar above the receptors (action-safe)
constexpr u64 kFailedEndUs = 1500000;   // everyone failed: show it this long, then results
constexpr double kEndPadS = 2.0;        // stay on the playfield after the last note
constexpr u64 kFlashUs = 150000;        // receptor glow after a hit
constexpr u64 kGradeShowUs = 900000;    // judgement text
constexpr u64 kHintUs = 1600000;        // "Press START to continue"
constexpr u32 kPlayerColour[2] = {0xFF47B3FF, 0xFF5D47FF};   // as on Players and song select
constexpr u32 kBox = 0x1E282FFF, kDark = 0x0A141BFF, kMenuGrey = 0x9AA4ABFF;

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

// Grade colours: stars gold, S cyan, A green, B blue, C purple, D grey, failed red.
u32 tierColour(int tier) {
    if (tier == 0) return 0xFF4040FF;
    if (tier <= 4) return 0xFFE060FF;
    if (tier <= 7) return 0x80F0FFFF;
    if (tier <= 10) return 0x60F060FF;
    if (tier <= 13) return 0x60A0FFFF;
    if (tier <= 16) return 0xC070FFFF;
    return 0xB0B0B0FF;
}

const game::Grade kShown[] = {game::Grade::Marvelous, game::Grade::Perfect, game::Grade::Great,
                              game::Grade::Good, game::Grade::Boo, game::Grade::Miss};

void printCentred(const render::Font& font, float cx, float y, u32 colour, float scale, const char* text) {
    const float w = std::strlen(text) * render::Font::kGlyphW * scale;
    font.print(cx - w / 2, y, colour, scale, "%s", text);
}

}  // namespace

Gameplay::Gameplay(wii::Video& video, const wii::Input& input, const render::ArrowArt& art,
                   const render::Font& font, Session& session)
    : video_(video), input_(input), art_(art), font_(font), session_(session),
      offset_s_(session.config.globalOffsetS) {
    if (loadSong(session)) start();
}

bool Gameplay::loadSong(const Session& session) {
    const ddr::SongEntry* entry = session.chosen();
    if (!entry || session.players.empty()) {
        error_ = "no song or no players";
        return false;
    }
    song_dir_ = entry->dir;
    music_ = entry->music;
    if (!ddr::LoadSongFile(entry->SimfilePath(), song_)) {
        error_ = "can't load " + entry->SimfilePath();
        return false;
    }
    song_.SetGroupOffset(session.library.packs[session.pack].info.syncOffset);
    for (const SessionPlayer& sp : session.players) {
        Player p;
        p.port = sp.port;
        p.autoplay = sp.autoplay;
        p.bot = sp.bot;
        p.input_offset_s = session.config.ports[sp.port].inputOffsetS;
        p.speed = sp.speed;
        p.life_bar = sp.life_bar;
        const int ci = sp.chart < (int)entry->charts.size() ? entry->charts[sp.chart].index : -1;
        if (ci < 0 || ci >= (int)song_.charts.size() || song_.charts[ci].notes.empty()) {
            error_ = "chart missing in " + entry->SimfilePath();
            return false;
        }
        p.chart = ci;
        p.score_key = game::ChartKey(*entry, entry->charts[sp.chart]);
        if (const game::HighScoreEntry* best = session.scores.find(p.score_key)) {
            p.had_best = true;
            p.prev_best = best->best;
        }
        players_.push_back(std::move(p));
    }
    layout();
    return true;
}

void Gameplay::start() {
    end_s_ = 0;
    for (Player& p : players_) {
        const ddr::Chart& c = song_.charts[p.chart];
        p.judge.load(c.notes, c.numTracks);
        p.steps = game::MakeAutoplay(p.judge);
        p.next_step = 0;
        p.first_visible = 0;
        p.row_grade = game::Grade::None;
        p.note_offset_us = p.row_grade_us = 0;
        std::fill(std::begin(p.lane_flash_us), std::end(p.lane_flash_us), 0);
        std::fill(std::begin(p.mine_flash_us), std::end(p.mine_flash_us), 0);
        p.life = game::LifeMeter(p.life_bar == ddr::LifeBar::On);   // NoFail: FailType Off
        p.failed = false;
        for (const game::Judge::NoteState& n : p.judge.notes()) end_s_ = std::max(end_s_, n.end_time);
        for (const game::Judge::MineState& m : p.judge.mines()) end_s_ = std::max(end_s_, m.time);
    }
    all_failed_us_ = 0;
    paused_ = false;
    end_s_ += kEndPadS;

    stream_.close();
    clock_.reset();
    last_tick_report_ = 0;
    chart_s_ = 0;
    if (!stream_.open((song_dir_ + music_).c_str())) {
        error_ = music_ + ": " + stream_.error();
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

void Gameplay::finish() {
    stop(State::Done);
    for (Player& p : players_) {
        if (p.autoplay) continue;
        const game::Score& s = p.judge.score();
        p.new_best = session_.scores.submit(p.score_key,
                                            game::HighScore{s.percentX100(), p.failed, s.max_combo});
        session_.scores_dirty = true;
    }
}

bool Gameplay::startPressed(int port) const {
    return (input_.down(port) & PAD_BUTTON_START) && !(input_.held(port) & PAD_TRIGGER_Z);
}

// The music and the song clock stop together: ASND's tick counter for a paused voice stops,
// and SongClock freezes until the first tick after the resume (doc 06 §4). Nothing is judged
// while paused, and lane events from the pause are dropped.
void Gameplay::pause(int by, u64 now_us) {
    stream_.pause();
    clock_.pause(now_us);
    if (clock_.started()) chart_s_ = clock_.now(now_us) / 1e6 + offset_s_;
    paused_ = true;
    pause_by_ = by;
    pause_cursor_ = 0;
    pause_us_ = now_us;
    hint_us_ = 0;
}

void Gameplay::resume(u64 now_us) {
    stream_.resume();
    clock_.resume(now_us);
    paused_ = false;
    if (all_failed_us_) all_failed_us_ += now_us - pause_us_;   // the 1.5 s wait doesn't run while paused
    // A lane let go during the pause is released at the paused time, so a hold isn't still
    // held; a press during the pause is dropped (step again).
    for (Player& p : players_) {
        if (p.autoplay || p.failed) continue;
        const u16 held = input_.held(p.port);
        for (int lane = 0; lane < 4; lane++)
            if (p.judge.laneDown(lane) && !(held & kLaneButton[lane])) p.judge.release(lane, chart_s_);
    }
}

// Anyone's START continues. Only the player who paused moves the cursor and picks with A;
// A or B on Continue, and B anywhere, explain how to continue (doc 06 §7).
Gameplay::PauseAction Gameplay::pauseInput(u64 now_us) {
    for (const Player& p : players_) {
        if (!p.bot && startPressed(p.port)) {
            resume(now_us);
            return PauseAction::None;
        }
    }
    const u16 d = input_.down(players_[pause_by_].port);
    if (d & PAD_BUTTON_UP) pause_cursor_ = (pause_cursor_ + 2) % 3;
    if (d & PAD_BUTTON_DOWN) pause_cursor_ = (pause_cursor_ + 1) % 3;
    if ((d & PAD_BUTTON_B) || ((d & PAD_BUTTON_A) && pause_cursor_ == 0)) {
        hint_us_ = now_us;
    } else if (d & PAD_BUTTON_A) {
        return pause_cursor_ == 1 ? PauseAction::Restart : PauseAction::Quit;
    }
    return PauseAction::None;
}

bool Gameplay::frame() {
    const u16 held = input_.held(0);
    const u64 now_us = wii::Input::nowUs();

    if (state_ == State::Playing && paused_) {
        switch (pauseInput(now_us)) {
            case PauseAction::Quit: return false;   // back to song select, nothing recorded
            case PauseAction::Restart: start(); break;
            case PauseAction::None: break;
        }
    } else if (state_ == State::Playing) {
        int by = -1;   // START on a player's pad pauses (bots have no pad)
        for (size_t i = 0; i < players_.size() && by < 0; i++)
            if (!players_[i].bot && startPressed(players_[i].port)) by = (int)i;
        if (by >= 0) {
            pause(by, now_us);
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
                bool all_done = true, all_failed = true;
                for (Player& p : players_) {
                    all_failed = all_failed && p.failed;
                    if (p.failed) continue;   // greyed out: no more judging
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
                    all_done = all_done && (p.failed || p.judge.finished());
                }
                if (all_failed && !all_failed_us_) all_failed_us_ = now_us;
                // Audio over: keep chart time running on the CPU clock (M4c open item 1).
                if (stream_.stats().finished) clock_.freeRun();
                if ((all_done && chart_s_ >= end_s_) ||
                    (all_failed_us_ && now_us - all_failed_us_ > kFailedEndUs))
                    finish();
            }
        }
    } else {
        // Results or error: A/START (or B on an error) on any port goes back to song select.
        for (int port = 0; port < wii::Input::kPorts; port++)
            if ((input_.down(port) & (PAD_BUTTON_START | PAD_BUTTON_A)) && !(input_.held(port) & PAD_TRIGGER_Z))
                return false;
    }

    render::rect(0, 0, video_.width(), video_.height(), 0x101828FF);
    if (state_ == State::Playing) {
        for (Player& p : players_) {
            const ddr::TimingData& td = song_.TimingFor(song_.charts[p.chart]);
            drawPlayer(p, td.GetBeatFromElapsedTime(chart_s_), now_us);
        }
    }
    GRRLIB_SetAntiAliasing(false);   // the bitmap font is drawn without filtering
    if (state_ == State::Playing) {
        drawHud(chart_s_);
        if (paused_) drawPause(now_us);
    } else if (state_ == State::Done) {
        drawResults();
    } else {
        font_.print(64, 200, 0xFF5050FF, 1, "%s", error_.c_str());
        font_.print(64, 220, 0xB0B0B0FF, 1, "A/START back");
    }
    GRRLIB_SetAntiAliasing(true);

    quit_frames_ = ((held & PAD_BUTTON_START) && (held & PAD_TRIGGER_Z)) ? quit_frames_ + 1 : 0;
    return quit_frames_ < 120;
}

// doc 06 §11.3: 1P centred, 2P versus at x = 160 / 480.
void Gameplay::layout() {
    if (players_.size() == 1) {
        players_[0].centre_x = 320;
    } else {
        players_[0].centre_x = 160;
        players_[1].centre_x = 480;
    }
}

// Lane events carry the CPU time they were sampled at; SongClock's map turns that into
// song time, then the global offset into chart time (doc 06 §3.2).
void Gameplay::handleEvents(double offset_us) {
    for (int i = 0; i < input_.eventCount(); i++) {
        const input::LaneEvent& e = input_.event(i);
        const double t0 = (e.time_us + offset_us) / 1e6 + offset_s_;
        for (Player& p : players_) {
            if (p.autoplay || p.failed || p.port != e.port) continue;
            const double t = t0 + p.input_offset_s;   // per-mat InputOffset (doc 06 §11.1)
            if (e.pressed)
                p.judge.press(e.lane, t);
            else
                p.judge.release(e.lane, t);
        }
    }
}

void Gameplay::drainJudge(Player& p, u64 now_us) {
    for (const game::JudgeEvent& e : p.judge.events()) {
        if (p.life_bar != ddr::LifeBar::Off && !p.failed) {
            p.life.apply(e);
            if (p.life.failed()) {   // never with no fail: that bar keeps moving
                p.failed = true;
                p.failed_us = now_us;
            }
        }
        if (e.kind == game::JudgeEvent::kMine && e.lane >= 0 && e.lane < game::Judge::kMaxLanes)
            p.mine_flash_us[e.lane] = now_us;
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
            case game::JudgeEvent::kMine:
                break;
        }
    }
    p.judge.events().clear();
}

void Gameplay::drawPlayer(Player& p, double beat, u64 now_us) {
    const float h = video_.height();
    // ArrowArt textures have a mid handle (GRRLIB_SetMidHandle), so GRRLIB draws them centred
    // on the position given, scaled and rotated around that centre.
    const float ppb = (float)p.speed * kLane;   // ITG: 1x = 64 px per beat
    auto laneX = [&](int lane) { return p.centre_x + (lane - 1.5f) * kLane; };
    auto beatY = [&](double b) { return kReceptorY + (float)((b - beat) * ppb); };
    const u16 held = input_.held(p.port);

    // Lane channel.
    const float field_w = 4 * kLane;   // 2P: x 32-288 and 352-608, inside action-safe
    render::rect(p.centre_x - field_w / 2, 0, field_w, h, 0x00000070);

    // Receptors, lit while the lane is down, with a glow in the grade's colour after a hit.
    for (int lane = 0; lane < 4; lane++) {
        const float x = laneX(lane), y = kReceptorY;
        const bool pressed = p.autoplay ? p.judge.laneDown(lane) : (held & kLaneButton[lane]) != 0;
        const float s = pressed ? 0.9f : 1.0f;
        render::img(x, y, art_.receptor, kLaneDegrees[lane], s, s, pressed ? 0xFFFFFFFF : 0xB0B0B0FF);
        const u64 since = now_us - p.lane_flash_us[lane];
        if (p.lane_flash_us[lane] && since < kFlashUs) {
            const float k = 1.0f - (float)since / kFlashUs;
            render::img(x, y, art_.glow, 0, 1.2f + 0.4f * (1 - k), 1.2f + 0.4f * (1 - k),
                        withAlpha(gradeColour(p.lane_flash[lane]), 0.8f * k));
        }
        const u64 msince = now_us - p.mine_flash_us[lane];   // a mine went off: white burst
        if (p.mine_flash_us[lane] && msince < 2 * kFlashUs) {
            const float k = 1.0f - (float)msince / (2 * kFlashUs);
            render::img(x, y, art_.glow, 0, 2.0f - k, 2.0f - k, withAlpha(0xFFFFFFFF, k));
        }
    }

    // Mines still to come: a red blob with a white core.
    for (const game::Judge::MineState& m : p.judge.mines()) {
        if (m.state != game::Judge::MineState::kPending || m.lane < 0 || m.lane >= 4) continue;
        const float y = beatY(m.row / 48.0);
        if (y < -kArt) continue;
        if (y > h + kArt) break;
        render::img(laneX(m.lane), y, art_.glow, 0, 0.8f, 0.8f, 0xFF3030F0);
        render::rect(laneX(m.lane) - 5, y - 5, 10, 10, 0xFFFFFFFF);
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
        const float x = laneX(n.lane);
        if (!n.hold) {
            if (n.grade != game::Grade::None && !missed) continue;   // hit: gone
            render::img(x, beatY(b0), art_.note, kLaneDegrees[n.lane], 1, 1,
                        missed ? 0x707070A0 : quantColour(n.row));
            continue;
        }
        if (n.hold_result == game::HoldResult::Held) continue;
        const bool dead = missed || n.hold_result == game::HoldResult::LetGo;
        const bool active = !dead && n.grade != game::Grade::None;   // head hit, still going
        const double hb = active ? std::max(b0, beat) : b0;
        const float yh = beatY(hb), yt = beatY(n.end_row / 48.0);
        if (yt < -kArt) continue;
        if (yt - yh >= 1) {   // a sliver under 1 px (a short hold at its tail) blanked GX's textures
            const bool lit = active && p.judge.laneDown(n.lane);
            const u32 body = dead ? 0x606060A0
                             : n.roll ? (lit ? 0xFFE080FF : 0xD0A030FF)    // rolls: amber
                                      : (lit ? 0x90FF90FF : 0x50B050FF);   // holds: green
            render::img(x, (yh + yt) / 2, art_.hold, 0, 1, (yt - yh) / kArt, body);
        }
        render::img(x, yt, art_.glow, 0, 0.6f, 0.6f, dead ? 0x606060A0 : n.roll ? 0xFFD060C0 : 0x80FF80C0);
        render::img(x, yh, art_.note, kLaneDegrees[n.lane], 1, 1,
                    dead ? 0x707070A0 : quantColour(n.row));
    }

    if (p.failed) {   // greyed out (doc 06 §11.4)
        render::rect(p.centre_x - field_w / 2, 0, field_w, h, 0x101828C0);
        printCentred(font_, p.centre_x, 200, 0xFF4040FF, 3, "FAILED");
    }
    if (p.life_bar != ddr::LifeBar::Off) drawLife(p);

    // Judgement, ms error, combo and score.
    char buf[48];
    if (p.failed) {
        std::snprintf(buf, sizeof buf, "%.2f%%", p.judge.score().percent());
        printCentred(font_, p.centre_x, 340, 0xB0B0B0FF, 2, buf);
        return;
    }
    if (p.row_grade != game::Grade::None && now_us - p.row_grade_us < kGradeShowUs)
        printCentred(font_, p.centre_x, 196, gradeColour(p.row_grade), 2, game::GradeName(p.row_grade));
    if (p.note_offset_us && now_us - p.note_offset_us < kGradeShowUs) {
        const double ms = p.note_offset * 1000;
        std::snprintf(buf, sizeof buf, "%+.1f ms %s%s", ms, ms < 0 ? "early" : "late",
                      p.note_early_only ? " (kept)" : "");
        printCentred(font_, p.centre_x, 232, 0xD0D0D0FF, 1, buf);
    }
    if (p.judge.score().combo >= 2) {
        std::snprintf(buf, sizeof buf, "%d combo", p.judge.score().combo);
        printCentred(font_, p.centre_x, 252, 0xFFFFFFFF, 2, buf);
    }
    std::snprintf(buf, sizeof buf, "%.2f%%", p.judge.score().percent());
    printCentred(font_, p.centre_x, 340, 0xFFFFFFFF, 2, buf);
    printCentred(font_, p.centre_x, 374, p.autoplay ? 0xFFC040FF : 0xB0B0B0FF, 1, playerLabel(p).c_str());
}

// ITG-style life bar across the top of the playfield: green, red when in danger, white when
// full (hot). Failed: an empty outline.
void Gameplay::drawLife(const Player& p) {
    const float w = 4 * kLane - 16, x = p.centre_x - w / 2;
    render::rect(x - 2, kLifeY - 2, w + 4, kLifeH + 4, 0xFFFFFFC0);
    render::rect(x, kLifeY, w, kLifeH, 0x101828FF);
    const double life = p.life.life();
    if (life <= 0) return;
    const u32 c = p.life.hot() ? 0xF0F8FFFF : p.life.danger() ? 0xFF4040FF : 0x50D070FF;
    render::rect(x, kLifeY, (float)(w * life), kLifeH, c);
}

std::string Gameplay::playerLabel(const Player& p) const {
    const ddr::Chart& c = song_.charts[p.chart];
    char buf[64], port[24];
    std::snprintf(port, sizeof port, "port %d", p.port + 1);
    std::snprintf(buf, sizeof buf, "P%d %s %s %d", (int)(&p - players_.data()) + 1,
                  p.autoplay ? "AUTO" : port,
                  ddr::DifficultyToString(c.difficulty), c.meter);
    return buf;
}

// Text stays in the title-safe area (x 64-576, y 48-432; doc 06 §11.3).
void Gameplay::drawHud(double chart_s) {
    const u32 grey = 0xB0B0B0FF, warn = 0xFFC040FF, bad = 0xFF5050FF;
    if (players_.size() == 1) {   // room beside a centred playfield for the running counts
        const game::Score& s = players_[0].judge.score();
        font_.print(64, 48, 0xFFFFFFFF, 1, "%s", song_.title.c_str());
        float y = 120;
        for (game::Grade g : kShown) {
            font_.print(472, y, gradeColour(g), 1, "%-9s%4d", game::GradeName(g), s.rows[(int)g]);
            y += 16;
        }
        font_.print(472, y + 8, 0xFFFFFFFF, 1, "OK %d NG %d", s.holds_held, s.holds_let_go);
        font_.print(472, y + 24, 0xFFFFFFFF, 1, "max combo %d", s.max_combo);
        font_.print(472, y + 48, grey, 1, "mean %+.1f ms", s.meanOffset() * 1000);
        font_.print(472, y + 64, grey, 1, "sd   %.1f ms", s.offsetStdDev() * 1000);
        if (s.held_misses) font_.print(472, y + 80, warn, 1, "held miss %d", s.held_misses);
    }
    if (paused_) return;   // the pause menu's help strip goes there
    const wii::AudioStream::Stats st = stream_.stats();
    const u32 dropped = video_.stats().dropped_total;
    font_.print(64, 400, st.underruns ? bad : grey, 1, "%6.2f s  offset %+.0f ms  underruns %lu",
                chart_s, offset_s_ * 1000, (unsigned long)st.underruns);
    font_.print(64, 416, dropped ? warn : grey, 1, "%s  dropped %lu  START pause", video_.modeName(),
                (unsigned long)dropped);
}

// The pause box, as in the menu prototype: the paused player's colour on top, a row per choice
// with the key that picks it, and the help strip at the bottom.
void Gameplay::drawPause(u64 now_us) {
    const u32 white = 0xFFFFFFFF, black = 0x000000FF;
    const u32 pc = kPlayerColour[pause_by_ & 1];
    render::rect(0, 0, video_.width(), video_.height(), 0x00000099);
    render::rect(200, 140, 240, 200, kBox);
    render::rect(200, 140, 240, 4, pc);
    printCentred(font_, 320, 156, white, 2, "PAUSED");
    static const char* const kRows[3][2] = {{"Continue", "START"}, {"Restart", "A"}, {"Quit", "A"}};
    const float pulse = 0.6f + 0.4f * std::sin(now_us / 1e6f * 6);
    for (int k = 0; k < 3; k++) {
        const float y = 196 + k * 40;
        const bool sel = k == pause_cursor_;
        render::rect(232, y, 176, 36, sel ? pc : kDark);
        if (sel) render::rect(228, y, 4, 36, 0xFFFFFF00 | (u32)(255 * pulse));
        font_.print(248, y + 10, sel ? black : white, 1, "%s", kRows[k][0]);
        const float kw = std::strlen(kRows[k][1]) * render::Font::kGlyphW;
        font_.print(392 - kw, y + 10, sel ? black : kMenuGrey, 1, "%s", kRows[k][1]);
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, "paused by P%d", pause_by_ + 1);
    printCentred(font_, 320, 320, kMenuGrey, 1, buf);
    font_.print(48, 400, kMenuGrey, 1, "Anyone: START continues");
    font_.print(48, 416, kMenuGrey, 1, "P%d: Up/Down choose   A restart or quit", pause_by_ + 1);
    if (hint_us_ && now_us - hint_us_ < kHintUs) {
        render::rect(112, 440, 416, 28, 0x000000E0);
        render::rect(112, 440, 4, 28, 0xFFBE00FF);
        printCentred(font_, 320, 446, white, 1, "Press START to continue");
    }
}

// Results (doc 06 §7): one column per player.
void Gameplay::drawResults() {
    const u32 white = 0xFFFFFFFF, grey = 0xB0B0B0FF, warn = 0xFFC040FF;
    font_.print(64, 48, white, 2, "%s", song_.title.c_str());
    for (const Player& p : players_) {
        const game::Score& s = p.judge.score();
        const float x = p.centre_x - 88;
        char buf[32];
        printCentred(font_, p.centre_x, 92, p.autoplay ? warn : grey, 1, playerLabel(p).c_str());
        const int tier = game::GradeTier(s.percentX100(), p.failed);
        printCentred(font_, p.centre_x, 106, tierColour(tier), 3,
                     p.failed ? "FAILED" : game::GradeTierName(tier));
        std::snprintf(buf, sizeof buf, "%.2f%%", s.percent());
        printCentred(font_, p.centre_x, 156, p.failed ? 0xFF4040FF : white, 2, buf);
        char rec[48];
        u32 rec_colour = grey;
        if (p.autoplay) {
            std::snprintf(rec, sizeof rec, "autoplay, not saved");
        } else if (p.new_best && p.had_best) {
            std::snprintf(rec, sizeof rec, "New best! (was %.2f%%)", p.prev_best.percent_x100 / 100.0);
            rec_colour = 0xFFE060FF;
        } else if (p.new_best) {
            std::snprintf(rec, sizeof rec, "New best!");
            rec_colour = 0xFFE060FF;
        } else {
            std::snprintf(rec, sizeof rec, "best %.2f%% %s", p.prev_best.percent_x100 / 100.0,
                          game::GradeTierName(p.prev_best.tier()));
        }
        printCentred(font_, p.centre_x, 190, rec_colour, 1, rec);
        float y = 210;
        for (game::Grade g : kShown) {
            font_.print(x, y, gradeColour(g), 1, "%-10s%5d", game::GradeName(g), s.rows[(int)g]);
            y += 16;
        }
        font_.print(x, y, white, 1, "%-10s%5d", "Held", s.holds_held);
        font_.print(x, y + 16, white, 1, "%-10s%5d", "Let go", s.holds_let_go);
        font_.print(x, y + 32, white, 1, "%-10s%5d", "Mines hit", s.mines_hit);
        font_.print(x, y + 48, white, 1, "%-10s%5d", "Max combo", s.max_combo);
        font_.print(x, y + 66, grey, 1, "mean %+6.1f ms", s.meanOffset() * 1000);
        font_.print(x, y + 82, grey, 1, "sd   %6.1f ms", s.offsetStdDev() * 1000);
        if (s.held_misses) font_.print(x, y + 98, warn, 1, "held misses %d", s.held_misses);
    }
    font_.print(64, 416, grey, 1, "A/START back");
}

}  // namespace screens
