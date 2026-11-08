# 04 — GameCube Controller and Dance Pad Integration

*Checked 2026-11-08 against the devkitPro libogc sources (`gc/ogc/pad.h`, `libogc/pad.c`, `gc/ogc/si.h` on `master`, release v3.2.1), Dolphin's `SI_Device.h`/`SI_DeviceDanceMat.cpp`, and the two supplied projects. **[src]** = read in source. **[exp]** = must be tested.*

---

## 1. How the Wii reads GameCube controllers

- Wiis with GameCube ports expose them to **Wii-mode software** through the same **Serial Interface (SI)** hardware the GameCube used. Homebrew reads them directly with libogc's `PAD_*` API, with no GameCube-mode switch. Both supplied projects do exactly this from Wii-mode `.dol`s **[src: Dance Clone `WiiInput.cpp:80`, StepMania-Wii `InputHandler_Wii.cpp:30`]**.
- Hardware polls each port. `PAD_Read()` copies the latest **SI response buffers**: it checks `SI_IsChanBusy`, `SI_GetStatus` and `SI_GetResponse` and doesn't start a transfer for a normal read. Polling is enabled through `SI_EnablePolling` **[src: `pad.c`]**.
- `PAD_ScanPads()` calls `PAD_Read()` for all 4 channels and computes edge states from the previous call **[src]**:

```c
__pad_keys[i].up    = oldstate & ~state;
__pad_keys[i].down  = state & (state ^ oldstate);
__pad_keys[i].state = state;
```

So **"down" means pressed since the previous `PAD_ScanPads()` call.** Edges are only as fine-grained as your scan rate.

## 2. The libogc API you need

From `gc/ogc/pad.h` **[src]**:

```c
u32  PAD_Init(void);
u32  PAD_ScanPads(void);        // returns bitmask: bit i set => channel i connected (LSB = port 1)
u16  PAD_ButtonsHeld(int pad);  // level
u16  PAD_ButtonsDown(int pad);  // rising edges since previous scan
u16  PAD_ButtonsUp(int pad);    // falling edges since previous scan
u32  PAD_Read(PADStatus *status);   // raw: fills PADStatus[4]
sampling_callback PAD_SetSamplingCallback(sampling_callback cb);
```

```c
#define PAD_BUTTON_LEFT   0x0001
#define PAD_BUTTON_RIGHT  0x0002
#define PAD_BUTTON_DOWN   0x0004
#define PAD_BUTTON_UP     0x0008
#define PAD_TRIGGER_Z     0x0010
#define PAD_TRIGGER_R     0x0020
#define PAD_TRIGGER_L     0x0040
#define PAD_BUTTON_A      0x0100
#define PAD_BUTTON_B      0x0200
#define PAD_BUTTON_X      0x0400
#define PAD_BUTTON_Y      0x0800
#define PAD_BUTTON_MENU   0x1000   // same value as START
#define PAD_BUTTON_START  0x1000
```

Device detection, from `gc/ogc/si.h` **[src]**: `u32 SI_GetType(s32 chan);` and `u32 SI_Probe(s32 chan);` with `SI_TYPE_MASK 0x18000000`, `SI_TYPE_GC 0x08000000`, `SI_GC_STANDARD 0x01000000`, `SI_GC_CONTROLLER (SI_TYPE_GC|SI_GC_STANDARD)`, `SI_GC_WAVEBIRD`, `SI_GC_KEYBOARD`, `SI_GC_STEERING (SI_TYPE_GC|0)`, `SI_GC_NOMOTOR 0x20000000`.

Notes:
- The `PAD_ScanPads()` connected mask is **LSB-first** (`1<<i`). The `PAD_CHANn_BIT` constants (`0x80000000 >> n`) are MSB-first and are used by `PAD_Reset`/`PAD_Read`'s return value. Don't mix them up **[src]**.
- In devkitPro libogc, `PAD_ScanPads` doesn't call `PAD_Init`, so call `PAD_Init()` once at startup **[src]**.

## 3. Is a dance pad a "standard controller"?

**libogc's gate:** while resetting or probing a channel, `pad.c` does this **[src]**:

```c
if(((type&SI_TYPE_MASK)-SI_TYPE_GC) || !(type&SI_GC_STANDARD)) {
    __pad_doreset();
    return;          // device not enabled -> PAD reports it as disconnected
}
```

