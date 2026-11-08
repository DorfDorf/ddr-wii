// SPDX-License-Identifier: GPL-3.0-or-later
// SongSelect - choose a song and each player's difficulty (doc 06 §7, §11.2; M5 step 2).
//
// The layout follows ITGmania's default theme, Simply Love (GPLv3), in its 4:3 form: banner
// top left, the song's artist/BPM/length under it, the difficulty grid with meter blocks, one
// pane per player along the bottom with the chart's counts, and the music wheel on the right
// (packs as folders, the open pack's songs under its header). Re-drawn here with our own
// placeholder graphics and bitmap font; no theme files are used.
//
// Controls, as on a dance mat in ITG: Left/Right move the wheel (held: repeat), Up/Down change
// that player's difficulty, START opens a pack or picks the song. START on a port that hasn't
// joined joins it as the next player. Debug: Z toggles that player's autoplay, X leaves,
// Y on port 1 adds/removes an autoplay player, L/R on port 1 change the global offset.
#pragma once

#include <gctypes.h>

#include <vector>

#include "screens/Session.h"

namespace wii { class Video; class Input; }
namespace render { struct ArrowArt; class Font; }

namespace screens {

class SongSelect {
public:
    SongSelect(wii::Video& video, const wii::Input& input, const render::Font& font, Session& session);

    // One frame of logic + drawing. Returns false when a song was picked (picked() is true)
    // or the exit chord (START+Z for 2 s) was held.
    bool frame();
    bool picked() const { return picked_; }

private:
    struct Item { int pack, song; };   // song -1: the pack's header

    void buildItems();
    void moveCursor(int delta);
    void pickCharts();                 // each player's chart nearest their preferred difficulty
    void handleInput();
    void draw();
    void drawSongInfo(const ddr::SongEntry* s);
    void drawGrid(const ddr::SongEntry* s);
    void drawPane(int slot, const ddr::SongEntry* s);
    void drawWheel();

    wii::Video& video_;
    const wii::Input& input_;
    const render::Font& font_;
    Session& session_;

    std::vector<Item> items_;
    int cursor_ = 0;          // into items_
    float scroll_ = 0;        // drawn position, eases towards cursor_
    int repeat_dir_ = 0;      // held Left/Right
    u64 repeat_at_us_ = 0;
    u32 frames_ = 0;
    u32 quit_frames_ = 0;
    bool picked_ = false;
};

}  // namespace screens
