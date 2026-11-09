// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/wii/WiiAudio.h"

#include <asndlib.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <mad.h>
#include <tremor/ivorbisfile.h>
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

// ---- Sources: something that produces 16-bit PCM frames ----

// OGG and MP3 files are read whole into one fixed buffer at the top of MEM2, reserved the
// first time it's needed and reused for every song (doc 05 M4c open item 5): no multi-MB
// malloc/free per song, so repeated song loads can't fragment the heap. One file at a time.
constexpr u32 kFileBufBytes = 24u << 20;
u8* g_file_buf = nullptr;

// Reads all of f (and closes it) into the file buffer, followed by `guard` zero bytes.
const char* readWhole(FILE* f, u8*& data, size_t& size, size_t guard) {
    if (!g_file_buf) {
        const u32 lo = (u32)SYS_GetArena2Lo(), hi = (u32)SYS_GetArena2Hi();
        const u32 p = (hi - kFileBufBytes) & ~31u;
        if (hi > kFileBufBytes && p >= lo) {
            SYS_SetArena2Hi((void*)p);
            g_file_buf = (u8*)p;
        }
    }
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    const char* err = nullptr;
    if (!g_file_buf) {
        err = "no MEM2 for the song buffer";
    } else if (len <= 0 || (u32)len + guard > kFileBufBytes) {
        err = len <= 0 ? "empty file" : "file bigger than the 24 MB song buffer";
    } else if (fread(g_file_buf, 1, len, f) != (size_t)len) {
        err = "SD read failed";
    } else {
        memset(g_file_buf + len, 0, guard);
        data = g_file_buf;
        size = len;
    }
    fclose(f);
    return err;
}

struct Source {
    u32 rate = 0;
    int channels = 0;
    u32 total_frames = 0;
    bool little_endian = false;   // WAV data is LE; decoders write the CPU's order (BE)
    u32 start_delay_frames = 0;   // silence/delay before the music in the decoded output
    bool start_delay_known = true;
    virtual ~Source() {}
    // Writes up to `frames` frames to dst; returns how many (fewer only at the end).
    virtual u32 read(u8* dst, u32 frames) = 0;
    // Moves the read position to `frame` (0 = the start). False if it can't.
    virtual bool seek(u32 frame) = 0;
};

// WAV: streamed from the SD card (a 5-minute WAV is ~55 MB, too big to keep in memory).
struct WavSource : Source {
    FILE* f = nullptr;
    u32 left = 0;
    long data_offset = 0;

    explicit WavSource(FILE* file) : f(file) {}
    ~WavSource() override { fclose(f); }

    const char* open() {
        u8 head[4096];
        const size_t n = fread(head, 1, sizeof head, f);
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        audio::WavInfo info;
        if (!audio::parseWavHeader(head, n, size > 0 ? (u64)size : 0, info))
            return "not a 16-bit PCM WAV";
        fseek(f, info.data_offset, SEEK_SET);
        data_offset = (long)info.data_offset;
        rate = info.rate;
        channels = info.channels;
        total_frames = left = info.frames();
        little_endian = true;
        return nullptr;
    }
    u32 read(u8* dst, u32 frames) override {
        if (frames > left) frames = left;
        const u32 got = frames ? fread(dst, 2 * channels, frames, f) : 0;
        left = got < frames ? 0 : left - got;
        return got;
    }
    bool seek(u32 frame) override {
        if (frame > total_frames) frame = total_frames;
        if (fseek(f, data_offset + (long)frame * 2 * channels, SEEK_SET) != 0) return false;
        left = total_frames - frame;
        return true;
    }
};

// OGG Vorbis: the whole file is read into memory first (no SD reads while playing,
// doc 06 §4), then decoded on the fly with Tremor (integer decoder).
struct OggSource : Source {
    u8* data = nullptr;
    size_t size = 0, pos = 0;
    OggVorbis_File vf;
    bool vf_open = false;

