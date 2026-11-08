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

constexpr const char* kSongsRoot = "sd:/ddrwii/songs";
constexpr const char* kTestSongFolder = "Click 120";   // starts here if found

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

const game::Grade kShown[] = {game::Grade::Marvelous, game::Grade::Perfect, game::Grade::Great,
                              game::Grade::Good, game::Grade::Boo, game::Grade::Miss};

void printCentred(const render::Font& font, float cx, float y, u32 colour, float scale, const char* text) {
    const float w = std::strlen(text) * render::Font::kGlyphW * scale;
    font.print(cx - w / 2, y, colour, scale, "%s", text);
}

}  // namespace

Gameplay::Gameplay(wii::Video& video, const wii::Input& input, const render::ArrowArt& art,
                   const render::Font& font)
    : video_(video), input_(input), art_(art), font_(font) {
    // P1 on port 1; P2 joins on the ready screen.
    players_.resize(1);
    players_[0].port = 0;
    layout();
    scanLibrary();
    if (!songs_.empty()) loadSong();
}

void Gameplay::scanLibrary() {
    const u64 t0 = cpuUs();
    if (!ddr::ScanSongLibrary(kSongsRoot, library_)) {
        error_ = std::string("can't open ") + kSongsRoot;
        return;
    }
    scan_us_ = cpuUs() - t0;
    for (size_t p = 0; p < library_.packs.size(); p++)
        for (size_t s = 0; s < library_.packs[p].songs.size(); s++) {
            if (library_.packs[p].songs[s].folder == kTestSongFolder) song_index_ = (int)songs_.size();
            songs_.push_back({(int)p, (int)s});
        }
    if (songs_.empty()) error_ = std::string("no playable songs in ") + kSongsRoot;
}

