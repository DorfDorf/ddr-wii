// SPDX-License-Identifier: GPL-3.0-or-later
// SongMedia - loads the banner and plays the preview for the song under the music wheel's
// cursor, on a worker thread so the wheel keeps moving while the SD card is read (doc 06 §7,
// M5 step 6).
//
// SongSelect calls request() once the cursor has rested on a row; the worker always works on
// the latest request and drops older ones. The banner (render::Banner) comes back through
// banner(). The preview plays the song's #SAMPLESTART for #SAMPLELENGTH seconds (12 s if
// unset, as in StepMania), fading in over 0.25 s and out over the last 1.5 s, then loops.
// Previews use the same wii::AudioStream as gameplay, so only one SongMedia may exist, and it
// must be destroyed (which stops the worker and the sound) before anything else plays audio.
#pragma once

#include <gctypes.h>
#include <grrlib.h>

#include <string>

namespace screens {

class SongMedia {
public:
    SongMedia();
    ~SongMedia();
    SongMedia(const SongMedia&) = delete;
    SongMedia& operator=(const SongMedia&) = delete;

    // Paths are full paths, "" for none. Drops the banner shown so far.
    void request(const std::string& banner, const std::string& music, double start_s,
                 double length_s);
    // The banner for the latest request once it's loaded; nullptr before that or if none.
    // Main thread only; valid until the next request() or banner() call.
    const GRRLIB_texImg* banner();

private:
    struct Request {
        u32 gen = 0;
        std::string banner, music;
        double start_s = 0, length_s = 0;
    };

    static void* run(void* self);
    void loop();

    u32 lwp_ = 0;
    u32 mutex_ = 0;
    volatile bool quit_ = false;
    Request req_;                       // guarded by mutex_
    GRRLIB_texImg* ready_ = nullptr;    // guarded: loaded for ready_gen_, not yet taken
    u32 ready_gen_ = 0;
    GRRLIB_texImg* shown_ = nullptr;    // main thread
    u32 shown_gen_ = 0;                 // main thread: gen of the latest request
};

}  // namespace screens