    static size_t cbRead(void* ptr, size_t sz, size_t n, void* src) {
        OggSource* s = (OggSource*)src;
        size_t bytes = sz * n;
        if (bytes > s->size - s->pos) bytes = s->size - s->pos;
        memcpy(ptr, s->data + s->pos, bytes);
        s->pos += bytes;
        return sz ? bytes / sz : 0;
    }
    static int cbSeek(void* src, ogg_int64_t off, int whence) {
        OggSource* s = (OggSource*)src;
        ogg_int64_t base = 0;
        if (whence == SEEK_CUR) base = (ogg_int64_t)s->pos;
        if (whence == SEEK_END) base = (ogg_int64_t)s->size;
        const ogg_int64_t p = base + off;
        if (p < 0 || p > (ogg_int64_t)s->size) return -1;
        s->pos = (size_t)p;
        return 0;
    }
    static long cbTell(void* src) { return (long)((OggSource*)src)->pos; }

    ~OggSource() override {
        if (vf_open) ov_clear(&vf);
    }
    const char* open(FILE* f) {
        if (const char* err = readWhole(f, data, size, 0)) return err;
        const ov_callbacks cb = {cbRead, cbSeek, nullptr, cbTell};
        if (ov_open_callbacks(this, &vf, nullptr, 0, cb) != 0) return "not an OGG Vorbis file";
        vf_open = true;
        const vorbis_info* vi = ov_info(&vf, -1);
        if (!vi || vi->channels < 1 || vi->channels > 2) return "OGG: only mono or stereo";
        rate = vi->rate;
        channels = vi->channels;
        const ogg_int64_t total = ov_pcm_total(&vf, -1);
        total_frames = total > 0 ? (u32)total : 0;
        return nullptr;
    }
    u32 read(u8* dst, u32 frames) override {
        const u32 frame_bytes = 2 * channels;
        const u32 want = frames * frame_bytes;
        u32 bytes = 0;
        while (bytes < want) {
            int section;
            const long n = ov_read(&vf, (char*)dst + bytes, want - bytes, &section);
            if (n == OV_HOLE) continue;   // a gap in the stream: keep going
            if (n <= 0) break;            // end of file or a fatal error
            bytes += n;
        }
        return bytes / frame_bytes;
    }
    bool seek(u32 frame) override { return ov_pcm_seek(&vf, frame) == 0; }
};

// MP3: the whole file is read into memory, then decoded with libmad. Timing follows ITGmania
// v1.3.0's RageSoundReader_MP3 [src], because simfile offsets were synced against it:
//   - no gapless trimming: the LAME encoder delay and libmad's 529-sample synthesis delay
//     stay in, and sample 0 is the first sample libmad synthesises;
//   - a first frame tagged "Xing" (VBR) is skipped, but one tagged "Info" (CBR) is played
//     as audio (usually 1152 samples of silence);
//   - frames with a bad CRC produce no output.
// The LAME delay is read only to show how late the music starts (start_delay_frames).
struct Mp3Source : Source {
    u8* data = nullptr;
    size_t size = 0;
    mad_stream stream;
    mad_frame frame;
    mad_synth synth;
    bool mad_open = false;
    bool first_frame = true;
    u32 pcm_pos = 0, pcm_len = 0;   // unread samples of the last synthesised frame

