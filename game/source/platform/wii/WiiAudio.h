// SPDX-License-Identifier: GPL-3.0-or-later
// WiiAudio - stream a 16-bit WAV from the SD card through ASND voice 0 (doc 06 §4, M4b).
//
// A fill thread reads the file into a ring of kChunks buffers. The ASND voice callback
// (interrupt level, once per 21.3 ms mixing tick while ASND's second slot is empty) retires
// buffers ASND no longer points at, counts their frames as played, and queues the next one.
// Each retirement is a position report {gettime(), frames played} for core/timing/SongClock.
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
        u32 read_max_us = 0;     // slowest single chunk read from the SD card
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

    // Opens the WAV, starts the fill thread and fills the ring. Doesn't play yet.
    bool open(const char* path);
    void play();
    void close();
    ~AudioStream() { close(); }

    bool isOpen() const { return file_ != nullptr; }
    const char* error() const { return error_; }
    u32 rate() const { return rate_; }
    int channels() const { return channels_; }
    u32 totalFrames() const { return total_frames_; }

    Report report() const;       // latest position report, read with interrupts off
    Stats stats() const;

private:
    static void* fillThread(void* self);
    static void voiceCallback(s32 voice);
    void fillOnce();

    void* file_ = nullptr;       // FILE*
    const char* error_ = "";
    u32 rate_ = 0;
    int channels_ = 0;
    u32 total_frames_ = 0;
    u32 frames_read_ = 0;
    u32 read_max_us_ = 0;
    u32 lwp_ = 0;                // lwp_t
    volatile bool stop_ = false;
    bool playing_ = false;
};

}  // namespace wii
