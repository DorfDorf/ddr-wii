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
  // (as StepMania does), so use row-aligned beats.
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
  // ",,," is one measure break, as in StepMania.
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
  RunFuzz(dir, fuzzIterations);

  std::printf("\n%d checks, %d failures%s\n", g_checks, g_failures,
              g_sink == 12345.678 ? " " : "");
  return g_failures == 0 ? 0 : 1;
}
