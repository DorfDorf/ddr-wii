// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/SongSelect.h"

#include <gccore.h>
#include <grrlib.h>
#include <ogc/lwp_watchdog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "platform/wii/MenuSound.h"
#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/Draw.h"
#include "render/Font.h"

namespace screens {
namespace {

// Layout (640x480 logical canvas; boxes inside action-safe x 32-608, text inside
// title-safe where it can be, doc 06 §11.3). Positions follow Simply Love's 4:3 screen.
constexpr float kLeftX = 48, kLeftW = 264;            // banner, info, grid, P1 pane
constexpr float kBannerY = 52, kBannerH = 103;        // Simply Love's 418x164 banner shape
constexpr u64 kMediaRestUs = 200000;                   // cursor rest before banner + preview
constexpr float kInfoY = 160, kInfoH = 52;
constexpr float kGridY = 218, kGridRowH = 22;
constexpr int kGridRows = 5;
constexpr float kWheelX = 328, kWheelW = 272;
constexpr float kWheelTop = 48, kRowH = 32;
constexpr int kWheelRows = 9;                         // y 48-336; the cursor row is the middle
constexpr float kPaneY = 344, kPaneH = 84;
constexpr float kPaneX[Session::kMaxPlayers] = {kLeftX, kWheelX};
constexpr float kPaneW = 264;
constexpr float kGlyph = render::Font::kGlyphW;

constexpr double kMinSpeed = 0.25, kMaxSpeed = 8.0, kSpeedStep = 0.25;   // ITG x-mods

constexpr const char* kStartFolder = "Click 120";   // the hardware checklist's test song

// Simply Love's text colours (SL.Colors) with its default colour index (3): difficulties
// Beginner..Challenge take indices 11, 12, 1, 2, 3; Edit is grey; P1 3, P2 1.
u32 difficultyColour(ddr::Difficulty d) {
    switch (d) {
        case ddr::Difficulty::Beginner: return 0xFFBE00FF;
        case ddr::Difficulty::Easy: return 0xFF7D00FF;
        case ddr::Difficulty::Medium: return 0xFF5D47FF;
        case ddr::Difficulty::Hard: return 0xFF577EFF;
        case ddr::Difficulty::Challenge: return 0xFF47B3FF;
        default: return 0xB4B7BAFF;
    }
}
constexpr u32 kPlayerColour[Session::kMaxPlayers] = {0xFF47B3FF, 0xFF5D47FF};

constexpr u32 kBg = 0x101828FF, kBox = 0x1E282FFF, kDark = 0x0A141BFF, kPackBg = 0x4C565DFF;
constexpr u32 kWhite = 0xFFFFFFFF, kGrey = 0x9AA4ABFF, kBlack = 0x000000FF;

const char* lifeBarText(ddr::LifeBar b) {
    switch (b) {
        case ddr::LifeBar::On: return "life bar on";
        case ddr::LifeBar::NoFail: return "bar, no fail";
        default: return "life bar off";
    }
}

// Prints at most max_chars characters (cut with "..").
void printFit(const render::Font& font, float x, float y, u32 colour, float scale, const std::string& s,
              int max_chars) {
    if ((int)s.size() <= max_chars) {
        font.print(x, y, colour, scale, "%s", s.c_str());
    } else {
        font.print(x, y, colour, scale, "%s..", s.substr(0, std::max(0, max_chars - 2)).c_str());
    }
}

void printCentred(const render::Font& font, float cx, float y, u32 colour, float scale, const std::string& s,
                  int max_chars) {
    const int n = std::min((int)s.size(), max_chars);
    printFit(font, cx - n * kGlyph * scale / 2, y, colour, scale, s, max_chars);
}

std::string lengthText(double s) {
    if (s <= 0) return "?";
    char buf[16];
    const int t = (int)(s + 0.5);
    std::snprintf(buf, sizeof buf, "%d:%02d", t / 60, t % 60);
    return buf;
}

std::string bpmText(const ddr::SongEntry& s) {
    if (s.bpmRandom) return "???";
    char buf[24];
    if (std::lround(s.bpmMin) == std::lround(s.bpmMax))
        std::snprintf(buf, sizeof buf, "%ld", std::lround(s.bpmMax));
    else
        std::snprintf(buf, sizeof buf, "%ld - %ld", std::lround(s.bpmMin), std::lround(s.bpmMax));
    return buf;
}

}  // namespace

SongSelect::SongSelect(wii::Video& video, const wii::Input& input, const render::Font& font, Session& session)
    : video_(video), input_(input), font_(font), session_(session) {
    const ddr::SongLibrary& lib = session_.library;
    if (session_.open_pack < 0 && session_.song < 0) {
        // First visit: start on the test song if it's there, else on the first pack.
        for (size_t p = 0; p < lib.packs.size(); p++)
            for (size_t s = 0; s < lib.packs[p].songs.size(); s++)
                if (lib.packs[p].songs[s].folder == kStartFolder) {
                    session_.open_pack = session_.pack = (int)p;
                    session_.song = (int)s;
                }
    }
    buildItems();
    scroll_ = (float)cursor_;
    pickCharts();
    if (!session_.message.empty()) {   // e.g. the calibration's "offset saved"
        hint_.note(session_.message);
        session_.message.clear();
    }
}

void SongSelect::buildItems() {
    items_.clear();
    cursor_ = 0;
    const ddr::SongLibrary& lib = session_.library;
    for (int p = 0; p < (int)lib.packs.size(); p++) {
        if (p == session_.pack && session_.song < 0) cursor_ = (int)items_.size();
        items_.push_back({p, -1});
        if (p != session_.open_pack) continue;
        for (int s = 0; s < (int)lib.packs[p].songs.size(); s++) {
            if (p == session_.pack && s == session_.song) cursor_ = (int)items_.size();
            items_.push_back({p, s});
        }
    }
}

void SongSelect::moveCursor(int delta) {
    if (items_.empty()) return;
    const int n = (int)items_.size();
    cursor_ = ((cursor_ + delta) % n + n) % n;
    // Keep the drawn position near the cursor when the wheel wraps around.
    if (scroll_ - cursor_ > n / 2) scroll_ -= n;
    if (cursor_ - scroll_ > n / 2) scroll_ += n;
    session_.pack = items_[cursor_].pack;
    session_.song = items_[cursor_].song;
    pickCharts();
}

// ITG keeps each player's difficulty from song to song: take the chart with the preferred
// difficulty, else the nearest one. The test autoplay P2 (no pad) follows the first player.
void SongSelect::pickCharts() {
    const ddr::SongEntry* s = session_.chosen();
    if (!s || s->charts.empty()) return;
    for (SessionPlayer& p : session_.players) {
        if (p.bot && !session_.players.empty() && !session_.players[0].bot)
            p.preferred = session_.players[0].preferred;
        int best = 0, best_d = 1000;
        for (int i = 0; i < (int)s->charts.size(); i++) {
            const int d = std::abs((int)s->charts[i].difficulty - (int)p.preferred);
            if (d < best_d) { best = i; best_d = d; }
        }
        p.chart = best;
    }
}

// Opens or closes a pack; the cursor goes to (or stays on) the pack's header.
void SongSelect::setPackOpen(int pack, bool open) {
    session_.open_pack = open ? pack : -1;
    session_.pack = pack;
    session_.song = -1;
    buildItems();
    scroll_ = (float)cursor_;
}

void SongSelect::handleWheel() {
    const std::vector<SessionPlayer>& players = session_.players;
    const u64 now = wii::Input::nowUs();
    const u16 down0 = input_.down(0);

    // A port that isn't playing: the players are set on the Players screen, nowhere else.
    for (int port = 0; port < wii::Input::kPorts; port++) {
        if (!(input_.down(port) & (PAD_BUTTON_A | PAD_BUTTON_START))) continue;
        bool playing = false;
        for (const SessionPlayer& p : players) playing = playing || (p.port == port && !p.bot);
        if (!playing) hint_.say("Players are chosen on the Players screen");
    }
    if (down0 & PAD_TRIGGER_R) {   // the offset calibration screen (controller only)
        calibrate_ = true;
        return;
    }

    // Up/Down from any player's pad, held to repeat.
    u16 down = 0, held = 0;
    for (const SessionPlayer& p : players) {
        if (p.bot) continue;
        down |= input_.down(p.port);
        held |= input_.held(p.port);
    }
    if (items_.empty()) return;
    if (wheel_up_.update(down & PAD_BUTTON_UP, held & PAD_BUTTON_UP, now)) {
        moveCursor(-1);
        wii::sfx::move();
    }
    if (wheel_down_.update(down & PAD_BUTTON_DOWN, held & PAD_BUTTON_DOWN, now)) {
        moveCursor(1);
        wii::sfx::move();
    }

    const Item it = items_[cursor_];
    if (down & PAD_BUTTON_RIGHT) {   // open the pack under the cursor
        if (it.song < 0 && session_.open_pack != it.pack) {
            setPackOpen(it.pack, true);
            wii::sfx::move();
        }
    } else if (down & PAD_BUTTON_LEFT) {   // close the open pack (from a song or its header)
        if (it.song >= 0 || session_.open_pack == it.pack) {
            setPackOpen(it.pack, false);
            wii::sfx::move();
        }
    } else if (down & PAD_BUTTON_A) {
        if (it.song < 0) {
            setPackOpen(it.pack, session_.open_pack != it.pack);
            wii::sfx::select();
        } else if (session_.chosen() && !session_.chosen()->charts.empty()) {
            openPopup();
            wii::sfx::select();
        }
    } else if (down & PAD_BUTTON_B) {
        back_ = true;
        wii::sfx::back();
    } else if (it.song >= 0 && session_.chosen() && !session_.chosen()->charts.empty()) {
        // Z opens the song's high scores on that player's chart (not START+Z, the exit chord).
        for (const SessionPlayer& p : players) {
            if (p.bot || !(input_.down(p.port) & PAD_TRIGGER_Z) || (input_.held(p.port) & PAD_BUTTON_START))
                continue;
            scores_ = true;
            scores_chart_ = p.chart;
            break;
        }
    }
}

void SongSelect::handleScores() {
    const ddr::SongEntry* s = session_.chosen();
    if (!s || s->charts.empty()) {
        scores_ = false;
        return;
    }
    for (const SessionPlayer& p : session_.players) {
        if (p.bot) continue;
        const u16 d = input_.down(p.port);
        if ((d & PAD_BUTTON_B) || ui::zPressed(input_, p.port)) {
            if (d & PAD_BUTTON_B) wii::sfx::back();   // as in the prototype, Z has no sound
            scores_ = false;
            return;
        }
        const int dir = (d & PAD_BUTTON_LEFT) ? -1 : (d & PAD_BUTTON_RIGHT) ? 1 : 0;
        const int c = std::max(0, std::min((int)s->charts.size() - 1, scores_chart_ + dir));
        if (c != scores_chart_) wii::sfx::move();
        scores_chart_ = c;
    }
}

void SongSelect::openPopup() {
    popup_ = true;
    for (size_t i = 0; i < session_.players.size(); i++) {
        SessionPlayer& p = session_.players[i];
        p.ready = p.bot;   // the test autoplay P2 has no pad: always ready
        row_[i] = 0;
        speed_rep_[i][0].reset();
        speed_rep_[i][1].reset();
    }
}

void SongSelect::handlePopup() {
    const ddr::SongEntry* s = session_.chosen();
    std::vector<SessionPlayer>& players = session_.players;
    const u64 now = wii::Input::nowUs();
    if (!s || s->charts.empty()) {
        popup_ = false;
        return;
    }
    for (size_t i = 0; i < players.size() && i < Session::kMaxPlayers; i++) {
        SessionPlayer& p = players[i];
        if (p.bot) continue;
        const u16 d = input_.down(p.port), h = input_.held(p.port);
        if (d & PAD_BUTTON_B) {   // anyone's B closes it for everyone
            wii::sfx::back();
            popup_ = false;
            for (SessionPlayer& q : players) q.ready = false;
            return;
        }
        // Speed repeats while Left/Right is held; the repeaters run every frame.
        const bool sp_left = speed_rep_[i][0].update(d & PAD_BUTTON_LEFT, h & PAD_BUTTON_LEFT, now);
        const bool sp_right = speed_rep_[i][1].update(d & PAD_BUTTON_RIGHT, h & PAD_BUTTON_RIGHT, now);
        if (p.ready) {   // arrows locked; A un-readies
            if (d & PAD_BUTTON_A) {
                p.ready = false;
                wii::sfx::select();
            }
            continue;
        }
        int& row = row_[i];
        if (d & (PAD_BUTTON_UP | PAD_BUTTON_DOWN)) wii::sfx::move();
        if (d & PAD_BUTTON_UP) row = (row + kRows - 1) % kRows;
        if (d & PAD_BUTTON_DOWN) row = (row + 1) % kRows;
        const int dir = (d & PAD_BUTTON_LEFT) ? -1 : (d & PAD_BUTTON_RIGHT) ? 1 : 0;
        ddr::PortConfig& pc = session_.config.ports[p.port];
        if (row == 0 && dir) {   // the song's charts, easiest first
            const int c = std::max(0, std::min((int)s->charts.size() - 1, p.chart + dir));
            if (c != p.chart) wii::sfx::move();
            p.chart = c;
            p.preferred = s->charts[c].difficulty;
            pickCharts();   // an autoplay P2 follows
        } else if (row == 1 && (sp_left || sp_right)) {
            const double sp = std::max(kMinSpeed, std::min(kMaxSpeed, p.speed + (sp_left ? -kSpeedStep : kSpeedStep)));
            if (sp != p.speed) {
                p.speed = pc.scrollSpeed = sp;
                session_.config_dirty = true;
                wii::sfx::move();
            }
        } else if (row == 2 && dir) {   // Off, On, On no fail
            p.life_bar = (ddr::LifeBar)(((int)p.life_bar + dir + 3) % 3);
            pc.lifeBar = p.life_bar;
            session_.config_dirty = true;
            wii::sfx::move();
        } else if (row == 3 && dir) {
            p.autoplay = !p.autoplay;
            wii::sfx::move();
        }
        if (d & PAD_BUTTON_A) {
            wii::sfx::select();
            if (row != kRows - 1) row = kRows - 1;
            else p.ready = true;
        }
    }
    bool all = !players.empty();
    for (const SessionPlayer& p : players) all = all && p.ready;
    if (all) picked_ = true;
}

bool SongSelect::frame() {
    frames_++;
    if (popup_)
        handlePopup();
    else if (scores_)
        handleScores();
    else
        handleWheel();
    updateMedia();
    draw();
    const bool quit = exit_.update(input_);
    return !picked_ && !calibrate_ && !back_ && !quit;
}

// Like ITG, wait until the cursor has rested on a row before loading its banner and preview,
// so scrolling through the wheel doesn't start a load per row.
void SongSelect::updateMedia() {
    const u64 now = wii::Input::nowUs();
    if (session_.pack != media_pack_ || session_.song != media_song_) {
        media_pack_ = session_.pack;
        media_song_ = session_.song;
        media_moved_us_ = now;
        media_requested_ = false;
        media_.request("", "", 0, 0);   // drop the old banner and stop the old preview now
    }
    if (media_requested_ || now - media_moved_us_ < kMediaRestUs) return;
    media_requested_ = true;
    const ddr::SongLibrary& lib = session_.library;
    if (const ddr::SongEntry* s = session_.chosen()) {
        media_.request(s->banner.empty() ? "" : s->dir + s->banner, s->MusicPath(), s->sampleStart,
                       s->sampleLength);
    } else if (session_.pack >= 0 && session_.pack < (int)lib.packs.size()) {
        const ddr::PackEntry& p = lib.packs[session_.pack];
        media_.request(p.banner.empty() ? "" : p.dir + p.banner, "", 0, 0);
    }
}

// ---------------------------------------------------------------------------
// Drawing

void SongSelect::draw() {
    render::rect(0, 0, video_.width(), video_.height(), kBg);
    scroll_ += (cursor_ - scroll_) * 0.35f;
    if (std::fabs(cursor_ - scroll_) < 0.01f) scroll_ = (float)cursor_;

    // Boxes first, then all text without filtering (the bitmap font).
    const ddr::SongEntry* s = session_.chosen();
    render::rect(0, 0, video_.width(), 46, 0x000000A0);   // header bar
    drawWheel();
    GRRLIB_SetAntiAliasing(false);
    font_.print(kLeftX, 24, kWhite, 1, "SELECT MUSIC");
    char buf[48];
    std::snprintf(buf, sizeof buf, "%d songs  scan %lu ms  %d cached", (int)session_.library.SongCount(),
                  (unsigned long)(session_.scan_us / 1000), session_.cache_hits);
    font_.print(600 - std::strlen(buf) * kGlyph, 24, kGrey, 1, "%s", buf);
    drawSongInfo(s);
    drawGrid(s);
    for (int i = 0; i < Session::kMaxPlayers; i++) drawPane(i, s);
    // The help strip's first line. The panes fill y 344-428, so it sits under them, alone.
    if (!popup_ && !scores_)
        font_.print(kLeftX, 436, kGrey, 1, "Up/Down scroll   Right open   Left close   A select   Z scores");
    if (!session_.scanned)
        font_.print(kLeftX, 200, 0xFF5050FF, 1, "can't open sd:/ddrwii/songs");
    else if (session_.library.packs.empty())
        font_.print(kLeftX, 200, 0xFF5050FF, 1, "no songs in sd:/ddrwii/songs");
    GRRLIB_SetAntiAliasing(true);
    if (popup_) drawPopup(s);
    if (scores_) drawScores(s);
    hint_.draw(font_);
}

void SongSelect::drawSongInfo(const ddr::SongEntry* s) {
    const ddr::SongLibrary& lib = session_.library;
    // The banner once SongMedia has loaded it, else the title on a dark panel.
    render::rect(kLeftX, kBannerY, kLeftW, kBannerH, kDark);
    render::rect(kLeftX, kBannerY + kBannerH - 3, kLeftW, 3,
                 s ? difficultyColour(ddr::Difficulty::Medium) : kPackBg);
    const float cx = kLeftX + kLeftW / 2;
    const int fit = (int)(kLeftW / kGlyph) - 2;
    if (const GRRLIB_texImg* bn = media_.banner()) {
        GRRLIB_SetAntiAliasing(true);   // filtered: it's scaled
        render::img(kLeftX, kBannerY, bn, 0, kLeftW / bn->w, (kBannerH - 3) / bn->h, 0xFFFFFFFF);
        GRRLIB_SetAntiAliasing(false);
    } else if (s) {
        const float scale = (int)s->title.size() * kGlyph * 2 <= kLeftW - 16 ? 2 : 1;
        printCentred(font_, cx, kBannerY + 30, kWhite, scale, s->title, fit);
        printCentred(font_, cx, kBannerY + 66, kGrey, 1, s->subtitle, fit);
    } else if (session_.pack < (int)lib.packs.size()) {
        const ddr::PackEntry& p = lib.packs[session_.pack];
        printCentred(font_, cx, kBannerY + 30, kWhite, 1, p.info.displayTitle, fit);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%d songs", (int)p.songs.size());
        printCentred(font_, cx, kBannerY + 56, kGrey, 1, buf, fit);
    }

    // Artist, BPM, length (Simply Love's SongDescription box).
    render::rect(kLeftX, kInfoY, kLeftW, kInfoH, kBox);
    const float lx = kLeftX + 8, vx = kLeftX + 72;
    font_.print(lx, kInfoY + 6, kGrey, 1, "ARTIST");
    font_.print(lx, kInfoY + 28, kGrey, 1, "BPM");
    font_.print(kLeftX + 160, kInfoY + 28, kGrey, 1, "LENGTH");
    if (s) {
        printFit(font_, vx, kInfoY + 6, kWhite, 1, s->artist, (int)((kLeftW - 76) / kGlyph));
        printFit(font_, vx, kInfoY + 28, kWhite, 1, bpmText(*s), 10);
        font_.print(kLeftX + 216, kInfoY + 28, kWhite, 1, "%s", lengthText(s->lengthSeconds).c_str());
    }
}

// Difficulty grid: one row per chart (up to five), meter in the difficulty's colour, then
// ten blocks lit up to the meter; each player's cursor sits beside their row.
void SongSelect::drawGrid(const ddr::SongEntry* s) {
    const float meter_w = 32, bx = kLeftX + 56, block = 16, gap = 3;
    int first = 0;
    if (s) {   // scroll so every player's row is visible (more than five charts: edits)
        int lo = 1000, hi = -1;
        for (const SessionPlayer& p : session_.players) { lo = std::min(lo, p.chart); hi = std::max(hi, p.chart); }
        if (hi >= kGridRows) first = std::min(hi - kGridRows + 1, lo);
    }
    for (int r = 0; r < kGridRows; r++) {
        const float y = kGridY + r * kGridRowH;
        render::rect(kLeftX + 16, y, meter_w, kGridRowH - 2, kBox);
        const int ci = first + r;
        const bool has = s && ci < (int)s->charts.size();
        for (int b = 0; b < 10; b++) {
            const bool lit = has && b < s->charts[ci].meter;
            render::rect(bx + b * (block + gap), y + 2, block, block,
                         lit ? difficultyColour(s->charts[ci].difficulty) : 0x182025FF);
        }
        if (!has) continue;
        const ddr::ChartInfo& c = s->charts[ci];
        char buf[8];
        std::snprintf(buf, sizeof buf, "%d", c.meter);
        printCentred(font_, kLeftX + 16 + meter_w / 2, y + 3, difficultyColour(c.difficulty), 1, buf, 3);
        for (size_t i = 0; i < session_.players.size() && i < Session::kMaxPlayers; i++) {
            if (session_.players[i].chart != ci) continue;
            if (i == 0)
                font_.print(kLeftX, y + 3, kPlayerColour[0], 1, ">");
            else
                font_.print(kLeftX + kLeftW - 6, y + 3, kPlayerColour[1], 1, "<");
        }
    }
}

// One pane per player slot along the bottom: the chart and its counts (Simply Love's
// PaneDisplay), coloured by difficulty, with the speed, the life bar setting and the best.
void SongSelect::drawPane(int slot, const ddr::SongEntry* s) {
    const float x = kPaneX[slot], y = kPaneY;
    if (slot >= (int)session_.players.size()) {
        render::rect(x, y, kPaneW, kPaneH, 0x1E282F80);
        return;
    }
    const SessionPlayer& p = session_.players[slot];
    const ddr::ChartInfo* c = (s && p.chart < (int)s->charts.size()) ? &s->charts[p.chart] : nullptr;
    render::rect(x, y, kPaneW, kPaneH, c ? difficultyColour(c->difficulty) : kPlayerColour[slot]);
    char buf[48];
    std::snprintf(buf, sizeof buf, "P%d  %.2fx%s", slot + 1, p.speed, p.autoplay ? "  AUTO" : "");
    font_.print(x + 8, y + 6, kBlack, 1, "%s", buf);
    const char* life = lifeBarText(p.life_bar);
    if (!c) {
        font_.print(x + 8, y + 30, kBlack, 1, "%s", ddr::DifficultyToString(p.preferred));
        font_.print(x + 8, y + 48, kBlack, 1, "open a pack with Right or A");
        font_.print(x + kPaneW - 8 - std::strlen(life) * kGlyph, y + 66, kBlack, 1, "%s", life);
        return;
    }
    std::snprintf(buf, sizeof buf, "%s %d", ddr::DifficultyToString(c->difficulty), c->meter);
    font_.print(x + kPaneW - 8 - std::strlen(buf) * kGlyph, y + 6, kBlack, 1, "%s", buf);
    font_.print(x + 8, y + 30, kBlack, 1, "Steps %4d  Holds %3d", c->steps, c->holds);
    font_.print(x + 8, y + 48, kBlack, 1, "Jumps %4d  Mines %3d", c->jumps, c->mines);
    font_.print(x + 8, y + 66, kBlack, 1, "Rolls %4d", c->rolls);
    font_.print(x + kPaneW - 8 - std::strlen(life) * kGlyph, y + 66, kBlack, 1, "%s", life);

    // The machine best for this chart (doc 06 §7), on the right. Looked up only when the
    // slot's song or chart changes (the table doesn't change while song select is open, and
    // map entries don't move).
    PaneBest& pb = pane_best_[slot];
    if (pb.pack != session_.pack || pb.song != session_.song || pb.chart != p.chart) {
        pb.pack = session_.pack;
        pb.song = session_.song;
        pb.chart = p.chart;
        pb.entry = session_.scores.find(game::ChartKey(*s, *c));
    }
    const game::HighScore* best = pb.entry ? pb.entry->best() : nullptr;
    if (best) {
        std::snprintf(buf, sizeof buf, "%.2f%%", best->percent_x100 / 100.0);
    } else {
        std::snprintf(buf, sizeof buf, "--");
    }
    font_.print(x + kPaneW - 8 - std::strlen(buf) * kGlyph, y + 30, kBlack, 1, "%s", buf);
    if (best) {   // the name and grade under it
        std::snprintf(buf, sizeof buf, "%s  %s", best->name.c_str(), game::GradeTierName(best->tier()));
        font_.print(x + kPaneW - 8 - std::strlen(buf) * kGlyph, y + 48, kBlack, 1, "%s", buf);
    }
}

// The high scores popup (the prototype's): the chart's top 10, a row each with rank, name,
// speed, %, grade (F in red) and a "no bar" / "no fail" tag.
void SongSelect::drawScores(const ddr::SongEntry* s) {
    if (!s || scores_chart_ >= (int)s->charts.size()) return;
    const ddr::ChartInfo& c = s->charts[scores_chart_];
    const game::ChartScores* list = session_.scores.find(game::ChartKey(*s, c));
    const int n = list ? (int)list->top.size() : 0;

    render::rect(0, 0, video_.width(), video_.height(), 0x00000099);
    render::rect(48, 56, 544, 380, kBox);
    render::rect(48, 56, 544, 4, kPlayerColour[0]);
    for (int k = 0; k < n; k++) render::rect(64, 125 + k * 22, 512, 20, k % 2 ? kBox : kDark);

    GRRLIB_SetAntiAliasing(false);
    font_.print(64, 68, kWhite, 1, "HIGH SCORES");
    printFit(font_, 576 - std::min((int)s->title.size(), 40) * kGlyph, 68, kGrey, 1, s->title, 40);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s %s %d %s", scores_chart_ > 0 ? "<" : " ",
                  ddr::DifficultyToString(c.difficulty), c.meter,
                  scores_chart_ + 1 < (int)s->charts.size() ? ">" : " ");
    printCentred(font_, 320, 92, difficultyColour(c.difficulty), 1, buf, 40);
    if (!n) printCentred(font_, 320, 210, kGrey, 1, "No scores yet", 20);
    for (int k = 0; k < n; k++) {
        const game::HighScore& e = list->top[k];
        const float y = 128 + k * 22;
        font_.print(80, y, kGrey, 1, "%2d.", k + 1);
        font_.print(128, y, k == 0 ? 0xFFE060FF : kWhite, 1, "%s", e.name.c_str());
        if (e.speed > 0) font_.print(192, y, kGrey, 1, "%.2fx", e.speed);
        std::snprintf(buf, sizeof buf, "%.2f%%", e.percent_x100 / 100.0);
        font_.print(400 - std::strlen(buf) * kGlyph, y, kWhite, 1, "%s", buf);
        font_.print(432, y, e.failed ? 0xFF4040FF : kWhite, 1, "%s", game::GradeTierName(e.tier()));
        const char* tag = e.life_bar == ddr::LifeBar::Off ? "no bar" : e.life_bar == ddr::LifeBar::NoFail ? "no fail" : "";
        font_.print(488, y, kGrey, 1, "%s", tag);
    }
    if (list) font_.print(64, 372, kGrey, 1, "%d plays, %d clears", list->plays, list->clears);
    ui::help(font_, "Left/Right difficulty", "B or Z close");
    GRRLIB_SetAntiAliasing(true);
}