    // Byte offset of a Xing/Info tag in the frame at p, after the header and side info.
    static size_t tagOffset(const u8* p) {
        const bool mpeg1 = ((p[1] >> 3) & 3) == 3;
        const bool mono = (p[3] >> 6) == 3;
        const bool crc = !(p[1] & 1);
        return 4 + (crc ? 2 : 0) + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
    }
    // 'X' for Xing, 'I' for Info, 0 for neither. Reads the LAME encoder delay if present.
    char tagType(const u8* p, const u8* end, int* enc_delay) {
        const u8* t = p + tagOffset(p);
        if (t + 8 > end) return 0;
        char type = 0;
        if (!memcmp(t, "Xing", 4)) type = 'X';
        if (!memcmp(t, "Info", 4)) type = 'I';
        if (!type) return 0;
        const u32 flags = (u32)t[4] << 24 | t[5] << 16 | t[6] << 8 | t[7];
        const size_t lame = 8 + (flags & 1 ? 4 : 0) + (flags & 2 ? 4 : 0) + (flags & 4 ? 100 : 0) +
                            (flags & 8 ? 4 : 0);
        // LAME extension: 9-byte encoder name ("LAME3.100", "Lavc..."), delay at +21 (12 bits).
        if (t + lame + 24 <= end && t[lame] >= 'A' && t[lame] <= 'z')
            *enc_delay = t[lame + 21] << 4 | t[lame + 22] >> 4;
        return type;
    }

    ~Mp3Source() override {
        if (mad_open) {
            mad_synth_finish(&synth);
            mad_frame_finish(&frame);
            mad_stream_finish(&stream);
        }
    }
    const char* open(FILE* f) {
        // libmad needs MAD_BUFFER_GUARD zero bytes after the data to decode the last frame.
        if (const char* err = readWhole(f, data, size, MAD_BUFFER_GUARD)) return err;

        // Scan the frame headers once: rate, channels, length and the Xing/Info tag.
        mad_stream s;
        mad_header h;
        mad_stream_init(&s);
        mad_header_init(&h);
        mad_stream_buffer(&s, data, size + MAD_BUFFER_GUARD);
        u64 samples = 0;
        bool first = true;
        int enc_delay = -1;
        char tag = 0;
        for (;;) {
            if (mad_header_decode(&h, &s) != 0) {
                if (s.error == MAD_ERROR_BUFLEN) break;
                if (s.error == MAD_ERROR_LOSTSYNC) skipId3(&s);
                if (MAD_RECOVERABLE(s.error)) continue;
                break;
            }
            const u32 n = 32 * MAD_NSBSAMPLES(&h);
            if (first) {
                first = false;
                rate = h.samplerate;
                channels = h.mode == MAD_MODE_SINGLE_CHANNEL ? 1 : 2;
                tag = tagType(s.this_frame, data + size, &enc_delay);
                if (tag == 'X') continue;   // skipped, like ITGmania
            }
            samples += n;
        }
        mad_header_finish(&h);
        mad_stream_finish(&s);
        if (!rate) return "no MP3 frames found";
        total_frames = (u32)samples;
        // How late the music starts: Info frame + LAME delay + libmad's 529-sample delay.
        start_delay_frames = (tag == 'I' ? 1152 : 0) + (enc_delay >= 0 ? enc_delay : 0) + 529;
        start_delay_known = enc_delay >= 0;

        mad_stream_init(&stream);
        mad_frame_init(&frame);
        mad_synth_init(&synth);
        mad_stream_buffer(&stream, data, size + MAD_BUFFER_GUARD);
        mad_open = true;
        return nullptr;
    }
    // An ID3v2 tag where libmad lost sync: skip it whole.
    static void skipId3(mad_stream* s) {
        const u8* p = s->this_frame;
        if (s->bufend - p < 10 || memcmp(p, "ID3", 3)) return;
        const u32 len = 10 + (p[6] << 21 | p[7] << 14 | p[8] << 7 | p[9]) + (p[5] & 0x10 ? 10 : 0);
        if ((long)len <= s->bufend - p) mad_stream_skip(s, len);
    }
    // Decodes the next frame into synth. False at the end of the file.
    bool nextFrame() {
        for (;;) {
            if (mad_frame_decode(&frame, &stream) != 0) {
                if (stream.error == MAD_ERROR_BUFLEN) return false;
                if (stream.error == MAD_ERROR_LOSTSYNC) skipId3(&stream);
                if (stream.error == MAD_ERROR_BADDATAPTR) {   // ITGmania plays these anyway
                    mad_synth_frame(&synth, &frame);
                    break;
                }
                if (MAD_RECOVERABLE(stream.error)) continue;   // bad CRC etc.: no output
                return false;
            }
            if (first_frame) {
                first_frame = false;
                int unused;
                if (tagType(stream.this_frame, data + size, &unused) == 'X') continue;
            }
            mad_synth_frame(&synth, &frame);
            break;
        }
        pcm_pos = 0;
        pcm_len = synth.pcm.length;
        return true;
    }
    // Back to the start, then skip whole frames by their headers (fast, no decoding) up to
    // two frames before `target`. Those two are decoded and dropped so the bit reservoir the
    // next frame may need is filled, and the rest of the frame is skipped sample by sample.
    bool seek(u32 target) override {
        mad_synth_finish(&synth);
        mad_frame_finish(&frame);
        mad_stream_finish(&stream);
        mad_stream_init(&stream);
        mad_frame_init(&frame);
        mad_synth_init(&synth);
        mad_stream_buffer(&stream, data, size + MAD_BUFFER_GUARD);
        first_frame = true;
        pcm_pos = pcm_len = 0;
        u32 at = 0;   // output samples before the stream's position
        const u32 per = 1152;
        while (at + 3 * per <= target) {
            if (mad_header_decode(&frame.header, &stream) != 0) {
                if (stream.error == MAD_ERROR_BUFLEN) return false;
                if (stream.error == MAD_ERROR_LOSTSYNC) skipId3(&stream);
                if (MAD_RECOVERABLE(stream.error)) continue;
                return false;
            }
            if (first_frame) {
                first_frame = false;
                int unused;
                if (tagType(stream.this_frame, data + size, &unused) == 'X') continue;
            }
            at += 32 * MAD_NSBSAMPLES(&frame.header);
        }
        while (at < target) {
            if (!nextFrame()) return false;
            if (at + pcm_len > target) {
                pcm_pos = target - at;
                return true;
            }
            at += pcm_len;
            pcm_pos = pcm_len;
        }
        return true;
    }
    static s16 toS16(mad_fixed_t x) {
        x += 1L << (MAD_F_FRACBITS - 16);   // round
        if (x >= MAD_F_ONE) x = MAD_F_ONE - 1;
        if (x < -MAD_F_ONE) x = -MAD_F_ONE;
        return (s16)(x >> (MAD_F_FRACBITS + 1 - 16));
    }
    u32 read(u8* dst, u32 frames) override {
        s16* out = (s16*)dst;   // big-endian, the CPU's order
        u32 done = 0;
        while (done < frames) {
            if (pcm_pos >= pcm_len && !nextFrame()) break;
            const mad_fixed_t* l = synth.pcm.samples[0];
            const mad_fixed_t* r = synth.pcm.samples[synth.pcm.channels > 1 ? 1 : 0];
            while (pcm_pos < pcm_len && done < frames) {
                *out++ = toS16(l[pcm_pos]);
                if (channels == 2) *out++ = toS16(r[pcm_pos]);
                pcm_pos++;
                done++;
            }
        }
        return done;
    }
};

