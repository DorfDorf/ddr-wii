// SPDX-License-Identifier: GPL-3.0-or-later
// ddr-wii entry point. M2: boots straight into the render test scene.
#include <gccore.h>
#include <stdlib.h>

#include "platform/wii/WiiVideo.h"
#include "render/ArrowArt.h"
#include "render/Font.h"
#include "screens/RenderTest.h"

int main(int, char**) {
    wii::Video video;
    if (!video.init()) return EXIT_FAILURE;
    PAD_Init();

    render::ArrowArt art;
    render::Font font;
    if (!art.create() || !font.create()) {
        video.shutdown();
        return EXIT_FAILURE;
    }

    {
        screens::RenderTest scene(video, art, font);
        video.resetStats();
        bool running = true;
        while (running) {
            video.beginFrame();
            running = scene.frame();
            video.endFrame();
        }
    }

    font.destroy();
    art.destroy();
    video.shutdown();
    return EXIT_SUCCESS;   // back to the loader (HBC / Dolphin)
}