// The song settings popup over the screen: a row per setting, a column per player, each
// player's cursor as an outline in their colour (the menu prototype's layout).
void SongSelect::drawPopup(const ddr::SongEntry* s) {
    static const char* const kRowName[kRows] = {"Difficulty", "Speed", "Life bar", "Autoplay", "Ready"};
    static const char* const kLifeName[3] = {"< Off >", "< On >", "< On, no fail >"};
    constexpr float kBoxX = 48, kBoxY = 56, kBoxW = 544, kBoxH = 380;
    constexpr float kColX[Session::kMaxPlayers] = {248, 416};
    constexpr float kColW = 152, kRowY = 108, kRowStep = 46, kRowH = 40;
    const std::vector<SessionPlayer>& players = session_.players;

    render::rect(0, 0, video_.width(), video_.height(), 0x00000099);
    render::rect(kBoxX, kBoxY, kBoxW, kBoxH, kBox);
    render::rect(kBoxX, kBoxY, kBoxW, 4, kPlayerColour[0]);
    for (int r = 0; r < kRows; r++) {
        const float y = kRowY + r * kRowStep;
        render::rect(64, y, 512, kRowH, kDark);
        for (size_t n = 0; n < players.size() && n < Session::kMaxPlayers; n++) {
            render::rect(kColX[n], y, kColW, kRowH, 0x26323AFF);
            if (row_[n] != r || players[n].ready) continue;
            const u32 c = kPlayerColour[n];   // the cursor: a 2 px outline
            render::rect(kColX[n], y, kColW, 2, c);
            render::rect(kColX[n], y + kRowH - 2, kColW, 2, c);
            render::rect(kColX[n], y, 2, kRowH, c);
            render::rect(kColX[n] + kColW - 2, y, 2, kRowH, c);
        }
    }

    GRRLIB_SetAntiAliasing(false);
    font_.print(64, 68, kWhite, 1, "SONG SETTINGS");
    if (s) printFit(font_, 576 - std::min((int)s->title.size(), 30) * kGlyph, 68, kGrey, 1, s->title, 30);
    for (size_t n = 0; n < players.size() && n < Session::kMaxPlayers; n++)
        printCentred(font_, kColX[n] + kColW / 2, 88, kPlayerColour[n], 1, n == 0 ? "P1" : "P2", 2);
    for (int r = 0; r < kRows; r++) {
        const float y = kRowY + r * kRowStep;
        if (r == 3) {
            font_.print(80, y + 4, kWhite, 1, "Autoplay");
            font_.print(80, y + 21, kGrey, 1, "demo, no score");
        } else {
            font_.print(80, y + 12, kWhite, 1, "%s", kRowName[r]);
        }
        for (size_t n = 0; n < players.size() && n < Session::kMaxPlayers; n++) {
            const SessionPlayer& p = players[n];
            char buf[32];
            u32 c = kWhite;
            if (r == 0 && s && p.chart < (int)s->charts.size()) {
                const ddr::ChartInfo& ch = s->charts[p.chart];
                std::snprintf(buf, sizeof buf, "< %s %d >", ddr::DifficultyToString(ch.difficulty), ch.meter);
                c = difficultyColour(ch.difficulty);
            } else if (r == 1) {
                std::snprintf(buf, sizeof buf, "< %.2fx >", p.speed);
            } else if (r == 2) {
                std::snprintf(buf, sizeof buf, "%s", kLifeName[(int)p.life_bar]);
            } else if (r == 3) {
                std::snprintf(buf, sizeof buf, "%s", p.autoplay ? "< On >" : "< Off >");
            } else {
                std::snprintf(buf, sizeof buf, "%s", p.ready ? "Ready" : "press A");
                c = p.ready ? 0x50D070FF : kGrey;
            }
            if (p.ready && r < kRows - 1) c = kGrey;
            printCentred(font_, kColX[n] + kColW / 2, y + 12, c, 1, buf, 18);
        }
    }
    bool any_ready = false;
    for (const SessionPlayer& p : players) any_ready = any_ready || (p.ready && !p.bot);
    ui::help(font_, any_ready ? "Up/Down row   Left/Right change   A on Ready   (A again un-readies)"
                              : "Up/Down row   Left/Right change   A on Ready",
             "B back to the song list");
    GRRLIB_SetAntiAliasing(true);
}

