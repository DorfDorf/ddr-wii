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
constexpr u64 kRepeatDelayUs = 400000, kRepeatUs = 80000;   // held Left/Right

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

u64 cpuUs() { return ticks_to_microsecs(gettime()); }

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
// difficulty, else the nearest one.
void SongSelect::pickCharts() {
    const ddr::SongEntry* s = session_.chosen();
    if (!s || s->charts.empty()) return;
    for (SessionPlayer& p : session_.players) {
        int best = 0, best_d = 1000;
        for (int i = 0; i < (int)s->charts.size(); i++) {
            const int d = std::abs((int)s->charts[i].difficulty - (int)p.preferred);
            if (d < best_d) { best = i; best_d = d; }
        }
        p.chart = best;
    }
}

void SongSelect::handleInput() {
    std::vector<SessionPlayer>& players = session_.players;
    const u16 down0 = input_.down(0);

    // Join: START on a port that isn't playing yet (doc 06 §11.2). The press only joins.
    for (int port = 0; port < wii::Input::kPorts; port++) {
        if (!(input_.down(port) & PAD_BUTTON_START) || (input_.held(port) & PAD_TRIGGER_Z)) continue;
        auto it = std::find_if(players.begin(), players.end(),
                               [&](const SessionPlayer& p) { return p.port == port; });
        if (it != players.end()) {
            // Already playing, unless it's a debug autoplay player (Y): a human takes it over.
            if (!it->autoplay || port == 0) continue;
            it->autoplay = false;
            return;
        }
        if ((int)players.size() >= Session::kMaxPlayers) continue;
        SessionPlayer np;
        np.port = port;
        np.speed = session_.config.ports[port].scrollSpeed;
        if (!players.empty()) np.preferred = players[0].preferred;
        players.push_back(np);
        pickCharts();
        return;
    }

    // Debug: Y on port 1 adds an autoplay player on a free port, or removes it.
    if (down0 & PAD_BUTTON_Y) {
        // Only the extra test player (autoplay on a port other than 1), never P1 on autoplay.
        auto it = std::find_if(players.begin(), players.end(),
                               [](const SessionPlayer& p) { return p.autoplay && p.port != 0; });
        if (it != players.end()) {
            players.erase(it);
        } else if ((int)players.size() < Session::kMaxPlayers) {
            for (int port = 1; port < wii::Input::kPorts; port++) {
                bool used = false;
                for (const SessionPlayer& p : players) used = used || p.port == port;
                if (used) continue;
                SessionPlayer np;
                np.port = port;
                np.speed = session_.config.ports[port].scrollSpeed;
                np.autoplay = true;
                np.preferred = players.empty() ? ddr::Difficulty::Easy : players[0].preferred;
                players.push_back(np);
                break;
            }
        }
        pickCharts();
    }
    if (down0 & PAD_TRIGGER_R) {   // the offset calibration screen
        calibrate_ = true;
        return;
    }

    if (players.empty() && (down0 & PAD_BUTTON_B)) back_ = true;

    // Per player: difficulty, speed, autoplay, leave. Wheel and START from any joined human port.
    const ddr::SongEntry* s = session_.chosen();
    int dir = 0, held_dir = 0;
    bool start = false;
    for (size_t i = 0; i < players.size(); i++) {
        SessionPlayer& p = players[i];
        if (p.autoplay && p.port != 0) continue;   // a debug autoplay player has no pad
        const u16 d = input_.down(p.port), h = input_.held(p.port);
        if (h & PAD_BUTTON_A) {   // A held: Up/Down set the scroll speed (saved per port)
            double sp = p.speed;
            if (d & PAD_BUTTON_UP) sp = std::min(kMaxSpeed, sp + kSpeedStep);
            if (d & PAD_BUTTON_DOWN) sp = std::max(kMinSpeed, sp - kSpeedStep);
            if (sp != p.speed) {
                p.speed = sp;
                session_.config.ports[p.port].scrollSpeed = sp;
                session_.config_dirty = true;
            }
        } else if (s && !s->charts.empty()) {
            const int n = (int)s->charts.size();
            int c = p.chart;
            if (d & PAD_BUTTON_UP) c = std::max(0, c - 1);
            if (d & PAD_BUTTON_DOWN) c = std::min(n - 1, c + 1);
            if (c != p.chart) {
                p.chart = c;
                p.preferred = s->charts[c].difficulty;
            }
        }
        if ((d & PAD_TRIGGER_Z) && !(h & PAD_BUTTON_START)) p.autoplay = !p.autoplay;
        if (d & PAD_BUTTON_LEFT) dir = -1;
        if (d & PAD_BUTTON_RIGHT) dir = 1;
        if (h & PAD_BUTTON_LEFT) held_dir = -1;
        if (h & PAD_BUTTON_RIGHT) held_dir = 1;
        if ((d & PAD_BUTTON_START) && !(h & PAD_TRIGGER_Z)) start = true;
        if (d & PAD_BUTTON_B) back_ = true;
        if (d & PAD_BUTTON_X) {
            players.erase(players.begin() + i);
            return;
        }
    }

    const u64 now = cpuUs();
    if (dir) {
        moveCursor(dir);
        repeat_dir_ = dir;
        repeat_at_us_ = now + kRepeatDelayUs;
    } else if (held_dir && held_dir == repeat_dir_ && now >= repeat_at_us_) {
        moveCursor(held_dir);
        repeat_at_us_ = now + kRepeatUs;
    } else if (!held_dir) {
        repeat_dir_ = 0;
    }

    if (start && !items_.empty()) {
        const Item it = items_[cursor_];
        if (it.song < 0) {   // a pack header: open it, or close it if it's open
            session_.open_pack = session_.open_pack == it.pack ? -1 : it.pack;
            session_.pack = it.pack;
            session_.song = -1;
            buildItems();
            scroll_ = (float)cursor_;
        } else if (!players.empty()) {
            picked_ = true;
        }
    }
}

