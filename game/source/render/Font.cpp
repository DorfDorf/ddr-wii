// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/Font.h"

#include <stdarg.h>
#include <stdio.h>

#include "render/Draw.h"

// libogc's console font: 256 glyphs x 16 rows, one byte per row, MSB = leftmost pixel.
extern "C" u8 console_font_8x16[];

namespace render {

bool Font::create() {
    constexpr int kCols = 16, kRows = 16;   // 256 glyphs in a 128x256 texture
    tex_ = GRRLIB_CreateEmptyTexture(kCols * kGlyphW, kRows * kGlyphH);
    if (!tex_) return false;
    for (int c = 0; c < 256; c++) {
        const int ox = (c % kCols) * kGlyphW, oy = (c / kCols) * kGlyphH;
        for (int row = 0; row < kGlyphH; row++) {
            const u8 bits = console_font_8x16[c * kGlyphH + row];
            for (int col = 0; col < kGlyphW; col++) {
                const bool on = bits & (0x80 >> col);
                GRRLIB_SetPixelTotexImg(ox + col, oy + row, tex_, on ? 0xFFFFFFFF : 0x00000000);
            }
        }
    }
    GRRLIB_FlushTex(tex_);
    GRRLIB_InitTileSet(tex_, kGlyphW, kGlyphH, 0);
    return true;
}

void Font::destroy() {
    if (tex_) GRRLIB_FreeTexture(tex_);
    tex_ = nullptr;
}

void Font::print(float x, float y, u32 colour, float scale, const char* fmt, ...) const {
    if (!tex_) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const int len = n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1;
    const float step = kGlyphW * scale;
    for (int i = 0; i < len; i++) {
        const u8 ch = (u8)buf[i];
        if (ch != ' ') tile(x + i * step, y, tex_, 0, scale, scale, colour, ch);
    }
}

}  // namespace render
