// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/Banner.h"

#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <jpeglib.h>
#include <png.h>

namespace render {
namespace {

bool readFile(const char* path, std::vector<unsigned char>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bool ok = size > 8 && size < 16 * 1024 * 1024;
    if (ok) {
        out.resize((size_t)size);
        ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    }
    std::fclose(f);
    return ok;
}

bool decodePng(const std::vector<unsigned char>& file, std::vector<unsigned char>& rgba, int& w, int& h) {
    png_image img;
    std::memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&img, file.data(), file.size())) return false;
    if (img.width < 1 || img.height < 1 || img.width > (unsigned)Banner::kMaxSide ||
        img.height > (unsigned)Banner::kMaxSide) {
        png_image_free(&img);
        return false;
    }
    img.format = PNG_FORMAT_RGBA;
    rgba.resize(PNG_IMAGE_SIZE(img));
    if (!png_image_finish_read(&img, nullptr, rgba.data(), 0, nullptr)) {
        png_image_free(&img);
        return false;
    }
    w = (int)img.width;
    h = (int)img.height;
    return true;
}

// libjpeg's default error handler calls exit(); jump back here instead.
struct JpegError {
    jpeg_error_mgr mgr;
    std::jmp_buf jump;
};
void jpegErrorExit(j_common_ptr c) { std::longjmp(((JpegError*)c->err)->jump, 1); }

bool decodeJpeg(const std::vector<unsigned char>& file, std::vector<unsigned char>& rgba, int& w, int& h) {
    jpeg_decompress_struct cinfo;
    JpegError err;
    cinfo.err = jpeg_std_error(&err.mgr);
    err.mgr.error_exit = jpegErrorExit;
    if (setjmp(err.jump)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, file.data(), (unsigned long)file.size());
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    w = (int)cinfo.output_width;
    h = (int)cinfo.output_height;
    if (w < 1 || h < 1 || w > Banner::kMaxSide || h > Banner::kMaxSide || cinfo.output_components != 3) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    std::vector<unsigned char> row((size_t)w * 3);
    rgba.resize((size_t)w * h * 4);
    while (cinfo.output_scanline < cinfo.output_height) {
        const int y = (int)cinfo.output_scanline;
        JSAMPROW r = row.data();
        jpeg_read_scanlines(&cinfo, &r, 1);
        unsigned char* dst = &rgba[(size_t)y * w * 4];
        for (int x = 0; x < w; x++) {
            dst[4 * x] = row[3 * x];
            dst[4 * x + 1] = row[3 * x + 1];
            dst[4 * x + 2] = row[3 * x + 2];
            dst[4 * x + 3] = 255;
        }
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return true;
}

}  // namespace

GRRLIB_texImg* Banner::load(const char* path) {
    std::vector<unsigned char> file, rgba;
    if (!readFile(path, file)) return nullptr;
    int w = 0, h = 0;
    const bool jpeg = file[0] == 0xFF && file[1] == 0xD8;
    if (!(jpeg ? decodeJpeg(file, rgba, w, h) : decodePng(file, rgba, w, h))) return nullptr;
    file.clear();
    file.shrink_to_fit();

    GRRLIB_texImg* tex = GRRLIB_CreateEmptyTexture(kW, kH);
    if (!tex || !tex->data) {
        if (tex) GRRLIB_FreeTexture(tex);
        return nullptr;
    }
    // Box filter: each texel averages the source pixels it covers (at least one).
    for (int ty = 0; ty < kH; ty++) {
        const int y0 = ty * h / kH;
        int y1 = (ty + 1) * h / kH;
        if (y1 <= y0) y1 = y0 + 1;
        for (int tx = 0; tx < kW; tx++) {
            const int x0 = tx * w / kW;
            int x1 = (tx + 1) * w / kW;
            if (x1 <= x0) x1 = x0 + 1;
            unsigned sum[4] = {0, 0, 0, 0};
            for (int y = y0; y < y1; y++) {
                const unsigned char* p = &rgba[((size_t)y * w + x0) * 4];
                for (int x = x0; x < x1; x++, p += 4) {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    sum[3] += p[3];
                }
            }
            const unsigned n = (unsigned)((y1 - y0) * (x1 - x0));
            const u32 c = (sum[0] / n) << 24 | (sum[1] / n) << 16 | (sum[2] / n) << 8 | (sum[3] / n);
            GRRLIB_SetPixelTotexImg(tx, ty, tex, c);
        }
    }
    GRRLIB_FlushTex(tex);
    return tex;
}

}  // namespace render
