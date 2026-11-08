// SPDX-License-Identifier: GPL-3.0-or-later
// ddr-wii entry point. Boots into the M4b audio clock test; B on port 1 cycles through it,
// the M2b playfield stress test and the M2 render test.
#include <fat.h>
#include <gccore.h>
#include <stdlib.h>

#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/ArrowArt.h"
#include "render/Font.h"
#include "screens/AudioClockTest.h"
#include "screens/PlayfieldStress.h"
#include "screens/RenderTest.h"

namespace {

// Runs one scene until it quits (returns false) or B is pressed (returns true).
template <typename Scene>
bool runScene(wii::Video& video, wii::Input& input, Scene& scene) {
    video.resetStats();
    for (;;) {
        video.beginFrame();
        input.update();   // drains the timestamped SI samples; scenes read input from here
        if (input.down(0) & PAD_BUTTON_B) return true;
        const bool running = scene.frame();
        video.endFrame();
        if (!running) return false;
    }
}

}  // namespace

int main(int, char**) {
    wii::Video video;
    if (!video.init()) return EXIT_FAILURE;
    wii::Input input;
    input.init();

    render::ArrowArt art;
    render::Font font;
    if (!art.create() || !font.create()) {
        video.shutdown();
        return EXIT_FAILURE;
    }

    fatInitDefault();   // SD card (sd:/); the audio test reports it if this failed

    int which = 0;
    for (;;) {
        bool next;
        if (which == 0) {
            screens::AudioClockTest scene(video, input, font);
            next = runScene(video, input, scene);
        } else if (which == 1) {
            screens::PlayfieldStress scene(video, input, art, font);
            next = runScene(video, input, scene);
        } else {
            screens::RenderTest scene(video, input, art, font);
            next = runScene(video, input, scene);
        }
        if (!next) break;
        which = (which + 1) % 3;
    }

    input.shutdown();
    font.destroy();
    art.destroy();
    video.shutdown();
    return EXIT_SUCCESS;   // back to the loader (HBC / Dolphin)
}
