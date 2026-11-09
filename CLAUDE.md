# Dance Dance Wiivolution (ddr-wii)

A four-panel rhythm game (StepMania-style) for Wii homebrew, played on a GameCube dance mat. Players see the name Dance Dance Wiivolution (title screen, exit prompt, the Homebrew Channel's `meta.xml`); the repository, code, file headers and SD folders keep `ddr-wii` / `sd:/ddrwii`, so existing cards keep working. Small personal project, GPL-3.0-or-later. C++17 on devkitPPC + libogc + GRRLIB, tested in Dolphin and then on a real Wii by the owner.

## Commands

The Wii build needs devkitPro's own MSYS2 bash, so from Git Bash wrap `make` like this:

```bash
/c/devkitPro/msys2/usr/bin/bash.exe -lc "cd '$PWD' && make -C game"              # build game/game.dol
/c/devkitPro/msys2/usr/bin/bash.exe -lc "cd '$PWD' && make -C game run-dolphin"  # build and boot in Dolphin
/c/devkitPro/msys2/usr/bin/bash.exe -lc "cd '$PWD' && make -C tools/padtest"     # pad diagnostic (doc 04)
/c/devkitPro/msys2/usr/bin/bash.exe -lc "cd '$PWD' && WIILOAD=tcp:<wii-ip> make -C game run"  # send to a Wii in the Homebrew Channel
game/tests/run-tests.sh                                                           # PC tests for core/, about 10 s
```

Run the PC tests before committing changes to `game/source/core/`, and build the game before committing changes anywhere in `game/source/`. The test corpus and how to regenerate the expected output are in `game/tests/README.md`.

## Code rules

- `game/source/core/` is portable C++17 with no libogc or GRRLIB includes. Put anything that doesn't need the Wii there (parsing, timing, judging, scoring, config files), so it can be tested on the PC.
- `platform/wii/` holds thin libogc wrappers, `render/` the GRRLIB drawing, `screens/` the scenes.
- Every new source file starts with `// SPDX-License-Identifier: GPL-3.0-or-later`.
- Code ported from ITGmania v1.3.0 keeps its original notices and lists what changed (see `core/timing/TimingData.cpp`).
- Never copy code from StepMania-Wii's Wii files (no licence, possibly GPLv2-only); re-implement the idea instead. Dance Clone's code may be used, but never ship its art, sounds or fonts. Details in doc 07 §8.
- `reference/` is read-only and stays out of git.
- Shell scripts and Makefiles keep LF line endings.

## Assets

- Until real art exists, placeholders are generated in code (`render/ArrowArt`, `render/Font`).
- Small files the game embeds (images, fonts, sound effects) go in `game/data/`. The Makefile only has a build rule for `.jpg`, so add a matching rule for each new file type (for example `%.png.o %_png.h : %.png`).
- Every asset that isn't generated in code gets a line in `game/data/CREDITS.md`: where it came from and its licence. Only add files whose licence allows sharing them with the game.
- Songs stay on the SD card and never go in git. Make test audio (click tracks) with a script.

## Key decisions

- A new lightweight engine instead of porting StepMania (doc 02).
- GPLv3, so chart and timing code can be ported from ITGmania v1.3.0 (doc 02).
- Audio plays through ASND, and the audio sample count is the master song clock (doc 06 §3–4).
- SongClock follows ASND's mixing tick (`ASND_SetCallback` + the voice tick counter), not buffer-finished reports, which are late by a rate-dependent amount (doc 06 §4).
- MP3s decode like ITGmania v1.3.0: no gapless trim, a `Xing` first frame skipped but an `Info` one played (doc 06 §4).
- The dance mat is read as a standard GameCube controller, one mat per port (doc 04).
- The game reads pads only through `wii::Input` (SI polling interrupt + `PAD_Read`, timestamped and debounced), never `PAD_ScanPads`/`PAD_ButtonsHeld`: SI gives each new sample to one reader. libogc 3.2.1's `PAD_SetSamplingCallback` is a no-op (doc 04 §5).
- Menus follow ITGmania's Simply Love theme (GPLv3) for layout, colours and controls, redrawn with our own placeholder graphics; no theme files are copied (doc 06 §7).
- Menu grammar: arrows move, A selects, B goes back one level, START pauses a song and commits irreversible things (saving a name, exiting), Z opens high scores. Everything works with the mat's buttons; L/R/X/Y are controller-only shortcuts (doc 06 §7, target menu flow).
- The title is the boot scene; Options and the exit prompt open from it, the test scenes and the calibration from Options (calibration also with R in song select). START+Z held 2 s on any pad exits from anywhere (doc 06 §7).
- Each screen and popup has one help strip (`screens/MenuUi`, x 48, y 400/416, the prototype's wording); song select alone shows one line at y 436 because its panes fill y 344–428 (doc 06 §7).
- Menu sounds are made in code (`core/audio/MenuTones`) and play on ASND voice 1, only when a press did something; voice 0 stays the song/preview stream and its clock. No sounds during a song except START pausing, none on calibration or Test input (doc 06 §7).
- Options shows only settings that do something now: no Visual offset, and no TV safe area until the layouts can scale (doc 06 §7, §11.3).
- Pausing uses `ASND_PauseVoice` (a paused voice's tick counter stops); SongClock freezes and re-anchors on the first tick after the resume. Anyone's START continues; only the player who paused can Restart or Quit (doc 06 §4, §7).
- Settings live in `sd:/ddrwii/config.ini` (offsets in ms), written whole to a temp file and renamed (doc 06 §7). The global offset is set on the calibration screen (doc 06 §3.5).
- The number of players (1 or 2) is chosen on the Players screen before song select; nobody joins anywhere else, and the lower port is P1 on the left (doc 06 §7, §11.2).
- No play modes: each player sets their life bar per song in the song settings popup, Off / On / On, no fail (ITG's life bar and failing, ported from ITGmania's `LifeMeterBar`; no fail is ITG's FailType Off, so the bar keeps moving after it empties; doc 06 §3.4, §7).
- Scoring is ITG's: timing windows, dance-point weights and the 17 grade tiers from Simply Love's ITG mode (windows plus ITGmania's 1.5 ms `TimingWindowAdd`). Machine scores live in `sd:/ddrwii/scores.txt`: a top 10 per chart, one list whatever the speed or life bar setting, each entry with a three-letter name, speed and life bar setting; a new score wins a tie (doc 06 §7).
- Gameplay puts each run in the top 10 under the port's last name, and scores.txt is written as soon as Gameplay returns; Results only renames the entry, so a name typed but not saved counts. The last name is kept per port in `config.ini` (`[PortN] Name`), not in scores.txt (doc 06 §7).
- Song select loads banners and plays previews on a worker thread (`screens/SongMedia`). Banners are decoded with libpng/libjpeg, not GRRLIB's loaders. The scan is cached in `sd:/ddrwii/cache.txt` (doc 06 §7).
- 2 players now, 4 after M6 (doc 06 §11). Playfields stay inside the TV's safe area (doc 06 §11.3).
- The Reset and Power buttons set a flag that ends the current scene like the exit chord; the pending saves run on the way out, then Reset returns to the loader and Power switches off. The SD mount is retried for 2 s; without a card the title says so once and nothing is saved (doc 06 §8).

## Docs

- The docs table in `README.md` is the map. Current truth is in doc 05 (roadmap and status) and doc 06 (design). Docs 01, 07 and 07a are background research from the start of the project.
- Status lives only in doc 05. Update it there when a milestone moves, not in the README.
- Record real-hardware results in doc 05 under their milestone, with the date, the commit tested and the setup (TV, video mode).
- When the design changes, update the matching section of doc 06. Add new decisions to the list above.
- Claim markers: [src] read in source code, [doc] official docs, [mem] from memory (verify it), [exp] still to be tested.

## Working with the owner

Claude can build, run the PC tests and use Dolphin. Only the owner can use the real Wii, the dance mat and the TV. When something needs hardware, give them a short checklist: what to run, what to look at, and what to report back.

Work on a branch and merge into `main` through a pull request.