bool SongSelect::frame() {
    frames_++;
    if (frames_ == 300) session_.message.clear();   // shown for about 5 s
    handleInput();
    draw();
    const u16 held = input_.held(0);
    quit_frames_ = ((held & PAD_BUTTON_START) && (held & PAD_TRIGGER_Z)) ? quit_frames_ + 1 : 0;
    return !picked_ && !calibrate_ && !back_ && quit_frames_ < 120;
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
    font_.print(kLeftX, 24, kWhite, 1, "SELECT MUSIC  %s",
                session_.config.playMode == ddr::PlayMode::Competitive ? "COMPETITIVE" : "CASUAL");
    char buf[48];
    std::snprintf(buf, sizeof buf, "%d songs  scan %lu ms", (int)session_.library.SongCount(),
                  (unsigned long)(session_.scan_us / 1000));
    font_.print(600 - std::strlen(buf) * kGlyph, 24, kGrey, 1, "%s", buf);
    drawSongInfo(s);
    drawGrid(s);
    for (int i = 0; i < Session::kMaxPlayers; i++) drawPane(i, s);
    if (!session_.message.empty())
        font_.print(kLeftX, 436, 0xFFC040FF, 1, "%s", session_.message.c_str());
    else
        font_.print(kLeftX, 436, kGrey, 1, "offset %+.1f ms  A+Up/Down speed  R calibrate  B back",
                    session_.config.globalOffsetS * 1000);
    if (!session_.scanned)
        font_.print(kLeftX, 200, 0xFF5050FF, 1, "can't open sd:/ddrwii/songs");
    else if (session_.library.packs.empty())
        font_.print(kLeftX, 200, 0xFF5050FF, 1, "no songs in sd:/ddrwii/songs");
    GRRLIB_SetAntiAliasing(true);
}

void SongSelect::drawSongInfo(const ddr::SongEntry* s) {
    const ddr::SongLibrary& lib = session_.library;
    // Banner placeholder (real banners in M5 step 6): the title on a dark panel.
    render::rect(kLeftX, kBannerY, kLeftW, kBannerH, kDark);
    render::rect(kLeftX, kBannerY + kBannerH - 3, kLeftW, 3,
                 s ? difficultyColour(ddr::Difficulty::Medium) : kPackBg);
    const float cx = kLeftX + kLeftW / 2;
    const int fit = (int)(kLeftW / kGlyph) - 2;
    if (s) {
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
// PaneDisplay), coloured by difficulty. An empty slot asks for START.
void SongSelect::drawPane(int slot, const ddr::SongEntry* s) {
    const float x = kPaneX[slot], y = kPaneY;
    if (slot >= (int)session_.players.size()) {
        render::rect(x, y, kPaneW, kPaneH, 0x1E282F80);
        if (frames_ / 30 % 2 == 0 || session_.players.empty())
            printCentred(font_, x + kPaneW / 2, y + 34, kGrey, 1, "Press START", 30);
        return;
    }
    const SessionPlayer& p = session_.players[slot];
    const ddr::ChartInfo* c = (s && p.chart < (int)s->charts.size()) ? &s->charts[p.chart] : nullptr;
    render::rect(x, y, kPaneW, kPaneH, c ? difficultyColour(c->difficulty) : kPlayerColour[slot]);
    char buf[48];
    std::snprintf(buf, sizeof buf, "P%d  port %d%s", slot + 1, p.port + 1, p.autoplay ? "  AUTO" : "");
    font_.print(x + 8, y + 6, kBlack, 1, "%s", buf);
    if (!c) {
        font_.print(x + 8, y + 30, kBlack, 1, "%s", ddr::DifficultyToString(p.preferred));
        font_.print(x + 8, y + 66, kBlack, 1, "Speed %.2fx", p.speed);
        return;
    }
    std::snprintf(buf, sizeof buf, "%s %d", ddr::DifficultyToString(c->difficulty), c->meter);
    font_.print(x + kPaneW - 8 - std::strlen(buf) * kGlyph, y + 6, kBlack, 1, "%s", buf);
    font_.print(x + 8, y + 30, kBlack, 1, "Steps %4d  Holds %3d", c->steps, c->holds);
    font_.print(x + 8, y + 48, kBlack, 1, "Jumps %4d  Mines %3d", c->jumps, c->mines);
    font_.print(x + 8, y + 66, kBlack, 1, "Rolls %4d  Speed %.2fx", c->rolls, p.speed);
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
