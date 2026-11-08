// SPDX-License-Identifier: GPL-3.0-or-later
// padtest.c - GameCube port / dance-mat diagnostic for Wii homebrew (libogc).
// See docs/04-wii-controller-integration.md section 6.
//
// Shows each port's SI device type, live button bits, four lane indicators,
// the largest number of arrows held at once and the interval between PAD samples.
// Hold START+Z on port 1 for 2 s to exit back to the loader.

#include <gccore.h>
#include <ogc/lwp_watchdog.h>   // gettime(), diff_usec()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xfb;
static GXRModeObj *rmode;

static volatile u64 last_sample_tick = 0;
static volatile u32 sample_count = 0, min_us = 0xFFFFFFFF, max_us = 0;

// Called by libogc each time new SI pad data is sampled. Runs in interrupt
// context, so keep it tiny (no printf, no malloc).
static void on_pad_sample(void) {
    u64 now = gettime();
    if (last_sample_tick) {
        u32 us = (u32)diff_usec(last_sample_tick, now);
        if (us < min_us) min_us = us;
        if (us > max_us) max_us = us;
    }
    last_sample_tick = now;
    sample_count++;
}

static const char *type_name(u32 t) {
    if (t & SI_ERROR_NO_RESPONSE)            return "none";
    if ((t & SI_TYPE_MASK) != SI_TYPE_GC)    return "non-GC";
    if (t & SI_GC_WIRELESS)                  return "wireless";
    if (t & SI_GC_STANDARD)                  return "GC standard (PAD ok)";
    return "GC non-standard (PAD rejects)";
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    VIDEO_Init();
    PAD_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    PAD_SetSamplingCallback(on_pad_sample);

    int max_simultaneous[4] = {0};
    u32 quit_frames = 0;

    while (1) {
        u32 connected = PAD_ScanPads();
        printf("\x1b[2;0H");   // cursor to row 2
        printf("DDR pad test  -  hold START+Z for 2 s to exit\n\n");

        for (int ch = 0; ch < 4; ch++) {
            u32 type = SI_GetType(ch);
            u16 held = PAD_ButtonsHeld(ch);
            u16 down = PAD_ButtonsDown(ch);
            int n = !!(held & PAD_BUTTON_LEFT) + !!(held & PAD_BUTTON_DOWN)
                  + !!(held & PAD_BUTTON_UP)   + !!(held & PAD_BUTTON_RIGHT);
            if (n > max_simultaneous[ch]) max_simultaneous[ch] = n;

            printf("Port %d %s  SI=0x%08X %-30s\n", ch + 1,
                   (connected & (1u << ch)) ? "[conn]" : "[----]",
                   (unsigned)type, type_name(type));
            printf("   held=0x%04X down=0x%04X   [%c] [%c] [%c] [%c]   max-at-once=%d  \n\n",
                   held, down,
                   (held & PAD_BUTTON_LEFT)  ? 'L' : ' ',
                   (held & PAD_BUTTON_DOWN)  ? 'D' : ' ',
                   (held & PAD_BUTTON_UP)    ? 'U' : ' ',
                   (held & PAD_BUTTON_RIGHT) ? 'R' : ' ',
                   max_simultaneous[ch]);
        }
        printf("PAD samples: %lu  interval min/max: %lu / %lu us        \n",
               (unsigned long)sample_count, (unsigned long)min_us, (unsigned long)max_us);

        u16 h0 = PAD_ButtonsHeld(0);
        quit_frames = ((h0 & PAD_BUTTON_START) && (h0 & PAD_TRIGGER_Z)) ? quit_frames + 1 : 0;
        if (quit_frames > 120) break;

        VIDEO_WaitVSync();
    }
    return 0;   // returns to the loader (HBC / Dolphin)
}
