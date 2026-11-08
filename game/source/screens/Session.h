// SPDX-License-Identifier: GPL-3.0-or-later
// Session - what SongSelect hands to Gameplay and keeps between songs: the library, the
// wheel position, the chosen song and the joined players in join order (doc 06 §7, §11.2).
#pragma once

#include <gctypes.h>

#include <vector>

#include <string>

#include "core/chart/Chart.h"
#include "core/config/Config.h"
#include "core/songs/SongLibrary.h"

namespace screens {

struct SessionPlayer {
    int port = 0;                                         // pad port, 0-3
    bool autoplay = false;
    ddr::Difficulty preferred = ddr::Difficulty::Easy;    // kept from song to song, as in ITG
    int chart = 0;                                        // index into SongEntry::charts
    double speed = 2.0;                                   // scroll speed (x-mod), from config.ini
};

struct Session {
    static constexpr int kMaxPlayers = 2;   // 4 after M6 (doc 06 §11)

    ddr::SongLibrary library;
    u64 scan_us = 0;
    bool scanned = false;                   // false: sd:/ddrwii/songs couldn't be opened

    // Music wheel: the open pack (-1 none) and the cursor (song -1 = on the pack's header).
    int open_pack = -1;
    int pack = 0, song = -1;

    std::vector<SessionPlayer> players;     // player number = position (join order)
    ddr::Config config;                     // config.ini: global offset, per-port input offsets
    bool config_loaded = false;             // false: no config.ini yet (defaults)
    bool config_dirty = false;              // changed since the last save (main.cpp saves it)
    std::string message;                    // one-line note for song select (e.g. "offset saved")

    const ddr::SongEntry* chosen() const {
        if (pack < 0 || pack >= (int)library.packs.size()) return nullptr;
        const ddr::PackEntry& p = library.packs[pack];
        return song >= 0 && song < (int)p.songs.size() ? &p.songs[song] : nullptr;
    }
};

}  // namespace screens
