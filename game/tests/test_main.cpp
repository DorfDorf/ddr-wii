// SPDX-License-Identifier: GPL-3.0-or-later
//
// ddr-wii -- host-side tests for game/source/core (M4a).
// Copyright (C) 2026 ddr-wii contributors
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. See the LICENSE file in the repository root.
//
// Three parts:
//  1. oracle: every note of every corpus chart must match the time dumped by
//     tools/simfile-oracle.py (game/tests/expected/) within 0.1 ms;
//  2. unit:   hand-computed cases for MsdFile, TimingData, the .sm warp
//     conversion, note parsing, pack.ini;
//  3. fuzz:   truncated and mutated corpus files must load without crashing.
//
// Usage: ddr_tests [TESTS_DIR] [--fuzz-iterations N]
// TESTS_DIR defaults to the directory holding corpus/ and expected/ (".").

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include "../source/core/chart/Chart.h"
#include "../source/core/chart/MsdFile.h"
#include "../source/core/chart/NoteData.h"
#include "../source/core/chart/PackIni.h"
#include "../source/core/chart/SmLoader.h"
#include "../source/core/chart/SscLoader.h"
#include "../source/core/chart/StringUtil.h"
#include "../source/core/timing/TimingData.h"
#include "../source/core/input/InputFilter.h"
#include "../source/core/audio/WavFile.h"
#include "../source/core/timing/SongClock.h"
#include "../source/core/game/Judge.h"
#include "../source/core/songs/SongLibrary.h"
#include "../source/core/config/Config.h"
#include "../source/core/game/Calibration.h"
#include "../source/core/game/LifeMeter.h"
#include "../source/core/game/HighScores.h"

