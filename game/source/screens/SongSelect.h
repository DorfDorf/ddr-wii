// SPDX-License-Identifier: GPL-3.0-or-later
// SongSelect - choose a song, then each player's settings in the song settings popup
// (doc 06 §7 target menu flow, §11.2; M5 step 2).
//
// The layout follows ITGmania's default theme, Simply Love (GPLv3), in its 4:3 form: banner
// top left, the song's artist/BPM/length under it, the difficulty grid with meter blocks, one
// pane per player along the bottom with the chart's counts, and the music wheel on the right
// (packs as folders, the open pack's songs under its header). Re-drawn here with our own
// placeholder graphics and bitmap font; no theme files are used.
//
// The players come from the Players screen. Any player moves the wheel: Up/Down (held: repeat
// after 0.4 s every 75 ms, every 35 ms after 2 s), Right opens a pack, Left closes it, A opens
// a pack or the song settings popup, Z (on a song) the high scores, B goes back to Players.
// R on port 1 (controller) opens the offset calibration. START+Z for 2 s exits.
//
// The high scores popup shows one chart's top 10 (rank, name, speed, %, grade and "no bar" /
// "no fail" tags), starting on the chart of the player who pressed Z. Any player's Left/Right
// steps through the song's charts; B or Z closes it.
//
// The popup has one column per player, each with their own cursor: Difficulty (the song's
// charts; remembered from song to song), Speed (0.25x-8x, saved per port), Life bar (Off /
// On / On, no fail; saved per port), Autoplay (a demo, not scored) and Ready. Up/Down pick a
// row, Left/Right change it, A on another row jumps to Ready, A on Ready readies the player
// and locks their arrows, A again un-readies. B (anyone) closes it for everyone. The song
// starts when every player is ready. An autoplay P2 added with Y on Players is always ready.
#pragma once

#include <gctypes.h>

#include <vector>

#include "core/input/MenuRepeat.h"
#include "screens/Session.h"
#include "screens/SongMedia.h"

namespace wii { class Video; class Input; }
namespace render { struct ArrowArt; class Font; }

namespace screens {

class SongSelect {
public:
    SongSelect(wii::Video& video, const wii::Input& input, const render::Font& font, Session& session);

    // One frame of logic + drawing. Returns false when a song was picked (picked()), the
    // calibration screen was asked for (calibrate()), B went back (back()) or the exit chord
    // (START+Z for 2 s) was held.
    bool frame();
    bool picked() const { return picked_; }
    bool calibrate() const { return calibrate_; }
    bool back() const { return back_; }

private:
    struct Item { int pack, song; };   // song -1: the pack's header
    static constexpr int kRows = 5;    // popup rows: difficulty, speed, life bar, autoplay, ready

    void buildItems();
    void moveCursor(int delta);
    void setPackOpen(int pack, bool open);   // cursor goes to the pack's header
    void pickCharts();                 // each player's chart nearest their preferred difficulty
    void handleWheel();
    void openPopup();
    void handlePopup();
    void handleScores();
    void draw();
    void drawSongInfo(const ddr::SongEntry* s);
    void drawGrid(const ddr::SongEntry* s);
    void drawPane(int slot, const ddr::SongEntry* s);
    void drawWheel();
    void drawPopup(const ddr::SongEntry* s);
    void drawScores(const ddr::SongEntry* s);
    void updateMedia();                // ask for the banner and preview once the cursor rests

    wii::Video& video_;
    const wii::Input& input_;
    const render::Font& font_;
    Session& session_;

    std::vector<Item> items_;
    int cursor_ = 0;          // into items_
    float scroll_ = 0;        // drawn position, eases towards cursor_
    input::MenuRepeat wheel_up_, wheel_down_;
    u32 frames_ = 0;
    u32 message_frame_ = 0;   // when session_.message was last set
    u32 quit_frames_ = 0;
    bool picked_ = false;
    bool calibrate_ = false;
    bool back_ = false;

    bool popup_ = false;
    int row_[Session::kMaxPlayers] = {};
    input::MenuRepeat speed_rep_[Session::kMaxPlayers][2];   // Left, Right on the speed row

    bool scores_ = false;              // the high scores popup is up
    int scores_chart_ = 0;             // the chart it shows (index into SongEntry::charts)

    struct PaneBest {                  // drawPane's cached high-score lookup, per slot
        int pack = -2, song = -2, chart = -1;
        const game::ChartScores* entry = nullptr;
    };
    PaneBest pane_best_[Session::kMaxPlayers];

    SongMedia media_;                  // banner + preview worker (M5 step 6)
    int media_pack_ = -2, media_song_ = -2;   // the row the cursor was last seen on
    u64 media_moved_us_ = 0;
    bool media_requested_ = false;
};

}  // namespace screens
