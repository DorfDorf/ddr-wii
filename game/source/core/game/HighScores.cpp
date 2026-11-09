// SPDX-License-Identifier: GPL-3.0-or-later
#include "HighScores.h"

#include <cstdio>
#include <vector>

#include "../util/TextFile.h"
#include "../chart/StringUtil.h"

namespace game {
namespace {

// Simply Love metrics.ini GradePercentTier01-16, x100; tier 17 (D) is everything below.
constexpr int kTierMin[kNumGradeTiers - 1] = {10000, 9900, 9800, 9600, 9400, 9200, 8900, 8600,
                                              8300,  8000, 7600, 7200, 6800, 6400, 6000, 5500};
const char* const kTierNames[kNumGradeTiers + 1] = {
    "F",  "****", "***", "**", "*",  "S+", "S",  "S-", "A+",
    "A",  "A-",   "B+",  "B",  "B-", "C+", "C",  "C-", "D"};

}  // namespace

int GradeTier(int percent_x100, bool failed) {
  if (failed) return 0;
  for (int i = 0; i < kNumGradeTiers - 1; ++i)
    if (percent_x100 >= kTierMin[i]) return i + 1;
  return kNumGradeTiers;
}

const char* GradeTierName(int tier) {
  return tier >= 0 && tier <= kNumGradeTiers ? kTierNames[tier] : "";
}

bool Better(const HighScore& a, const HighScore& b) {
  if (a.failed != b.failed) return !a.failed;
  return a.percent_x100 > b.percent_x100;
}

std::string ChartKey(const ddr::SongEntry& song, const ddr::ChartInfo& chart) {
  // dir is "<root>/<pack>/<song>/": keep "<pack>/<song>".
  std::vector<std::string> parts;
  ddr::Split(song.dir, '/', parts);
  std::string folder = parts.size() >= 2 ? parts[parts.size() - 2] + "/" + parts.back()
                                         : song.dir;
  return ddr::CleanField(folder) + "\t" + ddr::DifficultyToString(chart.difficulty) + "\t" +
         ddr::CleanField(chart.description);
}

bool HighScoreTable::submit(const std::string& key, const HighScore& score) {
  auto it = map_.find(key);
  const bool first = it == map_.end();
  HighScoreEntry& e = map_[key];
  e.plays++;
  if (!score.failed) e.clears++;
  if (first || Better(score, e.best)) {
    e.best = score;
    return true;
  }
  return false;
}

const HighScoreEntry* HighScoreTable::find(const std::string& key) const {
  auto it = map_.find(key);
  return it == map_.end() ? nullptr : &it->second;
}

void HighScoreTable::parse(const std::string& text) {
  std::vector<std::string> lines;
  ddr::Split(text, '\n', lines);
  std::map<std::string, HighScoreEntry> old;   // lines with a play mode, merged per chart
  for (std::string& line : lines) {
    ddr::TrimRight(line, "\r");
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> f;
    ddr::Split(line, '\t', f, false);   // the description may be empty
    const bool with_mode = f.size() == 9 && (f[0] == "Casual" || f[0] == "Competitive");
    if (with_mode)
      f.erase(f.begin());
    else if (f.size() != 8)
      continue;
    if (f[0].empty() || ddr::StringToDifficulty(f[1]) == ddr::Difficulty::Invalid) continue;
    HighScoreEntry e;
    e.best.percent_x100 = ddr::StringToInt(f[3]);
    e.best.failed = f[4] == "1";
    e.best.max_combo = ddr::StringToInt(f[5]);
    e.plays = ddr::StringToInt(f[6]);
    e.clears = ddr::StringToInt(f[7]);
    if (e.best.percent_x100 < 0 || e.best.percent_x100 > 10000 || e.plays < 1) continue;
    const std::string key = f[0] + "\t" + f[1] + "\t" + f[2];
    if (!with_mode) {
      map_[key] = e;
      continue;
    }
    auto it = old.find(key);
    if (it == old.end()) {
      old[key] = e;
      continue;
    }
    HighScoreEntry& m = it->second;
    if (Better(e.best, m.best)) m.best = e.best;
    m.plays += e.plays;
    m.clears += e.clears;
  }
  for (const auto& kv : old) map_.insert(kv);   // a new-format line for the chart wins
}

std::string HighScoreTable::write() const {
  std::string out =
      "# ddr-wii high scores, one chart per line (tab-separated):\n"
      "# pack/song, difficulty, description, percent x100, failed, max combo, plays, clears\n";
  char buf[96];
  for (const auto& kv : map_) {
    const HighScoreEntry& e = kv.second;
    std::snprintf(buf, sizeof buf, "\t%d\t%d\t%d\t%d\t%d\n", e.best.percent_x100,
                  e.best.failed ? 1 : 0, e.best.max_combo, e.plays, e.clears);
    out += kv.first;
    out += buf;
  }
  return out;
}

bool LoadHighScores(const std::string& path, HighScoreTable& out) {
  std::string text;
  if (!ddr::ReadFileToString(path, text)) return false;
  out.parse(text);
  return true;
}

bool SaveHighScores(const std::string& path, const HighScoreTable& table) {
  return ddr::WriteFileAtomic(path, table.write());
}

}  // namespace game
