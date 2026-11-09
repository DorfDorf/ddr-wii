// SPDX-License-Identifier: GPL-3.0-or-later
// Session - what the menus hand to Gameplay and keep between songs: the library, the wheel
// position, the chosen song and the players set on the Players screen, P1 first (the lower
// port, on the left; doc 06 §7, §11.2).
#pragma once

#include <gctypes.h>

#include <vector>

#include <string>

#include "core/chart/Chart.h"
#include "core/config/Config.h"
#include "core/game/HighScores.h"
#include "core/songs/SongLibrary.h"

namespace screens {

struct SessionPlayer {
    int port = 0;                                         // pad port, 0-3
    bool autoplay = false;                                // chosen in the song settings popup
    bool bot = false;                                     // Y on Players: autoplay, no pad
    ddr::LifeBar life_bar = ddr::LifeBar::Off;            // from config.ini, set in the popup
    bool ready = false;                                   // in the song settings popup
    ddr::Difficulty preferred = ddr::Difficulty::Easy;    // kept from song to song, as in ITG
    int chart = 0;                                        // index into SongEntry::charts
    double speed = 2.0;                                   // scroll speed (x-mod), from config.ini
};

struct Session {
    static constexpr int kMaxPlayers = 2;   // 4 after M6 (doc 06 §11)

    ddr::SongLibrary library;
    u64 scan_us = 0;
    bool scanned = false;                   // false: sd:/ddrwii/songs couldn't be opened
    int cache_hits = 0;                     // songs taken from cache.txt instead of parsed

    // Music wheel: the open pack (-1 none) and the cursor (song -1 = on the pack's header).
    int open_pack = -1;
    int pack = 0, song = -1;

    std::vector<SessionPlayer> players;     // player number = position, sorted by port
    int lead_port = -1;                     // the port that leads the Players screen
    ddr::Config config;                     // config.ini: global offset, per-port input offsets
    bool config_loaded = false;             // false: no config.ini yet (defaults)
    bool config_dirty = false;              // changed since the last save (main.cpp saves it)
    std::string message;                    // one-line note for song select (e.g. "offset saved")
    game::HighScoreTable scores;            // scores.txt: machine bests per chart
    bool scores_dirty = false;              // Gameplay recorded a play (main.cpp saves it)

    const ddr::SongEntry* chosen() const {
        if (pack < 0 || pack >= (int)library.packs.size()) return nullptr;
        const ddr::PackEntry& p = library.packs[pack];
        return song >= 0 && song < (int)p.songs.size() ? &p.songs[song] : nullptr;
    }
};

}  // namespace screens