using namespace ddr;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      ++g_failures;                                                       \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
    }                                                                     \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                             \
  do {                                                                    \
    ++g_checks;                                                           \
    double a_ = (a), b_ = (b);                                            \
    if (!(std::fabs(a_ - b_) <= (tol))) {                                 \
      ++g_failures;                                                       \
      std::printf("  FAIL %s:%d: %s = %.9f, expected %.9f\n", __FILE__,   \
                  __LINE__, #a, a_, b_);                                  \
    }                                                                     \
  } while (0)

static const double kOracleTolerance = 0.0001;  // 0.1 ms

// ---------------------------------------------------------------------------
// 1. Oracle comparison

struct Row {
  int chart, row, col;
  char type;
  double time;
  bool operator<(const Row& o) const {
    return std::tie(chart, row, col, type) < std::tie(o.chart, o.row, o.col, o.type);
  }
};

static bool ReadLines(const std::string& path, std::vector<std::string>& out) {
  std::string text;
  if (!ReadFileToString(path, text)) return false;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    std::string line = text.substr(pos, nl - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(line);
    pos = nl + 1;
  }
  return true;
}

static std::vector<Row> OurRows(const Song& song) {
  std::vector<Row> rows;
  for (size_t ci = 0; ci < song.charts.size(); ++ci) {
    for (const Note& n : song.charts[ci].notes) {
      rows.push_back({static_cast<int>(ci), n.row, n.lane, NoteTypeChar(n.type), n.time});
      if (n.IsHold())
        rows.push_back({static_cast<int>(ci), n.endRow, n.lane, '3', n.endTime});
    }
  }
  std::sort(rows.begin(), rows.end());
  return rows;
}

static void RunOracle(const std::string& dir) {
  std::printf("[oracle] corpus vs tools/simfile-oracle.py, tolerance %.1f ms\n",
              kOracleTolerance * 1000);
  std::vector<std::string> index;
  if (!ReadLines(dir + "/expected/index.txt", index)) {
    std::printf("  FAIL cannot read %s/expected/index.txt\n", dir.c_str());
    ++g_failures;
    return;
  }
  size_t totalNotes = 0, files = 0;
  double worst = 0;
  std::string worstWhere;
  for (const std::string& rel : index) {
    if (rel.empty() || rel[0] == '#') continue;
    ++files;
    // expected/<dir>__<file>.tsv
    std::string expName = rel;
    size_t slash = expName.find('/');
    if (slash != std::string::npos) expName.replace(slash, 1, "__");
    std::vector<std::string> lines;
    if (!ReadLines(dir + "/expected/" + expName + ".tsv", lines)) {
      std::printf("  FAIL missing expected/%s.tsv\n", expName.c_str());
      ++g_failures;
      continue;
    }
    std::vector<Row> want;
    for (const std::string& l : lines) {
      if (l.empty() || l[0] == '#') continue;
      Row r{};
      char type = 0;
      if (std::sscanf(l.c_str(), "%d\t%d\t%d\t%c\t%lf", &r.chart, &r.row, &r.col, &type,
                      &r.time) != 5) {
        std::printf("  FAIL bad line in %s: %s\n", expName.c_str(), l.c_str());
        ++g_failures;
        continue;
      }
      r.type = type;
      want.push_back(r);
    }
    std::sort(want.begin(), want.end());

    Song song;
    LoadSongFile(dir + "/corpus/" + rel, song);
    std::vector<Row> got = OurRows(song);

    ++g_checks;
    bool ok = got.size() == want.size();
    size_t n = std::min(got.size(), want.size());
    double fileWorst = 0;
    size_t bad = 0;
    for (size_t i = 0; i < n; ++i) {
      const Row& a = got[i];
      const Row& b = want[i];
      if (a.chart != b.chart || a.row != b.row || a.col != b.col || a.type != b.type) {
        if (bad++ < 3)
          std::printf("  FAIL %s: note %zu is chart %d row %d col %d '%c', oracle has "
                      "chart %d row %d col %d '%c'\n",
                      rel.c_str(), i, a.chart, a.row, a.col, a.type, b.chart, b.row,
                      b.col, b.type);
        ok = false;
        continue;
      }
      double err = std::fabs(a.time - b.time);
      if (!(err <= kOracleTolerance)) {
        if (bad++ < 3)
          std::printf("  FAIL %s: chart %d row %d col %d: %.9f s, oracle %.9f s\n",
                      rel.c_str(), a.chart, a.row, a.col, a.time, b.time);
        ok = false;
      }
      if (err > fileWorst) fileWorst = err;
    }
    if (got.size() != want.size())
      std::printf("  FAIL %s: %zu notes, oracle has %zu\n", rel.c_str(), got.size(),
                  want.size());
    if (!ok) ++g_failures;
    if (fileWorst > worst) {
      worst = fileWorst;
      worstWhere = rel;
    }
    totalNotes += want.size();
    std::printf("  %-4s %-45s %6zu notes  max |err| %.3g ms\n", ok ? "ok" : "FAIL",
                rel.c_str(), want.size(), fileWorst * 1000);
  }
  std::printf("  %zu files, %zu notes, worst error %.3g ms (%s)\n", files, totalNotes,
              worst * 1000, worstWhere.c_str());
}

// ---------------------------------------------------------------------------
// 2. Unit tests

static Song LoadSm(const std::string& s) {
  Song song;
  LoadSmFromString(s, song);
  return song;
}
static Song LoadSsc(const std::string& s) {
  Song song;
  LoadSscFromString(s, song);
  return song;
}
static int Rows(double beat) { return BeatToNoteRow(beat); }

static const char* kChart4 =
    "#NOTES:dance-single::Easy:1::1000\n0100\n0010\n0001\n;";

static void TestMsdFile() {
  std::printf("[unit] MsdFile\n");
  {
    MsdFile m;
    m.ReadFromString("#A:b:c;\n#D:e;", true);
    CHECK(m.GetNumValues() == 2);
    CHECK(m.GetNumParams(0) == 3);
    CHECK(m.GetParam(0, 2) == "c");
    CHECK(m.GetParam(1, 1) == "e");
    CHECK(m.GetParam(5, 5).empty());
  }
  {
    MsdFile m;  // escapes
    m.ReadFromString("#T:a\\:b\\;c\\\\d;", true);
    CHECK(m.GetParam(0, 1) == "a:b;c\\d");
    MsdFile k;  // escapes kept
    k.ReadFromString("#T:a\\:b;", false);
    CHECK(k.GetParam(0, 1) == "a\\:b");
  }
  {
    MsdFile m;  // missing ';' recovered at a '#' starting a line
    m.ReadFromString("#A:1\n  #B:2;\n#C:x#y;", true);
    CHECK(m.GetNumValues() == 3);
    CHECK(m.GetParam(0, 1) == "1");
    CHECK(m.GetParam(1, 1) == "2");
    CHECK(m.GetParam(2, 1) == "x#y");  // not at line start: plain character
  }
  {
    MsdFile m;  // comments
    m.ReadFromString("// top\n#A:b// tail\n;#C:d;", true);
    CHECK(m.GetNumValues() == 2);
    CHECK(m.GetParam(0, 1) == "b\n");
  }
  {
    MsdFile m;  // unterminated value at EOF, empty input
    m.ReadFromString("#A:b", true);
    CHECK(m.GetParam(0, 1) == "b");
    MsdFile e;
    e.ReadFromString("", true);
    CHECK(e.GetNumValues() == 0);
  }
}

static void TestTimingBasics() {
  std::printf("[unit] TimingData\n");
  // 120 BPM to beat 4, then 60 BPM. Offset 0.1 s.
  TimingData td(0.1);
  td.AddSegment(TimingSegment::BPM(0, 120));
  td.AddSegment(TimingSegment::BPM(Rows(4), 60));
  td.TidyUpData(false);
  CHECK_NEAR(td.GetElapsedTimeFromBeat(0), -0.1, 1e-12);
  CHECK_NEAR(td.GetElapsedTimeFromBeat(4), 1.9, 1e-12);
  CHECK_NEAR(td.GetElapsedTimeFromBeat(6), 3.9, 1e-12);
  CHECK_NEAR(td.GetElapsedTimeFromBeat(-2), -1.1, 1e-12);  // first BPM before 0
  CHECK_NEAR(td.GetBeatFromElapsedTime(2.9), 5.0, 1e-12);
  CHECK_NEAR(td.GetBPMAtBeat(5), 60, 1e-9);

  // Inverse over a range. GetElapsedTimeFromBeat rounds the beat to a row
  // (as ITGmania does), so use row-aligned beats.
  CHECK_NEAR(td.GetElapsedTimeFromBeat(1.0 + 0.4 / 48), td.GetElapsedTimeFromBeat(1.0), 1e-12);
  for (double b = -4; b < 20; b += 17.0 / 48)
    CHECK_NEAR(td.GetBeatFromElapsedTime(td.GetElapsedTimeFromBeat(b)), b, 1e-9);

  // Stop: a note on the stop is hit before it; the beat holds during it.
  TimingData st;
  st.AddSegment(TimingSegment::BPM(0, 120));
  st.AddSegment(TimingSegment::Stop(Rows(4), 1.0));
  st.TidyUpData(false);
  CHECK_NEAR(st.GetElapsedTimeFromBeat(4), 2.0, 1e-12);
  CHECK_NEAR(st.GetElapsedTimeFromBeat(5), 3.5, 1e-12);
  TimingData::GetBeatArgs a;
  a.elapsed_time = 2.5;
  st.GetBeatAndBPSFromElapsedTime(a);
  CHECK_NEAR(a.beat, 4, 1e-12);
  CHECK(a.freeze_out && !a.delay_out);
  CHECK_NEAR(st.GetBeatFromElapsedTime(3.25), 4.5, 1e-12);

  // Delay: a note on the delay is hit after it.
  TimingData dl;
  dl.AddSegment(TimingSegment::BPM(0, 120));
  dl.AddSegment(TimingSegment::Delay(Rows(4), 1.0));
  dl.TidyUpData(false);
  CHECK_NEAR(dl.GetElapsedTimeFromBeat(4), 3.0, 1e-12);
  TimingData::GetBeatArgs d;
  d.elapsed_time = 2.5;
  dl.GetBeatAndBPSFromElapsedTime(d);
  CHECK_NEAR(d.beat, 4, 1e-12);
  CHECK(d.delay_out && !d.freeze_out);

  // Warp 4..6 at 120 BPM: zero time, notes inside are not judgable.
  TimingData wp;
  wp.AddSegment(TimingSegment::BPM(0, 120));
  wp.AddSegment(TimingSegment::Warp(Rows(4), 2.0));
  wp.TidyUpData(false);
  CHECK_NEAR(wp.GetElapsedTimeFromBeat(5), 2.0, 1e-12);
  CHECK_NEAR(wp.GetElapsedTimeFromBeat(6), 2.0, 1e-12);
  CHECK_NEAR(wp.GetElapsedTimeFromBeat(7), 2.5, 1e-12);
  CHECK(wp.IsWarpAtRow(Rows(4)) && wp.IsWarpAtRow(Rows(5.9)) && !wp.IsWarpAtRow(Rows(6)));
  CHECK(!wp.IsJudgableAtRow(Rows(5)) && wp.IsJudgableAtRow(Rows(3)));
  CHECK_NEAR(wp.GetBeatFromElapsedTime(2.25), 6.5, 1e-12);

  // Scrolls only change the displayed beat.
  TimingData sc;
  sc.AddSegment(TimingSegment::BPM(0, 120));
  sc.AddSegment(TimingSegment::Scroll(0, 1.0));
  sc.AddSegment(TimingSegment::Scroll(Rows(8), 0.5));
  sc.TidyUpData(false);
  CHECK_NEAR(sc.GetDisplayedBeat(4), 4, 1e-12);
  CHECK_NEAR(sc.GetDisplayedBeat(10), 9, 1e-12);
  CHECK_NEAR(sc.GetElapsedTimeFromBeat(10), 5, 1e-12);

  // AddSegment: redundant BPMs are merged, same-row values replaced,
  // a zero-length stop deletes the stop on that row.
  TimingData ad;
  ad.AddSegment(TimingSegment::BPM(0, 100));
  ad.AddSegment(TimingSegment::BPM(Rows(4), 100));
  CHECK(ad.GetTimingSegments(SEGMENT_BPM).size() == 1);
  ad.AddSegment(TimingSegment::BPM(Rows(4), 150));
  ad.AddSegment(TimingSegment::BPM(Rows(4), 175));
  CHECK(ad.GetTimingSegments(SEGMENT_BPM).size() == 2);
  CHECK_NEAR(ad.GetBPMAtBeat(5), 175, 1e-9);
  ad.AddSegment(TimingSegment::Stop(Rows(2), 0.5));
  ad.AddSegment(TimingSegment::Stop(Rows(2), 0.0));
  CHECK(ad.GetTimingSegments(SEGMENT_STOP).empty());

  // Defaults.
  TimingData empty;
  empty.TidyUpData(true);
  CHECK(empty.empty());
  empty.TidyUpData(false);
  CHECK_NEAR(empty.GetBPMAtBeat(0), 60, 1e-12);
  CHECK(empty.GetTimingSegments(SEGMENT_SCROLL).size() == 1);
}

static void TestSmWarpConversion() {
  std::printf("[unit] .sm negative BPMs/stops -> warps (ProcessBPMsAndStops)\n");
  {
    Song s = LoadSm(std::string("#OFFSET:0;#BPMS:0=120,4=-120,6=120;") + kChart4);
    const auto& w = s.timing.GetTimingSegments(SEGMENT_WARP);
    CHECK(w.size() == 1);
    if (w.size() == 1) {
      CHECK(w[0].row == Rows(4));
      CHECK(w[0].length == Rows(4));  // 4..8
    }
    CHECK(s.timing.GetTimingSegments(SEGMENT_BPM).size() == 1);
    CHECK_NEAR(s.timing.GetElapsedTimeFromBeat(8), 2.0, 1e-12);
    CHECK_NEAR(s.timing.GetElapsedTimeFromBeat(9), 2.5, 1e-12);
  }
  {
    Song s = LoadSm(std::string("#OFFSET:0;#BPMS:0=120;#STOPS:4=-0.5;") + kChart4);
    const auto& w = s.timing.GetTimingSegments(SEGMENT_WARP);
    CHECK(w.size() == 1 && w[0].row == Rows(4) && w[0].length == Rows(1));
    CHECK(s.timing.GetTimingSegments(SEGMENT_STOP).empty());
    CHECK_NEAR(s.timing.GetElapsedTimeFromBeat(6), 2.5, 1e-12);
  }
  {
    // A negative stop compensated by a later positive stop: warp, then the
    // leftover as a stop.
    Song s = LoadSm(std::string("#OFFSET:0;#BPMS:0=120;#STOPS:4=-0.5,4.5=0.75;") + kChart4);
    const auto& w = s.timing.GetTimingSegments(SEGMENT_WARP);
    const auto& st = s.timing.GetTimingSegments(SEGMENT_STOP);
    CHECK(w.size() == 1 && w[0].row == Rows(4) && w[0].length == Rows(0.5));
    CHECK(st.size() == 1 && st[0].row == Rows(4.5));
    if (st.size() == 1) CHECK_NEAR(st[0].value, 0.25 + 0.25, 1e-12);
  }
  {
    // Stops before beat 0 move the offset.
    Song s = LoadSm(std::string("#OFFSET:0;#BPMS:0=120;#STOPS:-1=0.5;") + kChart4);
    CHECK_NEAR(s.timing.m_fBeat0OffsetInSeconds, -0.5, 1e-12);
    CHECK_NEAR(s.charts[0].notes[0].time, 0.5, 1e-12);
  }
  {
    // No BPM at beat 0: the next one is used from the start.
    Song s = LoadSm(std::string("#OFFSET:0;#BPMS:4=150;") + kChart4);
    CHECK(s.timing.GetTimingSegments(SEGMENT_BPM).size() == 1);
    CHECK_NEAR(s.timing.GetElapsedTimeFromBeat(4), 1.6, 1e-12);
    Song n = LoadSm(std::string("#OFFSET:0;#BPMS:;") + kChart4);
    CHECK_NEAR(n.timing.GetBPMAtBeat(0), 60, 1e-12);
  }
  {
    // An "infinite" BPM is an SM4-style warp.
    Song s = LoadSm(std::string("#OFFSET:0;#BPMS:0=120,4=99999999,5=120;") + kChart4);
    const auto& w = s.timing.GetTimingSegments(SEGMENT_WARP);
    CHECK(w.size() == 1 && w[0].row == Rows(4) && w[0].length == Rows(1));
  }
}

static void TestNoteParsing() {
  std::printf("[unit] note data\n");
  std::vector<Note> n = ParseNoteData(
      "1000\n0000\n0000\n0000\n,\n2004\n0000\n3000\n0003\n,"
      "0200\n0000\n0000\n0000\n,"    // hold without a tail: dropped
      "3000\n0000\n0M00\n0L0F\n",    // tail without a head: ignored
      4);
  CHECK(n.size() == 6);
  if (n.size() == 6) {
    CHECK(n[0].row == 0 && n[0].type == NoteType::Tap);
    CHECK(n[1].row == 192 && n[1].type == NoteType::HoldHead && n[1].endRow == 288);
    CHECK(n[2].row == 192 && n[2].lane == 3 && n[2].type == NoteType::RollHead &&
          n[2].endRow == 336);
    CHECK(n[3].type == NoteType::Mine && n[3].row == 3 * 192 + 96);
    CHECK(n[4].type == NoteType::Lift && n[4].lane == 1);
    CHECK(n[5].type == NoteType::Fake && n[5].lane == 3);
  }
  // A tap between head and tail breaks the hold.
  std::vector<Note> b = ParseNoteData("2000\n1000\n3000\n0000\n", 4);
  CHECK(b.size() == 1 && b[0].type == NoteType::Tap);
  // Keysounds, extra columns, 3-row measure (rows rounded to 0/64/128).
  std::vector<Note> k = ParseNoteData("1[12]000\n00000001\n01000\n", 4);
  CHECK(k.size() == 2);
  if (k.size() == 2) {
    CHECK(k[0].keysound == 12);
    CHECK(k[1].row == 128 && k[1].lane == 1);
  }
  // ",,," is one measure break, as in ITGmania.
  std::vector<Note> c = ParseNoteData("1000\n,,,\n1000\n", 4);
  CHECK(c.size() == 2 && c[1].row == 192);
  // Routine charts: players separated by '&'.
  std::vector<Note> r = ParseNoteData("10000000\n&\n00000001\n", 8, true);
  CHECK(r.size() == 2);
  CHECK(GuessNumTracks("\n  10000[3]\n") == 5);
}

static void TestSscSplitTiming() {
  std::printf("[unit] .ssc split timing\n");
  const char* ssc =
      "#VERSION:0.83;#OFFSET:0;#BPMS:0=120;"
      "#NOTEDATA:;#STEPSTYPE:dance-single;#DIFFICULTY:Hard;#METER:9;"
      "#STOPS:1=0.5;#NOTES:0000\n1000\n0000\n0000\n;"  // STOPS only: BPM 60
      "#NOTEDATA:;#STEPSTYPE:dance-single;#DIFFICULTY:Easy;#METER:1;"
      "#OFFSET:0.25;#NOTES:1000\n0000\n0000\n0000\n;";  // OFFSET only: song timing
  Song s = LoadSsc(ssc);
  CHECK(s.charts.size() == 2);
  if (s.charts.size() == 2) {
    CHECK(s.charts[0].hasOwnTiming);
    CHECK_NEAR(s.charts[0].notes[0].time, 1.0, 1e-12);
    CHECK(!s.charts[1].hasOwnTiming);
    CHECK_NEAR(s.charts[1].notes[0].time, 0.0, 1e-12);
    CHECK(s.charts[0].difficulty == Difficulty::Hard && s.charts[0].meter == 9);
  }
  // Old .ssc warps were absolute.
  Song old = LoadSsc(std::string("#VERSION:0.5;#BPMS:0=120;#WARPS:4=6;#NOTEDATA:;#NOTES:1;"));
  const auto& w = old.timing.GetTimingSegments(SEGMENT_WARP);
  CHECK(w.size() == 1 && w[0].length == Rows(2));
  CHECK(PickSimfile({"a.sm", "b.ogg", "a.ssc"}) == "a.ssc");
  CHECK(PickSimfile({"x.SM"}) == "x.SM");
  CHECK(PickSimfile({"x.ogg"}).empty());
}

static void TestPackIni() {
  std::printf("[unit] pack.ini\n");
  std::string ini =
      "\xEF\xBB\xBF; comment\r\n[Group]\r\nVersion=1\r\nDisplayTitle= My Pack \r\n"
      "SortTitle=\r\nSeries=ITG\r\n-- lua comment\r\nSyncOffset=ITG\r\nYear=2005\r\n";
  PackInfo p = ParsePackIni("mypack", &ini);
  CHECK(p.hasPackIni);
  CHECK(p.displayTitle == "My Pack");
  CHECK(p.sortTitle == "mypack");
  CHECK(p.translitTitle == "mypack");  // absent: stays the group name
  CHECK(p.series == "ITG");
  CHECK(p.year == 2005);
  CHECK_NEAR(p.syncOffset, -0.009, 1e-12);

  std::string nul = "[Group]\nVersion=1\nSyncOffset=NULL\n";
  CHECK_NEAR(ParsePackIni("x", &nul, DefaultSyncOffset::ITG).syncOffset, 0.0, 1e-12);
  std::string bad = "[Group]\nVersion=1\nSyncOffset=bogus\n";
  CHECK_NEAR(ParsePackIni("x", &bad, DefaultSyncOffset::ITG).syncOffset, -0.009, 1e-12);
  std::string noVersion = "[Group]\nSyncOffset=ITG\nDisplayTitle=Nope\n";
  PackInfo nv = ParsePackIni("x", &noVersion);
  CHECK(!nv.hasPackIni && nv.displayTitle == "x");
  CHECK_NEAR(nv.syncOffset, 0.0, 1e-12);
  CHECK_NEAR(ParsePackIni("x", nullptr, DefaultSyncOffset::ITG).syncOffset, -0.009, 1e-12);

  // The pack offset shifts every note by the same amount.
  Song s = LoadSm(std::string("#OFFSET:0;#BPMS:0=120;") + kChart4);
  double before = s.charts[0].notes[1].time;
  s.SetGroupOffset(p.syncOffset);
  CHECK_NEAR(s.charts[0].notes[1].time, before + 0.009, 1e-12);
}

static void TestHeaderTags() {
  std::printf("[unit] header tags\n");
  Song s = LoadSm(
      "#TITLE:T;#SUBTITLE:S;#ARTIST:A;#MUSIC:m.ogg;#BANNER:b.png;"
      "#SAMPLESTART:62.5;#SAMPLELENGTH:12;#DISPLAYBPM:100:200;#BPMS:0=1;"
      "#NOTES:dance-single:smaniac:Hard::0:1000\n;");
  CHECK(s.title == "T" && s.subtitle == "S" && s.artist == "A" && s.music == "m.ogg");
  CHECK_NEAR(s.sampleStart, 62.5, 1e-12);
  CHECK_NEAR(s.sampleLength, 12, 1e-12);
  CHECK(s.displayBpmSpecified && s.displayBpmMin == 100 && s.displayBpmMax == 200);
  CHECK(s.charts.size() == 1);
  if (!s.charts.empty()) {
    CHECK(s.charts[0].difficulty == Difficulty::Challenge);  // smaniac hack
    CHECK(s.charts[0].meter == 1);                           // empty meter -> 1
  }
  CHECK(LoadSm("#NOTES:dance-single:x:Easy:1;").charts.empty());  // < 7 fields
  CHECK_NEAR(HHMMSSToSeconds("1:02.5"), 62.5, 1e-12);
  CHECK_NEAR(HHMMSSToSeconds("1:00:00"), 3600, 1e-12);
  // Comments inside #NOTES (MsdFile strips them and keeps the newline).
  Song c = LoadSm("#BPMS:0=120;#NOTES:dance-single:::::1000 // a\n0100\n// b\n0010\n0001\n;");
  CHECK(c.charts.size() == 1 && c.charts[0].notes.size() == 4);
  if (c.charts.size() == 1 && c.charts[0].notes.size() == 4)
    CHECK(c.charts[0].notes[3].row == 144 && c.charts[0].notes[3].lane == 3);
}

// ---------------------------------------------------------------------------
// 3. Fuzz: truncated / mutated files must not crash.

static unsigned g_rng = 12345u;
static unsigned Rand() {
  g_rng = g_rng * 1103515245u + 12345u;
  return (g_rng >> 8) & 0xFFFFFF;
}

static double g_sink = 0;
static void Exercise(const std::string& text) {
  for (int fmt = 0; fmt < 2; ++fmt) {
    Song s;
    if (fmt == 0) LoadSmFromString(text, s);
    else LoadSscFromString(text, s);
    for (const Chart& c : s.charts) {
      const TimingData& td = s.TimingFor(c);
      for (double b : {-3.0, 0.0, 1.5, 17.25, 400.0}) {
        g_sink += td.GetElapsedTimeFromBeat(b);
        g_sink += td.GetDisplayedBeat(b);
      }
      for (double t : {-1.0, 0.0, 2.5, 60.0, 600.0}) g_sink += td.GetBeatFromElapsedTime(t);
      for (const Note& n : c.notes) g_sink += n.time;
    }
  }
}

static void RunFuzz(const std::string& dir, int iterations) {
  std::printf("[fuzz] truncated and mutated corpus files\n");
  std::vector<std::string> index, texts;
  ReadLines(dir + "/expected/index.txt", index);
  for (const std::string& rel : index) {
    if (rel.empty() || rel[0] == '#') continue;
    std::string t;
    if (ReadFileToString(dir + "/corpus/" + rel, t)) texts.push_back(t);
  }
  // Hand-picked nasty inputs.
  const char* nasty[] = {
      "", "#", "#:", ";", ":", "\\", "#\\", "#NOTES:", "#NOTES::::::",
      "#BPMS:=;", "#SPEEDS:=;", "#SPEEDS:==;", "#SCROLLS:=;", "#COMBOS:=;",
      "#TIMESIGNATURES:==;", "#BPMS:0=-1;#STOPS:0=-1;", "#BPMS:1e40=1e40;",
      "#BPMS:0=1e-300;#NOTES:a:b:c:d:e:1\n;", "#STOPS:-1e30=1;", "#DELAYS:-5=1,-5=2;",
      "#VERSION:0.83;#WARPS:0=1e30;#NOTEDATA:;#NOTES:1;",
      "#VERSION:0.83;#BPMS:0=120;#WARPS:-4=8,2=1e9;#NOTEDATA:;#NOTES:1\n1\n1;",
      "#BPMS:0=120,1=-1e30,2=1e30;#STOPS:1=-1e30,1.5=1e30;#NOTES:a:b:c:d:e:1111\n;",
      "#NOTEDATA:;#NOTEDATA:;#NOTES:;", "#BPMS:nan=inf;#OFFSET:nan;",
      "#BPMS:0=120;#NOTES:dance-single:::::2222\n0000\n3333[\n[[[]]]\n;",
      "#BPMS:999999999999r=1;#STOPS:5R=1;",
  };
  for (const char* s : nasty) Exercise(s);
  std::string longRow(200000, '2');
  Exercise("#BPMS:0=120;#NOTES:dance-single:::::" + longRow + "\n;");
  std::string manyMeasures = "#BPMS:0=120;#NOTES:dance-single:::::";
  for (int i = 0; i < 20000; ++i) manyMeasures += "1000\n,";
  Exercise(manyMeasures + ";");

  size_t truncations = 0;
  for (const std::string& t : texts) {
    size_t step = std::max<size_t>(1, t.size() / 150);
    for (size_t len = 0; len <= t.size(); len += step) {
      Exercise(t.substr(0, len));
      ++truncations;
    }
    for (int i = 0; i < 50 && !t.empty(); ++i) {
      Exercise(t.substr(0, Rand() % t.size()));
      ++truncations;
    }
  }
  static const char kAlphabet[] = "#:;,=\\/-.0123456789eErR\n\r\t 1234MLF&[]";
  for (int i = 0; i < iterations && !texts.empty(); ++i) {
    std::string t = texts[Rand() % texts.size()];
    int edits = 1 + Rand() % 8;
    for (int e = 0; e < edits && !t.empty(); ++e) {
      size_t pos = Rand() % t.size();
      char ch = (Rand() % 4 == 0) ? static_cast<char>(Rand() & 0xFF)
                                  : kAlphabet[Rand() % (sizeof(kAlphabet) - 1)];
      switch (Rand() % 3) {
        case 0: t[pos] = ch; break;
        case 1: t.insert(t.begin() + pos, ch); break;
        default: t.erase(pos, 1 + Rand() % 16); break;
      }
    }
    Exercise(t);
  }
  ++g_checks;  // reaching here without crashing is the check
  std::printf("  ok   %zu truncations, %d mutations, %zu hand-picked inputs\n",
              truncations, iterations, sizeof(nasty) / sizeof(nasty[0]) + 2);
}

// ---- InputFilter (debounce, doc 06 §3.4) ----

// Feeds (time_us, raw) samples and collects the reported changes as "b<bit><+|-><time_us>".
static std::vector<std::string> FeedAll(input::InputFilter& f,
                                        const std::vector<std::pair<uint64_t, uint16_t>>& samples) {
  std::vector<std::string> log;
  input::InputFilter::Change out[input::InputFilter::kButtons];
  for (const auto& s : samples) {
    const int n = f.feed(s.first, s.second, out);
    for (int i = 0; i < n; ++i)
      log.push_back("b" + std::to_string(out[i].button) + (out[i].pressed ? "+" : "-") +
                    std::to_string(out[i].time_us));
  }
  return log;
}

static void TestInputFilter() {
  std::printf("InputFilter\n");
  using V = std::vector<std::string>;
  {  // Clean press and release far apart: both reported at once with their own times.
    input::InputFilter f(20000);
    CHECK((FeedAll(f, {{0, 0}, {8000, 1}, {16000, 1}, {100000, 0}}) == V{"b0+8000", "b0-100000"}));
    CHECK(f.state() == 0);
  }
  {  // Bounce: release 5 ms after the press, re-press 3 ms later -> both dropped.
    input::InputFilter f(20000);
    CHECK((FeedAll(f, {{0, 1}, {5000, 0}, {8000, 1}, {40000, 1}}) == V{"b0+0"}));
    CHECK(f.state() == 1);
  }
  {  // Short real tap: release inside the window is held back, then reported with its own time.
    input::InputFilter f(20000);
    CHECK((FeedAll(f, {{0, 1}, {10000, 0}, {18000, 0}, {26000, 0}}) == V{"b0+0", "b0-10000"}));
  }
  {  // Jump: two panels in the same sample are two independent changes, in bit order.
    input::InputFilter f(20000);
    CHECK((FeedAll(f, {{0, 0}, {8000, 0x0003}}) == V{"b0+8000", "b1+8000"}));
    CHECK(f.state() == 0x0003);
  }
  {  // Debounce is per button: a change on another panel is not held back.
    input::InputFilter f(20000);
    CHECK((FeedAll(f, {{0, 0x1}, {4000, 0x3}}) == V{"b0+0", "b1+4000"}));
  }
  {  // reset() takes the current state without reporting anything.
    input::InputFilter f(20000);
    f.reset(0x0008);
    CHECK((FeedAll(f, {{0, 0x0008}, {8000, 0x0008}}) == V{}));
    CHECK((FeedAll(f, {{9000, 0}}) == V{"b3-9000"}));   // first change after reset: no window
  }
  {  // Window 0 disables debouncing: every change is reported.
    input::InputFilter f(0);
    CHECK((FeedAll(f, {{0, 1}, {1000, 0}, {2000, 1}}) == V{"b0+0", "b0-1000", "b0+2000"}));
  }
  {  // Times stay monotonic per button even when a held-back change is reported late.
    input::InputFilter f(20000);
    auto log = FeedAll(f, {{0, 1}, {5000, 0}, {9000, 1}, {12000, 0}, {30000, 0}, {31000, 1}, {40000, 1}});
    CHECK((log == V{"b0+0", "b0-12000", "b0+31000"}));
  }
}

// ---- WavFile header parsing (M4b) ----

static void Put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xFF); v.push_back(x >> 8); }
static void Put32(std::vector<uint8_t>& v, uint32_t x) { Put16(v, x & 0xFFFF); Put16(v, x >> 16); }
static void PutId(std::vector<uint8_t>& v, const char* id) { v.insert(v.end(), id, id + 4); }

