// SPDX-License-Identifier: GPL-3.0-or-later
// WavFile - parse a RIFF/WAVE header: format and where the PCM data is (doc 06 §4).
// Only 16-bit PCM, mono or stereo, which is what the M4b click tracks and most WAVs use.
// Portable C++17, no libogc: tested on the PC (game/tests).
#pragma once

#include <cstddef>
#include <cstdint>

namespace audio {

struct WavInfo {
    uint32_t rate = 0;          // frames per second
    uint16_t channels = 0;      // 1 or 2
    uint32_t data_offset = 0;   // byte offset of the first sample in the file
    uint32_t data_bytes = 0;    // PCM bytes (clipped to what the file holds)
    uint32_t frames() const { return channels ? data_bytes / (2u * channels) : 0; }
};

// Parses the first `size` bytes of a WAV file (the header plus at least the start of the
// "data" chunk; 4 KB is plenty). file_size is the whole file's size, so a truncated file
// still gives a usable data_bytes. Returns false for anything that isn't 16-bit PCM.
bool parseWavHeader(const uint8_t* bytes, size_t size, uint64_t file_size, WavInfo& out);

}  // namespace audio
