// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/SongMedia.h"

#include <gccore.h>
#include <unistd.h>

#include "platform/wii/WiiAudio.h"
#include "render/Banner.h"

namespace screens {
namespace {

constexpr double kDefaultLengthS = 12.0;   // StepMania's default #SAMPLELENGTH
constexpr double kFadeInS = 0.25, kFadeOutS = 1.5;
constexpr u32 kPollUs = 20000;

double playedSeconds(const wii::AudioStream& s) {
    return s.tickReport().ticks * (double)wii::AudioStream::kTickUsNum /
           wii::AudioStream::kTickUsDen / 1e6;
}

}  // namespace

SongMedia::SongMedia() {
    mutex_t m;
    LWP_MutexInit(&m, false);
    mutex_ = (u32)m;
    lwp_t t;
    // Below the main thread (64), so decoding never holds up a frame; the audio fill thread
    // AudioStream starts is at 80.
    LWP_CreateThread(&t, run, this, nullptr, 64 * 1024, 40);
    lwp_ = (u32)t;
}

SongMedia::~SongMedia() {
    quit_ = true;
    LWP_JoinThread((lwp_t)lwp_, nullptr);
    LWP_MutexDestroy((mutex_t)mutex_);
    if (ready_) GRRLIB_FreeTexture(ready_);
    if (shown_) GRRLIB_FreeTexture(shown_);
}

void SongMedia::request(const std::string& banner, const std::string& music, double start_s,
                        double length_s) {
    if (shown_) GRRLIB_FreeTexture(shown_);
    shown_ = nullptr;
    LWP_MutexLock((mutex_t)mutex_);
    req_.gen++;
    req_.banner = banner;
    req_.music = music;
    req_.start_s = start_s > 0 ? start_s : 0;
    req_.length_s = length_s > 0 ? length_s : kDefaultLengthS;
    shown_gen_ = req_.gen;
    LWP_MutexUnlock((mutex_t)mutex_);
}

const GRRLIB_texImg* SongMedia::banner() {
    LWP_MutexLock((mutex_t)mutex_);
    if (ready_ && ready_gen_ == shown_gen_) {
        if (shown_) GRRLIB_FreeTexture(shown_);
        shown_ = ready_;
        ready_ = nullptr;
    }
    LWP_MutexUnlock((mutex_t)mutex_);
    return shown_;
}

void* SongMedia::run(void* self) {
    ((SongMedia*)self)->loop();
    return nullptr;
}

void SongMedia::loop() {
    wii::AudioStream stream;
    u32 done = 0;                 // the request worked on last
    std::string music;            // what the stream has open
    double start_s = 0, length_s = 0;
    bool playing = false;
    while (!quit_) {
        LWP_MutexLock((mutex_t)mutex_);
        const Request r = req_;
        LWP_MutexUnlock((mutex_t)mutex_);

        if (r.gen != done) {
            done = r.gen;
            // A different song: stop its preview first, so the old music doesn't keep playing
            // while the banner loads.
            const bool same_music = r.music == music && r.start_s == start_s;
            if (!same_music) {
                stream.close();
                playing = false;
                music.clear();
            }
            GRRLIB_texImg* tex = r.banner.empty() ? nullptr : render::Banner::load(r.banner.c_str());
            LWP_MutexLock((mutex_t)mutex_);
            const bool current = req_.gen == r.gen;
            if (current) {
                if (ready_) GRRLIB_FreeTexture(ready_);
                ready_ = tex;
                ready_gen_ = r.gen;
                tex = nullptr;
            }
            LWP_MutexUnlock((mutex_t)mutex_);
            if (tex) GRRLIB_FreeTexture(tex);
            if (!current) continue;   // the cursor moved on: start over with the new request

            if (!same_music && !r.music.empty()) {
                music = r.music;
                start_s = r.start_s;
                length_s = r.length_s;
                if (stream.open(music.c_str(), start_s)) {
                    stream.setVolume(0);
                    stream.play();
                    playing = true;
                }
            }
        }

        if (playing) {
            const double t = playedSeconds(stream);
            if (t >= length_s || stream.stats().finished) {
                playing = stream.restart(start_s);
                stream.setVolume(0);
                if (playing) stream.play();
            } else {
                double v = 1.0;
                if (t < kFadeInS) v = t / kFadeInS;
                if (length_s - t < kFadeOutS) v = (length_s - t) / kFadeOutS;
                stream.setVolume((int)(v * 255));
            }
        }
        usleep(kPollUs);
    }
    stream.close();
}

}  // namespace screens
