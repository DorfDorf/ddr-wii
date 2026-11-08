// SPDX-License-Identifier: GPL-3.0-or-later
// ddr-wii entry point. Boots into the M2b playfield stress test; B on port 1 switches
// to the M2 render test scene and back.
#include <gccore.h>
#include <stdlib.h>

#include "platform/wii/WiiVideo.h"
#include "render/ArrowArt.h"
#include "render/Font.h"
#include "screens/PlayfieldStress.h"
#include "screens/RenderTest.h"

namespace {

// Runs one scene until it quits (returns false) or B is pressed (returns true).
template <typename Scene>
bool runScene(wii::Video& video, Scene& scene) {
    video.resetStats();
    for (;;) {
        video.beginFrame();
        PAD_ScanPads();
        if (PAD_ButtonsDown(0) & PAD_BUTTON_B) return true;
        const bool running = scene.frame();
        video.endFrame();
        if (!running) return false;
    }
}

}  // namespace

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

    bool stress = true;
    for (;;) {
        bool next;
        if (stress) {
            screens::PlayfieldStress scene(video, art, font);
            next = runScene(video, scene);
        } else {
            screens::RenderTest scene(video, art, font);
            next = runScene(video, scene);
        }
        if (!next) break;
        stress = !stress;
    }

    font.destroy();
    art.destroy();
    video.shutdown();
    return EXIT_SUCCESS;   // back to the loader (HBC / Dolphin)
}