// A WAV header: optional LIST chunk (odd length, padded) before fmt, then data.
static std::vector<uint8_t> MakeWav(uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits,
                                    uint32_t data_len, bool list_chunk) {
  std::vector<uint8_t> v;
  PutId(v, "RIFF"); Put32(v, 0); PutId(v, "WAVE");
  if (list_chunk) { PutId(v, "LIST"); Put32(v, 3); v.insert(v.end(), {'a', 'b', 'c', 0}); }
  PutId(v, "fmt "); Put32(v, 16);
  Put16(v, format); Put16(v, channels); Put32(v, rate);
  Put32(v, rate * channels * bits / 8); Put16(v, channels * bits / 8); Put16(v, bits);
  PutId(v, "data"); Put32(v, data_len);
  return v;
}

static void TestWavFile() {
  std::printf("WavFile\n");
  audio::WavInfo w;
  {
    auto h = MakeWav(1, 2, 48000, 16, 48000 * 4, false);
    CHECK(audio::parseWavHeader(h.data(), h.size(), h.size() + 48000 * 4, w));
    CHECK(w.rate == 48000 && w.channels == 2 && w.data_offset == 44 && w.frames() == 48000);
  }
  {  // padded odd-length chunk before fmt; mono 44.1 kHz
    auto h = MakeWav(1, 1, 44100, 16, 1000, true);
    CHECK(audio::parseWavHeader(h.data(), h.size(), h.size() + 1000, w));
    CHECK(w.rate == 44100 && w.channels == 1 && w.data_offset == 56 && w.frames() == 500);
  }
  {  // truncated file: data_bytes clipped to whole frames that exist
    auto h = MakeWav(1, 2, 48000, 16, 100000, false);
    CHECK(audio::parseWavHeader(h.data(), h.size(), h.size() + 1001, w));
    CHECK(w.data_bytes == 1000);
  }
  {  // not 16-bit PCM, or not a WAV
    auto h8 = MakeWav(1, 2, 48000, 8, 100, false);
    CHECK(!audio::parseWavHeader(h8.data(), h8.size(), h8.size() + 100, w));
    auto hf = MakeWav(3, 2, 48000, 16, 100, false);   // IEEE float
    CHECK(!audio::parseWavHeader(hf.data(), hf.size(), hf.size() + 100, w));
    CHECK(!audio::parseWavHeader(hf.data(), 20, 20, w));   // header cut before data
    const uint8_t junk[16] = {'R', 'I', 'F', 'X'};
    CHECK(!audio::parseWavHeader(junk, sizeof junk, sizeof junk, w));
  }
}

