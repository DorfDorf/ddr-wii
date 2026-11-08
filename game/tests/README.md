# Host-side tests for `game/source/core/` (M4a)

These tests build `core/` (the `.sm`/`.ssc` parser and the timing engine) with
your PC compiler and check it against an independent implementation, the Python
[`simfile`](https://github.com/garcia/simfile) library (MIT). No Wii, Dolphin
or devkitPro is needed.

## Running

```sh
game/tests/run-tests.sh
```

That builds `build/ddr_tests` and runs all three parts. It exits non-zero on any
failure. A run takes about 10 s including the build.

One-time setup on Windows: the tests use the `g++` in devkitPro's MSYS2. Open
the devkitPro MSYS2 shell and run `pacman -S gcc`. `run-tests.sh` works from
Git Bash: it hands the build to devkitPro's bash, because Git Bash's MSYS
runtime can't run that compiler. On Linux/macOS, any C++17 `g++`/`clang++`
works (`make -C game/tests CXX=clang++`).

MSVC isn't set up or tested here, but the code is plain C++17. From a VS
developer prompt in `game/tests`, this should work:
`cl /std:c++17 /EHsc /O2 /Fe:ddr_tests.exe test_main.cpp ..\source\core\chart\*.cpp ..\source\core\timing\*.cpp`
then `ddr_tests.exe .`

## What is tested

1. **Oracle.** Every corpus file is loaded with `LoadSongFile()`. Each note's
   row, column, type and time (hold/roll tails included) must match
   `expected/*.tsv` within **0.1 ms**. The dumps come from
   `tools/simfile-oracle.py`. The current worst error is about 6e-11 ms,
   because both sides compute in double.
2. **Unit tests.** Hand-computed cases: MsdFile escapes, comments and the
   missing-`;` recovery; stops vs delays; warps; scrolls; `AddSegment`
   merging; `.sm` negative BPMs and stops turned into warps
   (`ProcessBPMsAndStops`, which `simfile` doesn't implement); hold
   matching; split timing; old-`.ssc` absolute warps; `pack.ini` and
   `SyncOffset`.
3. **Fuzz.** Every corpus file is truncated at about 200 lengths, and 3000
   random mutations are applied (bytes from `#:;,=\/-.0-9eErR`, newlines, note
   characters, random bytes; inserts and deletes). The results go through both
   loaders and the timing lookups, together with hand-picked nasty inputs
   (empty, lone `#`/`\`, `#SPEEDS:=;`, 1e40 BPMs, NaN, 200k-column rows, 20k
   measures). The check is that nothing crashes or hangs. Use
   `--fuzz-iterations N` for longer runs. A run with 200k mutations under
   `-D_GLIBCXX_ASSERTIONS -fsanitize=undefined -fsanitize-undefined-trap-on-error -ftrapv`
   passed.

## Corpus

| File | Covers |
|---|---|
| `simfile-testdata/` | `garcia/simfile` testdata (L9, Springtime, nekonabe, blank), MIT, see `LICENSE.md` there. Springtime has split timing, a delay and orphan hold tails |
| `synthetic/01` | constant BPM, two charts |
| `synthetic/02` | 61 BPM changes at off-grid decimal beats |
| `synthetic/03` | stops (incl. beat 0, on notes, `#FREEZES` alias) |
| `synthetic/04` | BPM change and stop on the same beat |
| `synthetic/05` | `.ssc` delays, delay + stop on one beat |
| `synthetic/06` | warps: plain, overlapping, with a stop inside, notes inside |
| `synthetic/07` | split timing: per-chart BPMs/stops/delays/warps/offset |
| `synthetic/08` | `#VERSION:0.53`: chart timing tags ignored |
| `synthetic/09` | CRLF, tabs, comments, odd spacing, lower-case tags |
| `synthetic/10` | missing semicolons |
| `synthetic/11` | quantisations 1–192 rows/measure incl. 3, 5, 7, 9, 20, 40 (rounded rows) |
| `synthetic/12` | holds, rolls, mines across BPM changes and stops |
| `synthetic/13` | lifts, fakes, `#FAKES` |
| `synthetic/14` | 1.5–1000 BPM; a 9.5-minute chart (precision) |
| `synthetic/15` | dance-double and dance-solo |
| `synthetic/16` | escaped tags, 6-decimal values |
| `synthetic/17` | stop and delay at beat 0 |
| `synthetic/18` | split timing with warps, scrolls and speeds |
| `synthetic/19` | `.sm` `#DELAYS`, Edit chart |
| `synthetic/20` | the M4c test song from `tools/make-test-song.py` (not `gen_corpus.py`) |

The synthetic files come from `corpus/gen_corpus.py` (seeded and committed).
After changing the corpus, regenerate the oracle dumps and `expected/index.txt`:

```sh
py -m pip install simfile "setuptools<81"   # simfile 2.1 still imports pkg_resources
py tools/simfile-oracle.py --regen
```

## Where `simfile` and ITGmania differ

We follow ITGmania v1.3.0, the code we ported. The oracle is used for note *times*, so
the corpus avoids these cases, or the oracle mirrors ITGmania's rule:

| Case | ITGmania v1.3.0 / ddr-wii | `simfile` | Handling |
|---|---|---|---|
| Unmatched hold head / tail | head dropped / tail ignored | kept | the oracle applies ITGmania's rule |
| Full-line comment or blank line inside a measure | skipped | counted as a row | avoided in corpus |
| `,,` | one measure break | an empty measure | avoided; unit-tested |
| Negative BPM/stop in `.sm` | becomes a warp | negative time | unit-tested by hand |
| Stop before beat 0 in `.sm` | moves the offset | ignored | unit-tested by hand |
| Duplicate stop on one beat | last one wins | pauses add up | avoided |
| Split-timing chart without its own `#OFFSET` | song's offset | 0 | avoided |
| Split-timing chart with timing tags but no `#BPMS` | 60 BPM | error | unit-tested by hand |
| No BPM at beat 0 | first BPM used from 0 | error | unit-tested by hand |
| Off-grid note rows (e.g. 7 rows/measure) | rounded to 1/48 beat | exact | the oracle rounds too |
