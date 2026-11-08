// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens/AudioClockTest.h"

#include <asndlib.h>
#include <gccore.h>
#include <grrlib.h>
#include <ogc/lwp_watchdog.h>

#include "platform/wii/WiiInput.h"
#include "platform/wii/WiiVideo.h"
#include "render/Font.h"

namespace screens {
namespace {

constexpr double kBpm = 120.0;
constexpr u64 kSettleUs = 3000000;             // ignore jitter while the low-pass settles
constexpr u64 kRefStartUs = 5000000, kRefEndUs = 15000000;   // drift reference window
constexpr u32 kFlashUs = 80000;                // flash length after each beat
constexpr u32 kDiffWindowS = 10;               // buffers-clock averaging window

struct Track { const char* path; const char* name; };
constexpr Track kTracks[] = {
    {"sd:/ddrwii/test/click120-48k.wav", "48 kHz"},
    {"sd:/ddrwii/test/click120-44k.wav", "44.1 kHz"},
    {"sd:/ddrwii/test/click120-44k.ogg", "OGG 44.1 kHz"},
    {"sd:/ddrwii/test/click120-44k.mp3", "MP3 44.1 kHz"},
};
// SongClock comes from ASND's mixing ticks; "buffers-clock" is the file content actually
// handed over (retired buffers) minus SongClock at that moment. Its average is a fixed
// lateness; drift in it means content and clock part ways (underruns, wrong rate).
constexpr int kTrackCount = sizeof(kTracks) / sizeof(kTracks[0]);

s64 absS64(s64 v) { return v < 0 ? -v : v; }

}  // namespace

AudioClockTest::AudioClockTest(wii::Video& video, const wii::Input& input, const render::Font& font)
    : video_(video), input_(input), font_(font) {
    start();
}

void AudioClockTest::start() {
    stream_.close();
    clock_.reset();
    last_report_ = 0;
    last_tick_report_ = 0;
    prev_song_us_ = -1;
    jitter_max_us_ = jitter_sec_us_ = jitter_acc_us_ = 0;
    diff_us_ = diff_sum_ = diff_avg_us_ = diff_ref_us_ = diff_ref_sum_ = 0;
    diff_n_ = diff_ref_n_ = 0;
    drift_min_us_ = drift_max_us_ = 0;
    seconds_ = 0;
    if (stream_.open(kTracks[file_].path)) stream_.play();
    start_cpu_us_ = second_start_us_ = wii::Input::nowUs();
    video_.resetStats();   // the preload above may have cost a frame; count from here
}

bool AudioClockTest::frame() {
    const u16 down = input_.down(0), held = input_.held(0);
    if (down & PAD_BUTTON_A) {
        file_ = (file_ + 1) % kTrackCount;
        start();
    } else if ((down & PAD_BUTTON_START) && !(held & PAD_TRIGGER_Z)) {
        start();
    }

    const u64 cpu = wii::Input::nowUs();
    if (stream_.isOpen()) {
        const wii::AudioStream::TickReport t = stream_.tickReport();
        if (t.count != last_tick_report_) {
            last_tick_report_ = t.count;
            clock_.observe(ticks_to_microsecs(t.tick),
                           (s64)t.ticks * wii::AudioStream::kTickUsNum / wii::AudioStream::kTickUsDen);
        }
        const wii::AudioStream::Report r = stream_.report();
        if (r.count != last_report_ && clock_.started()) {
            last_report_ = r.count;
            // File content played vs SongClock at the moment the buffer was retired.
            const s64 content_us = (s64)(r.frames * 1000000ull / stream_.rate());
            bufferDiff(content_us - (s64)(ticks_to_microsecs(r.tick) + clock_.offsetUs()));
        }
    }
    const s64 song_us = clock_.now(cpu);
    if (clock_.started()) measure(cpu, song_us);
    draw(song_us);

    quit_frames_ = ((held & PAD_BUTTON_START) && (held & PAD_TRIGGER_Z)) ? quit_frames_ + 1 : 0;
    return quit_frames_ < 120;
}

void AudioClockTest::bufferDiff(s64 d) {
    const u64 elapsed = wii::Input::nowUs() - start_cpu_us_;
    diff_us_ = d;
    diff_sum_ += d;
    diff_n_++;
    if (elapsed >= kRefStartUs && elapsed < kRefEndUs) {
        diff_ref_sum_ += d;
        diff_ref_n_++;
        diff_ref_us_ = diff_ref_sum_ / (s64)diff_ref_n_;
    }
}

void AudioClockTest::measure(u64 cpu, s64 song_us) {
    const u64 elapsed = cpu - start_cpu_us_;
    const wii::AudioStream::Stats st = stream_.stats();
    if (st.finished) return;   // the clock holds at the end; that isn't jitter

    // Jitter: how much more or less song time passed this frame than CPU time.
    if (prev_song_us_ >= 0 && elapsed > kSettleUs) {
        const u32 j = (u32)absS64((song_us - prev_song_us_) - (s64)(cpu - prev_cpu_us_));
        if (j > jitter_max_us_) jitter_max_us_ = j;
        if (j > jitter_acc_us_) jitter_acc_us_ = j;
    }
    prev_song_us_ = song_us;
    prev_cpu_us_ = cpu;

    if (cpu - second_start_us_ >= 1000000) {
        jitter_sec_us_ = jitter_acc_us_;
        jitter_acc_us_ = 0;
        second_start_us_ = cpu;
        // Buffer reports jitter by up to a tick (21 ms) at 44.1 kHz, so average them over
        // 10 s windows (~230 reports) before comparing with the reference.
        if (++seconds_ % kDiffWindowS == 0) {
            diff_avg_us_ = diff_n_ ? diff_sum_ / (s64)diff_n_ : 0;
            diff_sum_ = 0;
            diff_n_ = 0;
            if (elapsed >= kRefEndUs + kDiffWindowS * 1000000ull) {   // a window after the reference
                const s64 drift = diff_avg_us_ - diff_ref_us_;
                if (drift < drift_min_us_) drift_min_us_ = drift;
                if (drift > drift_max_us_) drift_max_us_ = drift;
            }
        }
    }
}

void AudioClockTest::draw(s64 song_us) {
    const int w = video_.width(), h = video_.height();
    const u32 white = 0xFFFFFFFF, grey = 0xB0B0B0FF, warn = 0xFFC040FF, bad = 0xFF5050FF;
    GRRLIB_Rectangle(0, 0, w, h, 0x101830FF, true);

    // Beat flash: beat 0 of each bar (the high click) red, the other three white.
    const double beats = song_us * kBpm / 60e6;
    const s64 beat = (s64)beats;
    const u32 since_beat_us = (u32)((beats - beat) * 60e6 / kBpm);
    const bool flash = clock_.started() && since_beat_us < kFlashUs;
    const int in_bar = (int)(beat % 4);
    if (flash) GRRLIB_Rectangle(40, 40, w - 80, 150, in_bar == 0 ? 0xFF3030FF : 0xFFFFFFFF, true);
    for (int i = 0; i < 4; i++) {
        const float x = w / 2 - 130 + i * 70;
        const bool on = clock_.started() && i == in_bar;
        GRRLIB_Rectangle(x, 206, 50, 30, on ? (i == 0 ? 0xFF3030FF : white) : 0x404860FF, true);
    }

    GRRLIB_SetAntiAliasing(false);
    const wii::AudioStream::Stats st = stream_.stats();
    // Kept above y = 462 (the owner's TV overscan cuts the bottom rows).
    const u32 dropped = video_.stats().dropped_total;   // frames over 1.5x the refresh period
    font_.print(40, 250, dropped ? warn : white, 1, "M4b audio clock  %s  %s  dropped %lu", video_.modeName(),
                kTracks[file_].name, (unsigned long)dropped);
    if (!stream_.isOpen()) {
        font_.print(40, 274, bad, 1, "%s: %s", kTracks[file_].path, stream_.error());
        font_.print(40, 292, grey, 1, "Run tools/stage-sd.sh, or for Dolphin see doc 03 4.3.");
    } else {
        const u32 secs = (u32)(song_us / 1000000);
        const u32 total = stream_.totalFrames() / stream_.rate();
        font_.print(40, 274, white, 1, "SongClock %8.3f s  beat %5lld   %lu:%02lu / %lu:%02lu%s",
                    song_us / 1e6, (long long)beat, (unsigned long)secs / 60, (unsigned long)secs % 60,
                    (unsigned long)total / 60, (unsigned long)total % 60, st.finished ? "  END" : "");
        font_.print(40, 292, white, 1, "ASND timer %8.3f s  ticks %lu   %lu Hz %s", ASND_GetTimerVoice(0) / 1e3,
                    (unsigned long)ASND_GetTickCounterVoice(0), (unsigned long)stream_.rate(),
                    stream_.channels() == 2 ? "stereo" : "mono");
        font_.print(40, 310, white, 1, "buffers-clock %7.2f ms  10 s avg %7.2f  ref %7.2f",
                    diff_us_ / 1e3, diff_avg_us_ / 1e3, diff_ref_us_ / 1e3);
        const s64 drift = absS64(drift_min_us_) > drift_max_us_ ? absS64(drift_min_us_) : drift_max_us_;
        font_.print(40, 328, drift > 2000 ? warn : white, 1, "drift vs ref %+6.2f..%+6.2f ms (target < 2)",
                    drift_min_us_ / 1e3, drift_max_us_ / 1e3);
        font_.print(40, 346, jitter_max_us_ > 1000 ? warn : white, 1,
                    "frame jitter max %5.2f ms  last 1 s %5.2f ms (target < 1)", jitter_max_us_ / 1e3,
                    jitter_sec_us_ / 1e3);
        font_.print(40, 364, st.underruns ? bad : white, 1,
                    "underruns %lu  ring min %lu/%d  snaps %lu", (unsigned long)st.underruns,
                    (unsigned long)st.ring_min, wii::AudioStream::kChunks, (unsigned long)clock_.snaps());
        // Share of real time the fill thread spends reading/decoding.
        const double audio_us = st.frames_read * 1e6 / stream_.rate();
        font_.print(40, 382, white, 1, "read/decode max %5.1f ms per %lu frames   CPU %4.1f%%",
                    st.read_max_us / 1e3, (unsigned long)wii::AudioStream::kChunkFrames,
                    audio_us > 0 ? st.decode_us * 100.0 / audio_us : 0.0);
        // MP3s start late by decoder output that ITGmania plays too (WiiAudio.cpp, Mp3Source).
        // The click in this file is late against the flash by this much, on purpose.
        if (stream_.startDelayFrames())
            font_.print(40, 446, warn, 1, "MP3 music starts %.1f ms late (%lu frames%s), as in ITGmania",
                        stream_.startDelayFrames() * 1000.0 / stream_.rate(),
                        (unsigned long)stream_.startDelayFrames(),
                        stream_.startDelayKnown() ? "" : ", no LAME tag");
    }
    font_.print(40, 410, grey, 1, "A next track (48k WAV, 44.1k WAV, OGG, MP3)  START restart");
    font_.print(40, 428, grey, 1, "B next test   START+Z 2 s exit");
    GRRLIB_SetAntiAliasing(true);
}

}  // namespace screens