So **only devices reporting a GameCube type with the "standard" bit set are readable via `PAD_*`.** For example, the steering wheel (`SI_GC_STEERING`, no standard bit) is rejected.

**What a GameCube dance mat reports:** Dolphin, which emulates the mat closely enough for DDR Mario Mix, defines

```c
SI_DANCEMAT = (SI_TYPE_GC | SI_GC_STANDARD | 0x00000300)   // = 0x09000300
```

**[src: Dolphin `Source/Core/Core/HW/SI/SI_Device.h`]**. Its status word puts mat buttons in the normal button positions **[src: `SI_DeviceDanceMat.cpp`]**:

| Mat panel | Bit | libogc name |
|---|---|---|
| Up arrow | 0x0008 | `PAD_BUTTON_UP` |
| Down arrow | 0x0004 | `PAD_BUTTON_DOWN` |
| Left arrow | 0x0001 | `PAD_BUTTON_LEFT` |
| Right arrow | 0x0002 | `PAD_BUTTON_RIGHT` |
| A / B (orange corner buttons) | 0x0100 / 0x0200 | `PAD_BUTTON_A` / `PAD_BUTTON_B` |
| "+" button | 0x0010 | `PAD_TRIGGER_Z` |
| "−" button | 0x1000 | `PAD_BUTTON_START` |
| (Active Life mat only) | 0x0400 / 0x0800 | `X` / `Y` |

