// SPDX-License-Identifier: GPL-3.0-or-later
// MenuTones - the four menu sounds, made in code (doc 06 §7, target menu flow): the menu
// prototype's sfx() tones, rendered to 16-bit mono PCM once at boot.
//   Move    660 Hz triangle, 50 ms
//   Select  880 Hz then 1320 Hz after 50 ms, triangle, 120 ms
//   Back    440 Hz triangle, 50 ms
//   Error   180 Hz square, 180 ms, quieter
// Each has a 5 ms exponential attack and an exponential decay to silence at its end. The
// peaks are kept low (about -14 dBFS, the error -21 dBFS) so a sound over a song preview
// doesn't drown it. Original to ddr-wii. Portable C++17: tested on the PC.
#pragma once

#include <cstdint>
#include <vector>

namespace audio {

enum class MenuSound { Move, Select, Back, Error };
constexpr int kMenuSoundCount = 4;

// The tone at rate samples per second.
std::vector<int16_t> MakeMenuTone(MenuSound s, int rate = 48000);

}  // namespace audio
