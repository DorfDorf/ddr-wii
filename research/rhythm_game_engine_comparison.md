# Rhythm game engines compared

*Part of the Nintendo research set (repositories and verification status: [nintendo_game_engine_analysis.md](nintendo_game_engine_analysis.md); recommendation: [reusable_implementation_patterns.md](reusable_implementation_patterns.md)). It compares Rhythm Heaven Fever's engine and Super Mario Galaxy's beat-sync library with ITGmania v1.3.0, the two Wii codebases already in the project (StepMania-Wii and Dance Clone, reviewed in [doc 07](../docs/07-existing-codebase-review.md)) and ddr-wii itself ([doc 06](../docs/06-technical-design.md)). Markers: **[src]**, **[src, NM]** (non-matching source), **[doc]**, **[mem]**, **[exp]**, **[measured]**. Nothing is copied; behaviour is described in our own words.*

---

## 1. Summary

- **Rhythm Heaven Fever keeps musical time in *ticks*, 48 per beat.** Each frame it adds exactly as many ticks as the music actually advanced. It reads the sequencer's tick for MIDI-style music, or the stream's play position converted through a tempo map for recorded music. Scripts (TickFlow) wait in ticks and carry the remainder, so cues stay on the beat on average **[src]**.
- **Every expected press is an object, an *input checker*.** It has a target tick and four window edges in **ticks**, so in milliseconds the windows tighten as the tempo rises. A press inside the inner "just" zone scores 100, the outer part 95, a "barely" 80 (recorded as a miss), and no press is a "through". Pressing with no target in range shrinks the windows for the next beat, an anti-mash rule **[src, NM]**.
- **Its input is read once per video frame**, from the newest Wii Remote sample, so press times are only known to the frame (16.7 ms at 60 Hz, 20 ms at 50 Hz). Its generous windows absorb that. ddr-wii's SI-interrupt timestamps are already finer, about 2 samples per frame in Dolphin **[src; measured in Dolphin]**.
- **Super Mario Galaxy runs a separate beat sequence beside the streamed music.** Once per beat it compares the beat with the stream's sample position and nudges the sequence tempo, a soft phase lock **[src]**.
- **ddr-wii's current design is the right one for a StepMania-style game:** a song clock locked to the audio tick counter with CPU-time interpolation, timestamped and debounced pad input, ITG windows in milliseconds and precomputed note times **[doc 06]**.
  - **Worth taking:** judgement *events* with Rhythm Heaven Fever's four outcomes, including an empty press, which drive animations and sounds; a beat-crossing scheduler for cues; the same audio-derived beat for menu music.
  - **Not worth taking:** tick-based windows, frame-quantised input, a scripting VM, the anti-mash rule.

---

## 2. The engines compared

