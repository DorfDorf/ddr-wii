// SPDX-License-Identifier: GPL-3.0-or-later
// Gameplay - plays the song SongSelect picked (M4c, M5 step 2): scrolling arrows, judged
// steps, combo and score, then the results (doc 06 §3.2, §3.4, §11.1).
//
// The song, the players (P1 first, each with their port, chart, life bar and autoplay) and the
// global offset come from the Session. Music plays through wii::AudioStream; SongClock follows
// ASND's mixing tick as in the M4b test, and runs on the CPU clock once the audio has ended so
// a chart that outlasts its audio still plays out. Notes scroll by beat at each player's
// speed (ITG x-mod: 1x = 64 px per beat). Mines and rolls are judged (core/game/Judge).
// A player with the life bar On has ITG's bar (core/game/LifeMeter): if it empties they fail,
// their playfield greys out and stops judging, and the song ends once everyone has failed
// (doc 06 §11.4). With "On, no fail" the bar keeps moving after it empties (ITG's FailType
// Off) and never fails them; with Off there's no bar. Once the song is over, frame() returns
// false with finished() set, and main.cpp shows screens/Results with result().
// START from any player's pad pauses the song for everyone (doc 06 §7): the music, the song
// clock, judging and autoplay stop. Anyone's START continues; only the player who paused moves
// the cursor and picks Restart (the same song, players and settings, from the start) or Quit
// (back to song select).
// A song that plays out (or that everyone failed) records each human player's score in the
// Session's high-score table, named with the port's last name (Results renames it); a quit
// song and autoplay players record nothing (doc 06 §7).
#pragma once

#include <gctypes.h>

#include <string>
#include <vector>

#include "core/chart/Chart.h"
#include "core/game/Judge.h"
#include "core/game/LifeMeter.h"
#include "screens/Results.h"
#include "screens/Session.h"
#include "core/timing/SongClock.h"
#include "platform/wii/WiiAudio.h"

namespace wii { class Video; class Input; }
namespace render { struct ArrowArt; class Font; }

namespace screens {

class Gameplay {
public:
    Gameplay(wii::Video& video, const wii::Input& input, const render::ArrowArt& art,
             const render::Font& font, Session& session);

    // Runs one frame of logic + drawing (does not present). Returns false when done: the song
    // is over (finished(): show the results), it was quit, or quit the app if quitApp().
    bool frame();
    bool quitApp() const { return quit_frames_ >= 120; }
    bool finished() const { return state_ == State::Done; }
    const SongResult& result() const { return result_; }

private:
    enum class State { Error, Playing, Done };

    struct Player {
        int port = 0;
        int chart = 0;              // index into song_.charts
        bool autoplay = false;
        bool bot = false;           // Y's test P2: no pad, can't pause
        double input_offset_s = 0;  // config.ini [PortN] InputOffset
        double speed = 2.0;         // scroll speed (x-mod)
        ddr::LifeBar life_bar = ddr::LifeBar::Off;
        game::LifeMeter life;       // life bar On or NoFail
        bool failed = false;
        u64 failed_us = 0;
        u64 mine_flash_us[game::Judge::kMaxLanes] = {};
        float centre_x = 320;       // playfield layout (doc 06 §11.3)
        game::Judge judge;
        std::vector<game::AutoStep> steps;
        size_t next_step = 0;
        size_t first_visible = 0;   // notes before this are gone for good
        // Feedback for the HUD.
        game::Grade row_grade = game::Grade::None;
        u64 row_grade_us = 0;
        double note_offset = 0;     // last hit's error, seconds
        bool note_early_only = false;
        u64 note_offset_us = 0;
        u64 lane_flash_us[game::Judge::kMaxLanes] = {};
        game::Grade lane_flash[game::Judge::kMaxLanes] = {};
        // High score (doc 06 §7).
        std::string score_key;      // game::ChartKey for this chart
        bool had_best = false;      // a best existed before this play
        game::HighScore prev_best;
    };

    bool loadSong(const Session& session);
    void start();
    void stop(State next);
    void finish();              // the song played out: record the scores, then the results
    void pause(int by, u64 now_us);
    void resume(u64 now_us);
    enum class PauseAction { None, Restart, Quit };
    PauseAction pauseInput(u64 now_us);
    bool startPressed(int port) const;   // START down, not the START+Z exit chord
    void layout();
    void handleEvents(double offset_us);
    void drainJudge(Player& p, u64 now_us);
    void drawPlayer(Player& p, double beat, u64 now_us);
    void drawLife(const Player& p);
    void drawHud(double chart_s);
    void drawPause(u64 now_us);
    std::string playerLabel(const Player& p) const;

    wii::Video& video_;
    const wii::Input& input_;
    const render::ArrowArt& art_;
    const render::Font& font_;

    State state_ = State::Error;
    u64 all_failed_us_ = 0;     // when the last player failed (the song ends shortly after)
    Session& session_;
    bool paused_ = false;       // the pause menu is up
    int pause_by_ = 0;          // index into players_ of who paused
    int pause_cursor_ = 0;      // 0 Continue, 1 Restart, 2 Quit
    u64 pause_us_ = 0;          // when it paused
    u64 hint_us_ = 0;           // "Press START to continue" shown since
    std::string error_;
    std::string song_dir_;
    std::string music_;        // file name in song_dir_
    ddr::Song song_;
    std::vector<Player> players_;
    double offset_s_ = 0;       // config.ini GlobalOffset (doc 06 §3.2): chart time = song time + this

    wii::AudioStream stream_;
    timing::SongClock clock_;
    u32 last_tick_report_ = 0;
    double end_s_ = 0;          // chart time when the song is over
    double chart_s_ = 0;
    u32 quit_frames_ = 0;
    SongResult result_;         // filled by finish()
};

}  // namespace screens
