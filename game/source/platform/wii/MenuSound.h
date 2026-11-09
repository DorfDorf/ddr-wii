// SPDX-License-Identifier: GPL-3.0-or-later
// MenuSound - the menu sounds (doc 06 §7): core/audio/MenuTones rendered once at boot and
// played as one-shots on ASND voice 1. Songs and previews keep voice 0, and its tick counter
// is the song clock, so a menu sound never touches the clock. A new sound cuts off the one
// still playing, so held-arrow repeats don't pile up.
#pragma once

#include "core/audio/MenuTones.h"

namespace wii {
namespace sfx {

void init();   // after the video is up; starts ASND if nothing has yet
void play(audio::MenuSound s);

inline void move() { play(audio::MenuSound::Move); }
inline void select() { play(audio::MenuSound::Select); }
inline void back() { play(audio::MenuSound::Back); }
inline void error() { play(audio::MenuSound::Error); }

}  // namespace sfx
}  // namespace wii