| | Rhythm Heaven Fever | Galaxy `RhythmLib` | ITGmania v1.3.0 | StepMania-Wii | Dance Clone | ddr-wii |
|---|---|---|---|---|---|---|
| **Clock source** | Per frame: sequencer tick, or stream/wave play position through a tempo map; with no music, tempo × 48 ÷ 60 ÷ frame rate ([`_18` L119-197][rhf-tfm-frame]) | Stream sample position checked against a beat counter once per beat ([L39-88][smg-beatproc]) | Sound position with a timestamp each frame ([`GameSoundManager.cpp` L580-625][itg-gsm]) | ASND double buffer; position query broken (always frame 0, counts queued not played) [doc 07] | Wall clock from start, plus a fixed 109 ms fudge [doc 07] | ASND tick counter recorded each audio tick with `gettime()`; linear map to CPU time ([doc 06 §4](../docs/06-technical-design.md#4-audio-system)) |
| **Musical unit** | float ticks, 48 per beat | beats of a control sequence | seconds → beats via `TimingData` | seconds (SM 3.9 `TimingData`) | ms; BPM changes only | seconds (`double`) → beats via ported ITGmania `TimingData` |
| **Smoothing** | Sequences: average of 8 frames plus 10% of the error ([L199-224][rhf-seq]); streams: raw ([L226-314][rhf-strm]) | Squared, capped tempo correction ([L90-110][smg-adjust]) | Sound driver position map (first set) | — | — | Offset moves 1/64 of the way per report, snaps over 50 ms, never backwards ([doc 06 §4](../docs/06-technical-design.md#4-audio-system)) |
| **Events** | TickFlow bytecode: rest N ticks, async calls carrying the remainder ([L111-216][rhf-tf-exec] **[src, NM]**) | Note-on rhythm types in a control track → callbacks ([L54-90][smg-seqparser]) | Notes precomputed, judged at input time | Notes from SM 3.9 | Notes, judged per frame | Notes precomputed as `double` seconds ([doc 06 §3.2](../docs/06-technical-design.md#32-from-audio-time-to-chart-time)) |
| **Input sampling** | `KPADRead` once per frame, newest sample only ([`Controller.cpp` L70-74][rhf-kpad]; [`Controller.hpp` L78-84][rhf-hold]) | — | Timestamped `InputFilter` events | `WPAD`/`PAD` once per frame [doc 07] | `PAD_ScanPads` once per frame [doc 07] | SI polling interrupt → `PAD_Read` → `{gettime(), buttons}` ring; ≈120 samples/s in Dolphin **[measured, doc 05 M3]**; real Wii pending **[exp]** |
| **Windows** | Per cue, in ticks (e.g. just ±6, barely ±10) | — | ms, plus `TimingWindowAdd` | SM 3.9 ms | ms: 25/50/100/150/250 | ITG ms: 23.0 / 44.5 / 103.5 / 136.5 / 181.5 ([doc 06 §3.4](../docs/06-technical-design.md#34-judgement)) |
| **Which note** | Every checker that accepts the button and contains the press is judged; no nearest search ([L224-325][rhf-match] **[src, NM]**) | — | Nearest | Nearest | **Earliest** in range (known bug) [doc 07] | Nearest unjudged in the lane, plus ITG's early-hit rule |
| **Mash handling** | Empty press → strict windows for one beat; 6-frame cool-down on A/B release | — | Early-hit rule | — | — | Early-hit rule; 20 ms debounce |
| **Feedback** | Checker callbacks: hit, barely, through; whiff callback for empty presses | Rhythm callbacks | Judgement messages to theme actors | StepMania actors | Hard-coded | `Gameplay::Player` keeps last hit time and grade per lane |
| **Calibration** | None found in the engine code reviewed | Fixed 500-sample allowance | Global offset, visual delay | — | — | Global offset screen, per-port input offset; visual offset still to do ([doc 06 §3.5](../docs/06-technical-design.md#35-latency-budget-and-calibration)) |
| **PAL** | Ticks per frame use 50 fps; sprites advance ×1.2 | Loop paced at 59.94 Hz by OS time | Time-based | — | Per-frame loop | Time-based; identical at 50 and 60 Hz |

---

## 3. The song clock: where musical time comes from

**Rhythm Heaven Fever** ([`TickFlowManager.cpp`][rhf-tfm], matching). Each frame, `_18`:

1. Works out this frame's ticks from the current music type:
   - **No music**: tempo × speed × 48 ÷ 60 ÷ (60 or 50) ([`calcTicksPerFrame`, hpp L138-141][rhf-tpf]; frame rate from the NTSC flag, [L850-852][rhf-fps]).
   - **Sequence**: the difference in the sequencer's tick since last frame. Because the sequencer advances in audio-frame steps, the per-frame value jitters, so the engine uses the mean of the last 8 frames plus 10% of the accumulated error ([L199-224][rhf-seq]). That is a small phase-locked loop.
   - **Stream or wave**: the difference in play-sample position, converted with the stream's **tempo map**, a list of regions each with a beat count and a sample count. If the interval crosses a region boundary it is split, each part converted at its own tempo. A loop back to the start is detected and unwrapped. A position equal to the length is pulled back by one sample ([L226-314][rhf-strm]).
2. Stores the value in a 32-frame ring. The average of that ring gives the current effective tempo, used to convert ticks ↔ frames for animation timers ([L764-779][rhf-avg]; [`rethinkTempo` hpp L156-163][rhf-tpf]).
3. Runs every script until none spawns another this frame (L167-193).

A speed change multiplies ticks per frame and also changes the music: `SetTempoRatio` for sequences, `SetPitch` for streams ([L530-545][rhf-speed]).

**Super Mario Galaxy** ([`AudBgmTempoAdjuster.cpp`][smg-beatproc], matching):
- A control sequence plays at the nominal tempo, and each beat note calls `beatProc`.
- `beatProc` computes where that beat *should* be in the stream (samples per beat × beat index) and how far the stream's play position is from it.
- It returns a tempo factor: 1 + min(e², 1) when the stream is ahead, 1 ÷ (1 + min(e², 1)) when it is behind. *e* is the error in beats, after a fixed 500-sample allowance ([L90-110][smg-adjust]).
- Intro and loop lengths are tracked for both the beat counter and the stream, so a loop on one side doesn't look like a one-loop error ([L47-83][smg-beatproc]).
- The sequence then runs at tempo × factor until the next beat ([`AudRhythmSeqParser.cpp` L65-74][smg-seqparser]).

**ITGmania**:
- Each frame it asks the music for its position *and the time that position was true*, then derives beat, BPS, freeze and delay state from `TimingData` ([`GameSoundManager.cpp` L580-625][itg-gsm]).
- A separate "visible" song time subtracts `VisualDelaySeconds`, so the note field can lead the audio ([`SongPosition.cpp` L12-50][itg-songpos]).

**ddr-wii**:
- `SongClock` records `{gettime(), ASND tick counter}` on every ASND tick (64000/3 µs of song at any rate). It keeps a linear CPU-time → song-time map that moves its offset 1/64 of the way to each report, snaps on errors over 50 ms, never runs backwards and stops 200 ms after the last report.
- Buffer-finished reports are only a cross-check ([doc 06 §4](../docs/06-technical-design.md#4-audio-system)).
- `SongClock::now()` gives sub-millisecond time between reports.

**Comparison.**
- Rhythm Heaven Fever and ddr-wii both lock musical time to what the audio actually played, which is the essential property; Dance Clone's wall clock lacks it.
- Rhythm Heaven Fever samples that lock once per frame and steps by it. ddr-wii interpolates with the CPU clock, so it can timestamp a press between frames. Rhythm Heaven Fever's design fits its frame-quantised input and tick windows; ddr-wii's fits millisecond windows.
- Galaxy's phase lock solves a problem ddr-wii doesn't have: it can read the stream position directly.

---

## 4. Musical time units and tempo maps

- **Ticks.** 48 per beat (TickFlow's resolution) in Rhythm Heaven Fever; floats, so fractions carry ([TickFlow.hpp L94][rhf-tf-h]).
- **Tempo maps tied to samples.** Each streamed song carries regions of {beats, samples, flags}. A beat is defined by sample positions, so the map is sample-exact and needs no song offset ([L272-300][rhf-strm]).
- **StepMania/ITG charts** are tied to beats: a BPM list, stops, delays and warps, plus a song `#OFFSET` and pack/global offsets. ddr-wii ports ITGmania's `TimingData` with a precomputed segment table ([doc 06 §3.3](../docs/06-technical-design.md#33-timingdata)).

Both representations convert into each other. ddr-wii must keep the StepMania one, because that is what `.sm`/`.ssc` files contain. Rhythm Heaven Fever's choice is a reminder to **test long songs with many BPM changes for drift** against the audio. ddr-wii's PC tests already compare `TimingData` with `garcia/simfile` (doc 06 §3.3).

---

## 5. Scheduling events on the beat

**TickFlow** ([`TickFlow.cpp`][rhf-tf-exec] **[src, NM]**; the manager is matching):
- Each running script ("flow") has a *rest* counter in ticks.
- Every frame the counter drops by this frame's ticks. While it is negative, the flow executes instructions: `rest N` adds N ticks, other instructions act at once (L111-156).
- Because the counter carries its negative remainder into the next rest, the average timing is exact and no event is more than one frame late.
- `async call` starts a child flow whose initial rest is the parent's current remainder plus an offset, so spawned flows stay phase-aligned (L161-166).
- There are calls and returns (a 16-deep stack), jumps, conditions with a condition-variable stack, rests in frames, and *categories*, which kill all flows above a level ([`TickFlow.hpp` L25-111][rhf-tf-h]; manager [L458-470][rhf-categories]).
- A minigame adds its own opcodes. In the `AgbTap` minigame, the script issues the "expect input" opcode at the cue, and that creates a checker due **48 ticks (one beat) later**, with the minigame's windows; other opcodes start character animations ([`AgbTap/MyFlow.cpp` L13-21][rhf-agbtap-flow], matching). The script data itself is not in the source files.

**Galaxy**: rhythm events come from notes in the control sequence and go to callbacks (§3; [`AudMeTrackCallback.cpp` L6-36][smg-callback]).

**ddr-wii**: notes are data with precomputed times, so judging needs no scheduler. Cues and effects do: beat flashes, background events, the "Ready/Go" sequence, lyrics, attacks later. The time-based equivalent of TickFlow's remainder carry is a **crossing test**: an event at beat *b* fires once when `prevBeat < b ≤ beat` (or the time equivalent). It is exact, needs no accumulator, and works with stops and warps, because `beat` comes from `TimingData`. That belongs in `core/` with PC tests.

---

## 6. Input sampling and timestamps

| | How input is read | Resolution of a press time |
|---|---|---|
| Rhythm Heaven Fever | `KPADRead` once per frame, newest sample's buttons only; trig/release from the previous frame's state ([L70-115][rhf-kpad]); GameCube pads via one `PADRead` per frame ([L442-463][rhf-padread]) | One video frame: 16.7 ms / 20 ms |
| | A and B ignore re-presses for 6 frames after a release ([L19-46, L91-99][rhf-cool]); A+B within 4 frames counts as one chord input ([L131-195][rhf-chord]) | — |
| | Presses are queued with their age in ticks and an optional lifetime, then offered to checkers ([`InputCheckManager.cpp` L356-378][rhf-queue] **[src, NM]**). When chords are enabled, a lone A press is held back for a few frames in case B follows, unless a checker expecting A is within ±12 ticks ([L44-60][rhf-chord-delay] **[src, NM]**) | — |
| ITGmania | `InputFilter` events with OS timestamps; debounce per button | ≈1 ms |
| StepMania-Wii, Dance Clone | Once per frame [doc 07] | One frame |
| ddr-wii | SI polling interrupt (`SI_RegisterPollingHandler`) → `PAD_Read` → `{gettime(), buttons}` into a lock-free ring, debounced per panel (20 ms) ([doc 04 §5](../docs/04-wii-controller-integration.md#5-timing-resolution)) | The SI poll interval: in Dolphin 120 samples/s, gaps 1.0–15.6 ms, read ≈0.6 ms after sampling **[measured, doc 05 M3]**; real Wii pending **[exp]** |

**Lesson.** ddr-wii's input path is already finer than Rhythm Heaven Fever's. Next steps from doc 04 still apply:
- measure the real-Wii SI rate;
- raise it with `SI_SetSamplingRate` if it is only twice per frame **[exp]**.

Rhythm Heaven Fever's 6-frame cool-down is a menu-friendly idea (gameplay already has the debounce), and is listed for `MenuInput` in the [GUI doc §5](nintendo_gui_architecture.md#5-menu-input-and-focus).

---

## 7. Judging

**Rhythm Heaven Fever's checker** ([`InputChecker.cpp`][rhf-checker] **[src, NM]**):
- Each frame the checker adds this frame's ticks to its elapsed count. When the count passes target + late "barely" edge without a press, it reports a **through** (L24-43).
- On a press, `lag = (elapsed − target) − age of the press`:
  - inside the "just" window: **100** in the inner part, **95** at the edges. When the just window is wider than 4 ticks, the inner part is the window shrunk by a quarter of its width on each side;
  - inside the "barely" window: **80**, recorded as a miss for the grade;
  - outside: not taken (L121-150).
- **Strict windows** replace the cue's windows with ±1 tick just and ±4 barely around the centre (L182-190). They apply for **48 ticks (one beat) after an empty press** on the A button or the A+B chord, so mashing is punished ([`InputCheckManager.cpp` L244, L410-444][rhf-lockout]).
- A **leniency scale** multiplies all window edges ([L335-349][rhf-leniency]).

Windows are set per cue by the minigame, in ticks ([`AgbTap/MyFlow.cpp` L16-21][rhf-agbtap-flow]; [`Metroman/MyFlow.cpp` L24][rhf-metroman] **[src, NM]**). One tick is 1250 ÷ BPM ms:

| Window (ticks) | 120 BPM | 150 BPM | 180 BPM | Closest ITG window (ddr-wii) |
|---|---|---|---|---|
| AgbTap inner "100" zone ±3 | ±31.3 ms | ±25.0 ms | ±20.8 ms | Excellent ±44.5 / Fantastic ±23.0 |
| AgbTap just ±6 | ±62.5 ms | ±50.0 ms | ±41.7 ms | Excellent ±44.5 |
| AgbTap barely ±10 | ±104.2 ms | ±83.3 ms | ±69.4 ms | Great ±103.5 |
| Metroman just ±4 / barely ±6 | ±41.7 / ±62.5 ms | ±33.3 / ±50.0 ms | ±27.8 / ±41.7 ms | — |
| Strict after an empty press: just ±1 / barely ±4 | ±10.4 / ±41.7 ms | ±8.3 / ±33.3 ms | ±6.9 / ±27.8 ms | — |

**Comparison.**
- Tick windows suit Rhythm Heaven Fever. Its cues are call-and-response at one tempo per minigame, and faster minigames are meant to be harder.
- StepMania/ITG players expect windows that don't change with BPM, and the ITG grading (17 tiers, dance points) depends on them. ddr-wii keeps ITG's millisecond windows.
- Rhythm Heaven Fever's inner/outer split is close to ITG's Fantastic/Excellent pair, so nothing new is needed there.
- The anti-mash rule isn't taken. ITG's early-hit rule and mines already handle mashing, and changing windows would break ITG scoring.

---

## 8. Feedback

**Rhythm Heaven Fever:**
- A minigame's checker class overrides four reactions: **hit**, **barely**, **through** and **release** (for held inputs). The overrides set character animations and sound effects and start pose timers measured in ticks ([`AgbTap/MyChecker.cpp` L13-58][rhf-agbtap-checker]).
- The manager calls a separate **empty press** callback when a press matches nothing, so characters react to every input ([L313-316][rhf-match] **[src, NM]**).
- A checkpoint manager collects Hit / Miss / Through per section for the final grade (`CheckPointManager.cpp`).

**ITGmania** sends judgement messages that theme actors (Simply Love's judgement sprite, combo and receptor glows) react to. Their timings are in the first set ([animation §10](animation_system_research.md#10-music-reactive-effects)).

**ddr-wii** stores the last hit time and grade per lane, and the planned `play/` renderer evaluates effects from stored event times ([recommended plan §4.5](recommended_implementation_plan.md#45-play)).

**Lesson.**
- Make `core/game/Judge` emit a small **event stream** per player into a fixed ring, for renderers and sounds to read. Events: *hit (grade, lane, offset, time)*, *miss*, *hold OK/NG*, *mine hit*, *empty press (lane, time)*. These are Rhythm Heaven Fever's four outcomes plus StepMania's hold and mine cases.
- The renderer reacts to each event: an explosion per grade, a receptor flash on an empty press, a judgement pop.
- It stays allocation-free and can be tested on the PC: "this input sequence produces these events".

---

## 9. Latency, calibration and PAL

- **Rhythm Heaven Fever.** No offset calibration appears in the engine code reviewed (`GameUtil`); the wide tick windows absorb TV and audio latency. On PAL, ticks per frame are computed for 50 fps, so musical time stays correct, while sprite and layout animations play 1.2 frames per frame ([rhythm §3](#3-the-song-clock-where-musical-time-comes-from); [animation doc §2](nintendo_animation_systems.md#2-the-time-model-frames-everywhere)).
- **Galaxy.** A fixed 500-sample allowance sits inside its tempo correction ([L90-110][smg-adjust]). Its loop runs at 59.94 Hz by OS time on PAL.
- **ITGmania.** Global offset, plus a separate visual delay.
- **ddr-wii.**
  - A calibration screen sets `globalOffset` (at least 24 steps, outliers dropped), plus a per-port `InputOffset`.
  - `visualOffset` still needs a picture-only pass ([doc 06 §3.5](../docs/06-technical-design.md#35-latency-budget-and-calibration)).
  - Being time-based, PAL needs nothing special.

---

## 10. What ddr-wii should take

| Take | From | Where in ddr-wii |
|---|---|---|
| Judgement event stream with hit / barely-equivalent grades / miss / empty press / hold / mine | Rhythm Heaven Fever checkers | `core/game/Judge` output → `play/` renderers and sounds |
| Beat-crossing scheduler for cues, firing once when `prevBeat < b ≤ beat` | TickFlow remainder carry, done in the time domain | `core/` (PC-tested), used by gameplay and menus |
| Menu music beat from the preview's own sample position, not a wall clock | Rhythm Heaven Fever derives ticks from whatever music is playing | `SongMedia` preview → beat for song-select pulses |
| Guards on positions: clamp at the stream end, unwrap loops | Rhythm Heaven Fever's stream handling | Preview loops in song select |
| 6-frame-style cool-down after menu actions (in ms) | Rhythm Heaven Fever controller | `MenuInput` |

Keep as is: `SongClock`, SI timestamps and debounce, ITG windows and rules, precomputed note times.

Don't take: tick windows, frame-quantised input, a bytecode script VM (charts are data), tempo-scaled frame animation, the anti-mash lockout.

[rhf-tfm]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlowManager.cpp
[rhf-tfm-frame]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlowManager.cpp#L119-L197
[rhf-seq]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlowManager.cpp#L199-L224
[rhf-strm]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlowManager.cpp#L226-L314
[rhf-avg]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlowManager.cpp#L764-L779
[rhf-fps]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlowManager.cpp#L850-L852
[rhf-speed]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlowManager.cpp#L530-L545
[rhf-categories]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlowManager.cpp#L458-L470
[rhf-tpf]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlowManager.hpp#L138-L163
[rhf-tf-h]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlow.hpp#L25-L111
[rhf-tf-exec]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/TickFlow.cpp#L111-L216
[rhf-kpad]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Controller.cpp#L70-L115
[rhf-hold]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Controller.hpp#L78-L84
[rhf-cool]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Controller.cpp#L19-L99
[rhf-chord]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Controller.cpp#L131-L195
[rhf-padread]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/Controller.cpp#L442-L463
[rhf-queue]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/InputCheckManager.cpp#L356-L378
[rhf-chord-delay]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/InputCheckManager.cpp#L44-L60
[rhf-match]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/InputCheckManager.cpp#L224-L325
[rhf-lockout]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/InputCheckManager.cpp#L244-L444
[rhf-leniency]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/InputCheckManager.cpp#L335-L349
[rhf-checker]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/GameUtil/InputChecker.cpp#L24-L190
[rhf-agbtap-flow]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/Game/AgbTap/MyFlow.cpp#L13-L21
[rhf-agbtap-checker]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/Game/AgbTap/MyChecker.cpp#L13-L58
[rhf-metroman]: https://github.com/conhlee/rhf/blob/02b140c9e7007e2a4ddd61c80ac3f2eb35251ed7/src/Game/Metroman/MyFlow.cpp#L24
[smg-beatproc]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/RhythmLib/AudBgmTempoAdjuster.cpp#L39-L88
[smg-adjust]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/RhythmLib/AudBgmTempoAdjuster.cpp#L90-L110
[smg-seqparser]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/RhythmLib/AudRhythmSeqParser.cpp#L54-L90
[smg-callback]: https://github.com/SMGCommunity/Petari/blob/c8e636b852115204ff4ea128422c4ab017f51838/src/Game/RhythmLib/AudMeTrackCallback.cpp#L6-L36
[itg-gsm]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/GameSoundManager.cpp#L580-L625
[itg-songpos]: https://github.com/itgmania/itgmania/blob/v1.3.0/src/SongPosition.cpp#L12-L50