bool endsWith(const char* s, const char* ext) {
    const size_t n = strlen(s), m = strlen(ext);
    return n >= m && strcasecmp(s + n - m, ext) == 0;
}

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
    Source* src = (Source*)source_;
    while (!g_eof && g_filled - g_finished < (u32)N) {
        const u32 i = g_filled % N;
        const u64 t0 = gettime();
        const u32 got = src->read(g_buf[i], kChunkFrames);
        const u32 us = ticks_to_microsecs(gettime() - t0);
        if (us > read_max_us_) read_max_us_ = us;
        decode_us_ += us;
        if (got == 0) {
            g_eof = true;
            break;
        }
        frames_read_ += got;
        const u32 bytes = got * 2 * src->channels;
        const u32 padded = (bytes + 31) & ~31u;
        memset(g_buf[i] + bytes, 0, padded - bytes);
        DCFlushRange(g_buf[i], padded);   // the DSP reads main memory, not the CPU cache
        g_bytes[i] = padded;
        g_frames[i] = got;
        g_filled = g_filled + 1;          // publish after the data is in memory
        if (got < kChunkFrames) g_eof = true;
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

bool AudioStream::open(const char* path, double start_s) {
    close();
    initAsnd();
    FILE* f = fopen(path, "rb");
    if (!f) {
        error_ = "can't open file (SD card / path?)";
        return false;
    }
    Source* src;
    const char* err;
    if (endsWith(path, ".ogg")) {
        OggSource* o = new OggSource;   // open() closes f
        src = o;
        err = o->open(f);
    } else if (endsWith(path, ".mp3")) {
        Mp3Source* m = new Mp3Source;   // open() closes f
        src = m;
        err = m->open(f);
    } else {
        WavSource* w = new WavSource(f);   // owns f
        src = w;
        err = w->open();
    }
    if (err) {
        delete src;
        error_ = err;
        return false;
    }
    if (start_s > 0 && !src->seek((u32)(start_s * src->rate))) {
        delete src;
        error_ = "can't seek to the start time";
        return false;
    }
    source_ = src;
    rate_ = src->rate;
    channels_ = src->channels;
    total_frames_ = src->total_frames;
    start_delay_frames_ = src->start_delay_frames;
    start_delay_known_ = src->start_delay_known;
    error_ = "";
    startFill();
    return true;
}

void AudioStream::startFill() {
    frames_read_ = 0;
    read_max_us_ = 0;
    decode_us_ = 0;
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
}

void AudioStream::stopFill() {
    g_active = false;
    ASND_StopVoice(0);
    stop_ = true;
    LWP_JoinThread((lwp_t)lwp_, nullptr);
    playing_ = false;
    paused_ = false;   // ASND_SetVoice clears the voice's pause flag
}

bool AudioStream::restart(double start_s) {
    if (!source_) return false;
    stopFill();
    Source* src = (Source*)source_;
    if (!src->seek(start_s > 0 ? (u32)(start_s * src->rate) : 0)) {
        error_ = "can't seek to the start time";
        return false;
    }
    startFill();
    return true;
}

void AudioStream::setVolume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 255) volume = 255;
    volume_ = volume;
    if (playing_) ASND_ChangeVolumeVoice(0, volume, volume);
}