// ---- SongClock (doc 06 §3.1) ----

static void TestSongClock() {
  std::printf("SongClock\n");
  timing::SongClock c;
  CHECK(c.now(123456) == 0 && !c.started());

  // Simulate ASND: the audio really plays at `rate` (100 ppm fast, worse than any crystal),
  // a report comes every 2048 frames (~42.7 ms), and each report lands at the next 21.33 ms
  // mixing tick, so it is late by 0..21.3 ms. Video frames read now() every 16.683 ms.
  const double rate = 1.0001;
  const double tick = 1024.0 / 48000 * 1e6, report = 2048.0 / 48000 * 1e6;
  uint32_t lcg = 1;
  double next_report_audio = report;
  double min_err = 1e9, max_err = -1e9, max_jitter = 0;
  int64_t prev = 0;
  bool monotonic = true;
  const uint64_t start = 5000000;   // cpu time when the song starts
  // Events in time order: reports (song time n*report reached at cpu n*report/rate, seen
  // `late` us later) and video frames (every 16683 us from 2 s on; errors counted from 10 s).
  double next_frame = 2e6, report_cpu = -1;
  auto draw_late = [&] { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 8) / 16777216.0 * tick; };
  report_cpu = next_report_audio / rate + draw_late();
  while (next_frame < 300e6) {
    if (report_cpu <= next_frame) {
      c.observe(start + (uint64_t)report_cpu, (int64_t)next_report_audio);
      next_report_audio += report;
      report_cpu = next_report_audio / rate + draw_late();
      continue;
    }
    const double cpu = next_frame;
    next_frame += 16683;
    const double played = cpu * rate;    // true song time at this cpu time
    const int64_t v = c.now(start + (uint64_t)cpu);
    const double err = v - played;       // negative: the clock trails by the mean report lateness
    if (cpu > 10e6 && err < min_err) min_err = err;   // after the low-pass has settled
    if (cpu > 10e6 && err > max_err) max_err = err;
    if (prev && v < prev) monotonic = false;
    if (prev) {
      const double j = std::fabs((v - prev) - 16683 * rate);   // vs. song time in one frame
      if (j > max_jitter) max_jitter = j;
    }
    prev = v;
  }
  std::printf("  clock error %.2f..%.2f ms, frame jitter %.3f ms, snaps %u\n", min_err / 1000,
              max_err / 1000, max_jitter / 1000, c.snaps());
  CHECK(monotonic);
  CHECK(c.snaps() == 0);
  CHECK(max_jitter < 1000);              // doc 05 M4b: jitter < 1 ms frame to frame
  CHECK(max_err - min_err < 4000);       // the error stays in a narrow band: no drift
  CHECK(min_err > -22000 && max_err < 1000);   // trails by at most one tick

  // Hold: with no reports the clock stops kHoldUs after the last one.
  timing::SongClock h;
  h.observe(1000000, 0);
  CHECK(h.now(1100000) == 100000);
  CHECK(h.now(5000000) == timing::SongClock::kHoldUs);
  // A report far off (seek or underrun) snaps instead of smoothing, and may go back.
  h.observe(6000000, 10000000);
  CHECK(h.snaps() == 1 && h.now(6000000) == 10000000);
  h.observe(6100000, 0);
  CHECK(h.snaps() == 2 && h.now(6100000) == 0);
  // Free run (the audio has ended): no hold, the CPU clock carries on.
  h.freeRun();
  CHECK(h.now(9100000) == 3000000);
}


