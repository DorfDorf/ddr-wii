// SPDX-License-Identifier: GPL-3.0-or-later
#include "MenuTones.h"

#include <cmath>

namespace audio {

std::vector<int16_t> MakeMenuTone(MenuSound s, int rate) {
    double len = 0.05, freq = 660, peak = 0.2;
    bool square = false;
    switch (s) {
        case MenuSound::Move: break;
        case MenuSound::Select: len = 0.12; freq = 880; break;
        case MenuSound::Back: freq = 440; break;
        case MenuSound::Error: len = 0.18; freq = 180; peak = 0.09; square = true; break;
    }
    constexpr double kFloor = 0.0001, kAttack = 0.005;
    const int n = (int)(len * rate);
    std::vector<int16_t> out(n);
    double phase = 0;   // in cycles
    for (int i = 0; i < n; i++) {
        const double t = (double)i / rate;
        const double f = s == MenuSound::Select && t >= 0.05 ? 1320 : freq;
        phase += f / rate;
        phase -= std::floor(phase);
        const double wave = square ? (phase < 0.5 ? 1.0 : -1.0) : 1.0 - 4.0 * std::fabs(phase - 0.5);
        // Exponential ramps, as Web Audio's exponentialRampToValueAtTime.
        const double env = t < kAttack ? kFloor * std::pow(peak / kFloor, t / kAttack)
                                       : peak * std::pow(kFloor / peak, (t - kAttack) / (len - kAttack));
        out[i] = (int16_t)std::lround(wave * env * 32767);
    }
    return out;
}

}  // namespace audio
