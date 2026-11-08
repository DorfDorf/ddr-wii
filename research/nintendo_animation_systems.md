# Nintendo animation systems

*Part of the Nintendo research set (repositories, commits and how far each is verified: [nintendo_game_engine_analysis.md](nintendo_game_engine_analysis.md); recommendation: [reusable_implementation_patterns.md](reusable_implementation_patterns.md)). It extends the first set's [animation_system_research.md](animation_system_research.md), which covered homebrew, ITGmania and Simply Love. Markers: **[src]**, **[src, NM]** (non-matching source), **[doc]**, **[mem]**, **[exp]**. All behaviour is described in our own words; formulas are standard maths restated, not code.*

---

## 1. Summary

- **Authored keyframe clips do most of the work.** UI motion is drawn by artists in a layout tool as keyframe curves: cubic Hermite with per-key slopes for position, scale, rotation, colour and texture transforms, and step keys for visibility and texture swaps. A clip is bound to panes **by name** and played by a **playhead** with loop, once, reverse or ping-pong modes **[src]**.
- **Code adds motion through state machines.** Galaxy's *Nerve* pattern runs one state per frame with a step counter, and its helpers turn "steps in this state" into an eased 0–1 value. Tweens are pure functions of time-in-state, with no tween objects. TP uses frame-counting helpers per pane **[src]**.
- **The small touches are explicit:**
  - mirrored-frame continuation when a highlight reverses (Galaxy, RHF);
  - scrubbing an authored clip with a code-driven curve (Galaxy's ring wipe);
  - a "pop with wobble" curve and a damped-vibration curve (Galaxy) **[src]**.
- **Rhythm Heaven Fever's sprites are cell animations:**
  - multi-part sprites built from rectangles in one texture, with step keys that hold for N frames;
  - queued clips, sprites attached to another sprite's part, and playback speed scaled by the current tempo;
  - two-colour recolouring in the shader **[src]**.
- **Every system steps in frames.** PAL handling is patched in: Rhythm Heaven Fever adds 1.2 frames per frame at 50 Hz, Galaxy runs its loop at 59.94 Hz by OS time, and EGG computes a time scale **[src]**. ddr-wii keeps its rule of time-based animation only (doc 06 §2), and reproduces these features in seconds and beats (§11).

---

## 2. The time model: frames everywhere

| Game | How animation time advances [src] | PAL 50 Hz handling |
|---|---|---|
| Rhythm Heaven Fever | Cell animations and layout animations add `speed` frames per video frame ([`CCellAnim::update` L69-134][rhf-cell-update]; [`CLayoutAnimation` update L79-105][rhf-layanim-cpp]) | Adds `1.2 × speed` per frame when not NTSC (L93-98 and L86-91 respectively) |
| Super Mario Galaxy | Nerve steps count frames; layout clips advance per frame | The main loop is paced by an OS timer at 59.94 Hz instead of the retrace, so logic runs at NTSC speed on a 50 Hz display ([`MainLoopFramework.cpp` L305-308, L398-401][smg-ticks]; [`GameSystemFrameControl.cpp` L8-17][smg-framectrl]) |
| Wii Sports (EGG) | Faders and scenes count frames ([`eggColorFader.cpp` L72-105][egg-fader]) | `AsyncDisplay::calcS` computes a time scale when emulating NTSC timing on PAL ([L66-87][egg-calcs]) |
| Twilight Princess | `CPaneMgrAlpha::alphaAnime` and `CPaneMgr::scaleAnime` count frames ([`d_pane_class_alpha.cpp` L71-120][tp-ratecalc]) | — |
| NSMBW | `FrameCtrl_c::play` adds `rate` per frame ([`m_2d.cpp` L147-169][nsmbw-framectrl]) | — |
| Mario Strikers Charged | Front-end timelines place items by **start time and duration in seconds**, and transitions take `Update(float)` ([`tlInstance.h` L7-74][msc-tl]; [`ScreenTransitionManager.h` L11-35][msc-transition]) | Not needed for seconds-based items |

**Lesson.** The patches show the cost of frame stepping: each game needs its own PAL fix. ddr-wii's planned `core/anim` advances by seconds (UI time), song seconds or beats, so 50 Hz and 60 Hz give the same picture with no special case (earlier [animation §2](animation_system_research.md#2-the-time-model)).

---

## 3. Keyframe curves

NW4R's layout animation (Wii Sports; matching on all releases but USA rev 0) is the clearest **[src]**:

| Curve | Key | How a value is found |
|---|---|---|
| **Hermite** (position, rotation, scale, size, colours, texture SRT, indirect texture SRT) | frame, value, slope ([`lyt_animation.cpp` L47-51][lyt-keys]) | Binary search for the surrounding keys. Below the first key or past the last, the end value is held. Otherwise cubic Hermite with the two values and two slopes (slopes are per frame) ([`GetHermiteCurveValue` L102-154][lyt-hermite]) |
| **Step** (visibility, texture pattern index) | frame, integer value ([L39-43][lyt-keys]) | Binary search; the value of the key at or before the frame ([`GetStepCurveValue` L72-100][lyt-step]) |
| **Jump inside a Hermite curve** | Two keys with the same frame | At exactly that frame the *second* key's value is used, giving an instant jump without a separate step track ([L128-134][lyt-hermite]) |

The cubic is the standard Hermite basis: with *t* = (frame − f₀)/(f₁ − f₀) and segment length *d* = f₁ − f₀,
value = v₀·(2t³ − 3t² + 1) + v₁·(−2t³ + 3t²) + s₀·d·(t³ − 2t² + t) + s₁·d·(t³ − t²).

**Channels.** J2D supports seven animation kinds per pane: transform, colour, texture pattern, texture SRT, TEV register colours, visibility and vertex colour ([`J2DPane.h` L124-141][j2d-anims]). NW4R applies pane SRT, visibility and vertex colour to panes, and texture SRT, colours and patterns to materials ([L157-300][lyt-apply]).

**Lesson.** The earlier plan's `anim::Track` (step / linear / Hermite keys) is right. Add:
- **duplicate-time keys as jumps**;
- **hold-before-first and hold-after-last**;
- **a step track for integer channels** (visibility, atlas cell index).

Store slopes **per second**, so curves don't depend on the frame rate.

---

## 4. Clips and binding

- **A clip is data, separate from the layout.** `Layout::CreateAnimTransform` loads one; `BindAnimation` attaches it ([`lyt_layout.cpp` L190-300][lyt-layout]). `AnimTransformBasic::Bind(pane, recursive)` walks the tree and links each pane whose name the clip targets ([L379-460][lyt-bind]). Each pane keeps a list of `AnimationLink`s and applies every enabled one in `AnimateSelf` ([`lyt_pane.cpp` L306-321][lyt-animself]).
- **Groups** are named sets of panes, so one clip can drive a subset (NW4R `Group`; NSMBW's `GroupRegister(names, animeIdxs)`, [`d_lytbase.hpp` L86][nsmbw-lytbase]).
- **Game code plays clips by index or name, by state:**
  - NSMBW: `AnimeStartSetup(idx, startAtEnd)`, `LoopAnimeStartSetup`, `ReverseAnimeStartSetup`, `isAnime(idx)` ([L90-121][nsmbw-lytbase]).
  - Galaxy: `startAnimAtFirstStep(name)` in a nerve.
  - RHF: `playFromBeginForward` / `playFromBeginReverse` ([`LayoutAnimation.hpp` L8-75][rhf-layanim-h]).
- **Mario Strikers Charged's timelines** let code override a placed item's position, colour or UVs while the authored animation keeps playing ([`tlInstance.h` L7-74][msc-tl]). A component switches slides with an option to keep the current time ([`tlComponent.cpp` L12-55][msc-tlcomp]).

**Lesson.** In ddr-wii, a *clip* is a small set of tracks keyed by element and channel ("cursor.alpha", "title.y"), built in code or from a tiny text format. A scene binds clips to its elements once, by handle, and plays them by state (§6). Overrides are additive: code offsets are applied on top of the clip's value, as Mario Strikers Charged does.

---

## 5. Playheads: frame controllers

| Controller | Modes | Details [src] |
|---|---|---|
| J3D `J3DFrameCtrl` ([`J3DAnimation.h` L932-980][j3d-fc-h]; [`update` L136-204][j3d-fc-update]) | **Once** (clamp just before the end), **reset** (back to start when done), **loop** (between a loop point and the end, so an intro can play once), **reverse** (forward, bounce, stop at start), **loop-reverse** (endless ping-pong) | Overshoot is **reflected**, not clamped, at a bounce. A state bitmask reports "ended" and "looped" for the current frame |
| NSMBW `m2d::FrameCtrl_c` ([header L7-34][nsmbw-fc-h]; [`play` L147-200][nsmbw-framectrl]) | loop / no-loop × forward / reverse | Keeps the previous frame too, so code can detect a frame being crossed. `isStop()` reports the end in either direction |
| RHF `CLayoutAnimation` ([L50-105][rhf-layanim-cpp]) | loop / once, forward / reverse | **Reversing mid-play maps the current frame to the mirrored frame** (length − current − 1), so the picture doesn't jump (L54-61). The first update after a start is skipped, so frame 0 is shown (L79-84) |

**Lesson.** ddr-wii's `anim::Playhead` works in seconds:
- `{start, end, loopStart, rate, mode: Once|Reset|Loop|PingPong|PingPongLoop}`;
- events "ended" and "looped" for the current update;
- a remembered previous time, so "crossed time T" events (sounds, particles) fire exactly once;
- reflection at bounces;
- `reverseFromHere()`, which mirrors the time.

---

## 6. State-driven tweens

- **Galaxy's Nerve pattern.** A `NerveExecutor` owns a `Spine` holding the current state (a *nerve*), a pending next state and a step counter ([`NerveExecutor.hpp` L8-21][smg-nerve-h]).
  - `Spine::update` applies any pending change, runs the state, increments the step, then applies changes again ([`Spine.cpp` L7-12][smg-spine]).
  - `setNerve` runs the old state's exit hook and defers the switch. A switch resets the step to 0, so `isFirstStep()` is the entry action ([L14-44][smg-spine]).
  - Every UI controller (buttons, wipes, counters) is written as states like `exeAppear`, `exeWait`, `exeDecided` **[src]**.
- **Tweens from time-in-state.**
  - `MR::calcNerveRate(actor, stepMax)` is "steps in this state ÷ stepMax", clamped to 0–1. There is also a version with a start step ([`LayoutUtil.cpp` L677-690][smg-nerverate]).
  - `calcNerveEaseInValue`, `…EaseOutValue`, `…EaseInOutValue` and `calcNerveValue` map that rate through an easing curve to a value range ([`LiveActorUtil.cpp` L1815-1867][smg-nervevalue]).
  - So "slide in over 12 frames with ease-out" is one line inside `exeAppear`, and nothing has to be updated or cleaned up.
- **TP's per-pane counters** (`alphaAnime(frames, from, to, curve)`) are the older, stateful version: each call advances a member counter ([`d_pane_class_alpha.cpp` L108-120][tp-ratecalc]).
- **Mirrored continuation.** Galaxy's button starts its "pointing" clip at (clip length − the current frame of the "not pointing" clip), so a highlight that reverses keeps its current look ([`calcPointingAnimStartFrame` L228-247][smg-btn-mirror]). RHF does the same by reversing the playhead in place (§5).
- **Scrubbing an authored clip.** Galaxy's ring wipe stops its clip's playback rate and sets the clip frame each step from sin(½·angle), with the angle derived from the nerve step. Artists draw the shape and code decides the timing and easing ([`WipeRing.cpp` L48-66][smg-wipering]).

**Lesson.** This is the most useful animation pattern for ddr-wii's UI:
- A `core/ui` `StateMachine<State>` with `enter`, `exit` and `timeInState()` **in seconds**.
- Element code reads `ease::outCubic(rate(timeInState, 0.15))`. There are no tween lists to manage, no allocations, and it is trivially PC-testable.
- Keep a `Tween` object only for motion that must survive a state change, such as the wheel's position smoothing (earlier plan: `Smooth`, `WheelMotion`).

---

## 7. Easing and reaction curves

From Galaxy's `MathUtil.cpp` ([L141-185][smg-math]) and TP's `rateCalc` ([L71-84][tp-ratecalc]). *t* runs 0 → 1:

| Curve | Value at *t* | Where |
|---|---|---|
| Linear | t | Galaxy `getLinerValue`; TP type 0 |
| Ease in (sine) | 1 − cos(t·π/2) | Galaxy `getEaseInValue` |
| Ease out (sine) | sin(t·π/2) | Galaxy `getEaseOutValue` |
| Ease in-out (sine) | (1 − cos(t·π))/2 | Galaxy `getEaseInOutValue` |
| Quadratic in | t² | TP type 1 |
| Quadratic "(1−t)²" | (1 − t)², which falls from 1 to 0 | TP type 2 |
| **Reaction pop** (`getScaleWithReactionValueZeroToOne`, frequency *f*, amplitude *a*) | First half: ease-out from 0 to 1 at double speed. Second half, with u = 2t − 1: 1 + (1 − cos(π·f·u))·(1 − u)·a, a wobble above 1 that dies out | Galaxy, used for icons and counters that "boing" into place |
| **Converging vibration** (`getConvergeVibrationValue`) | A sine whose phase speeds up (by t⁴) while its amplitude decays with (1 − t), blended toward the end value by 1 − (1 − t)⁴ | Galaxy, for shakes that settle |

The sine set matches ITGmania's and Penner's in spirit; the earlier plan already includes ITGmania's five curves.

**Lesson.** Add **reaction pop** and **converging vibration** to `core/anim`'s easing set. They suit judgement text, combo milestones, the "FULL COMBO" banner and grade reveals, with parameters (frequency, amplitude) instead of hand-made keys.

---

## 8. Cell animation (multi-part sprites)

Rhythm Heaven Fever's `CellAnim` (BRCAD files) is its sprite system ([`CellAnimStr.hpp` L13-89][rhf-cellstr]):

- A **bank** holds one texture (direct or colour-indexed with a palette), a list of **cells** and a list of **animations**.
- A **cell** is a list of **parts** (OBJs). Each part is a rectangle in the texture with its own offset, scale, rotation, flip and opacity, so one cell is a whole posed character built from pieces.
- An **animation** is a list of **keys**. Each key shows one cell for N frames (0 allowed) with its own offset, scale, rotation and opacity: step animation, no interpolation.
- **Playback** ([`CCellAnim::update` L69-134][rhf-cell-update], matching):
  - speed per frame;
  - PAL ×1.2;
  - **tempo sync**: speed × current tempo ÷ the clip's authored tempo (default 120 BPM), applied only when they differ by 2 BPM or more (L76-90);
  - a queue of up to 8 follow-on clips played when the current one ends (L105-112);
  - loop, or destroy or disable at the end.
- **Hierarchy:** a sprite can be attached to a part of another sprite and follow its matrix ([`makeMtx` L136-231][rhf-cell-mtx]).
- **Drawing:** for each part, compose the matrices, set two TEV colours and draw one quad ([`draw` L233-319][rhf-cell-draw]). The shared setup computes `colour0·(1 − texture) + colour1·texture` with alpha from the texture times opacity ([L175-194][rhf-cell-tev] **[src, NM]**). That recolours greyscale art into any two-colour ramp.

**Lesson.** The earlier plan's `anim::Flipbook` covers single-image frame sequences. The cell model adds three cheap, useful things for gameplay art:
- **multi-part frames from one atlas** (a receptor = base + glow + outline parts);
- **tempo-scaled playback**, for idle animations that breathe on the beat (better done in beats directly, §9);
- **two-colour recolouring** (one greyscale arrow, coloured per note quantisation).

---

## 9. Music-synchronised animation

- **Rhythm Heaven Fever.** Characters' cell clips speed up with the song tempo (§8). Animation timers are set in **ticks** and converted to frames, for example "hold this pose 1.5 beats" via ticks → frames ([`AgbTap/MyChecker.cpp` L13-58][rhf-agbtap-checker]; [`fn_801E26B4`/`fn_801E2928` L514-522][rhf-ticks-frames]). Script events (beat animations, cues) are scheduled by the TickFlow interpreter in musical time ([rhythm doc §5](rhythm_game_engine_comparison.md#5-scheduling-events-on-the-beat)).
- **Super Mario Galaxy.** A control sequence running alongside the streamed music sends rhythm events (beats and other types) to up to 32 registered callbacks ([`AudMeTrackCallback.cpp` L6-36][smg-rhythm-cb]). Objects that pulse or flip on the beat listen to these **[src]**. The sequence's tempo is nudged to stay in phase with the stream ([rhythm doc §3](rhythm_game_engine_comparison.md#3-the-song-clock-where-musical-time-comes-from)).
- **ITGmania** (first set): effects can run on a beat clock (`CLOCK_BGM_BEAT`) instead of a time clock ([earlier animation §10](animation_system_research.md#10-music-reactive-effects)).

**Lesson.** ddr-wii already has the right primitive: song beats from `TimingData` and `SongClock`. Beat-driven UI and backgrounds read `beat` and `fract(beat)` directly (ITGmania's model). They don't need Rhythm Heaven Fever's tempo scaling or Galaxy's event callbacks. Use callbacks only for one-shot events ("beat crossed") from the playhead's crossed-time check (§5).

---

## 10. Particles and pane effects

- **JParticle** (`JPAEmitter`, `JPAParticle`, `JPAResourceManager`, field and key blocks, in tp `libs/JSystem/src/JParticle`) and **NW4R `ef`** (`ef_emitter`, `ef_particle`, `ef_particlemanager`, animation curves, in ogws `src/nw4r/ef`) are data-driven emitter systems, built from resource files with curves over each particle's life **[src: files]**.
- **Galaxy's `PaneEffectKeeper`** attaches named particle emitters to layout panes: `add(name, pane, effect)`, delete one, delete all. Sparkles follow a button or a star counter without the effect code knowing about the layout **[src: [`PaneEffectKeeper.hpp`][smg-paneeffect]]**.

**Lesson.** The earlier plan's fixed `ParticlePool` stays. Add an **emitter-attached-to-element** helper (emitter position = element's global position each update), so a judgement burst or menu sparkle follows its UI element. Describe emitters as small parameter structs (rate, life, speed range, colour over life) instead of resource files.

---

## 11. What to build in core/anim

Additions to the earlier sketch ([animation_system_research.md §13](animation_system_research.md#13-proposed-animation-core)), all in seconds or beats, all allocation-free and PC-tested:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later   (sketch only, not code to commit yet)
namespace anim {

// §3: keys may share a time (jump); values hold before the first and after the last key.
struct Key   { float t, v, slopeIn, slopeOut; };             // slopes per second
template <int N> struct Track     { Key k[N]; int n = 0; float at(float t) const; };
template <int N> struct StepTrack { float t[N]; int16_t v[N]; int n = 0; int at(float t) const; };

// §5: a playhead in seconds with J3D-style modes, reflection at bounces, and crossing events.
struct Playhead {
    enum Mode : uint8_t { Once, Reset, Loop, PingPong, PingPongLoop } mode = Once;
    float start = 0, end = 1, loopStart = 0, rate = 1, t = 0, prevT = 0;
    uint8_t events = 0;                                      // Ended | Looped this update
    void advance(float dt);
    bool crossed(float at) const;                            // prevT < at <= t (direction-aware)
    void reverseFromHere();                                  // mirror t so the picture doesn't jump
};

// §7: extra curves beside ITGmania's five and the Penner set.
float reactionPop(float t, float freq, float amp);
float convergeVibration(float t, float dampScale, float rate);

}  // namespace anim

namespace ui {
// §6: Nerve-style state machine with time in state, in seconds.
template <class S> struct StateMachine {
    S state{}, next{}; bool pending = false; float time = 0;
    void set(S s) { next = s; pending = true; }              // applied before the next update
    bool entered() const { return time == 0; }               // first update in the state
    void update(float dt);                                   // apply pending → time = 0; else time += dt
    float rate(float duration) const;                        // clamp(time / duration, 0, 1)
};
}  // namespace ui
```

| Feature | Model in the decompilations | Used for in ddr-wii |
|---|---|---|
| `Track` with jump keys, `StepTrack` | NW4R `lyt` | Authored UI motion, atlas cell swaps |
| `Playhead` modes and crossing events | J3D, NSMBW, RHF | Clips, loops with intros, beat-crossed events |
| `reverseFromHere` | RHF, Galaxy | Focus highlights, wheel rows |
| `StateMachine` with `timeInState` | Galaxy Nerve | Buttons, cursor, scenes, results screen sequence |
| Reaction pop, converging vibration | Galaxy | Judgement, combo milestones, grade reveal |
| Multi-part cells, two-colour recolour | RHF CellAnim | Receptors, notes by quantisation, player colours |
| Emitter attached to element | Galaxy `PaneEffectKeeper` | Bursts that follow UI elements |

Not adopted: per-frame steps, PAL multipliers, tempo-scaled frame rates (beats are used directly instead), and resource-file formats.

[rhf-cell-update]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/CellAnim.cpp#L69-L134
[rhf-cell-mtx]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/CellAnim.cpp#L136-L231
[rhf-cell-draw]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/CellAnim.cpp#L233-L319
[rhf-cell-tev]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/CellAnimManager.cpp#L175-L194
[rhf-cellstr]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/CellAnimStr.hpp#L13-L89
[rhf-layanim-cpp]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/LayoutAnimation.cpp#L50-L105
[rhf-layanim-h]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/LayoutAnimation.hpp#L8-L75
[rhf-agbtap-checker]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/Game/AgbTap/MyChecker.cpp#L13-L58
[rhf-ticks-frames]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlowManager.cpp#L514-L522
[smg-ticks]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/System/MainLoopFramework.cpp#L305-L401
[smg-framectrl]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/System/GameSystemFrameControl.cpp#L8-L17
[smg-nerve-h]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/include/Game/System/NerveExecutor.hpp#L8-L21
[smg-spine]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/LiveActor/Spine.cpp#L7-L44
[smg-nerverate]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/Util/LayoutUtil.cpp#L677-L690
[smg-nervevalue]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/Util/LiveActorUtil.cpp#L1815-L1867
[smg-math]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/Util/MathUtil.cpp#L141-L185
[smg-btn-mirror]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/Screen/ButtonPaneController.cpp#L228-L247
[smg-wipering]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/Screen/WipeRing.cpp#L48-L66
[smg-rhythm-cb]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/RhythmLib/AudMeTrackCallback.cpp#L6-L36
[smg-paneeffect]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/include/Game/Screen/PaneEffectKeeper.hpp#L10-L21
[egg-fader]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/egg/core/eggColorFader.cpp#L72-L105
[egg-calcs]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/egg/core/eggAsyncDisplay.cpp#L66-L87
[lyt-keys]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_animation.cpp#L39-L51
[lyt-step]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_animation.cpp#L72-L100
[lyt-hermite]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_animation.cpp#L102-L154
[lyt-apply]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_animation.cpp#L157-L300
[lyt-bind]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_animation.cpp#L379-L460
[lyt-layout]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_layout.cpp#L190-L300
[lyt-animself]: https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/lyt/lyt_pane.cpp#L306-L321
[j2d-anims]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/include/JSystem/J2DGraph/J2DPane.h#L124-L141
[j3d-fc-h]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/include/JSystem/J3DGraphAnimator/J3DAnimation.h#L932-L980
[j3d-fc-update]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/JSystem/src/J3DGraphAnimator/J3DAnimation.cpp#L136-L204
[tp-ratecalc]: https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/src/d/d_pane_class_alpha.cpp#L71-L120
[nsmbw-framectrl]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/source/dol/mLib/m_2d.cpp#L147-L200
[nsmbw-fc-h]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/include/game/mLib/m_2d/frame_ctrl.hpp#L7-L34
[nsmbw-lytbase]: https://github.com/NSMBW-Community/NSMBW-Decomp/blob/58fe6985e3ca55261beb543fe7558f66c3ddd7c4/include/game/bases/d_lytbase.hpp#L80-L121
[msc-tl]: https://github.com/yannicksuter/mscharged-decomp/blob/b843f01b0ba78077329eaa228c9201b8c8055d9b/include/Game/FE/tlInstance.h#L7-L74
[msc-tlcomp]: https://github.com/yannicksuter/mscharged-decomp/blob/b843f01b0ba78077329eaa228c9201b8c8055d9b/src/Game/FE/tlComponent.cpp#L12-L55
[msc-transition]: https://github.com/yannicksuter/mscharged-decomp/blob/b843f01b0ba78077329eaa228c9201b8c8055d9b/include/Game/Transitions/ScreenTransitionManager.h#L11-L35
