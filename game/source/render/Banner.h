// SPDX-License-Identifier: GPL-3.0-or-later
// Banner - loads a song or pack banner (PNG or JPEG) from the SD card into a small texture
// (doc 06 §7, M5 step 6).
//
// GRRLIB's own loaders aren't used: its PNG path returns a blank texture when a side isn't a
// multiple of 4 (ITG banners are often 418x164), and its JPEG path writes past its buffer for
// those sizes. Instead libpng (simplified API) and libjpeg decode to RGBA, and the image is
// box-filtered down to a fixed kW x kH RGBA8 texture, stretched to the banner shape as
// StepMania does. Images over kMaxSide on either side are refused.
//
// Safe to call from a worker thread: it only allocates memory and flushes the data cache.
#pragma once

#include <grrlib.h>

namespace render {

struct Banner {
    static constexpr int kW = 256, kH = 100;   // about Simply Love's 418x164 shape
    static constexpr int kMaxSide = 4096;

    // nullptr if the file can't be read or decoded. Free with GRRLIB_FreeTexture.
    static GRRLIB_texImg* load(const char* path);
};

}  // namespace render
