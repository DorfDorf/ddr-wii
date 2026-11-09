// SPDX-License-Identifier: GPL-3.0-or-later
// Results - after a song that played out (or that everyone failed): each player's grade, %,
// judgement counts and how the run did on the chart's top 10, with the name entry
// (doc 06 §7 target menu flow, slice 3).
//
// Gameplay has already put each human, non-autoplay run in the high-score table, named with
// that port's last name (config.ini [PortN] Name), so scores.txt is written when Gameplay
// returns even if nobody finishes typing. A run that made the chart's top 10 gets a
// three-letter name entry prefilled with that name: Up/Down change the letter (held: repeat),
// Left/Right move, START saves. Every change goes straight into the table, so a name typed but
// not saved still counts if the game is left with START+Z; saving also keeps it as the port's
// last name. Nothing saves by itself. With two players, each one's START saves their own name
// within 25 s; after that, any player's START saves every name still open (as typed), so one
// player can't hold up the other for long. With one player there's no timer. A from any
// player continues to song select once nobody is still entering a name.
#pragma once

#include <gctypes.h>

#include <string>
#include <vector>

#include "core/game/HighScores.h"
#include "core/game/Judge.h"
#include "core/input/MenuRepeat.h"
#include "screens/Session.h"

namespace wii { class Video; class Input; }
namespace render { class Font; }

namespace screens {

// One player's run, handed over by Gameplay (which is gone by the time Results runs).
struct PlayerResult {
    int port = 0;
    bool autoplay = false;          // not recorded
    bool bot = false;               // the Y test P2: autoplay, no pad
    ddr::Difficulty difficulty = ddr::Difficulty::Easy;
    int meter = 0;
    ddr::LifeBar life_bar = ddr::LifeBar::Off;
    double speed = 2.0;
    game::Score score;
    bool failed = false;
    std::string key;                // game::ChartKey
    u32 entry = 0;                  // HighScoreTable::submit's id; 0 = not in the top 10
    bool had_best = false;          // the chart had a best before this play
    game::HighScore prev_best;
};

struct SongResult {
    std::string title;
    std::vector<PlayerResult> players;   // P1 first
};

class Results {
public:
    // With two players: after this, any player's START saves every open name entry.
    static constexpr u64 kNameUs = 25000000;

    Results(wii::Video& video, const wii::Input& input, const render::Font& font, Session& session,
            SongResult result);

    // One frame of logic + drawing. Returns false when A continued to song select, or for
    // the exit chord (quitApp()).
    bool frame();
    bool quitApp() const { return quit_frames_ >= 120; }

private:
    struct Column {
        PlayerResult r;
        int rank = -1;              // in the chart's top 10 (0 = best), -1 not in it
        bool naming = false;        // the name entry is open
        bool saved = false;
        char name[4] = "AAA";
        int pos = 0;                // letter under the cursor
        input::MenuRepeat up, down;
    };

    void save(Column& c);
    void changeLetter(Column& c, int dir);
    bool anyNaming() const;
    int humans() const;                  // players with a pad (not the Y test P2)
    int secondsLeft(u64 now_us) const;   // of kNameUs, 0 once it's up
    void draw(u64 now_us);
    void drawColumn(const Column& c, int slot, u64 now_us);

    wii::Video& video_;
    const wii::Input& input_;
    const render::Font& font_;
    Session& session_;
    std::string title_;
    std::vector<Column> cols_;
    u64 start_us_ = 0;
    u64 hint_us_ = 0;               // "Press START to save the name" shown since
    u32 quit_frames_ = 0;
};

}  // namespace screens
