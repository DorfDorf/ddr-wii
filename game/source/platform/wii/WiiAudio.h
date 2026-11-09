// SPDX-License-Identifier: GPL-3.0-or-later
// WiiAudio - play a song file from the SD card through ASND voice 0 (doc 06 §4, M4b).
//   .wav  16-bit PCM, streamed from the SD card
//   .ogg  Vorbis, read into a fixed 24 MB MEM2 buffer first, decoded with Tremor
//   .mp3  read into the same buffer first, decoded with libmad the way ITGmania does (no gapless trim)
//
// A fill thread reads or decodes the file into a ring of kChunks buffers. The ASND voice
// callback (interrupt level, once per 21.3 ms mixing tick while ASND's second slot is empty)
// retires buffers ASND no longer points at, counts their frames as played, and queues the next.
// Two position reports:
//   - tick report: every ASND mixing tick, {gettime(), ticks mixed}. One tick is 1024 samples
//     at 48 kHz (64000/3 us) at any voice rate. This drives core/timing/SongClock.
//   - buffer report: each retired buffer, {gettime(), frames of the file played}. Coarser and
//     late by a rate-dependent amount, but it counts real file content, so comparing it with
//     SongClock shows underruns and drift.
//
// Re-implemented from the asndlib docs and source; no StepMania-Wii code (doc 07 §6).
#pragma once

#include <gctypes.h>

namespace wii {

class AudioStream {
public:
    static constexpr int kChunks = 16;            // ring size
    static constexpr u32 kChunkFrames = 2048;     // ~43 ms at 48 kHz; 16 chunks ~0.7 s

    struct Stats {
        u32 underruns = 0;       // ticks where the next buffer wasn't ready
        u32 ring_min = 0;        // fewest ready-but-unqueued chunks seen while playing
        u32 read_max_us = 0;     // slowest single chunk read or decode
        u64 decode_us = 0;       // total read/decode time; over frames_read gives the CPU share
        u32 frames_read = 0;
        bool eof = false;        // the fill thread reached the end of the file
        bool finished = false;   // ... and every buffer has played
    };

    struct Report {
        u64 tick = 0;            // gettime() in the callback that saw the frames finish
        u64 frames = 0;          // frames of the file played by then
        u32 count = 0;           // increments with every report
    };

    // Ensures ASND is running (ASND_Init once per boot).
    static void initAsnd();

    // Opens a .wav, .ogg or .mp3, starts the fill thread and fills the ring. Doesn't play yet.
    // start_s > 0 starts that far into the file (song-select previews); the reports then count
    // from there.
    bool open(const char* path, double start_s = 0);
    // Stops, seeks to start_s and fills the ring again (a preview looping). Then call play().
    bool restart(double start_s);
    // 0-255; applies now if playing, else from the next play().
    void setVolume(int volume);
    void play();
    // The pause menu: ASND_PauseVoice. A paused voice's tick counter stops too (asndlib.c
    // [src]), so the tick report's count picks up where it was on resume().
    void pause();
    void resume();
    bool paused() const { return paused_; }
    void close();
    ~AudioStream() { close(); }

    bool isOpen() const { return source_ != nullptr; }
    const char* error() const { return error_; }
    u32 rate() const { return rate_; }
    int channels() const { return channels_; }
    u32 totalFrames() const { return total_frames_; }
    // Decoder output before the music starts (MP3: Info frame + LAME delay + 529 frames).
    // Played, not trimmed, as in ITGmania. known = false: no LAME tag, delay only estimated.
    u32 startDelayFrames() const { return start_delay_frames_; }
    bool startDelayKnown() const { return start_delay_known_; }

    struct TickReport {
        u64 tick = 0;            // gettime() at the mixing tick
        u32 ticks = 0;           // ticks mixed since play()
        u32 count = 0;           // increments with every report
    };
    static constexpr u32 kTickUsNum = 64000, kTickUsDen = 3;   // one tick = 64000/3 us

    Report report() const;           // latest buffer report, read with interrupts off
    TickReport tickReport() const;   // latest tick report
    Stats stats() const;

private:
    static void* fillThread(void* self);
    static void voiceCallback(s32 voice);
    void fillOnce();
    void startFill();
    void stopFill();

    void* source_ = nullptr;     // Source* (WiiAudio.cpp)
    const char* error_ = "";
    u32 rate_ = 0;
    int channels_ = 0;
    u32 total_frames_ = 0;
    u32 frames_read_ = 0;
    u32 read_max_us_ = 0;
    u64 decode_us_ = 0;
    u32 start_delay_frames_ = 0;
    bool start_delay_known_ = true;
    u32 lwp_ = 0;                // lwp_t
    volatile bool stop_ = false;
    bool playing_ = false;
    bool paused_ = false;
    int volume_ = 255;
};

}  // namespace wii
