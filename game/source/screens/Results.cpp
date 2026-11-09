// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/Results.h"

#include <gccore.h>
#include <grrlib.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "platform/wii/MenuSound.h"
#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/Draw.h"
#include "render/Font.h"

namespace screens {
namespace {

constexpr u32 kWhite = 0xFFFFFFFF, kGrey = 0x9AA4ABFF, kBox = 0x1E282FFF, kLabel = 0xFFC040FF;
constexpr u32 kGold = 0xFFE060FF, kRed = 0xFF4040FF;
constexpr u32 kPlayerColour[2] = {0xFF47B3FF, 0xFF5D47FF};   // as on Players and song select
constexpr u64 kHintUs = 1600000;
constexpr float kGlyph = render::Font::kGlyphW;

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

// Grade colours: stars gold, S cyan, A green, B blue, C purple, D grey, failed red.
u32 tierColour(int tier) {
    if (tier == 0) return kRed;
    if (tier <= 4) return kGold;
    if (tier <= 7) return 0x80F0FFFF;
    if (tier <= 10) return 0x60F060FF;
    if (tier <= 13) return 0x60A0FFFF;
    if (tier <= 16) return 0xC070FFFF;
    return 0xB0B0B0FF;
}

const game::Grade kShown[] = {game::Grade::Marvelous, game::Grade::Perfect, game::Grade::Great,
                              game::Grade::Good, game::Grade::Boo, game::Grade::Miss};

void printCentred(const render::Font& font, float cx, float y, u32 colour, float scale, const char* text) {
    font.print(cx - std::strlen(text) * kGlyph * scale / 2, y, colour, scale, "%s", text);
}

void printRight(const render::Font& font, float right, float y, u32 colour, const char* text) {
    font.print(right - std::strlen(text) * kGlyph, y, colour, 1, "%s", text);
}

bool startPressed(const wii::Input& input, int port) {   // START, not the START+Z exit chord
    return (input.down(port) & PAD_BUTTON_START) && !(input.held(port) & PAD_TRIGGER_Z);
}

}  // namespace

Results::Results(wii::Video& video, const wii::Input& input, const render::Font& font, Session& session,
                 SongResult result)
    : video_(video), input_(input), font_(font), session_(session), title_(std::move(result.title)),
      start_us_(wii::Input::nowUs()) {
    for (PlayerResult& r : result.players) {
        Column c;
        c.rank = r.autoplay ? -1 : session_.scores.rank(r.key, r.entry);
        c.naming = c.rank >= 0;
        const std::string& last = session_.config.ports[r.port].name;
        if (game::ValidName(last)) std::memcpy(c.name, last.c_str(), 4);
        c.r = std::move(r);
        cols_.push_back(std::move(c));
    }
}

int Results::humans() const {
    int n = 0;
    for (const Column& c : cols_) n += c.r.bot ? 0 : 1;
    return n;
}

int Results::secondsLeft(u64 now_us) const {
    const u64 el = now_us - start_us_;
    return el >= kNameUs ? 0 : (int)((kNameUs - el + 999999) / 1000000);
}

bool Results::anyNaming() const {
    for (const Column& c : cols_)
        if (c.naming) return true;
    return false;
}

void Results::changeLetter(Column& c, int dir) {
    c.name[c.pos] = (char)('A' + ((c.name[c.pos] - 'A' + dir + 26) % 26));
    // Straight into the table, so the name counts even if nobody presses START.
    session_.scores.setName(c.r.key, c.r.entry, c.name);
    session_.scores_dirty = true;
}

void Results::save(Column& c) {
    c.naming = false;
    c.saved = true;
    session_.scores.setName(c.r.key, c.r.entry, c.name);
    session_.scores_dirty = true;
    std::string& last = session_.config.ports[c.r.port].name;
    if (last != c.name) {
        last = c.name;
        session_.config_dirty = true;
    }
}

bool Results::frame() {
    const u64 now = wii::Input::nowUs();
    bool chord = false;   // START+Z on any pad
    for (int port = 0; port < wii::Input::kPorts; port++) {
        const u16 h = input_.held(port);
        chord = chord || ((h & PAD_BUTTON_START) && (h & PAD_TRIGGER_Z));
    }
    quit_frames_ = chord ? quit_frames_ + 1 : 0;

    const bool naming_before = anyNaming();
    // Nothing saves by itself. Within the time each player's START saves their own name; once
    // it's up, any player's START saves every name still open, so nobody is held up for long.
    const bool time_up = humans() > 1 && now - start_us_ >= kNameUs;
    bool save_all = false;
    if (time_up)
        for (const Column& c : cols_)
            if (!c.r.bot && startPressed(input_, c.r.port)) save_all = true;
    for (Column& c : cols_) {
        if (!c.naming) continue;
        const int port = c.r.port;
        const u16 d = input_.down(port), h = input_.held(port);
        if (c.up.update(d & PAD_BUTTON_UP, h & PAD_BUTTON_UP, now)) {
            changeLetter(c, 1);
            wii::sfx::move();
        }
        if (c.down.update(d & PAD_BUTTON_DOWN, h & PAD_BUTTON_DOWN, now)) {
            changeLetter(c, -1);
            wii::sfx::move();
        }
        if ((d & PAD_BUTTON_LEFT) && c.pos > 0) {
            c.pos--;
            wii::sfx::move();
        }
        if ((d & PAD_BUTTON_RIGHT) && c.pos < 2) {
            c.pos++;
            wii::sfx::move();
        }
        if (d & PAD_BUTTON_A) {
            hint_us_ = now;
            wii::sfx::error();
        }
        if (!time_up && startPressed(input_, port)) {
            save(c);
            wii::sfx::select();
        }
    }
    if (save_all && anyNaming()) {
        for (Column& c : cols_)
            if (c.naming) save(c);
        wii::sfx::select();
    }
    if (quitApp()) {
        for (Column& c : cols_)
            if (c.naming) save(c);   // leaving the game keeps the name as typed
        return false;
    }

    // A continues once nobody is entering a name (not on the frame the last name was saved).
    bool done = false;
    if (!naming_before) {
        for (const Column& c : cols_)
            if (!c.r.bot && (input_.down(c.r.port) & PAD_BUTTON_A)) done = true;
        if (done) wii::sfx::select();
    }

    draw(now);
    return !done;
}

void Results::draw(u64 now_us) {
    render::rect(0, 0, video_.width(), video_.height(), 0x101828FF);
    for (size_t i = 0; i < cols_.size(); i++) {   // the name boxes first, then all text
        const Column& c = cols_[i];
        if (c.rank < 0) continue;
        const float cx = cols_.size() == 1 ? 320 : (i == 0 ? 160 : 480);
        render::rect(cx - 96, 362, 192, 32, kBox);
        render::rect(cx - 96, 362, 4, 32, kPlayerColour[i & 1]);
        const bool blink_on = (now_us / 333333) % 3 != 0;
        if (c.naming && blink_on) render::rect(cx - 40 + c.pos * 24, 391, 16, 3, kPlayerColour[i & 1]);
    }
    GRRLIB_SetAntiAliasing(false);   // the bitmap font is drawn without filtering
    font_.print(64, 48, kWhite, 2, "%s", title_.c_str());
    for (size_t i = 0; i < cols_.size(); i++) drawColumn(cols_[i], (int)i, now_us);

    // The help strip (the prototype's): who is still typing, and who waits for them.
    std::string typing, waiting;
    for (size_t i = 0; i < cols_.size(); i++) {
        std::string& s = cols_[i].naming ? typing : waiting;
        if (cols_[i].naming || !cols_[i].r.bot) s += (s.empty() ? "P" : ", P") + std::to_string(i + 1);
    }
    if (typing.empty()) {
        font_.print(48, 400, kGrey, 1, "A continue");
    } else {
        font_.print(48, 400, kGrey, 1, "Up/Down letter   Left/Right move   START saves the name");
        const int left = secondsLeft(now_us);
        if (humans() < 2) {
            // One player: no timer, START whenever they're ready.
        } else if (left == 0)
            font_.print(48, 416, kGrey, 1, "Time's up: any player's START saves every name");
        else if (waiting.empty())
            font_.print(48, 416, kGrey, 1, "in %d s either player's START saves both names", left);
        else
            font_.print(48, 416, kGrey, 1, "%s waits for %s's name   in %d s %s's START saves it", waiting.c_str(),
                        typing.c_str(), left, waiting.c_str());
    }
    if (hint_us_ && now_us - hint_us_ < kHintUs) {
        GRRLIB_SetAntiAliasing(true);
        render::rect(112, 440, 416, 28, 0x000000E0);
        render::rect(112, 440, 4, 28, 0xFFBE00FF);
        GRRLIB_SetAntiAliasing(false);
        printCentred(font_, 320, 446, kWhite, 1, "Press START to save the name");
    }
    GRRLIB_SetAntiAliasing(true);
}

// One column per player (doc 06 §7): the prototype's layout.
void Results::drawColumn(const Column& c, int slot, u64 now_us) {
    const PlayerResult& r = c.r;
    const game::Score& s = r.score;
    const float cx = cols_.size() == 1 ? 320 : (slot == 0 ? 160 : 480), x = cx - 88;
    char buf[64];

    static const char* const kBarTag[3] = {"  no bar", "", "  no fail"};
    std::snprintf(buf, sizeof buf, "P%d%s %s %d%s", slot + 1, r.autoplay ? " AUTO" : "",
                  ddr::DifficultyToString(r.difficulty), r.meter, kBarTag[(int)r.life_bar]);
    printCentred(font_, cx, 92, kLabel, 1, buf);
    const int tier = game::GradeTier(s.percentX100(), r.failed);
    printCentred(font_, cx, 106, tierColour(tier), 3, game::GradeTierName(tier));
    std::snprintf(buf, sizeof buf, "%.2f%%", s.percent());
    printCentred(font_, cx, 156, r.failed ? kRed : kWhite, 2, buf);

    // New best, Rank N or the old best.
    const game::ChartScores* list = session_.scores.find(r.key);
    const game::HighScore* best = list ? list->best() : nullptr;
    u32 rec_colour = kGrey;
    if (r.autoplay) {
        std::snprintf(buf, sizeof buf, "autoplay, not saved");
    } else if (c.rank == 0) {
        if (r.had_best)
            std::snprintf(buf, sizeof buf, "New best! (was %.2f%%)", r.prev_best.percent_x100 / 100.0);
        else
            std::snprintf(buf, sizeof buf, "New best!");
        rec_colour = kGold;
    } else if (c.rank > 0 && best) {
        std::snprintf(buf, sizeof buf, "Rank %d (best %.2f%%)", c.rank + 1, best->percent_x100 / 100.0);
        rec_colour = kGold;
    } else if (best) {
        std::snprintf(buf, sizeof buf, "best %.2f%% %s", best->percent_x100 / 100.0,
                      game::GradeTierName(best->tier()));
    } else {
        buf[0] = 0;
    }
    printCentred(font_, cx, 184, rec_colour, 1, buf);

    float y = 200;
    for (game::Grade g : kShown) {
        font_.print(x, y, gradeColour(g), 1, "%-10s%5d", game::GradeName(g), s.rows[(int)g]);
        y += 16;
    }
    font_.print(x, y, kWhite, 1, "%-10s%5d", "Held", s.holds_held);
    font_.print(x, y + 16, kWhite, 1, "%-10s%5d", "Let go", s.holds_let_go);
    font_.print(x, y + 32, kWhite, 1, "%-10s%5d", "Mines hit", s.mines_hit);
    font_.print(x, y + 48, kWhite, 1, "%-10s%5d", "Max combo", s.max_combo);

    if (c.rank >= 0) {   // the name entry (its box is drawn in draw())
        font_.print(cx - 84, 370, kGrey, 1, "NAME");
        for (int k = 0; k < 3; k++) font_.print(cx - 40 + k * 24, 362, kWhite, 2, "%c", c.name[k]);
        if (c.saved) {
            printRight(font_, cx + 88, 370, kGrey, "saved");
        } else if (c.naming && humans() > 1) {
            const int left = secondsLeft(now_us);
            std::snprintf(buf, sizeof buf, "%d s", left);
            printRight(font_, cx + 88, 370, left <= 5 ? kRed : kGrey, buf);
        }
    } else if (s.hits) {
        font_.print(x, 362, kGrey, 1, "mean %+6.1f ms", s.meanOffset() * 1000);
        font_.print(x, 378, kGrey, 1, "sd   %6.1f ms", s.offsetStdDev() * 1000);
    }
}

}  // namespace screens
