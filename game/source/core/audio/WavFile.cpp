// SPDX-License-Identifier: GPL-3.0-or-later
#include "WavFile.h"

#include <cstring>

namespace audio {
namespace {

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

}  // namespace

bool parseWavHeader(const uint8_t* b, size_t size, uint64_t file_size, WavInfo& out) {
    out = WavInfo{};
    if (size < 12 || std::memcmp(b, "RIFF", 4) || std::memcmp(b + 8, "WAVE", 4)) return false;
    bool have_fmt = false;
    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8_t* id = b + pos;
        const uint32_t len = le32(b + pos + 4);
        const size_t body = pos + 8;
        if (!std::memcmp(id, "fmt ", 4)) {
            if (len < 16 || body + 16 > size) return false;
            uint16_t format = le16(b + body);
            const uint16_t channels = le16(b + body + 2);
            const uint32_t rate = le32(b + body + 4);
            const uint16_t bits = le16(b + body + 14);
            // WAVE_FORMAT_EXTENSIBLE: the real format is the first 2 bytes of the sub-format GUID.
            if (format == 0xFFFE && len >= 40 && body + 26 <= size) format = le16(b + body + 24);
            if (format != 1 || bits != 16 || (channels != 1 && channels != 2) || rate == 0) return false;
            out.rate = rate;
            out.channels = channels;
            have_fmt = true;
        } else if (!std::memcmp(id, "data", 4)) {
            if (!have_fmt) return false;
            uint64_t avail = file_size > body ? file_size - body : 0;
            uint64_t bytes = len < avail ? len : avail;
            bytes -= bytes % (2u * out.channels);   // whole frames only
            out.data_offset = (uint32_t)body;
            out.data_bytes = (uint32_t)bytes;
            return true;
        }
        pos = body + len + (len & 1);   // chunks are padded to even sizes
    }
    return false;
}

}  // namespace audio