// ---------------------------------------------------------------------------
// Judge (M4c, doc 06 §3.4)

static ddr::Note JNote(int row, double time, int lane, double end_time = -1) {
  ddr::Note n;
  n.row = row;
  n.beat = row / 48.0;
  n.time = time;
  n.lane = lane;
  if (end_time >= 0) {
    n.type = ddr::NoteType::HoldHead;
    n.endTime = end_time;
  }
  n.endRow = row;
  return n;
}

static ddr::Note JMine(int row, double time, int lane) {
  ddr::Note n = JNote(row, time, lane);
  n.type = ddr::NoteType::Mine;
  return n;
}

static ddr::Note JRoll(int row, double time, int lane, double end_time) {
  ddr::Note n = JNote(row, time, lane, end_time);
  n.type = ddr::NoteType::RollHead;
  return n;
}

// Mines, rolls and the life meter (M5 step 4).
static void TestMinesRollsLife() {
  std::printf("[unit] mines, rolls, life\n");
  using game::Judge;
  {  // Mines: down as it passes = hit; pressed within 70 ms = hit; else avoided.
    Judge j;
    j.load({JMine(0, 1.0, 0), JMine(48, 2.0, 1), JMine(96, 3.0, 2), JMine(144, 4.0, 3)}, 4);
    j.press(0, 0.9);    // lane 0 held across 1.0
    j.release(0, 1.1);
    j.press(1, 2.05);   // 50 ms after the mine
    j.release(1, 2.1);
    j.press(2, 3.2);    // 200 ms after: too late to hit it
    j.release(2, 3.25);
    j.press(3, 3.80);   // tapped and let go before it
    j.release(3, 3.85);
    j.update(10);
    CHECK(j.score().mines_hit == 2 && j.score().mines_avoided == 2);
    CHECK(j.mines()[0].state == Judge::MineState::kHit && j.mines()[2].state == Judge::MineState::kAvoided);
    int ev = 0;
    for (const game::JudgeEvent& e : j.events()) ev += e.kind == game::JudgeEvent::kMine;
    CHECK(ev == 2 && j.finished());
  }
  {  // Rolls: holding isn't enough; re-presses within 0.35 s keep it.
    Judge held, tapped, slow;
    for (Judge* j : {&held, &tapped, &slow}) j->load({JRoll(0, 1.0, 0, 2.0)}, 4);
    held.press(0, 1.0);
    held.update(3);
    CHECK(held.score().holds_let_go == 1);
    for (double t = 1.0; t < 2.0; t += 0.3) { tapped.press(0, t); tapped.release(0, t + 0.05); }
    tapped.update(3);
    CHECK(tapped.score().holds_held == 1);
    slow.press(0, 1.0); slow.release(0, 1.05);
    slow.press(0, 1.4); slow.release(0, 1.45);   // 0.4 s gap
    slow.update(3);
    CHECK(slow.score().holds_let_go == 1);
  }
  {  // Autoplay avoids a mine right after a tap and taps through a roll.
    Judge j;
    j.load({JNote(0, 1.0, 0), JMine(6, 1.125, 0), JRoll(48, 2.0, 1, 3.0)}, 4);
    for (const game::AutoStep& s : game::MakeAutoplay(j)) {
      if (s.pressed) j.press(s.lane, s.time); else j.release(s.lane, s.time);
    }
    j.update(10);
    CHECK(j.score().mines_hit == 0 && j.score().holds_held == 1);
    CHECK(j.score().rows[(int)game::Grade::Marvelous] == 2);
  }
  {  // Life: ITG values from 0.5.
    game::LifeMeter l;
    CHECK_NEAR(l.life(), 0.5, 1e-12);
    l.row(game::Grade::Marvelous);
    CHECK_NEAR(l.life(), 0.508, 1e-12);
    l.row(game::Grade::Miss);              // -0.1, then 5 good rows before gains count
    CHECK_NEAR(l.life(), 0.408, 1e-12);
    for (int i = 0; i < 4; i++) l.row(game::Grade::Marvelous);
    CHECK_NEAR(l.life(), 0.408, 1e-12);
    l.row(game::Grade::Marvelous);
    CHECK_NEAR(l.life(), 0.416, 1e-12);
    l.mine();
    CHECK_NEAR(l.life(), 0.366, 1e-12);
    l.hold(game::HoldResult::LetGo, game::Grade::Perfect);
    CHECK_NEAR(l.life(), 0.286, 1e-12);
    l.hold(game::HoldResult::LetGo, game::Grade::Miss);   // missed head: the Miss row counted
    CHECK_NEAR(l.life(), 0.286, 1e-12);
    CHECK(!l.danger());
    for (int i = 0; i < 3; i++) l.row(game::Grade::Miss);
    CHECK(l.failed() && l.life() == 0);
    for (int i = 0; i < 50; i++) l.row(game::Grade::Marvelous);
    CHECK(l.failed() && l.life() == 0);   // stays failed
  }
  {  // Full life: any loss costs at least 10% (harsh hot penalty); regen stacks to 10.
    game::LifeMeter l;
    for (int i = 0; i < 100; i++) l.row(game::Grade::Perfect);
    CHECK(l.hot());
    l.row(game::Grade::Boo);
    CHECK_NEAR(l.life(), 0.9, 1e-12);
    l.row(game::Grade::Boo);
    l.row(game::Grade::Boo);               // 0.9 - 0.05 - 0.05; regen 10 rows
    CHECK_NEAR(l.life(), 0.8, 1e-12);
    for (int i = 0; i < 9; i++) l.row(game::Grade::Marvelous);
    CHECK_NEAR(l.life(), 0.8, 1e-12);
    l.row(game::Grade::Marvelous);
    CHECK_NEAR(l.life(), 0.808, 1e-12);
  }
}