(Dolphin's comments: "Map the dpad to the blue arrows, the buttons to the orange arrows" and "Z = + button, Start = - button. Active Life Mats have a different layout.")

Dance Clone's notes say the same: "minus → Z / MENU, plus → START, B, A, LEFT/RIGHT/UP/DOWN" (`WiiInput.cpp:68-77`). The plus/minus labels are swapped relative to Dolphin's comment. That doesn't matter for gameplay, because we only need the four arrows plus Start and Back.

**Conclusion:** the standard GameCube DDR mat (the Konami/Nintendo "Action Pad" for DDR Mario Mix) has the standard bit set, so it passes libogc's gate, and its arrows arrive as D-pad bits. **[exp] What's left to confirm** is the SI type your particular mat reports. Third-party mats probably emulate a plain controller (`0x09000000`), but they can differ, and the test program below prints it.

**Differences between mats:**
- *GameCube DDR mats* (Mario Mix and similar third-party mats): as above.
- *Active Life / Family Trainer mats* (Bandai Namco, which plug into a GC port on the Wii): a different panel layout, with extra X/Y bits **[src: Dolphin comments]**. They're usable, but the panel-to-lane mapping differs, so make the mapping configurable.
- *USB or Wii-remote-based mats* (e.g. DDR Hottest Party mats, which plug into a Wiimote): out of scope. They go through WPAD, not PAD.

## 4. Simultaneous presses (jumps)

- Each arrow is an independent bit. **libogc doesn't filter opposite D-pad directions** (no Left+Right or Up+Down masking in `pad.c`) **[src]**. Dance Clone relies on this for jumps **[src: `Player.cpp`, `Constants.cpp: jumpAllowDelay = 80`]**.
- Whether a mat electrically reports all four panels at once is a hardware property. DDR mats are designed for jumps, but **[exp]**: test it with the program below.
- **Game logic:** treat each lane independently (a press in lane L judges the nearest unjudged note in lane L). A "jump" is just two notes on the same row, and its combo and score effect is applied once (Dance Clone fixed a bug where "jumps [were] counted as two steps for the combo total", `CHANGELOG v0.65`).

## 5. Timing resolution

- If you call `PAD_ScanPads()` once per frame, a press is timestamped with **≤ 16.7 ms** of error at 60 Hz (20 ms at PAL 50 Hz). That's coarser than a "Marvelous" window (±22.5 ms in StepMania), which isn't good enough for strict judging.
- **Better (to test in M3):**
  1. Register `PAD_SetSamplingCallback(cb)` **[src: prototype in `pad.h`]**. The callback runs whenever new SI samples are taken. Inside it, read the buttons and push `{gettime(), buttons}` into a ring buffer. The main loop drains the buffer and judges with the real timestamps.
  2. Measure how often the callback fires (SI polling is tied to video lines, so expect a few times per frame **[exp]**). If needed, raise the SI poll rate (libogc's SI API; libogc2 adds `PAD_SetSamplingRate`) **[exp]**.
- Whatever the input rate, **convert input timestamps to song time with the same clock the audio uses** (doc 06 §3).

## 6. Input test program (Milestone 3)

A minimal, self-contained program using only the APIs verified above plus the standard libogc video/console setup from the devkitPro template. It shows each port's SI type, live button bits, four lane indicators, the largest number of arrows held at once, and the interval between PAD samples.

```c
// padtest.c — GameCube port / dance-mat diagnostic for Wii homebrew (libogc)
#include <gccore.h>
#include <ogc/lwp_watchdog.h>   // gettime(), diff_usec()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xfb;
static GXRModeObj *rmode;

static volatile u64 last_sample_tick = 0;
static volatile u32 sample_count = 0, min_us = 0xFFFFFFFF, max_us = 0;

// Called by libogc each time new SI pad data is sampled (timestamping experiment).
// [exp] confirm the `sampling_callback` typedef in your pad.h is `void (*)(void)`; it runs in
// interrupt context, so keep it tiny (no printf, no malloc).
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
        printf("\x1b[2;0H");   // cursor to row 2 (libogc console supports ANSI positioning)
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
```

**Why START+Z to exit instead of the Reset button?** It avoids the reset/power callback signatures, which changed between libogc versions (Dance Clone's `void f(void)` style is old) **[exp: check `SYS_SetResetCallback`'s current prototype before using it]**. Arrow-only combinations can't trigger the exit by accident during play.

**What to verify with it:**

| Check | Pass criterion |
|---|---|
| Mat detected | Port shows `[conn]` and `GC standard (PAD ok)`. Note the SI value |
| Four arrows map to L/D/U/R | Each panel lights the right letter |
| Jumps | `max-at-once` reaches 2 for L+R and U+D. Try 3–4 by hand |
| No ghosting | Releasing a panel clears it within one frame |
| Sampling | Note the min/max interval (expect several samples per frame **[exp]**) |
| Dolphin keyboard | The same checks with the keyboard profile from doc 03 §4.4 |

If the mat shows `GC non-standard (PAD rejects)`, PAD can't read it. The fallback would be reading it via raw `SI_Transfer`/`SI_GetResponse` with our own polling. That's possible, but **[exp]** and more work. All current evidence says this won't be needed.

## 7. How the supplied projects do it

| | Dance Clone (`Source/Platform/Wii/WiiInput.cpp:58-126`) | StepMania-Wii (`trunk/arch/InputHandler/InputHandler_Wii.cpp:27-90`) |
|---|---|---|
| Scan | `WPAD_ScanPads(); PAD_ScanPads()` once per frame | Same |
| Presence | `PAD_ScanPads()` mask → `danceMatActive` | Not used |
| Arrows | `PAD_ButtonsDown` → edge array, `PAD_ButtonsHeld` → level array, 4 channels | `Down|Held` OR'd into a level only, 2 channels, merged with the Wiimote D-pad |
| Extras | A/B/Start/Z, Wiimote IR cursor for menus | Start/Select(Z)/A/B. HOME → `exit(0)` |
| Bugs | `Button5` assigned twice (`:104-105`), so Z is overwritten. `Button4` held logic is odd (`:118`) | Merging Wiimote and pad on the same channel can confuse players |
| Verdict | **Best reference for the design.** Re-implement cleanly | Fine, but less DDR-specific |

## 8. Recommended input module (summary; see doc 06)

```c
struct LaneEvent { u64 tick; u8 port; u8 lane; bool pressed; };
// Producer: PAD sampling callback (or PAD_ScanPads per frame as fallback)
//           diffs per-port button bits -> LaneEvents with timestamps into a lock-free ring buffer.
// Consumer: gameplay loop drains events, converts tick -> song time, judges per lane.
// Mapping:  per-port table {LEFT,DOWN,UP,RIGHT} -> lanes 0..3 (configurable for Active Life mats);
//           START = confirm/pause, B or Z = back. Menus also accept arrows.
// Players:  each port that pressed START to join is bound to a player; events are routed by port
//           (local multiplayer: 2 players now, 4 later: doc 06 §11). Debounce state is per port.
```
