// SPDX-License-Identifier: GPL-3.0-or-later
// ddr-wii entry point. Loads config.ini, scans the song library, then runs the target menu flow
// (doc 06 §7): Title -> Players (1 or 2) -> song select -> gameplay -> results -> song select.
// B goes back one level (song select -> Players -> Title). Options and the exit prompt open from
// the title; the offset calibration opens from Options and with R in song select; the test
// scenes (M4b audio clock, M2b playfield stress, M2 render) from Options. START+Z held for 2 s
// exits from anywhere, and so do the Wii's Reset and Power buttons (doc 06 §8).
#include <fat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdlib.h>
#include <unistd.h>

#include "platform/wii/MenuSound.h"
#include "platform/wii/WiiAudio.h"
#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/ArrowArt.h"
#include "render/Font.h"
#include "screens/AudioClockTest.h"
#include "screens/Gameplay.h"
#include "screens/OffsetCalibration.h"
#include "screens/Options.h"
#include "screens/Players.h"
#include "screens/PlayfieldStress.h"
#include "screens/RenderTest.h"
#include "screens/Results.h"
#include "screens/Session.h"
#include "screens/SongSelect.h"
#include "screens/Title.h"

namespace {

constexpr const char* kSongsRoot = "sd:/ddrwii/songs";

// The Wii's Reset and Power buttons. The callbacks run at interrupt level and only set a flag;
// runScene then ends the scene as if the exit chord had been held, so the pending config.ini
// and scores.txt writes still happen on the way out (doc 06 §8).
volatile bool g_reset = false, g_power = false;
void onReset(u32, void*) { g_reset = true; }
void onPower() { g_power = true; }
bool shutdownRequested() { return g_reset || g_power; }

// A button pressed this frame on any pad (every pad works in every menu, CLAUDE.md).
bool anyDown(const wii::Input& input, u16 buttons) {
    for (int port = 0; port < wii::Input::kPorts; port++)
        if (input.down(port) & buttons) return true;
    return false;
}

// Runs one scene until it quits (returns false) or, with b_next, B is pressed (returns true).
template <typename Scene>
bool runScene(wii::Video& video, wii::Input& input, Scene& scene, bool b_next = true) {
    video.resetStats();
    for (;;) {
        if (shutdownRequested()) return false;
        video.beginFrame();
        input.update();   // drains the timestamped SI samples; scenes read input from here
        if (b_next && anyDown(input, PAD_BUTTON_B)) return true;
        const bool running = scene.frame();
        video.endFrame();
        if (!running) return false;
    }
}

// Writes config.ini if a screen changed it (scroll speeds, life bars). Without an SD card
// there's nowhere to write; the title said so at boot.
void saveConfigIfChanged(screens::Session& session) {
    if (!session.config_dirty) return;
    session.config_dirty = false;
    if (!session.sd_mounted) return;
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
    if (!session.sd_mounted) return;
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

    SYS_SetResetCallback(onReset);
    SYS_SetPowerCallback(onPower);
    wii::AudioStream::initAsnd();   // ASND and the 24 MB song buffer, before any other thread exists

    // The SD card (sd:/). Some cards need a moment after power-on (doc 06 §8): retry for 2 s.
    screens::Session session;
    const u64 sd_start = gettime();
    while (!(session.sd_mounted = fatInitDefault()) && ticks_to_millisecs(gettime() - sd_start) < 2000)
        usleep(100000);
    if (!session.sd_mounted) session.message = "No SD card found: nothing will be saved";

    // config.ini first: it sets the pad debounce and the sync of packs without a Pack.ini.
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

    wii::sfx::init();   // the menu sounds, on ASND voice 1

    enum class Scene { Title, Players, SongSelect, Options };
    Scene scene = Scene::Title;
    int title_row = screens::Title::kPlay;
    bool players_from_title = true;
    for (;;) {
        if (shutdownRequested()) break;   // Reset or Power: the saves below, then out
        if (scene == Scene::Title) {
            screens::Title::Choice choice;
            {
                screens::Title title(video, input, art, font, session, title_row);
                runScene(video, input, title, false);
                choice = title.choice();
            }
            if (choice == screens::Title::Choice::Play) {
                scene = Scene::Players;
                players_from_title = true;
            } else if (choice == screens::Title::Choice::Options) {
                session.options_row = 1;
                session.options_pads = false;
                scene = Scene::Options;
            } else {
                break;   // Exit, or the exit chord
            }
        } else if (scene == Scene::Players) {
            bool chosen, back;
            {
                screens::Players players(video, input, font, session, players_from_title);
                runScene(video, input, players, false);
                chosen = players.chosen();
                back = players.back();
            }
            saveConfigIfChanged(session);
            if (chosen) {
                scene = Scene::SongSelect;
            } else if (back) {
                scene = Scene::Title;
                title_row = screens::Title::kPlay;
            } else {
                break;   // the exit chord
            }
        } else if (scene == Scene::Options) {
            screens::Options::Next next;
            {
                screens::Options options(video, input, font, session);
                runScene(video, input, options, false);
                next = options.next();
            }
            if (next == screens::Options::Next::None) {
                saveConfigIfChanged(session);
                break;   // the exit chord
            }
            if (next == screens::Options::Next::Back) {
                saveConfigIfChanged(session);   // B saves config.ini
                scene = Scene::Title;
                title_row = screens::Title::kOptions;
            } else if (next == screens::Options::Next::Calibrate) {
                screens::OffsetCalibration cal(video, input, art, font, session);
                runScene(video, input, cal, false);
            } else {
                // The test scenes in turn: B goes to the next, and from the last back to Options.
                bool next_test = true;
                {
                    screens::AudioClockTest scene1(video, input, font);
                    next_test = runScene(video, input, scene1);
                }
                if (next_test) {
                    screens::PlayfieldStress scene2(video, input, art, font);
                    next_test = runScene(video, input, scene2);
                }
                if (next_test) {
                    screens::RenderTest scene3(video, input, art, font);
                    next_test = runScene(video, input, scene3);
                }
                if (!next_test) break;   // the exit chord
            }
        } else {
            bool play, calibrate, back;
            {
                screens::SongSelect select(video, input, font, session);
                runScene(video, input, select, false);
                play = select.picked();
                calibrate = select.calibrate();
                back = select.back();
            }
            saveConfigIfChanged(session);
            if (back) {
                scene = Scene::Players;
                players_from_title = false;
                continue;
            }
            if (!play && !calibrate) break;   // the exit chord
            if (calibrate) {
                screens::OffsetCalibration cal(video, input, art, font, session);
                runScene(video, input, cal, false);
                continue;
            }
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
            // back to song select
        }
    }
    saveConfigIfChanged(session);

    input.shutdown();
    font.destroy();
    art.destroy();
    video.shutdown();
    if (g_power) SYS_ResetSystem(SYS_POWEROFF, 0, 0);   // off, to standby or idle as the console is set
    return EXIT_SUCCESS;   // back to the loader (HBC / Dolphin); the Reset button too
}
