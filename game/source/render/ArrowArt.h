// SPDX-License-Identifier: GPL-3.0-or-later
// ArrowArt - placeholder note/receptor textures generated at startup.
// Generated in code so no third-party art is shipped (docs/05 M2 "Reuse").
#pragma once

#include <grrlib.h>

namespace render {

struct ArrowArt {
    static constexpr int kSize = 64;   // texture is kSize x kSize, arrow points up

    GRRLIB_texImg* note = nullptr;      // white fill + dark outline; tint with the draw colour
    GRRLIB_texImg* receptor = nullptr;  // light outline, translucent dark fill
    GRRLIB_texImg* hold = nullptr;      // translucent vertical bar; stretch in y for a hold body
    GRRLIB_texImg* glow = nullptr;      // soft radial blob for receptor glows and hit explosions

    bool create();
    void destroy();
};

}  // namespace render