void AudioStream::play() {
    if (!source_ || playing_ || g_filled == 0) return;
    const bool le = ((Source*)source_)->little_endian;
    const s32 fmt = channels_ == 2 ? (le ? VOICE_STEREO_16BIT_LE : VOICE_STEREO_16BIT)
                                   : (le ? VOICE_MONO_16BIT_LE : VOICE_MONO_16BIT);
    const u32 level = IRQ_Disable();
    g_ring_min = N;
    ASND_SetVoice(0, fmt, rate_, 0, g_buf[0], g_bytes[0], volume_, volume_, voiceCallback);
    g_queued = 1;
    // ASND may refuse the second buffer until the DSP has taken the new voice; the
    // callback then queues it on the next tick.
    if (g_filled > 1 && ASND_AddVoice(0, g_buf[1], g_bytes[1]) == SND_OK) g_queued = 2;
    g_active = true;
    IRQ_Restore(level);
    playing_ = true;
}

void AudioStream::pause() {
    if (!playing_ || paused_) return;
    ASND_PauseVoice(0, 1);
    paused_ = true;
}

void AudioStream::resume() {
    if (!playing_ || !paused_) return;
    ASND_PauseVoice(0, 0);
    paused_ = false;
}

void AudioStream::close() {
    if (!source_) return;
    stopFill();
    delete (Source*)source_;
    source_ = nullptr;
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
    s.decode_us = decode_us_;
    s.frames_read = frames_read_;
    s.eof = g_eof;
    s.finished = g_eof && g_queued == g_filled;
    return s;
}

}  // namespace wii
