// SPDX-License-Identifier: GPL-3.0-or-later
// Gameplay - Milestone M4c scene: plays one song from the SD card with scrolling arrows,
// judged steps, combo and score (doc 06 §3.2, §3.4, §11.1).
//
// Songs come from the song library (core/songs/SongLibrary, sd:/ddrwii/songs/<pack>/<song>/),
// scanned once at start; L/R on port 1 pick the song until song select exists (M5 step 2).
// It starts on the generated test song (tools/make-test-song.py) if it's there. Music plays through wii::AudioStream; SongClock follows ASND's
// mixing tick as in the M4b test. Notes scroll by beat. Each player has their own chart,
// Judge and autoplay switch and is bound to a pad port. P2 joins on the ready screen;
// the results show every player side by side.
#pragma once

#include <gctypes.h>

#include <string>
#include <vector>

#include "core/chart/Chart.h"
#include "core/game/Judge.h"
#include "core/songs/SongLibrary.h"
#include "core/timing/SongClock.h"
#include "platform/wii/WiiAudio.h"

namespace wii { class Video; class Input; }
namespace render { struct ArrowArt; class Font; }

namespace screens {

class Gameplay {
public:
    Gameplay(wii::Video& video, const wii::Input& input, const render::ArrowArt& art,
             const render::Font& font);

    // Runs one frame of logic + drawing (does not present). Returns false to quit.
    bool frame();

private:
    enum class State { Error, Ready, Playing, Done };

    struct Player {
        int port = 0;
        int chart = 0;              // index into charts_
        bool autoplay = false;
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
    };

    void scanLibrary();
    void loadSong();
    void start();
    void stop(State next);
    void layout();
    void handleMenu();
    void handleEvents(double offset_us);
    void drainJudge(Player& p, u64 now_us);
    void drawPlayer(Player& p, double beat, u64 now_us);
    void drawHud(double chart_s);
    void drawReady();
    void drawResults();
    std::string playerLabel(const Player& p) const;

    wii::Video& video_;
    const wii::Input& input_;
    const render::ArrowArt& art_;
    const render::Font& font_;

    State state_ = State::Error;
    std::string error_;
    ddr::SongLibrary library_;
    struct SongRef { int pack, song; };
    std::vector<SongRef> songs_;   // every library song, in library order
    int song_index_ = 0;
    u64 scan_us_ = 0;
    std::string song_dir_;
    std::string music_;        // file name in song_dir_
    ddr::Song song_;
    std::vector<int> charts_;   // dance-single charts in song_.charts
    std::vector<Player> players_;
    double offset_s_ = 0;       // globalOffset (doc 06 §3.2): chart time = song time + this

    wii::AudioStream stream_;
    timing::SongClock clock_;
    u32 last_tick_report_ = 0;
    double end_s_ = 0;          // chart time when the song is over
    double chart_s_ = 0;
    u32 quit_frames_ = 0;
};

}  // namespace screens
