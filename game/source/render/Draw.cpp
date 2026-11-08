// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/Draw.h"

namespace render {
namespace {

DrawCounts g_counts;
const GRRLIB_texImg* g_last_tex = nullptr;

void countTextured(const GRRLIB_texImg* tex, float w, float h) {
    g_counts.draws++;
    if (tex != g_last_tex) g_counts.tex_switches++;
    g_last_tex = tex;
    g_counts.pixels += w * h;
}

}  // namespace

void resetCounts() {
    g_counts = DrawCounts{};
    g_last_tex = nullptr;
}

const DrawCounts& counts() { return g_counts; }

void img(float x, float y, const GRRLIB_texImg* tex, float degrees, float sx, float sy, u32 colour) {
    if (!tex) return;
    countTextured(tex, tex->w * sx, tex->h * sy);
    GRRLIB_DrawImg(x, y, tex, degrees, sx, sy, colour);
}

void tile(float x, float y, const GRRLIB_texImg* tex, float degrees, float sx, float sy, u32 colour,
          int frame) {
    if (!tex) return;
    countTextured(tex, tex->tilew * sx, tex->tileh * sy);
    GRRLIB_DrawTile(x, y, tex, degrees, sx, sy, colour, frame);
}

void rect(float x, float y, float w, float h, u32 colour) {
    g_counts.draws++;
    g_counts.pixels += w * h;
    GRRLIB_Rectangle(x, y, w, h, colour, true);
}

}  // namespace render
