# Animation systems

*Part of the five research documents (start with [recommended_implementation_plan.md](recommended_implementation_plan.md)). Versions and pinned commits: [gui_framework_comparison.md §1](gui_framework_comparison.md#1-what-was-reviewed). Markers: **[src]** read in source, **[doc]** documentation, **[mem]** from memory (verify), **[exp]** still to be tested.*

---

## 1. Summary

- **Almost every Wii homebrew project animates in fixed steps per frame** (libgui's effects, WiiFlow, USB Loader GX, HBC, GRRLIB's particle example). That makes animations 20% slower on PAL 50 Hz, and WiiFlow compensates by switching to bigger factors in 50 Hz mode **[src]**. ITGmania animates everything in **seconds or song beats** **[src]**. ddr-wii must look the same at 50 and 60 Hz (doc 06 §2), so it should follow ITGmania.
- **What to reuse:** ITGmania's tween curves, its periodic effects and their beat-driven *effect clocks*, its wheel motion, and the timings of Simply Love's judgement and the default noteskin's receptor and explosion animations. USB Loader GX's Hermite keyframe evaluation and libgui's time-based state blend are good small models.
- **What to write ourselves:** a small, allocation-free animation core in `game/source/core/anim/` (portable, so it is tested on the PC like the rest of `core/`): easing functions, tweens, keyframe tracks, frame-rate-independent smoothing and springs, periodic effects on a time or beat clock, flipbooks and a fixed-size particle pool. §13 sketches it.

---

## 2. The time model

| Project | How it animates | Time base | Evidence |
|---|---|---|---|
| libgui, legacy effects | `xoffsetDyn += effectAmount`, `alphaDyn += effectAmount`, `scaleDyn += effectAmount × 0.01` until the target | **Per frame** | [GuiElement.cpp:426-532][lg-fx] **[src]** |
| libgui, `GuiShape` state change | `blend += deltaTime / transition`, then smoothstep `b²(3 − 2b)` | **Seconds** | [GuiShape.cpp:90-130][lg-shape] **[src]** |
| libgui, key repeat | first repeat after 0.3 s, then every 0.05 s | Seconds | [InputController.h:81-82][lg-repeat] **[src]** |
| WiiFlow cover flow and buttons | `pos += (target − pos) × speed` each tick | **Per frame**; speeds go from 0.1/0.07 to 0.12/0.084 in 50 Hz mode | [coverflow.cpp:218-241][wf-speeds], [`_coverTick`][wf-covertick], [`SElement::tick`][wf-elemtick] **[src]** |
| USB Loader GX effects | as libgui, plus pulse/rock/orbit | Per frame | [gui_element.cpp:536-630][ul-fx] **[src]** |
| USB Loader GX banner animator | keyframes by frame number (Wii banner files) | Frames at a nominal rate | [Animator.cpp:120-241][ul-anim] **[src]** |
| HBC bubbles and dialogs | `py -= speed`, `val += step`; dialog slides over `TRANS_STEPS` frames | Per frame | [bubbles.c:153-224][hbc-bubbles], [dialogs.c:138-170][hbc-dfade] **[src]** |
| GRRLIB particle example | `alpha *= salpha` each frame | Per frame | [examples/particle/source/main.cpp:174-240][gr-particle] **[src]** |
| WiiLÖVE | `love.update(dt)` from `gettime()` deltas | Seconds | [timer.cpp][wl-timer] **[src]** |
| Dance Clone | decals timed against song time | Song ms | `Source/Game/Game.cpp:2004-2060` **[src]** |
| ITGmania | tween queue consumes `fDeltaTime`; effects advance by seconds or by **song beat** | Seconds / beats | [`UpdateTweening`][itg-tween-update], [`UpdateInternal`][itg-effclock] **[src]** |
| ddr-wii | gameplay effects from µs timestamps; the song wheel eases by `× 0.35` per frame | Mixed | [Gameplay.cpp:334-343](../game/source/screens/Gameplay.cpp#L334-L343), [SongSelect.cpp:316](../game/source/screens/SongSelect.cpp#L316) **[src]** |

**WiiFlow's 50 Hz fix is the evidence that per-frame factors are the wrong model.** Its factors scale by 60/50 = 1.2, a linear approximation of the exact correction **[src]**. The exact frame-rate-independent form of "move a fraction *k* of the way each frame" is

```
x += (target − x) × (1 − exp(−dt / τ))          with   τ = −1 / (60 · ln(1 − k))
```

**[mem: standard result]**. WiiFlow's k = 0.1 at 60 Hz is τ ≈ 0.16 s, and ddr-wii's wheel `0.35` is τ ≈ 0.039 s. With that form the same τ gives the same motion at 50 Hz, 60 Hz and through a dropped frame.

**Clocks for ddr-wii.** Two are enough:

1. **UI time:** seconds from `gettime()` since the scene started (monotonic). Menus, transitions, focus and idle animations use it.
2. **Song time and beat:** `SongClock` seconds, and the beat from `TimingData` (as the note field already computes, doc 06 §3.2). Gameplay effects use it, so they stay locked to the music and pause with it; beat-driven effects (§10) also use it in menus while a preview plays.

---

## 3. Sprite animation (flipbooks)

| Implementation | What it does | Verdict |
|---|---|---|
| libwiisprite [`Sprite::SetFrame` / `NextFrame` / `PrevFrame` / `SetFrameSequence`][ws-frames] | Frames cut from a sheet by `SetImage(image, frameW, frameH)`; UVs inset by a small correction to stop bleeding ([`_CalcFrame`][ws-calcframe]); the app steps frames itself **[src]** | Idea only (no timing) |
| GRRLIB tile sets ([`GRRLIB_InitTileSet`][gr-tileset], `GRRLIB_DrawTile(…, frame)`) | Frame index → UV rectangle **[src]** | What ddr-wii's debug font uses |
| Dance Clone `Decal` | Static / Once / Loop modes, a per-frame duration, an option to hold the last frame, timed by song time (`Source/Game/Decal.h:35-40`) **[src]** | **Adapt** (time-based, simple) |
| Funkin' Wii | Parses a Sparrow XML atlas (`SubTexture` rectangles) with tinyxml2 ([teststate.cpp][fw-test]) **[src]** | Shows the atlas-descriptor approach |
| ITGmania `Sprite` | Sprite sheets with per-state delays, used by Simply Love's judgement sheet (6–7 frames, doubled for early/late, [Player judgment.lua:104-112][sl-judg]) **[src]** | **Adapt** the "frame per judgement" layout |

**Recommendation:** a `Flipbook` value — first frame, frame count, frames per second, mode (once, loop, ping-pong), start time — that returns the atlas cell for a given time. Use it for multi-frame art (judgement sheets with early/late variants, animated mascots). Most hit effects need no frames at all: one sprite tweened in scale and alpha (§4) looks better and costs one quad.

---

## 4. Tweening and interpolation

| Implementation | What it does | Strength | Weakness |
|---|---|---|---|
| libgui effects ([GuiElement.cpp:426-532][lg-fx]) | One effect at a time per element (bitmask), constant step to a target | Tiny | Per frame, linear, no queue |
| WiiFlow targets ([`_updateTarget`][wf-target] + [`_coverTick`][wf-covertick]) | Layout computes targets; a tick chases them | Retargets naturally when the selection changes mid-move | Per frame; no curve control |
| WiiFlow fanart ([`CFanartElement::tick`][wf-fanart]) | INI-defined start/end x, y, alpha, scale, angle after a delay, linear steps | Data-driven | Per frame, linear |
| USB Loader GX banner animator ([`HermiteKeyHandler::GetFrame`][ul-anim]) | Keyframe tracks; between two keys a **cubic Hermite** with per-key slopes; step tracks for discrete values ([`StepKeyHandler`][ul-step]) | Authorable curves, compact | Frame-number based |
| ITGmania tween queue ([`BeginTweening`][itg-begin], [`UpdateTweening`][itg-tween-update], [`TweenState::MakeWeightedAverage`][itg-wavg]) | Each `BeginTweening(t, curve)` appends a state copied from the previous target; setters edit the newest target; `dt` is consumed across queued tweens, so a dropped frame lands exactly; a command can run when a tween starts | Expressive (`sleep`, chains like `zoom(0.8):decelerate(0.1):zoom(0.75):sleep(0.6):…`) | Heap-allocates a state per tween (`new TweenStateAndInfo`); tied to Lua and the actor tree |
| libgui `GuiShape` ([GuiShape.cpp:90-130][lg-shape]) | Time-based blend between two styles with smoothstep | Simple, correct | — |

**Recommendation:** three building blocks, all evaluated from a clock value and none allocating at runtime:

1. **`Tween`** — `{from, to, start, duration, ease}` evaluated at time *t*. Most UI motion (focus, slide-in, judgement pop) is one tween or a short fixed chain.
2. **`Track`** — a small fixed array of keys `{time, value, slope}` with linear, Hermite (USB Loader GX's formula) or step interpolation, for authored multi-step motion such as the judgement's "pop, hold, shrink away" written as data.
3. **`Smooth` / `Spring`** — chase a moving target with the exact exponential form of §2, or a critically damped spring when overshoot or velocity continuity is wanted (wheel settling, a banner card following the cursor) **[mem: standard]**.

ITGmania's queue semantics (chains with sleeps) map onto a `Track`; the queue itself isn't needed.

---

## 5. Easing functions

| Curve | Formula (t in 0..1) | Source |
|---|---|---|
| Linear | `t` | ITGmania `TweenLinear` ([Tween.cpp][itg-tween]) **[src]** |
| Accelerate (ease-in quad) | `t²` | ITGmania `TweenAccelerate` **[src]** |
| Decelerate (ease-out quad) | `1 − (1 − t)²` | ITGmania `TweenDecelerate` **[src]** |
| Spring (overshoot, settles) | `1 − cos(2.5πt) / (1 + 3t)` | ITGmania `TweenSpring` **[src]** |
| Cubic Bézier (1D / 2D) | control points | ITGmania `InterpolateBezier1D/2D` **[src]** |
| Smoothstep | `t²(3 − 2t)` | libgui `GuiShape` ([GuiShape.cpp:126][lg-shape]) **[src]** |
| Sine ease-out | `sin(πt / 2)` | HBC [`dialog_fade`][hbc-dfade] **[src]** |
| Cubic Hermite between keys | `h00·p0 + h10·m0·Δ + h01·p1 + h11·m1·Δ` | USB Loader GX [Animator.cpp:223-238][ul-anim] **[src]** |
| Penner set: in/out/in-out quad, cubic, quart, quint, sine, expo, circ, back (overshoot constant 1.70158), elastic, bounce | standard | **[mem]** — write them as small pure functions and unit-test the endpoints |

Simply Love's and ITG's animations use mostly *linear*, *decelerate* and *accelerate* (§10 values), so those three plus smoothstep, back-out and spring cover the UI.

---

## 6. Fade and slide transitions

| Implementation | Mechanics | Verdict |
|---|---|---|
| libgui `SLIDE_IN/OUT` + `FADE` ([GuiElement.cpp:428-519][lg-fx]); prompt waits for the effect ([menu.cpp:165-182][lg-prompt]); exit fade adds 15 alpha per frame ([menu.cpp:70-80][lg-updategui]) | Element slides from off-screen by a constant step; screen loop blocks until done **[src]** | Avoid the blocking and the steps |
| HBC [`dialog_fade`][hbc-dfade] | Dialog rises along `sin` while a full-screen mask fades, drawn in a blocking loop **[src]** | Adapt the curve |
| WiiFlow [`hide`/`show`][wf-hide] + theme `effect_x/effect_y/effect_scale_x/y` ([`_setHideAnim`][wf-hideanim]) | Each element glides between its place and a theme-defined hidden offset/scale **[src]** | **Adapt**: per-element entrances are what make screens feel alive |
| ITGmania In/Out/Cancel transitions ([ScreenWithMenuElements.cpp:108-124][itg-swme], [Transition.cpp][itg-trans]) | Transition actors drawn on top; the next screen loads when Out has finished ([`LoadDelayedScreen`][itg-sm-delay]) **[src]** | **Adapt** |
| Simply Love wheel sort | `zoomy 0 → accelerate 0.2 → zoomy 1` on, `decelerate 0.2 → zoomy 0` off ([metrics.ini:1547-1548][sl-wheel]) **[src]** | Values to start from |

**Recommendation:** a scene transition is a pair of timelines: the outgoing scene's Out (e.g. elements slide 24 px and fade over 0.2 s with accelerate) and the incoming scene's In (staggered by 30–50 ms per element, decelerate). Both scenes draw during the overlap; the shared background never fades. All of this is non-blocking (the frame loop keeps running, audio previews keep playing).

---

## 7. Rotation and scaling

| Implementation | Effect | Formula / mechanics |
|---|---|---|
| ITGmania periodic effects ([Actor.cpp:607-640][itg-effects]) | wag | `rotation += magnitude × sin(2πp)` **[src]** |
| | bounce | `pos += magnitude × sin(πp)` **[src]** |
| | bob | `pos += magnitude × sin(2πp)` **[src]** |
| | pulse | `zoom × lerp(min, max, sin(πp))` **[src]** |
| | spin | rotation grows with time (handled in `UpdateInternal`, [Actor.cpp:954][itg-effclock]) **[src]** |
| | vibrate | random offset each frame, scaled by zoom **[src]** |
| ITGmania effect timing ([PreDraw][itg-predraw], [`RecalcEffectPeriod`][itg-efftiming]) | One period = ramp to half + hold at half + ramp to full + hold at full + hold at zero; *p* runs 0 → 0.5 → 1 across the first four parts and is 0 during the last; `effectoffset` shifts the phase **[src]** | Lets one formula do blinks, ramps and pulses |
| USB Loader GX [`EFFECT_GOROUND`][ul-fx] | Orbit an element around a point by an angle, rotating it to match (the carousel) **[src]** | — |
| USB Loader GX [`EFFECT_PULSE`][ul-pulse], `EFFECT_ROCK_VERTICLE` | Scale breathing; ±10 px vertical rock **[src]** | — |
| WiiFlow cover oscillation ([`_coverMovesA/P`, `_cameraMoves`][wf-osc]) | Slow sinusoidal sway of the camera and the centre cover from the tick count **[src]** | Idle "breathing" for a selected item |

**Recommendation:** port ITGmania's periodic-effect formulas and timing as `anim::Periodic` (pure functions of a phase), and drive the phase from either UI time or song beats (§10).

---

## 8. Particle systems

| Implementation | Design | Wii concern |
|---|---|---|
| GRRLIB [particle example][gr-particle] | `std::vector<Particle*>`, a `new` per particle, `delete` on death; multiplicative per-frame decay; one `GRRLIB_DrawImg` each **[src]** | Heap churn and frame-rate dependence; ≈2.6 µs per particle at GRRLIB's measured cost (graphics doc §4) |
| HBC [bubbles][hbc-bubbles] | Fixed arrays of bubbles and their draw entries; respawn in place when off-screen; pointer pops a bubble into sub-bubbles **[src]** | No allocation; per-frame steps |
| ITGmania | No particle system in the engine; themes fake it with actors | — |

**Recommendation:** a fixed-capacity pool (e.g. 512) stored as arrays of position, velocity, life, size, colour and spin; `update(dt)` integrates and kills; drawing goes through the batched sprite path with additive blending from one atlas cell. Spawn from events: a few sparks per Marvelous hit, a burst at combo milestones, a slow ambient drift in menus. Budget it by drawn area as well as count (wii_graphics_architecture §5).

---

## 9. Animated backgrounds

| Implementation | Technique | Cost |
|---|---|---|
| Simply Love shared background ([Normal.lua][sl-bg]) | 10 sprites of one tiled texture at zoom 1.3, each with its own `texcoordvelocity` (e.g. 0.03, 0.01 texture widths per second), alpha 0.05–0.2, tinted with the theme colour; fades in with `accelerate(0.8)` **[src]** | 10 full-screen blended layers ≈ 3 Mpx per frame |
| HBC bubbles ([bubbles.c][hbc-bubbles]) | Sprite pool rising with a sine wobble; count follows the time of day ([`bubble_update_count`][hbc-bubcount]) **[src]** | Tens of small quads |
| HBC gradients ([`plot_gradient`][hbc-grad]) | Four corner colours per quad **[src]** | One quad |
| WiiFlow fanart ([fanart.cpp][wf-fanart]) and THP video (`source/gui/WiiMovie.cpp`, `gcvid.cpp`) | Keyframed layers; video decoded to textures (`STexture::fromTHP`) **[src]** | Video costs a JPEG decode per frame |
| Snes9x GX ([OgcEmulatorVideo.cpp:763-929][s9-video]) | A texture rewritten every frame (`DCStoreRange` + `GX_InvalidateTexAll`) **[src]** | Bandwidth of the upload |
| Wii64 ([`Gui::drawBackground`][w64-bg]) | One background texture recoloured through a TEV register **[src]** | One quad |

**Recommendation:** for menus, 3–4 UV-scrolling layers (Simply Love's idea with fewer layers), a gradient wash, and the HBC-style sprite pool for depth, all tinted from the theme palette. For gameplay, the song's `#BACKGROUND` image dimmed to the player's brightness setting, with a slow pan/zoom and an optional brightness pulse on the beat (§10). Background videos stay a non-goal (doc 06 §1).

---

## 10. Music-reactive effects

**ITGmania's effect clocks make beat sync free.** An actor's effect phase can advance with `CLOCK_BGM_BEAT` (the song's beat), `CLOCK_BGM_TIME`, per-player beats or cabinet lights instead of wall time ([Actor.cpp:896-950][itg-effclock], [`SetEffectClockString`][itg-effclockstr]) **[src]**. The default noteskin uses it everywhere:

| Element | ITGmania v1.3.0 default noteskin | Where |
|---|---|---|
| Receptor idle | `effectclock "beat"; diffuseramp` from 0.8 grey to white; `effecttiming .2,0,.8,0`; `effectoffset .05` — a flash on every beat | [metrics.ini:70-72][ns-metrics] **[src]** |
| Receptor step (no judgement) | `zoom .85; diffusealpha .9; linear .11; diffusealpha 1; zoom 1` | [metrics.ini:72][ns-metrics] **[src]** |
| Receptor overlay press / lift | additive, alpha 0.8 → `decelerate 0.2` → 0.4; lift: `decelerate .2` to alpha 0 and zoom 1.2 | [metrics.ini:74-77][ns-metrics] **[src]** |
| Tap note shimmer | four layers with `effectclock "beat"; diffuseramp` alpha 0 → 0.35, offsets 0, 0.25, 0.5, 0.75 | [Down Tap Note.lua:11-29][ns-tap] **[src]** |
| Hit explosion (W1…W5) | tint per grade, `sleep .1; decelerate .5; diffusealpha 0` | [metrics.ini:39-63][ns-metrics] **[src]** |
| Hold OK flash | `zoom 1; linear .2; zoom .9; linear .1; diffusealpha 0` | [metrics.ini:50, 63][ns-metrics] **[src]** |
| Held hold glow | `glowshift` with period 0.1 | [metrics.ini:66][ns-metrics] **[src]** |
| Mine hit | additive, rotate 0 → 90 → 180 over 0.6 s while fading | [metrics.ini:42, 55][ns-metrics] **[src]** |

Simply Love's gameplay text uses the same tools:

| Element | Simply Love `dd06138b` | Where |
|---|---|---|
| Judgement | `zoom(0.8):decelerate(0.1):zoom(0.75):sleep(0.6):accelerate(0.2):zoom(0)`; optional tilt `min(|offset|, 0.050) × 300 × multiplier` degrees, sign from early/late | [Player judgment.lua:114-123][sl-judg] **[src]** |
| Combo | `diffuseshift` with `effectperiod 0.8` coloured by the full-combo grade; separate 100 and 1000 milestone actors | [Player combo.lua:38-60][sl-combo] **[src]** |
| Wheel | 17 items, `SwitchSeconds=0.05` | [metrics.ini:1529-1543][sl-wheel] **[src]** |

**For ddr-wii:**

- A `BeatClock` derived from `SongClock` and the chart's `TimingData` (what the note field uses) drives receptor beat flashes, note shimmer, background pulses and combo colour shifts. It stops during `#STOPS` and follows BPM changes, as ITG's beat clock does **[mem: `g_fCurrentBGMBeat` follows the song position's beat]**.
- **In song select**, the preview plays from `#SAMPLESTART` (doc 06 §7), so the same clock, started at the preview's start beat, lets the wheel highlight and background pulse with the preview **[exp]**.
- **Audio analysis** (loudness or spectrum from the decoded PCM ring in `WiiAudio`) is possible but optional: the chart's timing is exact and costs nothing, while an FFT on the Broadway CPU competes with decoding **[exp]**.

---

## 11. Scrolling gameplay elements

- **Position from the beat.** `y = receptorY + (noteBeat − displayedBeat) × 64 px × speed` (doc 06 §3.2, [Gameplay.cpp:319-321](../game/source/screens/Gameplay.cpp#L319-L321)) **[src]**. Because it is a function of song time, there is no per-frame accumulation and no drift at 50 Hz.
- **ITGmania's general form** ([`ArrowEffects::GetYOffset`][itg-yoff]) blends beat spacing with time spacing (C-mods: `secondsUntilStep × scrollBPM / 60`), applies `#SCROLLS`/`#SPEEDS` through the displayed beat and speed percent, and supports M-mods (`maxScrollBPM / readBPM`) **[src]**. Add C-mod and M-mod from it when players ask; the x-mod path already exists.
- **Holds.** ITGmania draws hold bodies as strips of short segments (a 512-vertex `StripBuffer`, [NoteDisplay.cpp:756-770][itg-strip]) so mods can bend them and the texture tiles along the length ([`DrawHoldPart`][itg-holdpart]) **[src]**. ddr-wii has no bending mods, so one quad per body with a repeating V coordinate (a power-of-two body texture with `GX_REPEAT`) gives correct tiling at any length for one draw.
- **Culling.** Draw only the visible beat range: ddr-wii already walks a `first_visible` index ([Gameplay.cpp:357-367](../game/source/screens/Gameplay.cpp#L357-L367)) **[src]**; ITGmania finds the first and last displayed beat each frame ([NoteField.cpp:858-866][itg-nf]) **[src]**.
- **Visual smoothness.** Motion is quantised to the frame, so smooth scrolling needs a steady 60 (or 50) frames per second, not just a high average. The serial GRRLIB present path (graphics doc §2) gives steady pacing as long as a frame never overruns; keep worst-case frames well under budget **[exp: measure max frame time, not only average]**.

---

## 12. Reuse or implement

| Need | Reuse / adapt from | Write ourselves |
|---|---|---|
| Easing curves | **Port** ITGmania's five curves ([Tween.cpp][itg-tween]) | Penner set, smoothstep, back-out |
| Tween + chains | ITGmania's tween-queue semantics (idea) | Allocation-free `Tween` and `Track` |
| Keyframes | USB Loader GX Hermite evaluation ([Animator.cpp:197-241][ul-anim]) (formula) | `Track` with linear/Hermite/step keys |
| Chasing a target | WiiFlow's target/tick split ([`_updateTarget`][wf-target]) (structure) | Exact exponential `Smooth`, critically damped `Spring` |
| Periodic effects + beat clock | **Port** ITGmania's effect formulas and clock idea ([Actor.cpp:492-660, 896-950][itg-predraw]) | `Periodic`, `BeatClock` |
| Wheel motion | **Adapt** ITGmania [`WheelBase::Update`/`Move`][itg-wheel] into `core/` | — |
| State blends | libgui `GuiShape` time-based smoothstep ([GuiShape.cpp:90-130][lg-shape]) (idea) | Per-state style blend in the UI layer |
| Flipbooks | Dance Clone `Decal` modes (idea) | `Flipbook` |
| Particles | HBC fixed pool (idea) | `ParticlePool` (SoA, dt-based) |
| Animated backgrounds | Simply Love UV-scroll layers, HBC gradients (ideas) | Background layers in `render/` |
| Timing values | Simply Love judgement/combo, default noteskin receptor/explosion (numbers above) | Our own data tables with those starting values |

"Port" follows CLAUDE.md's rule for ITGmania code (original notices kept, changes listed).

---

## 13. Proposed animation core

All in `game/source/core/anim/` (no libogc includes, PC-tested in `game/tests/`), no heap allocation after setup:

```cpp
namespace anim {

using Ease = float (*)(float);              // t in [0,1] -> [0,1] (may overshoot for back/spring)
float linear(float), accelerate(float), decelerate(float), spring(float),
      smoothstep(float), backOut(float) /* , ... Penner set */;

struct Tween {                               // one property, one segment
    float from = 0, to = 1;
    double start = 0, duration = 0;          // seconds (or beats, with a beat clock)
    Ease ease = linear;
    float at(double t) const;                // clamps before start / after end
    bool done(double t) const { return t >= start + duration; }
};

struct Key { float time, value, slope; enum Interp : uint8_t { Step, Linear, Hermite } interp; };
template <int N> struct Track {              // authored multi-step motion, fixed capacity
    Key keys[N]; int count = 0;
    float at(float t) const;                 // Hermite as in USB Loader GX's Animator
};

struct Smooth {                              // frame-rate-independent "move toward target"
    float value = 0;
    void step(float target, float dt, float tau) { value += (target - value) * (1 - std::exp(-dt / tau)); }
};
struct Spring { float x = 0, v = 0; void step(float target, float dt, float omega); };   // critically damped

struct Periodic {                            // ITGmania effect timing, phase p in [0,1]
    float rampToHalf, holdAtHalf, rampToFull, holdAtFull, holdAtZero, offset;
    float phase(double clock) const;
};
float wag(float p), bounce(float p), bob(float p), pulse(float p, float lo, float hi);

struct Flipbook { int first, count; float fps; enum Mode : uint8_t { Once, Loop, PingPong } mode; double start;
                  int frameAt(double t) const; };

struct ParticlePool {                        // structure of arrays, fixed capacity
    static constexpr int kMax = 512;
    float x[kMax], y[kMax], vx[kMax], vy[kMax], life[kMax], size[kMax], spin[kMax];
    uint32_t colour[kMax]; int alive = 0;
    void spawn(/* emitter params */);
    void update(float dt);                   // integrate, fade by life, swap-remove the dead
};

}  // namespace anim
```

**Tests worth having** (cheap on the PC): curve endpoints and monotonicity; `Smooth` and `Spring` give the same value at t = 1 s whether stepped at 50 Hz, 60 Hz or with one long step; `Track` hits its keys exactly; `Periodic` matches ITGmania's phase table for a few timings; the particle pool never exceeds capacity.

Rendering of these values (which quad, which texture, which blend) stays in `render/` and the screens; `core/anim` only computes numbers.

---

## References

[lg-fx]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiElement.cpp#L426-L532
[lg-shape]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/libgui/GuiShape.cpp#L90-L130
[lg-repeat]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/drivers/InputController.h#L81-L82
[lg-prompt]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/menu.cpp#L99-L190
[lg-updategui]: https://github.com/dborth/libgui/blob/a22287e901086cbf28130f1d3a8485939613ed88/source/menu.cpp#L43-L97

[wf-speeds]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L218-L241
[wf-covertick]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L2558-L2587
[wf-target]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L1686-L1855
[wf-osc]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/coverflow.cpp#L1135-L1160
[wf-elemtick]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/gui.cpp#L677-L684
[wf-hide]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/gui.cpp#L374-L420
[wf-hideanim]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/menu/menu.cpp#L1661-L1668
[wf-fanart]: https://github.com/Fledge68/WiiFlow_Lite/blob/69c0d4d61984f4ed6fca51b4520320cadfac960d/source/gui/fanart.cpp#L226-L296

[ul-fx]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/GUI/gui_element.cpp#L536-L630
[ul-pulse]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/GUI/gui_element.cpp#L610-L630
[ul-anim]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/banner/Animator.cpp#L120-L241
[ul-step]: https://github.com/wiidev/usbloadergx/blob/e25c4f3501ed957b7db73f79c51fdf00715ab2e2/source/banner/Animator.cpp#L141-L196

[hbc-bubbles]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/bubbles.c#L36-L224
[hbc-bubcount]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/bubbles.c#L59-L97
[hbc-dfade]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/dialogs.c#L138-L170
[hbc-grad]: https://github.com/fail0verflow/hbc/blob/13305cfbe5db47eebc5e62aeed14f7adfbb83a5a/channel/channelapp/source/gfx.c#L316-L352

[gr-particle]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/examples/particle/source/main.cpp#L35-L240
[gr-tileset]: https://github.com/GRRLIB/GRRLIB/blob/a426e6170cbd9a4a69851a705637d753a09f8294/GRRLIB/GRRLIB/GRRLIB_bmf.c#L107-L123
[ws-frames]: https://github.com/nebiun/libwiisprite/blob/dd4173c956d0b4fc33731a35c1674457d9aac607/source/sprite.cpp#L319-L378
[ws-calcframe]: https://github.com/nebiun/libwiisprite/blob/dd4173c956d0b4fc33731a35c1674457d9aac607/source/sprite.cpp#L450-L461
[wl-timer]: https://github.com/HTV04/wiilove/blob/6b1d7e73959ef11fa8fc0df2e878ff8576acb0f3/src/wiilove/modules/timer.cpp
[fw-test]: https://github.com/NicktasticDEV/Funkin-Wii/blob/60bd9144466ca3a2447ba9117da7717b73e49cb6/funkin/source/states/teststate.cpp
[s9-video]: https://github.com/dborth/snes9xgx/blob/3c1aa18241e1fa1fab1bec1640abcf80c096cb23/source/drivers/ogc/OgcEmulatorVideo.cpp#L763-L929
[w64-bg]: https://github.com/emukidid/Wii64/blob/a3fb91bd64d2a5869f00b8349e747b3d568aa732/libgui/Gui.cpp#L159-L175

[itg-tween]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Tween.cpp
[itg-begin]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L997-L1036
[itg-tween-update]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L803-L858
[itg-wavg]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L1465
[itg-predraw]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L492-L660
[itg-effects]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L607-L640
[itg-effclock]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L896-L960
[itg-effclockstr]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L1094-L1120
[itg-efftiming]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Actor.cpp#L1141-L1155
[itg-swme]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/ScreenWithMenuElements.cpp#L108-L137
[itg-trans]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/Transition.cpp#L20-L45
[itg-sm-delay]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/ScreenManager.cpp#L718-L779
[itg-wheel]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/WheelBase.cpp#L149-L378
[itg-yoff]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/ArrowEffects.cpp#L540-L727
[itg-strip]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/NoteDisplay.cpp#L756-L770
[itg-holdpart]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/NoteDisplay.cpp#L778-L1097
[itg-nf]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/NoteField.cpp#L836-L900
[ns-metrics]: https://github.com/itgmania/itgmania/blob/v1.3.0/NoteSkins/dance/default/metrics.ini#L39-L77
[ns-tap]: https://github.com/itgmania/itgmania/blob/v1.3.0/NoteSkins/dance/default/Down%20Tap%20Note.lua#L11-L29

[sl-judg]: https://github.com/Simply-Love/Simply-Love-SM5/blob/dd06138b15492f4136796dfe4b6708ced0f7b9eb/Graphics/Player%20judgment.lua#L104-L123
[sl-combo]: https://github.com/Simply-Love/Simply-Love-SM5/blob/dd06138b15492f4136796dfe4b6708ced0f7b9eb/Graphics/Player%20combo.lua#L38-L60
[sl-wheel]: https://github.com/Simply-Love/Simply-Love-SM5/blob/dd06138b15492f4136796dfe4b6708ced0f7b9eb/metrics.ini#L1529-L1548
[sl-bg]: https://github.com/Simply-Love/Simply-Love-SM5/blob/dd06138b15492f4136796dfe4b6708ced0f7b9eb/BGAnimations/_shared%20background/Normal.lua#L1-L55
