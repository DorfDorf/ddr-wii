// SPDX-License-Identifier: GPL-3.0-or-later
//
// ddr-wii -- a StepMania-style rhythm game for Wii homebrew.
// Copyright (C) 2026 ddr-wii contributors
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. See the LICENSE file in the repository root.
//
// cache.txt (SongCache in SongLibrary.h). Text, one song per line, fields
// separated by tabs:
//   ok:   O dir sig folder simfile music banner title subtitle artist
//         titleTranslit subtitleTranslit sampleStart sampleLength bpmMin
//         bpmMax bpmRandom lengthSeconds nCharts, then per chart: index
//         difficulty meter description steps jumps holds rolls mines
//   skip: S dir sig warning
// The first line is "ddr-wii song cache <version>". Original to ddr-wii.

#include <cstdio>

#include "../util/TextFile.h"
#include "../chart/StringUtil.h"
#include "SongLibrary.h"

namespace ddr {
namespace {

constexpr int kSongFields = 19;   // through nCharts
constexpr int kChartFields = 9;

std::string Header() { return "ddr-wii song cache " + std::to_string(SongCache::kVersion); }

void Num(std::string& out, double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "\t%.6f", v);
  out += buf;
}
void Num(std::string& out, int v) { out += "\t" + std::to_string(v); }
void Str(std::string& out, const std::string& s) { out += "\t" + CleanField(s); }

}  // namespace

void SongCache::Parse(const std::string& text) {
  items.clear();
  std::vector<std::string> lines;
  Split(text, '\n', lines);
  if (lines.empty() || Trimmed(lines[0]) != Header()) return;
  for (size_t li = 1; li < lines.size(); li++) {
    std::string line = lines[li];
    TrimRight(line, "\r");
    std::vector<std::string> f;
    Split(line, '\t', f, false);   // empty fields are allowed
    if (f.size() < 4 || f[1].empty() || f[2].empty()) continue;
    Item item;
    item.sig = f[2];
    if (f[0] == "S" && f.size() == 4) {
      item.warning = f[3];
      items[f[1]] = item;
      continue;
    }
    if (f[0] != "O" || f.size() < kSongFields) continue;
    SongEntry& s = item.song;
    s.dir = f[1];
    s.folder = f[3];
    s.simfile = f[4];
    s.music = f[5];
    s.banner = f[6];
    s.title = f[7];
    s.subtitle = f[8];
    s.artist = f[9];
    s.titleTranslit = f[10];
    s.subtitleTranslit = f[11];
    s.sampleStart = StringToDouble(f[12]);
    s.sampleLength = StringToDouble(f[13]);
    s.bpmMin = StringToDouble(f[14]);
    s.bpmMax = StringToDouble(f[15]);
    s.bpmRandom = f[16] == "1";
    s.lengthSeconds = StringToDouble(f[17]);
    const int n = StringToInt(f[18]);
    if (n < 1 || f.size() != (size_t)(kSongFields + n * kChartFields)) continue;
    if (s.simfile.empty() || s.music.empty()) continue;
    bool good = true;
    for (int c = 0; c < n; c++) {
      const std::string* p = &f[kSongFields + c * kChartFields];
      ChartInfo ci;
      ci.index = StringToInt(p[0]);
      const int d = StringToInt(p[1]);
      if (d < 0 || d >= (int)Difficulty::Invalid || ci.index < 0) good = false;
      ci.difficulty = static_cast<Difficulty>(d);
      ci.meter = StringToInt(p[2]);
      ci.description = p[3];
      ci.steps = StringToInt(p[4]);
      ci.jumps = StringToInt(p[5]);
      ci.holds = StringToInt(p[6]);
      ci.rolls = StringToInt(p[7]);
      ci.mines = StringToInt(p[8]);
      s.charts.push_back(ci);
    }
    if (!good) continue;
    item.ok = true;
    items[f[1]] = item;
  }
}

std::string SongCache::Write() const {
  std::string out = Header() + "\n";
  for (const auto& kv : items) {
    const Item& it = kv.second;
    if (!it.ok) {
      out += "S\t" + CleanField(kv.first) + "\t" + it.sig + "\t" + CleanField(it.warning) + "\n";
      continue;
    }
    const SongEntry& s = it.song;
    out += "O\t" + CleanField(kv.first) + "\t" + it.sig;
    Str(out, s.folder);
    Str(out, s.simfile);
    Str(out, s.music);
    Str(out, s.banner);
    Str(out, s.title);
    Str(out, s.subtitle);
    Str(out, s.artist);
    Str(out, s.titleTranslit);
    Str(out, s.subtitleTranslit);
    Num(out, s.sampleStart);
    Num(out, s.sampleLength);
    Num(out, s.bpmMin);
    Num(out, s.bpmMax);
    Num(out, s.bpmRandom ? 1 : 0);
    Num(out, s.lengthSeconds);
    Num(out, (int)s.charts.size());
    for (const ChartInfo& c : s.charts) {
      Num(out, c.index);
      Num(out, (int)c.difficulty);
      Num(out, c.meter);
      Str(out, c.description);
      Num(out, c.steps);
      Num(out, c.jumps);
      Num(out, c.holds);
      Num(out, c.rolls);
      Num(out, c.mines);
    }
    out += "\n";
  }
  return out;
}

bool LoadSongCache(const std::string& path, SongCache& out) {
  std::string text;
  if (!ReadFileToString(path, text)) return false;
  out.Parse(text);
  return true;
}

bool SaveSongCache(const std::string& path, const SongCache& cache) {
  return WriteFileAtomic(path, cache.Write());
}

}  // namespace ddr
