// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/wii/MenuSound.h"

#include <asndlib.h>
#include <gccore.h>
#include <malloc.h>

#include <cstring>
#include <vector>

#include "platform/wii/WiiAudio.h"

namespace wii {
namespace sfx {
namespace {

constexpr int kVoice = 1;        // voice 0 is the song / preview stream (WiiAudio)
constexpr int kRate = 48000;     // ASND's own rate: no resampling
constexpr int kVolume = 255;     // the tones themselves are quiet (MenuTones.h)

struct Buffer {
    void* data = nullptr;
    int bytes = 0;
};
Buffer g_buf[audio::kMenuSoundCount];
bool g_ready = false;

}  // namespace

void init() {
    if (g_ready) return;
    AudioStream::initAsnd();
    for (int k = 0; k < audio::kMenuSoundCount; k++) {
        const std::vector<int16_t> pcm = audio::MakeMenuTone((audio::MenuSound)k, kRate);
        // ASND reads voices by DMA: 32-byte aligned, a whole number of 32-byte lines.
        const int bytes = ((int)(pcm.size() * sizeof(int16_t)) + 31) & ~31;
        void* p = memalign(32, bytes);
        if (!p) continue;
        std::memset(p, 0, bytes);
        std::memcpy(p, pcm.data(), pcm.size() * sizeof(int16_t));   // native = big-endian 16-bit
        DCFlushRange(p, bytes);
        g_buf[k] = {p, bytes};
    }
    g_ready = true;
}

void play(audio::MenuSound s) {
    const Buffer& b = g_buf[(int)s];
    if (!g_ready || !b.data) return;
    ASND_StopVoice(kVoice);
    ASND_SetVoice(kVoice, VOICE_MONO_16BIT, kRate, 0, b.data, b.bytes, kVolume, kVolume, nullptr);
}

}  // namespace sfx
}  // namespace wii
