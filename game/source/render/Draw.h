// SPDX-License-Identifier: GPL-3.0-or-later
// Draw - thin wrappers over GRRLIB draw calls that count what each frame costs:
// draw calls, texture switches and drawn pixel area (doc 06 §11.5). The counts are
// the same in Dolphin and on hardware, so they show where the budget goes.
#pragma once

#include <grrlib.h>

namespace render {

struct DrawCounts {
    u32 draws = 0;          // GRRLIB draw calls (each is a CPU-side setup + one quad)
    u32 tex_switches = 0;   // textured draws whose texture differs from the previous one
    float pixels = 0;       // screen area covered by every quad, transparent texels included
};

void resetCounts();
const DrawCounts& counts();

// Same arguments as GRRLIB_DrawImg / GRRLIB_DrawTile / GRRLIB_Rectangle (filled).
void img(float x, float y, const GRRLIB_texImg* tex, float degrees, float sx, float sy, u32 colour);
void tile(float x, float y, const GRRLIB_texImg* tex, float degrees, float sx, float sy, u32 colour,
          int frame);
void rect(float x, float y, float w, float h, u32 colour);

}  // namespace render