static void TestJudge() {
  std::printf("[unit] Judge\n");
  using game::Grade;
  using game::HoldResult;
  using game::Judge;
  const game::Windows w;
  CHECK(game::GradeForError(0.0, w) == Grade::Marvelous);
  // ITG's windows (Simply Love ITG mode + 1.5 ms).
  CHECK(game::GradeForError(0.0230, w) == Grade::Marvelous);
  CHECK(game::GradeForError(0.0231, w) == Grade::Perfect);
  CHECK(game::GradeForError(0.0445, w) == Grade::Perfect);
  CHECK(game::GradeForError(0.0446, w) == Grade::Great);
  CHECK(game::GradeForError(0.095, w) == Grade::Great);    // a Good with StepMania's windows
  CHECK(game::GradeForError(0.1035, w) == Grade::Great);
  CHECK(game::GradeForError(0.1036, w) == Grade::Good);
  CHECK(game::GradeForError(0.1365, w) == Grade::Good);
  CHECK(game::GradeForError(0.1366, w) == Grade::Boo);
  CHECK(game::GradeForError(0.1815, w) == Grade::Boo);
  CHECK(game::GradeForError(0.1816, w) == Grade::None);

  {  // Closest note: a press between two close notes takes the nearer, the other misses.
    Judge j;
    j.load({JNote(0, 1.0, 0), JNote(12, 1.1, 0)}, 4);
    j.press(0, 1.07);
    CHECK(j.notes()[1].grade == Grade::Perfect);
    CHECK(j.notes()[0].grade == Grade::None);
    j.update(1.2);
    CHECK(j.notes()[0].grade == Grade::Miss);
    CHECK(j.finished());
    CHECK(j.score().combo == 0 && j.score().max_combo == 1);
  }
  {  // Stray presses and other lanes do nothing.
    Judge j;
    j.load({JNote(0, 1.0, 1)}, 4);
    j.press(1, 0.5);
    j.press(0, 1.0);
    j.press(7, 1.0);
    CHECK(j.notes()[0].grade == Grade::None);
    j.press(1, 1.01);
    CHECK(j.notes()[0].grade == Grade::Marvelous);
  }
  {  // Early-hit rule: an early Boo keeps the note; a better re-hit replaces it.
    Judge j;
    j.load({JNote(0, 1.0, 0)}, 4);
    j.press(0, 0.86);
    CHECK(j.notes()[0].grade == Grade::None && j.notes()[0].early == Grade::Boo);
    j.release(0, 0.9);
    j.press(0, 1.01);
    CHECK(j.notes()[0].grade == Grade::Marvelous);
    CHECK(j.score().rows[(int)Grade::Marvelous] == 1 && j.score().combo == 1);
  }
  {  // ... and if the note passes unhit, it keeps the early grade, not a Miss.
    Judge j;
    j.load({JNote(0, 1.0, 0)}, 4);
    j.press(0, 0.83);   // Boo, early
    j.release(0, 0.85);
    j.press(0, 0.87);   // Good, early: not used, the first early grade stays
    j.release(0, 0.88);
    j.press(0, 1.15);   // Boo, late: ignored after an early hit
    j.update(1.2);
    CHECK(j.notes()[0].grade == Grade::Boo);
    CHECK_NEAR(j.notes()[0].offset, -0.17, 1e-9);
    CHECK(j.score().rows[(int)Grade::Boo] == 1 && j.score().rows[(int)Grade::Miss] == 0);
  }
  {  // With the rule off, an early Boo uses the note up.
    Judge j;
    Judge::Options o;
    o.early_hit_rule = false;
    j.load({JNote(0, 1.0, 0)}, 4, o);
    j.press(0, 0.86);
    CHECK(j.notes()[0].grade == Grade::Boo);
  }
  {  // Jumps: the rule doesn't apply, and the row counts once with its worst grade.
    Judge j;
    j.load({JNote(0, 1.0, 0), JNote(0, 1.0, 3), JNote(48, 2.0, 1)}, 4);
    j.press(0, 0.86);
    CHECK(j.notes()[0].grade == Grade::Boo);
    CHECK(j.score().rows[(int)Grade::Boo] == 0);   // row not done yet
    j.press(3, 1.0);
    CHECK(j.score().rows[(int)Grade::Boo] == 1 && j.score().combo == 0);
    j.press(1, 2.05);
    CHECK(j.score().rows[(int)Grade::Great] == 1 && j.score().combo == 1);
    CHECK(j.finished());
    // Points (ITG weights): Boo row -6 + Great row 2 out of 2 rows x 5; the % stops at 0.
    CHECK(j.score().dance_points == -4 && j.score().possible_points == 10);
    CHECK_NEAR(j.score().percent(), 0.0, 1e-9);
    CHECK(j.score().hits == 3);
  }
  {  // Holds: a short release is fine, a long one lets go, a missed head is NG.
    Judge j;
    j.load({JNote(0, 1.0, 0, 3.0), JNote(0, 1.0, 1, 3.0), JNote(0, 1.0, 2, 3.0)}, 4);
    j.press(0, 1.0);
    j.press(1, 1.0);
    j.release(0, 1.5);
    j.press(0, 1.7);    // released for 0.2 s: still OK
    j.release(1, 2.0);  // released for good
    j.update(2.32);     // the hold window is 0.3215 s
    CHECK(j.notes()[1].hold_result == HoldResult::None);
    j.update(2.33);
    CHECK(j.notes()[1].hold_result == HoldResult::LetGo);
    CHECK(j.notes()[2].hold_result == HoldResult::LetGo);   // head missed
    CHECK(!j.finished());
    j.update(3.0);
    CHECK(j.notes()[0].hold_result == HoldResult::Held);
    CHECK(j.finished());
    CHECK(j.score().holds_held == 1 && j.score().holds_let_go == 2);
  }
  {  // A release within hold_ok of the tail is still OK.
    Judge j;
    j.load({JNote(0, 1.0, 0, 2.0)}, 4);
    j.press(0, 1.0);
    j.release(0, 1.9);
    j.update(5.0);
    CHECK(j.notes()[0].hold_result == HoldResult::Held);
    CHECK_NEAR(j.score().percent(), 100.0, 1e-9);
  }
  {  // Held miss: a note passes while its lane is held down.
    Judge j;
    j.load({JNote(0, 1.0, 2)}, 4);
    j.press(2, 0.2);
    j.update(2.0);
    CHECK(j.notes()[0].grade == Grade::Miss && j.score().held_misses == 1);
  }
  {  // Only judgable taps and heads count.
    std::vector<ddr::Note> v = {JNote(0, 1.0, 0), JNote(0, 1.0, 1), JNote(0, 1.0, 2)};
    v[1].type = ddr::NoteType::Mine;
    v[2].judgable = false;
    Judge j;
    j.load(v, 4);
    CHECK(j.notes().size() == 1);
  }
  {  // Events: a note, then its row.
    Judge j;
    j.load({JNote(0, 1.0, 0)}, 4);
    j.press(0, 1.03);
    CHECK(j.events().size() == 2);
    CHECK(j.events()[0].kind == game::JudgeEvent::kNote && j.events()[0].grade == Grade::Perfect);
    CHECK_NEAR(j.events()[0].offset, 0.03, 1e-9);
    CHECK(j.events()[1].kind == game::JudgeEvent::kRow);
  }
}

