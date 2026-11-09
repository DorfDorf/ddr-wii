// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/wii/WiiInput.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>

namespace wii {
namespace {

// One SI poll: timestamp + every port's buttons. Written by the interrupt, read by update().
struct Sample {
    u64 tick;
    u16 buttons[Input::kPorts];
    u8 ok_mask;        // ports that returned fresh data
    u8 missing_mask;   // ports with no controller (need PAD_Reset)
};

constexpr u32 kRingSize = 256;   // ~2 s at 120 samples/s; power of two
Sample g_ring[kRingSize];
volatile u32 g_head = 0;         // next slot the interrupt writes (only the interrupt changes it)
volatile u32 g_tail = 0;         // next slot update() reads (only update() changes it)
volatile u32 g_overflows = 0;
volatile u32 g_irq_samples = 0;

// Runs in the SI polling (RDST) interrupt. Single core, so the interrupt can't race itself,
// and update() only reads slots before g_head.
void onPoll(u32, void*) {
    PADStatus st[PAD_CHANMAX];
    PAD_Read(st);   // the reader Nintendo's sampling callback was meant for; safe at IRQ level

    const u32 head = g_head;
    if (head - g_tail >= kRingSize) {
        g_overflows = g_overflows + 1;
        return;
    }
    Sample& s = g_ring[head % kRingSize];
    s.tick = gettime();
    s.ok_mask = 0;
    s.missing_mask = 0;
    for (int p = 0; p < PAD_CHANMAX; p++) {
        s.buttons[p] = st[p].button;
        if (st[p].err == PAD_ERR_NONE) s.ok_mask |= (u8)(1 << p);
        else if (st[p].err == PAD_ERR_NO_CONTROLLER) s.missing_mask |= (u8)(1 << p);
    }
    __asm__ __volatile__("" ::: "memory");   // slot contents before the index
    g_head = head + 1;
    g_irq_samples = g_irq_samples + 1;
}

// Lane for each PAD bit, or -1. Mats send arrows as the D-pad (doc 04 §3).
int laneForBit(int bit) {
    switch (1u << bit) {
        case PAD_BUTTON_LEFT:  return input::kLaneLeft;
        case PAD_BUTTON_DOWN:  return input::kLaneDown;
        case PAD_BUTTON_UP:    return input::kLaneUp;
        case PAD_BUTTON_RIGHT: return input::kLaneRight;
        default: return -1;
    }
}

}  // namespace

u64 Input::nowUs() { return ticks_to_microsecs(gettime()); }

bool Input::init(u32 debounce_us) {
    PAD_Init();
    for (auto& f : filters_) f.setWindow(debounce_us);
    g_head = g_tail = 0;
    g_overflows = g_irq_samples = 0;
    window_start_tick_ = last_irq_tick_ = gettime();
    last_irq_samples_ = 0;
    SI_RegisterPollingHandler(onPoll);
    return true;
}

void Input::shutdown() { SI_UnregisterPollingHandler(onPoll); }

void Input::handleSample(u64 tick, const u16* buttons, u8 ok_mask, u8 missing_mask) {
    stats_.samples_total++;
    window_samples_++;
    if (last_sample_tick_) {
        const u32 us = (u32)diff_usec(last_sample_tick_, tick);
        if (us < window_min_us_) window_min_us_ = us;
        if (us > window_max_us_) window_max_us_ = us;
    }
    last_sample_tick_ = tick;

    const u64 t_us = ticks_to_microsecs(tick);
    for (int p = 0; p < kPorts; p++) {
        const u8 bit = (u8)(1 << p);
        if (missing_mask & bit) {
            connected_ &= ~(u32)bit;
            primed_[p] = false;
            held_[p] = 0;
            continue;
        }
        if (!(ok_mask & bit)) continue;   // no fresh data this poll: keep the last state
        connected_ |= bit;
        if (!primed_[p]) {               // first data from this port: adopt it silently
            filters_[p].reset(buttons[p]);
            held_[p] = buttons[p];
            primed_[p] = true;
            continue;
        }
        input::InputFilter::Change changes[input::InputFilter::kButtons];
        const int n = filters_[p].feed(t_us, buttons[p], changes);
        for (int i = 0; i < n; i++) {
            if (changes[i].pressed) down_[p] |= (u16)(1u << changes[i].button);
            const int lane = laneForBit(changes[i].button);
            if (lane < 0 || event_count_ >= kMaxEvents) continue;
            events_[event_count_++] =
                input::LaneEvent{changes[i].time_us, (u8)p, (u8)lane, changes[i].pressed};
        }
        held_[p] = filters_[p].state();
    }
}

void Input::update() {
    event_count_ = 0;
    for (u16& d : down_) d = 0;

    u32 missing_all = 0;
    u32 drained = 0;
    const u32 head = g_head;
    __asm__ __volatile__("" ::: "memory");   // read slots after reading the index
    for (u32 tail = g_tail; tail != head; tail++) {
        const Sample& s = g_ring[tail % kRingSize];
        handleSample(s.tick, s.buttons, s.ok_mask, s.missing_mask);
        missing_all |= s.missing_mask;
        drained++;
    }
    g_tail = head;

    // No interrupt samples for half a second (none at all, or every pad unplugged: the SI poll
    // stops when no port is enabled, so nothing would re-probe them): read once per frame
    // instead. PAD_Read also re-arms the probe of a port that came back empty.
    const u64 now = gettime();
    const u32 irq_samples = g_irq_samples;
    if (irq_samples != last_irq_samples_) {
        last_irq_samples_ = irq_samples;
        last_irq_tick_ = now;
    }
    stats_.fallback = diff_usec(last_irq_tick_, now) >= 500000;
    if (stats_.fallback) {
        PADStatus st[PAD_CHANMAX];
        PAD_Read(st);
        u16 buttons[kPorts];
        u8 ok = 0, missing = 0;
        for (int p = 0; p < kPorts; p++) {
            buttons[p] = st[p].button;
            if (st[p].err == PAD_ERR_NONE) ok |= (u8)(1 << p);
            else if (st[p].err == PAD_ERR_NO_CONTROLLER) missing |= (u8)(1 << p);
        }
        handleSample(gettime(), buttons, ok, missing);
        missing_all |= missing;
        drained++;
    }
    stats_.last_drain = drained;

    // Ports with no controller get re-probed, as PAD_ScanPads does, so hot-plugging works.
    if (missing_all) {
        u32 reset_bits = 0;
        for (int p = 0; p < kPorts; p++)
            if (missing_all & (1u << p)) reset_bits |= PAD_CHAN0_BIT >> p;
        PAD_Reset(reset_bits);
    }

    // Per-second sample statistics.
    if (diff_usec(window_start_tick_, now) >= 1000000) {
        stats_.samples_per_sec = window_samples_;
        stats_.interval_min_us = window_samples_ > 1 ? window_min_us_ : 0;
        stats_.interval_max_us = window_max_us_;
        window_start_tick_ = now;
        window_samples_ = 0;
        window_min_us_ = 0xFFFFFFFF;
        window_max_us_ = 0;
    }
    stats_.overflows = g_overflows;
}

}  // namespace wii
