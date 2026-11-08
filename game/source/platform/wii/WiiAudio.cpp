// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/wii/WiiAudio.h"

#include <asndlib.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "core/audio/WavFile.h"

namespace wii {
namespace {

constexpr int N = AudioStream::kChunks;
constexpr u32 kMaxChunkBytes = AudioStream::kChunkFrames * 4;   // stereo 16-bit

// Ring state. One stream at a time, because the ASND callback has no user pointer.
// g_filled / g_queued / g_finished only ever grow; chunk i lives in g_buf[i % N].
//   fill thread:  writes chunk g_filled while g_filled - g_finished < N, then g_filled++
//   callback:     retires chunks ASND no longer uses (g_finished++), queues chunk g_queued
u8 g_buf[N][kMaxChunkBytes] ATTRIBUTE_ALIGN(32);
u32 g_bytes[N];                  // bytes handed to ASND (padded to 32)
u32 g_frames[N];                 // real frames in the chunk
volatile u32 g_filled = 0, g_queued = 0, g_finished = 0;
volatile bool g_eof = false;
volatile bool g_active = false;  // the callback may touch the ring
volatile u64 g_played = 0;       // frames of retired chunks
volatile u64 g_report_tick = 0;
volatile u32 g_report_count = 0;
volatile u64 g_tick_time = 0;    // gettime() at the latest ASND mixing tick
volatile u32 g_tick_count = 0;   // ASND_GetTickCounterVoice(0) at that tick
volatile u32 g_tick_reports = 0;
volatile u32 g_underruns = 0;
volatile u32 g_ring_min = N;

}  // namespace

// Interrupt level, once per ASND mixing tick (1024 samples at 48 kHz). The voice's tick
// counter advances by one per tick whatever the voice's sample rate, so this report is late
// by the same amount at every rate. The buffer reports in voiceCallback aren't: at 44.1 kHz a
// buffer ends part-way through a tick (doc 06 §4).
static void tickCallback() {
    if (!g_active) return;
    g_tick_time = gettime();
    g_tick_count = ASND_GetTickCounterVoice(0) / ASND_GetSamplesPerTick();
    g_tick_reports = g_tick_reports + 1;
}

void AudioStream::initAsnd() {
    static bool done = false;
    if (done) return;
    ASND_Init();
    ASND_SetCallback(tickCallback);
    ASND_Pause(0);
    done = true;
}

// Interrupt level (ASND's audio DMA callback), every tick while the voice's second slot is
// empty. No printf, no allocation, no blocking.
void AudioStream::voiceCallback(s32 voice) {
    if (!g_active) return;
    bool retired = false;
    while (g_finished < g_queued && ASND_TestPointer(voice, g_buf[g_finished % N]) == SND_OK) {
        g_played = g_played + g_frames[g_finished % N];
        g_finished = g_finished + 1;
        retired = true;
    }
    if (retired) {
        g_report_tick = gettime();
        g_report_count = g_report_count + 1;
    }
    if (g_queued - g_finished < 2 && g_filled > g_queued) {
        const u32 i = g_queued % N;
        if (ASND_AddVoice(voice, g_buf[i], g_bytes[i]) == SND_OK) {
            g_queued = g_queued + 1;
            const u32 ready = g_filled - g_queued;
            if (ready < g_ring_min) g_ring_min = ready;
        }
    }
    if (g_queued == g_finished && !g_eof) g_underruns = g_underruns + 1;   // nothing playing
}

void AudioStream::fillOnce() {
    FILE* f = (FILE*)file_;
    while (!g_eof && g_filled - g_finished < (u32)N) {
        const u32 i = g_filled % N;
        const u32 want = total_frames_ - frames_read_ < kChunkFrames ? total_frames_ - frames_read_
                                                                     : kChunkFrames;
        const u64 t0 = gettime();
        const u32 got = want ? fread(g_buf[i], 2 * channels_, want, f) : 0;
        const u32 us = ticks_to_microsecs(gettime() - t0);
        if (us > read_max_us_) read_max_us_ = us;
        if (got == 0) {
            g_eof = true;
            break;
        }
        frames_read_ += got;
        const u32 bytes = got * 2 * channels_;
        const u32 padded = (bytes + 31) & ~31u;
        memset(g_buf[i] + bytes, 0, padded - bytes);
        DCFlushRange(g_buf[i], padded);   // the DSP reads main memory, not the CPU cache
        g_bytes[i] = padded;
        g_frames[i] = got;
        g_filled = g_filled + 1;          // publish after the data is in memory
        if (got < want || frames_read_ >= total_frames_) g_eof = true;
    }
}

void* AudioStream::fillThread(void* self) {
    AudioStream* s = (AudioStream*)self;
    while (!s->stop_) {
        s->fillOnce();
        usleep(4000);   // the ring holds ~0.7 s, so polling every 4 ms is plenty
    }
    return nullptr;
}

bool AudioStream::open(const char* path) {
    close();
    initAsnd();
    FILE* f = fopen(path, "rb");
    if (!f) {
        error_ = "can't open file (SD card / path?)";
        return false;
    }
    u8 head[4096];
    const size_t n = fread(head, 1, sizeof head, f);
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    audio::WavInfo info;
    if (!audio::parseWavHeader(head, n, size > 0 ? (u64)size : 0, info)) {
        fclose(f);
        error_ = "not a 16-bit PCM WAV";
        return false;
    }
    fseek(f, info.data_offset, SEEK_SET);
    file_ = f;
    rate_ = info.rate;
    channels_ = info.channels;
    total_frames_ = info.frames();
    frames_read_ = 0;
    read_max_us_ = 0;
    error_ = "";

    g_active = false;
    g_filled = g_queued = g_finished = 0;
    g_eof = false;
    g_played = 0;
    g_report_tick = 0;
    g_report_count = 0;
    g_tick_time = 0;
    g_tick_count = 0;
    g_tick_reports = 0;
    g_underruns = 0;
    g_ring_min = N;

    fillOnce();   // preload the whole ring before playing
    stop_ = false;
    lwp_t t;
    LWP_CreateThread(&t, fillThread, this, nullptr, 16 * 1024, 80);   // high priority; it mostly sleeps
    lwp_ = (u32)t;
    return true;
}

void AudioStream::play() {
    if (!file_ || playing_ || g_filled == 0) return;
    const s32 fmt = channels_ == 2 ? VOICE_STEREO_16BIT_LE : VOICE_MONO_16BIT_LE;
    const u32 level = IRQ_Disable();
    g_ring_min = N;
    ASND_SetVoice(0, fmt, rate_, 0, g_buf[0], g_bytes[0], 255, 255, voiceCallback);
    g_queued = 1;
    // ASND may refuse the second buffer until the DSP has taken the new voice; the
    // callback then queues it on the next tick.
    if (g_filled > 1 && ASND_AddVoice(0, g_buf[1], g_bytes[1]) == SND_OK) g_queued = 2;
    g_active = true;
    IRQ_Restore(level);
    playing_ = true;
}

void AudioStream::close() {
    if (!file_) return;
    g_active = false;
    ASND_StopVoice(0);
    stop_ = true;
    LWP_JoinThread((lwp_t)lwp_, nullptr);
    fclose((FILE*)file_);
    file_ = nullptr;
    playing_ = false;
}

AudioStream::Report AudioStream::report() const {
    Report r;
    const u32 level = IRQ_Disable();   // the callback writes these from the interrupt
    r.tick = g_report_tick;
    r.frames = g_played;
    r.count = g_report_count;
    IRQ_Restore(level);
    return r;
}

AudioStream::TickReport AudioStream::tickReport() const {
    TickReport r;
    const u32 level = IRQ_Disable();
    r.tick = g_tick_time;
    r.ticks = g_tick_count;
    r.count = g_tick_reports;
    IRQ_Restore(level);
    return r;
}

AudioStream::Stats AudioStream::stats() const {
    Stats s;
    s.underruns = g_underruns;
    s.ring_min = g_ring_min;
    s.read_max_us = read_max_us_;
    s.eof = g_eof;
    s.finished = g_eof && g_queued == g_filled;
    return s;
}

}  // namespace wii