// Autoplay over every chart in the corpus must be 100% Marvelous (doc 05 M4c). This is
// what catches step-time and note-time mistakes before the Wii is involved.
static void TestAutoplayCorpus(const std::string& dir) {
  std::vector<std::string> index;
  if (!ReadLines(dir + "/expected/index.txt", index)) return;
  size_t charts = 0, notes = 0, holds = 0, perfect = 0, mines = 0;
  for (const std::string& rel : index) {
    if (rel.empty() || rel[0] == '#') continue;
    Song song;
    LoadSongFile(dir + "/corpus/" + rel, song);
    for (size_t ci = 0; ci < song.charts.size(); ++ci) {
      const Chart& c = song.charts[ci];
      game::Judge j;
      j.load(c.notes, c.numTracks);
      for (const game::AutoStep& s : game::MakeAutoplay(j)) {
        if (s.pressed)
          j.press(s.lane, s.time);
        else
          j.release(s.lane, s.time);
      }
      j.update(1e9);
      const game::Score& sc = j.score();
      int rows = 0;
      for (int g = 0; g < game::kNumGrades; ++g) rows += sc.rows[g];
      mines += j.mines().size();
      const bool ok = j.finished() && rows == sc.rows[(int)game::Grade::Marvelous] &&
                      sc.holds_let_go == 0 && sc.max_combo == rows && sc.held_misses == 0 &&
                      sc.mines_hit == 0 &&
                      (sc.possible_points == 0 || sc.percent() == 100.0);
      ++g_checks;
      if (!ok) {
        ++g_failures;
        std::printf("  FAIL autoplay %s chart %zu: %d rows, %d Marvelous, %d let go, "
                    "combo %d, %.2f%%, %d mines hit\n", rel.c_str(), ci, rows,
                    sc.rows[(int)game::Grade::Marvelous], sc.holds_let_go, sc.max_combo,
                    sc.percent(), sc.mines_hit);
      } else {
        ++perfect;
      }
      ++charts;
      notes += j.notes().size();
      holds += sc.holds_held + sc.holds_let_go;
    }
  }
  std::printf("[autoplay] %zu/%zu charts 100%% Marvelous, %zu notes, %zu holds, %zu mines avoided\n",
              perfect, charts, notes, holds, mines);
}

// The M4c test song (tools/make-test-song.py): every note must sit exactly on the click
// track's grid (beat k at k * 0.5 s), so a human's mean error there is pure latency.
static void TestTestSong(const std::string& dir) {
  std::printf("[unit] test song (tools/make-test-song.py)\n");
  Song song;
  CHECK(LoadSongFile(dir + "/corpus/synthetic/20_test_song.sm", song));
  CHECK(song.music == "click120-44k.ogg");
  CHECK(song.charts.size() == 3);
  bool on_grid = true;
  size_t holds = 0;
  for (const Chart& c : song.charts) {
    CHECK(!c.notes.empty() && c.notes.front().time == 4.0);   // measure 2
    for (const Note& n : c.notes) {
      if (std::fabs(n.time - n.beat * 0.5) > 1e-12) on_grid = false;
      if (n.IsHold()) {
        ++holds;
        if (std::fabs(n.endTime - n.endBeat * 0.5) > 1e-12) on_grid = false;
      }
    }
  }
  CHECK(on_grid);
  CHECK(holds == 32);   // 16 per chart in Easy and Medium
}

// ---------------------------------------------------------------------------
// Song library scan on the fake tree in library/ (doc 05 M5 step 1)

static void TestSongLibrary(const std::string& dir) {
  std::printf("[unit] song library\n");
  SongLibrary lib;
  CHECK(!ScanSongLibrary(dir + "/library-does-not-exist", lib));
  CHECK(ScanSongLibrary(dir + "/library", lib));
  CHECK(lib.packs.size() == 2);   // "Only Broken" has nothing playable
  CHECK(lib.SongCount() == 3);
  CHECK(lib.warnings.size() == 5);  // Empty, NoAudio, NoSingle, Sub, Only Broken/Bad
  if (lib.packs.size() != 2 || lib.SongCount() != 3) return;

  const PackEntry& b = lib.packs[0];  // Pack.ini SortTitle "0 first" puts it first
  CHECK(b.info.groupName == "B Pack");
  CHECK(b.info.displayTitle == "Bee Pack");
  CHECK(b.info.hasPackIni);
  CHECK_NEAR(b.info.syncOffset, -0.009, 1e-12);
  CHECK(b.songs.size() == 2);
  if (b.songs.size() == 2) {
    const SongEntry& alpha = b.songs[0];
    CHECK(alpha.simfile == "alpha.ssc");   // .ssc before .sm
    CHECK(alpha.title == "alpha");
    CHECK(alpha.subtitle == "ssc wins");
    CHECK(alpha.music == "music.mp3");     // no #MUSIC: the first audio file
    CHECK(alpha.banner == "banner.jpg");   // #BANNER file is missing: guessed
    CHECK(alpha.charts.size() == 2);       // dance-double left out
    if (alpha.charts.size() == 2) {
      CHECK(alpha.charts[0].difficulty == Difficulty::Easy && alpha.charts[0].meter == 4);
      CHECK(alpha.charts[0].index == 2);
      CHECK(alpha.charts[1].difficulty == Difficulty::Hard && alpha.charts[1].index == 0);
    }
    const SongEntry& zeta = b.songs[1];
    CHECK(zeta.title == "Zeta");
    CHECK(zeta.music == "ZETA.OGG");       // #MUSIC matched ignoring case
    CHECK(zeta.banner == "zeta-bn.png");
    CHECK(zeta.bpmMin == 120 && zeta.bpmMax == 120);
    CHECK_NEAR(zeta.lengthSeconds, 1.5, 1e-9);   // 4 rows of 4ths at 120 BPM
    CHECK(zeta.charts.size() == 1 && zeta.charts[0].steps == 4 && zeta.charts[0].jumps == 0);
    CHECK(zeta.SimfilePath() == dir + "/library/B Pack/Zeta/zeta.sm");
    CHECK(zeta.MusicPath() == dir + "/library/B Pack/Zeta/ZETA.OGG");
  }
  const PackEntry& a = lib.packs[1];
  CHECK(a.info.groupName == "a pack" && !a.info.hasPackIni);
  CHECK_NEAR(a.info.syncOffset, 0.0, 1e-12);
  CHECK(a.songs.size() == 1 && a.songs[0].title == "beta" && a.songs[0].music == "song.wav");

  SongLibrary itg;
  ScanSongLibrary(dir + "/library", itg, DefaultSyncOffset::ITG);
  CHECK(itg.packs.size() == 2 && itg.packs.size() > 1 &&
        std::fabs(itg.packs[1].info.syncOffset + 0.009) < 1e-12);
}

// ---------------------------------------------------------------------------
// config.ini and offset calibration (doc 05 M5 step 3)

static void TestConfig(const std::string& dir) {
  std::printf("[unit] config.ini\n");
  Config c;
  ParseConfig("[Options]\r\nGlobalOffset= -12.5 \r\nDefaultSyncOffset=itg\r\nInputDebounceMs=10\r\n"
              "PlayMode=competitive\r\n[Port2]\r\nInputOffset=4\r\nScrollSpeed=3.3\r\n", c);
  CHECK(c.playMode == PlayMode::Competitive);
  CHECK_NEAR(c.ports[1].scrollSpeed, 3.25, 1e-12);   // rounded to 0.25
  CHECK_NEAR(c.ports[0].scrollSpeed, 2.0, 1e-12);
  CHECK_NEAR(c.globalOffsetS, -0.0125, 1e-12);
  CHECK(c.defaultSyncOffset == DefaultSyncOffset::ITG);
  CHECK(c.inputDebounceMs == 10);
  CHECK_NEAR(c.ports[1].inputOffsetS, 0.004, 1e-12);
  CHECK_NEAR(c.ports[0].inputOffsetS, 0.0, 1e-12);

  // Bad values keep what was there; offsets are clamped to +-1 s.
  Config d = c;
  ParseConfig("[Options]\nGlobalOffset=abc\nInputDebounceMs=500\nDefaultSyncOffset=maybe\n"
              "[Port1]\nInputOffset=-5000\n", d);
  CHECK_NEAR(d.globalOffsetS, -0.0125, 1e-12);
  CHECK(d.inputDebounceMs == 10 && d.defaultSyncOffset == DefaultSyncOffset::ITG);
  CHECK_NEAR(d.ports[0].inputOffsetS, -1.0, 1e-12);

  // Write, then read back into defaults: the same values.
  Config e;
  ParseConfig(WriteConfigText(c), e);
  CHECK_NEAR(e.globalOffsetS, c.globalOffsetS, 1e-9);
  CHECK(e.defaultSyncOffset == c.defaultSyncOffset && e.inputDebounceMs == c.inputDebounceMs);
  CHECK_NEAR(e.ports[1].inputOffsetS, 0.004, 1e-9);
  CHECK(e.playMode == PlayMode::Competitive);
  CHECK_NEAR(e.ports[1].scrollSpeed, 3.25, 1e-9);

  // Save twice over the same file (the second rename replaces the first file), then load.
  const std::string path = dir + "/build/config-test.ini";
  std::remove(path.c_str());
  Config none;
  CHECK(!LoadConfig(path, none));
  CHECK(SaveConfig(path, d));
  CHECK(SaveConfig(path, c));
  Config f;
  CHECK(LoadConfig(path, f));
  CHECK_NEAR(f.globalOffsetS, -0.0125, 1e-9);
  CHECK_NEAR(f.ports[0].inputOffsetS, 0.0, 1e-9);
  FILE* tmp = std::fopen((path + ".tmp").c_str(), "rb");
  CHECK(tmp == nullptr);   // the temporary file was renamed away
  if (tmp) std::fclose(tmp);
  std::remove(path.c_str());
}

