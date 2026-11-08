// SPDX-License-Identifier: GPL-3.0-or-later
// Font - 8x16 debug font built from libogc's console font into a GRRLIB tile set.
#pragma once

#include <grrlib.h>

namespace render {

class Font {
public:
    static constexpr int kGlyphW = 8;
    static constexpr int kGlyphH = 16;

    bool create();
    void destroy();

    // printf-style text at (x, y), top-left. colour is RGBA. scale 1 = 8x16 pixels per glyph.
    void print(float x, float y, u32 colour, float scale, const char* fmt, ...) const
        __attribute__((format(printf, 6, 7)));

private:
    GRRLIB_texImg* tex_ = nullptr;
};

}  // namespace render