// The music wheel: pack headers (grey) and the open pack's songs (dark), cursor row in the
// middle with a pulsing highlight. Rows are drawn at scroll_, which eases after the cursor.
void SongSelect::drawWheel() {
    if (items_.empty()) return;
    const ddr::SongLibrary& lib = session_.library;
    const int n = (int)items_.size();
    const int mid = kWheelRows / 2;
    const int base = (int)std::floor(scroll_);
    const float frac = scroll_ - base;
    std::vector<std::pair<float, int>> rows;   // y, item
    for (int r = -1; r <= kWheelRows; r++) {
        const float y = kWheelTop + (r - frac) * kRowH;
        if (y < kWheelTop - kRowH / 2 || y > kWheelTop + (kWheelRows - 0.5f) * kRowH) continue;
        const int k = base + r - mid;
        if (n < kWheelRows && (k < 0 || k >= n)) continue;   // short list: no wrap-around copies
        rows.push_back({y, ((k % n) + n) % n});
    }
    for (const auto& row : rows) {
        const Item& it = items_[row.second];
        render::rect(kWheelX, row.first, kWheelW, kRowH - 1, it.song < 0 ? kPackBg : kDark);
    }
    const float hy = kWheelTop + mid * kRowH;   // the cursor row
    const float pulse = 0.18f + 0.10f * std::sin(frames_ * 0.1f);
    render::rect(kWheelX, hy, kWheelW, kRowH - 1, 0xFFFFFF00 | (u32)(pulse * 255));
    render::rect(kWheelX - 4, hy, 4, kRowH - 1, session_.players.empty() ? kWhite : kPlayerColour[0]);

    GRRLIB_SetAntiAliasing(false);
    const int fit = (int)((kWheelW - 16) / kGlyph);
    for (const auto& row : rows) {
        const Item& it = items_[row.second];
        const float ty = row.first + (kRowH - render::Font::kGlyphH) / 2;
        if (it.song < 0) {
            const ddr::PackEntry& p = lib.packs[it.pack];
            char count[8];
            std::snprintf(count, sizeof count, "%d", (int)p.songs.size());
            printFit(font_, kWheelX + 8, ty, kWhite, 1, p.info.displayTitle, fit - 4);
            font_.print(kWheelX + kWheelW - 8 - std::strlen(count) * kGlyph, ty, kGrey, 1, "%s", count);
        } else {
            const ddr::SongEntry& s = lib.packs[it.pack].songs[it.song];
            printFit(font_, kWheelX + 16, ty, kWhite, 1, s.title, fit - 1);
        }
    }
    GRRLIB_SetAntiAliasing(true);
}

}  // namespace screens