static void TestCalibration() {
  std::printf("[unit] calibration\n");
  game::Calibration c(0.5, 0.0);
  CHECK(!c.press(-0.3));             // before the first beat
  CHECK(c.press(1.020));             // beat 2, 20 ms late
  CHECK(!c.press(1.040));            // the other foot of a jump
  CHECK(!c.press(1.75 - 0.01));      // halfway between beats: too far from either
  CHECK_NEAR(c.lastError(), 0.020, 1e-12);
  CHECK(c.press(1.490));             // beat 3, 10 ms early
  CHECK_NEAR(c.mean(), 0.005, 1e-12);
  CHECK(!c.ready());

  // A steady player 30 ms late with +-10 ms of wobble: the suggestion is -30 ms.
  c.reset();
  for (int k = 4; k < 4 + 40; k++) c.press(k * 0.5 + 0.030 + ((k % 3) - 1) * 0.010);
  CHECK(c.ready() && c.count() == game::Calibration::kWindow);
  CHECK(std::fabs(c.suggestedOffset() + 0.030) < 0.002);
  CHECK(c.stdDev() > 0.005 && c.stdDev() < 0.012);

  // Only the last kWindow presses count: the player settles from 30 ms late to 0.
  for (int k = 100; k < 100 + 32; k++) c.press(k * 0.5);
  CHECK_NEAR(c.mean(), 0.0, 1e-12);
}

static void TestScoresAndGrades(const std::string& dir) {
  std::printf("[unit] ITG dance points, grades, high scores\n");
  using game::Grade;
  using game::HighScore;
  using game::Judge;
  {  // Weights: W1 5, W2 4, W3 2, W4 0, W5 -6, Miss -12, held 5, mine -6; 5 per row/hold.
    Judge j;
    j.load({JNote(0, 1.0, 0), JNote(48, 2.0, 1), JNote(96, 3.0, 2), JNote(144, 4.0, 3, 5.0),
            JMine(192, 6.0, 0)}, 4);
    j.press(0, 1.0);     // W1 +5
    j.press(1, 2.03);    // W2 +4
    j.press(2, 3.06);    // W3 +2
    j.press(3, 4.0);     // W1 +5, then held +5
    j.update(5.5);
    j.press(0, 6.0);     // mine -6
    j.update(7.0);
    CHECK(j.finished());
    CHECK(j.score().possible_points == 25);
    CHECK(j.score().dance_points == 15);
    CHECK(j.score().percentX100() == 6000);
  }
  {  // The % is truncated, not rounded: 2/3 is 66.66%, and anything short of all is < 100%.
    game::Score s;
    s.possible_points = 3;
    s.dance_points = 2;
    CHECK(s.percentX100() == 6666);
    s.possible_points = 100000;
    s.dance_points = 99999;
    CHECK(s.percentX100() == 9999);
    s.dance_points = -50;
    CHECK(s.percentX100() == 0);
  }
  // Simply Love's tiers.
  CHECK(game::GradeTier(10000, false) == 1);
  CHECK(game::GradeTier(9999, false) == 2);
  CHECK(game::GradeTier(9900, false) == 2);
  CHECK(game::GradeTier(9400, false) == 5);
  CHECK(std::string(game::GradeTierName(game::GradeTier(9400, false))) == "S+");
  CHECK(std::string(game::GradeTierName(game::GradeTier(8299, false))) == "A-");
  CHECK(std::string(game::GradeTierName(game::GradeTier(5500, false))) == "C-");
  CHECK(std::string(game::GradeTierName(game::GradeTier(5499, false))) == "D");
  CHECK(std::string(game::GradeTierName(game::GradeTier(10000, true))) == "F");

  // Better: passed before failed, then the higher %.
  HighScore pass{7000, false, 10}, fail{9000, true, 50}, high{7100, false, 5};
  CHECK(game::Better(pass, fail) && !game::Better(fail, pass));
  CHECK(game::Better(high, pass) && !game::Better(pass, pass));

  // Keys name the song by its last two folders.
  ddr::SongEntry song;
  song.dir = "sd:/ddrwii/songs/My Pack/My Song/";
  ddr::ChartInfo chart;
  chart.difficulty = ddr::Difficulty::Edit;
  chart.description = "tab\there";
  const std::string key = game::ChartKey(ddr::PlayMode::Competitive, song, chart);
  CHECK(key == "Competitive\tMy Pack/My Song\tEdit\ttab here");
  song.dir = "C:/elsewhere/songs/My Pack/My Song/";
  CHECK(game::ChartKey(ddr::PlayMode::Competitive, song, chart) == key);
  CHECK(game::ChartKey(ddr::PlayMode::Casual, song, chart) != key);

  game::HighScoreTable t;
  CHECK(t.submit(key, fail));            // first play: the best, even failed
  CHECK(t.submit(key, pass));            // a pass beats a failed 90%
  CHECK(!t.submit(key, HighScore{6000, false, 99}));
  CHECK(t.find(key)->best.percent_x100 == 7000 && t.find(key)->best.max_combo == 10);
  CHECK(t.find(key)->plays == 3 && t.find(key)->clears == 2);
  chart.difficulty = ddr::Difficulty::Hard;
  chart.description.clear();
  const std::string key2 = game::ChartKey(ddr::PlayMode::Casual, song, chart);
  CHECK(t.submit(key2, HighScore{10000, false, 300}));
  CHECK(t.find("nope") == nullptr);

  // Write, read back; junk lines are skipped.
  game::HighScoreTable u;
  u.parse(t.write() + "junk\nCasual\tA/B\tHard\t\t100\t0\t1\t0\t0\n"
          "Casual\tA/B\tNotADifficulty\t\t100\t0\t1\t1\t1\r\n");
  CHECK(u.size() == 2);
  CHECK(u.find(key) && u.find(key)->best.percent_x100 == 7000 && u.find(key)->plays == 3);
  CHECK(u.find(key2) && u.find(key2)->best.percent_x100 == 10000 &&
        u.find(key2)->best.max_combo == 300 && !u.find(key2)->best.failed);

  const std::string path = dir + "/build/scores-test.txt";
  std::remove(path.c_str());
  game::HighScoreTable none;
  CHECK(!game::LoadHighScores(path, none));
  CHECK(game::SaveHighScores(path, t));
  CHECK(game::SaveHighScores(path, t));
  game::HighScoreTable v;
  CHECK(game::LoadHighScores(path, v) && v.size() == 2);
  std::remove(path.c_str());
}

int main(int argc, char** argv) {
  std::string dir = ".";
  int fuzzIterations = 3000;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--fuzz-iterations") && i + 1 < argc)
      fuzzIterations = std::atoi(argv[++i]);
    else
      dir = argv[i];
  }

  RunOracle(dir);
  TestMsdFile();
  TestTimingBasics();
  TestSmWarpConversion();
  TestNoteParsing();
  TestSscSplitTiming();
  TestPackIni();
  TestHeaderTags();
  TestInputFilter();
  TestWavFile();
  TestSongClock();
  TestJudge();
  TestMinesRollsLife();
  TestAutoplayCorpus(dir);
  TestTestSong(dir);
  TestSongLibrary(dir);
  TestConfig(dir);
  TestCalibration();
  TestScoresAndGrades(dir);
  RunFuzz(dir, fuzzIterations);

  std::printf("\n%d checks, %d failures%s\n", g_checks, g_failures,
              g_sink == 12345.678 ? " " : "");
  return g_failures == 0 ? 0 : 1;
}
