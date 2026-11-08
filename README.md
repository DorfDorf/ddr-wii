# ddr-wii

A four-panel rhythm game (StepMania-style) for Nintendo Wii homebrew, played with a GameCube-port DDR dance pad. Developed and tested in Dolphin first, then on real hardware via the Homebrew Channel.

**Status:** M1 (toolchain + pad test) and M2 (graphics) are done. M2b (2- and 4-player playfield stress test) is built and passes in Dolphin, and is waiting for its real-Wii run. `game/` boots into the M2b test, which cycles through 1–4 worst-case playfields and shows frame time, CPU time, GPU wait, draw calls, texture switches and drawn area for each. B switches to the M2 render test (receptors, a time-driven arrow stream, a 1600-sprite stress ramp). Next: M2b on the Wii (`make -C game run` with `WIILOAD` set), M3 (real dance mat on a real Wii) and M4a (chart parser on PC). See [doc 03](docs/03-development-environment.md) for the installed setup.

## Build

From the devkitPro MSys2 shell (one-time setup: `tools/setup-devkitpro.sh`):

```bash
make -C game                       # -> game/game.dol
make -C game run-dolphin           # build and boot in Dolphin
make -C tools/padtest run-dolphin  # pad diagnostic (doc 04)
```

Dolphin settings can be overridden per run, e.g. to test other TV modes:

```bash
make -C game run-dolphin DOLPHIN_ARGS="-C Dolphin.Core.FallbackRegion=1"                              # NTSC
make -C game run-dolphin DOLPHIN_ARGS="-C Dolphin.Core.FallbackRegion=2 -C SYSCONF.IPL.E60=False"     # PAL50
```

Or use the VS Code tasks: `Ctrl+Shift+B` builds the game, and *Run Test Task* boots it in Dolphin.

## Plan in one paragraph

A new lightweight Wii-native engine built with devkitPPC + libogc + GRRLIB. Audio streams through ASND, and the audio sample clock is the master song clock. The `.sm`/`.ssc` parser and timing engine are ported from ITGmania v1.3.0 (GPLv3, like this project), and the dance mat is read through libogc's `PAD_*` API as a standard GameCube controller. Local multiplayer uses one mat per GameCube port: 2 players is a requirement, and 4 is a stretch goal pending a performance and readability test ([doc 06 §11](docs/06-technical-design.md#11-local-multiplayer-24-players)).

## Docs

| Doc | Contents |
|---|---|
| [01 — Project research](docs/01-project-research.md) | Ecosystem, existing engines, Wii homebrew stack, hardware limits |
| [02 — Architecture options](docs/02-architecture-options.md) | Port vs. adapt vs. new engine, and the recommendation |
| [03 — Development environment](docs/03-development-environment.md) | Windows setup: devkitPro, GRRLIB, Dolphin, VS Code, real Wii |
| [04 — Controller integration](docs/04-wii-controller-integration.md) | GameCube PAD API, dance-mat facts, pad test program |
| [05 — Roadmap](docs/05-development-roadmap.md) | Milestones and acceptance criteria |
| [06 — Technical design](docs/06-technical-design.md) | Timing engine, audio, rendering, charts, repo layout |
| [07 — Existing codebase review](docs/07-existing-codebase-review.md) | StepMania-Wii and Dance Clone Wii review |
| [Sources](docs/sources.md) | Links, dates checked, maintenance status |

## Chart parser and timing tests (PC, no Wii needed)

`game/source/core/` holds the `.sm`/`.ssc` parser and the beat↔seconds timing engine (ported from ITGmania v1.3.0, GPLv3). They're portable C++17 with no libogc. One command builds and tests them on the PC against the Python `simfile` library:

```sh
game/tests/run-tests.sh
```

On Windows this needs `g++` in devkitPro's MSYS2 (one-time: `pacman -S gcc` in the devkitPro MSYS2 shell). The script works from Git Bash. See [game/tests/README.md](game/tests/README.md) for the corpus, the oracle (`tools/simfile-oracle.py`) and the known differences between `simfile` and ITGmania.

## Reference material

The reviewed projects (StepMania-Wii, Dance Clone) aren't in this repository. They live in a local `reference/` folder because of their licences and the bundled third-party music and data. Their upstream locations are listed in [docs/sources.md](docs/sources.md).
