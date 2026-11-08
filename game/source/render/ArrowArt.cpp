// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/ArrowArt.h"

#include <math.h>

namespace render {
namespace {

struct Pt { float x, y; };

// Up-pointing arrow in a 64x64 box: triangular head over a rectangular stem.
constexpr Pt kArrow[] = {
    {32, 2}, {62, 32}, {44, 32}, {44, 62}, {20, 62}, {20, 32}, {2, 32},
};
constexpr int kArrowN = sizeof(kArrow) / sizeof(kArrow[0]);

bool insidePoly(float x, float y) {
    bool in = false;
    for (int i = 0, j = kArrowN - 1; i < kArrowN; j = i++) {
        const Pt a = kArrow[i], b = kArrow[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

float distToEdges(float x, float y) {
    float best = 1e9f;
    for (int i = 0, j = kArrowN - 1; i < kArrowN; j = i++) {
        const Pt a = kArrow[j], b = kArrow[i];
        const float vx = b.x - a.x, vy = b.y - a.y;
        float t = ((x - a.x) * vx + (y - a.y) * vy) / (vx * vx + vy * vy);
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        const float dx = a.x + t * vx - x, dy = a.y + t * vy - y;
        const float d = sqrtf(dx * dx + dy * dy);
        if (d < best) best = d;
    }
    return best;
}

struct Rgba { float r, g, b, a; };

// Rasterise the arrow with 4x4 supersampling. `pick` maps (inside, edge distance, y) to a colour.
template <typename Pick>
GRRLIB_texImg* rasterise(Pick pick) {
    const int n = ArrowArt::kSize;
    GRRLIB_texImg* tex = GRRLIB_CreateEmptyTexture(n, n);
    if (!tex) return nullptr;
    constexpr int kSS = 4;
    for (int py = 0; py < n; py++) {
        for (int px = 0; px < n; px++) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < kSS; sy++) {
                for (int sx = 0; sx < kSS; sx++) {
                    const float x = px + (sx + 0.5f) / kSS, y = py + (sy + 0.5f) / kSS;
                    if (!insidePoly(x, y)) continue;
                    const Rgba c = pick(distToEdges(x, y), y);
                    r += c.r * c.a; g += c.g * c.a; b += c.b * c.a; a += c.a;
                }
            }
            u32 out = 0;
            if (a > 0) {
                const auto u8c = [](float v) { return (u32)(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f)); };
                out = (u8c(r / a) << 24) | (u8c(g / a) << 16) | (u8c(b / a) << 8) | u8c(a / (kSS * kSS));
            }
            GRRLIB_SetPixelTotexImg(px, py, tex, out);
        }
    }
    GRRLIB_FlushTex(tex);
    GRRLIB_SetMidHandle(tex, true);   // rotate around the centre
    return tex;
}

// Fill a texture pixel by pixel. `pick(x, y)` returns RGBA for the pixel centre.
template <typename Pick>
GRRLIB_texImg* generate(Pick pick) {
    const int n = ArrowArt::kSize;
    GRRLIB_texImg* tex = GRRLIB_CreateEmptyTexture(n, n);
    if (!tex) return nullptr;
    for (int py = 0; py < n; py++)
        for (int px = 0; px < n; px++) GRRLIB_SetPixelTotexImg(px, py, tex, pick(px + 0.5f, py + 0.5f));
    GRRLIB_FlushTex(tex);
    GRRLIB_SetMidHandle(tex, true);
    return tex;
}

}  // namespace

bool ArrowArt::create() {
    note = rasterise([](float d, float y) -> Rgba {
        if (d < 3.0f) return {16, 16, 16, 255};             // outline
        const float shade = 255.0f - 70.0f * (y / kSize);   // lighter at the tip
        return {shade, shade, shade, 255};
    });
    receptor = rasterise([](float d, float) -> Rgba {
        if (d < 4.0f) return {220, 220, 220, 255};          // outline
        return {0, 0, 0, 110};                               // translucent body
    });
    hold = generate([](float x, float) -> u32 {
        const float d = fabsf(x - kSize / 2.0f);             // distance from the bar's centre line
        if (d > 22.0f) return 0;
        if (d > 18.0f) return 0xF0F0F0E0;                     // bright edges
        return 0xC0C0C0A0;                                    // translucent body
    });
    glow = generate([](float x, float y) -> u32 {
        const float dx = x - kSize / 2.0f, dy = y - kSize / 2.0f;
        const float d = sqrtf(dx * dx + dy * dy) / (kSize / 2.0f);
        if (d >= 1.0f) return 0;
        const float a = (1.0f - d) * (1.0f - d);
        return 0xFFFFFF00 | (u32)(a * 255.0f + 0.5f);
    });
    return note && receptor && hold && glow;
}

void ArrowArt::destroy() {
    GRRLIB_texImg* all[] = {note, receptor, hold, glow};
    for (GRRLIB_texImg* t : all)
        if (t) GRRLIB_FreeTexture(t);
    note = receptor = hold = glow = nullptr;
}

}  // namespace render
