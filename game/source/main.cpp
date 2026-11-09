// SPDX-License-Identifier: GPL-3.0-or-later
// ddr-wii entry point. Loads config.ini, scans the song library, then boots into the Players
// screen (1 or 2 players), then song select, which hands the picked song to gameplay, then the
// results (name entry), then back to song select. B in song select goes back to Players; B on Players (until the
// title screen exists) cycles through the M4b audio clock test, the M2b playfield stress test
// and the M2 render test, and back.
#include <fat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdlib.h>

#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/ArrowArt.h"
#include "render/Font.h"
#include "screens/AudioClockTest.h"
#include "screens/Gameplay.h"
#include "screens/OffsetCalibration.h"
#include "screens/Players.h"
#include "screens/PlayfieldStress.h"
#include "screens/RenderTest.h"
#include "screens/Results.h"
#include "screens/Session.h"
#include "screens/SongSelect.h"

namespace {

constexpr const char* kSongsRoot = "sd:/ddrwii/songs";

// Runs one scene until it quits (returns false) or, with b_next, B is pressed (returns true).
template <typename Scene>
bool runScene(wii::Video& video, wii::Input& input, Scene& scene, bool b_next = true) {
    video.resetStats();
    for (;;) {
        video.beginFrame();
        input.update();   // drains the timestamped SI samples; scenes read input from here
        if (b_next && (input.down(0) & PAD_BUTTON_B)) return true;
        const bool running = scene.frame();
        video.endFrame();
        if (!running) return false;
    }
}

// Writes config.ini if a screen changed it (scroll speeds, life bars).
void saveConfigIfChanged(screens::Session& session) {
    if (!session.config_dirty) return;
    session.config_dirty = false;
    if (!ddr::SaveConfig(screens::OffsetCalibration::kConfigPath, session.config))
        session.message = "Couldn't write sd:/ddrwii/config.ini";
}

constexpr const char* kScoresPath = "sd:/ddrwii/scores.txt";
constexpr const char* kCachePath = "sd:/ddrwii/cache.txt";

// Writes scores.txt after a song that recorded a score (when Gameplay returns, so the runs are
// kept even if the Wii is switched off on Results) and again after Results if a name changed.
void saveScoresIfChanged(screens::Session& session) {
    if (!session.scores_dirty) return;
    session.scores_dirty = false;
    if (!game::SaveHighScores(kScoresPath, session.scores))
        session.message = "Couldn't write sd:/ddrwii/scores.txt";
}

}  // namespace

int main(int, char**) {
    wii::Video video;
    if (!video.init()) return EXIT_FAILURE;
    render::ArrowArt art;
    render::Font font;
    if (!art.create() || !font.create()) {
        video.shutdown();
        return EXIT_FAILURE;
    }

    fatInitDefault();   // SD card (sd:/); song select and the audio test report it if this failed

    // config.ini first: it sets the pad debounce and the sync of packs without a Pack.ini.
    screens::Session session;
    session.config_loaded = ddr::LoadConfig(screens::OffsetCalibration::kConfigPath, session.config);
    game::LoadHighScores(kScoresPath, session.scores);   // none yet is fine
    wii::Input input;
    input.init((u32)session.config.inputDebounceMs * 1000);

    // The song cache (M5 step 6): only simfiles that changed since the last boot are parsed.
    const u64 t0 = ticks_to_microsecs(gettime());
    ddr::SongCache cache;
    ddr::LoadSongCache(kCachePath, cache);   // none yet is fine
    session.scanned = ddr::ScanSongLibrary(kSongsRoot, session.library, session.config.defaultSyncOffset,
                                           &cache);
    session.scan_us = ticks_to_microsecs(gettime()) - t0;
    session.cache_hits = cache.hits;
    if (session.scanned && cache.dirty && !ddr::SaveSongCache(kCachePath, cache))
        session.message = "Couldn't write sd:/ddrwii/cache.txt";

    int which = 0;
    bool in_song_select = false;   // else the Players screen
    for (;;) {
        bool next;
        if (which == 0 && !in_song_select) {
            bool chosen;
            {
                screens::Players scene(video, input, font, session);
                runScene(video, input, scene, false);
                chosen = scene.chosen();
                next = scene.back();
            }
            saveConfigIfChanged(session);
            if (chosen) {
                in_song_select = true;
                continue;
            }
        } else if (which == 0) {
            bool play, calibrate, back;
            {
                screens::SongSelect scene(video, input, font, session);
                runScene(video, input, scene, false);
                play = scene.picked();
                calibrate = scene.calibrate();
                back = scene.back();
            }
            saveConfigIfChanged(session);
            if (back) {
                in_song_select = false;
                continue;
            }
            if (!play && !calibrate) break;   // the exit chord
            if (calibrate) {
                screens::OffsetCalibration cal(video, input, art, font, session);
                runScene(video, input, cal, false);
                continue;
            }
            if (play) {
                bool quit, done;
                screens::SongResult result;
                {
                    screens::Gameplay game(video, input, art, font, session);
                    runScene(video, input, game, false);
                    quit = game.quitApp();
                    done = game.finished();
                    if (done) result = game.result();
                }   // the song's audio stream is closed here
                saveScoresIfChanged(session);
                if (quit) break;
                if (done) {
                    screens::Results res(video, input, font, session, std::move(result));
                    runScene(video, input, res, false);
                    saveScoresIfChanged(session);
                    saveConfigIfChanged(session);   // the last name per port
                    if (res.quitApp()) break;
                }
                continue;   // back to song select
            }
        } else if (which == 1) {
            screens::AudioClockTest scene(video, input, font);
            next = runScene(video, input, scene);
        } else if (which == 2) {
            screens::PlayfieldStress scene(video, input, art, font);
            next = runScene(video, input, scene);
        } else {
            screens::RenderTest scene(video, input, art, font);
            next = runScene(video, input, scene);
        }
        if (!next) break;
        which = (which + 1) % 4;
    }

    input.shutdown();
    font.destroy();
    art.destroy();
    video.shutdown();
    return EXIT_SUCCESS;   // back to the loader (HBC / Dolphin)
}