void Gameplay::loadSong() {
    const ddr::PackEntry& pack = library_.packs[songs_[song_index_].pack];
    const ddr::SongEntry& entry = pack.songs[songs_[song_index_].song];
    song_ = ddr::Song();
    charts_.clear();
    song_dir_ = entry.dir;
    music_ = entry.music;
    if (!ddr::LoadSongFile(entry.SimfilePath(), song_)) {
        error_ = "can't load " + entry.SimfilePath();
        state_ = State::Error;
        return;
    }
    song_.SetGroupOffset(pack.info.syncOffset);
    for (const ddr::ChartInfo& c : entry.charts)
        if (c.index < (int)song_.charts.size()) charts_.push_back(c.index);
    if (charts_.empty()) {
        error_ = "no dance-single charts in " + entry.SimfilePath();
        state_ = State::Error;
        return;
    }
    for (Player& p : players_) p.chart = std::min(p.chart, (int)charts_.size() - 1);
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

bool Gameplay::frame() {
    const u16 held = input_.held(0);
    const u64 now_us = cpuUs();

    if (state_ == State::Playing) {
        bool quit = false;   // START on any joined port (not START+Z, the exit chord)
        for (const Player& p : players_)
            quit = quit || ((input_.down(p.port) & PAD_BUTTON_START) && !(input_.held(p.port) & PAD_TRIGGER_Z));
        if (quit) {
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
        handleMenu();
    }

    render::rect(0, 0, video_.width(), video_.height(), 0x101828FF);
    if (state_ == State::Playing) {
        for (Player& p : players_) {
            const ddr::TimingData& td = song_.TimingFor(song_.charts[charts_[p.chart]]);
            drawPlayer(p, td.GetBeatFromElapsedTime(chart_s_), now_us);
        }
    }
    GRRLIB_SetAntiAliasing(false);   // the bitmap font is drawn without filtering
    if (state_ == State::Playing) {
        drawHud(chart_s_);
    } else if (state_ == State::Ready) {
        drawReady();
    } else if (state_ == State::Done) {
        drawResults();
    } else {
        font_.print(64, 200, 0xFF5050FF, 1, "%s", error_.c_str());
        font_.print(64, 220, 0xB0B0B0FF, 1, "Run tools/stage-sd.sh and copy sd/ to the card.");
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

// Ready screen (doc 06 §11.2, simplified for M4c):
//   any joined port: Up/Down its chart, Z its autoplay;
//   port 1: Left/Right global offset, Y adds/removes an autoplay P2 (port 2), X changes
//           that P2's chart (for testing with one pad or the keyboard);
//   port 2: START joins as a human P2;
//   A or START on a joined port plays.
void Gameplay::handleMenu() {
    const u16 down0 = input_.down(0);
    // L/R on port 1: previous/next song in the library (until song select, M5 step 2).
    // Also from the error screen, so one bad simfile doesn't block the rest.
    if (state_ != State::Done && (down0 & (PAD_TRIGGER_L | PAD_TRIGGER_R)) && songs_.size() > 1) {
        const int m = (int)songs_.size();
        song_index_ = (song_index_ + ((down0 & PAD_TRIGGER_L) ? m - 1 : 1)) % m;
        loadSong();
        return;
    }
    if (state_ == State::Error) return;
    if (state_ == State::Done) {
        for (const Player& p : players_)
            if (input_.down(p.port) & (PAD_BUTTON_START | PAD_BUTTON_A)) state_ = State::Ready;
        return;
    }
    const int n = (int)charts_.size();
    if (players_.size() == 1 && (input_.down(1) & PAD_BUTTON_START)) {
        players_.push_back(Player());
        players_[1].port = 1;
        players_[1].chart = std::min(players_[0].chart + 1, n - 1);
        layout();
        return;   // the join press doesn't also start the song
    }
    if (down0 & PAD_BUTTON_Y) {
        if (players_.size() == 1) {
            players_.push_back(Player());
            players_[1].port = 1;
            players_[1].autoplay = true;
            players_[1].chart = (players_[0].chart + 1) % n;
        } else {
            players_.pop_back();
        }
        layout();
    }
    if ((down0 & PAD_BUTTON_X) && players_.size() > 1) players_[1].chart = (players_[1].chart + 1) % n;
    if (down0 & PAD_BUTTON_LEFT) offset_s_ -= kOffsetStepS;
    if (down0 & PAD_BUTTON_RIGHT) offset_s_ += kOffsetStepS;
    bool play = false;
    for (Player& p : players_) {
        const u16 d = input_.down(p.port);
        if (d & PAD_BUTTON_UP) p.chart = (p.chart + n - 1) % n;
        if (d & PAD_BUTTON_DOWN) p.chart = (p.chart + 1) % n;
        if (d & PAD_TRIGGER_Z) p.autoplay = !p.autoplay;
        if (d & (PAD_BUTTON_A | PAD_BUTTON_START)) play = true;
    }
    if (play) start();
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
    // ArrowArt textures have a mid handle (GRRLIB_SetMidHandle), so GRRLIB draws them centred
    // on the position given, scaled and rotated around that centre.
    const float ppb = kSpeed * kLane;
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
        if (yt > yh) {
            const u32 body = dead ? 0x606060A0 : (active && p.judge.laneDown(n.lane)) ? 0x90FF90FF : 0x50B050FF;
            render::img(x, (yh + yt) / 2, art_.hold, 0, 1, (yt - yh) / kArt, body);
        }
        render::img(x, yt, art_.glow, 0, 0.6f, 0.6f, dead ? 0x606060A0 : 0x80FF80C0);
        render::img(x, yh, art_.note, kLaneDegrees[n.lane], 1, 1,
                    dead ? 0x707070A0 : quantColour(n.row));
    }

    // Judgement, ms error, combo and score.
    char buf[48];
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

std::string Gameplay::playerLabel(const Player& p) const {
    const ddr::Chart& c = song_.charts[charts_[p.chart]];
    char buf[64];
    std::snprintf(buf, sizeof buf, "P%d %s %s %d", (int)(&p - players_.data()) + 1,
                  p.autoplay ? "AUTO" : (p.port == 0 ? "port 1" : "port 2"),
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
    const wii::AudioStream::Stats st = stream_.stats();
    const u32 dropped = video_.stats().dropped_total;
    font_.print(64, 400, st.underruns ? bad : grey, 1, "%6.2f s  offset %+.0f ms  underruns %lu",
                chart_s, offset_s_ * 1000, (unsigned long)st.underruns);
    font_.print(64, 416, dropped ? warn : grey, 1, "%s  dropped %lu  START stop", video_.modeName(),
                (unsigned long)dropped);
}

void Gameplay::drawReady() {
    const u32 white = 0xFFFFFFFF, grey = 0xB0B0B0FF, warn = 0xFFC040FF;
    const ddr::PackEntry& pack = library_.packs[songs_[song_index_].pack];
    font_.print(64, 32, grey, 1, "%s   song %d/%d   L/R change", pack.info.displayTitle.c_str(),
                song_index_ + 1, (int)songs_.size());
    font_.print(64, 48, white, 2, "%s", song_.title.c_str());
    font_.print(64, 84, grey, 1, "%s", song_.subtitle.c_str());
    for (const Player& p : players_) {
        const float x = p.centre_x - 88;
        font_.print(x, 120, white, 1, "%s", playerLabel(p).c_str());
        for (size_t i = 0; i < charts_.size(); i++) {
            const ddr::Chart& c = song_.charts[charts_[i]];
            font_.print(x, 144 + 16 * i, (int)i == p.chart ? warn : grey, 1, "%c %s %d",
                        (int)i == p.chart ? '>' : ' ', ddr::DifficultyToString(c.difficulty), c.meter);
        }
    }
    if (players_.size() == 1) font_.print(64, 300, grey, 1, "P2: START on port 2, or Y for an autoplay P2");
    font_.print(64, 336, white, 1, "A/START play   Up/Down chart   Z autoplay");
    if (players_.size() > 1) font_.print(64, 352, white, 1, "Y remove P2   X change P2's chart");
    font_.print(64, 368, white, 1, "Left/Right offset %+.0f ms (lower it if steps are late)", offset_s_ * 1000);
    font_.print(64, 400, grey, 1, "library: %d packs, %d songs, %d skipped, scan %lu ms",
                (int)library_.packs.size(), (int)songs_.size(), (int)library_.warnings.size(),
                (unsigned long)(scan_us_ / 1000));
    font_.print(64, 416, grey, 1, "%s   B next test   START+Z 2 s exit", video_.modeName());
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
        std::snprintf(buf, sizeof buf, "%.2f%%", s.percent());
        printCentred(font_, p.centre_x, 112, white, 3, buf);
        float y = 172;
        for (game::Grade g : kShown) {
            font_.print(x, y, gradeColour(g), 1, "%-10s%5d", game::GradeName(g), s.rows[(int)g]);
            y += 16;
        }
        font_.print(x, y + 8, white, 1, "%-10s%5d", "Held", s.holds_held);
        font_.print(x, y + 24, white, 1, "%-10s%5d", "Let go", s.holds_let_go);
        font_.print(x, y + 40, white, 1, "%-10s%5d", "Max combo", s.max_combo);
        font_.print(x, y + 64, grey, 1, "mean %+6.1f ms", s.meanOffset() * 1000);
        font_.print(x, y + 80, grey, 1, "sd   %6.1f ms", s.offsetStdDev() * 1000);
        if (s.held_misses) font_.print(x, y + 96, warn, 1, "held misses %d", s.held_misses);
    }
    font_.print(64, 416, grey, 1, "A/START back");
}

}  // namespace screens
