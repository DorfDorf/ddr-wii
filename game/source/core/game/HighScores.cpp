// SPDX-License-Identifier: GPL-3.0-or-later
#include "HighScores.h"

#include <algorithm>
#include <cstdio>

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

bool ParseLifeBar(const std::string& s, ddr::LifeBar& out) {
  if (s == "Off") out = ddr::LifeBar::Off;
  else if (s == "On") out = ddr::LifeBar::On;
  else if (s == "NoFail") out = ddr::LifeBar::NoFail;
  else return false;
  return true;
}

bool ValidKey(const std::vector<std::string>& f, size_t at) {
  return f[at].find('/') != std::string::npos &&
         ddr::StringToDifficulty(f[at + 1]) != ddr::Difficulty::Invalid;
}

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

bool ValidName(const std::string& name) {
  if (name.size() != 3) return false;
  for (char c : name)
    if (c < 'A' || c > 'Z') return false;
  return true;
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

// A new play goes above the first entry it isn't worse than, so it wins a tie; a line read
// from the file goes below its equals, so the file's order is kept.
void HighScoreTable::insert(ChartScores& c, HighScore score, bool wins_ties) {
  size_t i = 0;
  while (i < c.top.size() && (wins_ties ? Better(c.top[i], score) : !Better(score, c.top[i]))) ++i;
  if (i >= (size_t)kTopN) return;
  c.top.insert(c.top.begin() + i, std::move(score));
  if (c.top.size() > (size_t)kTopN) c.top.resize(kTopN);
}

uint32_t HighScoreTable::submit(const std::string& key, HighScore score) {
  ChartScores& c = map_[key];
  c.plays++;
  if (!score.failed) c.clears++;
  score.id = next_id_++;
  const uint32_t id = score.id;
  insert(c, std::move(score), true);
  return rank(key, id) >= 0 ? id : 0;
}

int HighScoreTable::rank(const std::string& key, uint32_t id) const {
  if (!id) return -1;
  const ChartScores* c = find(key);
  if (!c) return -1;
  for (size_t i = 0; i < c->top.size(); ++i)
    if (c->top[i].id == id) return (int)i;
  return -1;
}

bool HighScoreTable::setName(const std::string& key, uint32_t id, const std::string& name) {
  const int r = rank(key, id);
  if (r < 0) return false;
  map_[key].top[r].name = name;
  return true;
}

const ChartScores* HighScoreTable::find(const std::string& key) const {
  auto it = map_.find(key);
  return it == map_.end() ? nullptr : &it->second;
}

void HighScoreTable::parse(const std::string& text) {
  std::vector<std::string> lines;
  ddr::Split(text, '\n', lines);
  for (std::string& line : lines) {
    ddr::TrimRight(line, "\r");
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> f;
    ddr::Split(line, '\t', f, false);   // the description may be empty
    if (f.size() == 6 && f[0] == "chart" && ValidKey(f, 1)) {
      const int plays = ddr::StringToInt(f[4]), clears = ddr::StringToInt(f[5]);
      if (plays < 0 || clears < 0) continue;
      ChartScores& c = map_[f[1] + "\t" + f[2] + "\t" + f[3]];
      c.plays += plays;
      c.clears += clears;
      continue;
    }
    if (f.size() == 10 && f[0] == "score" && ValidKey(f, 1)) {
      HighScore s;
      s.name = ValidName(f[4]) ? f[4] : kNoName;
      s.percent_x100 = ddr::StringToInt(f[5]);
      s.failed = f[6] == "1";
      s.max_combo = ddr::StringToInt(f[7]);
      if (!ParseLifeBar(f[8], s.life_bar)) continue;
      s.speed = ddr::StringToDouble(f[9]);
      if (s.percent_x100 < 0 || s.percent_x100 > 10000 || !(s.speed >= 0 && s.speed <= 8)) continue;
      insert(map_[f[1] + "\t" + f[2] + "\t" + f[3]], std::move(s), false);
      continue;
    }
    // The older one-best-per-chart lines, with or without a play mode in front.
    HighScore s;
    if (f.size() == 9 && (f[0] == "Casual" || f[0] == "Competitive")) {
      s.life_bar = f[0] == "Casual" ? ddr::LifeBar::Off : ddr::LifeBar::On;
      f.erase(f.begin());
    } else if (f.size() != 8) {
      continue;
    }
    if (!ValidKey(f, 0)) continue;
    s.percent_x100 = ddr::StringToInt(f[3]);
    s.failed = f[4] == "1";
    s.max_combo = ddr::StringToInt(f[5]);
    const int plays = ddr::StringToInt(f[6]), clears = ddr::StringToInt(f[7]);
    if (s.percent_x100 < 0 || s.percent_x100 > 10000 || plays < 1 || clears < 0) continue;
    ChartScores& c = map_[f[0] + "\t" + f[1] + "\t" + f[2]];
    c.plays += plays;
    c.clears += clears;
    insert(c, std::move(s), false);
  }
  // A list without its chart line (a hand-edited file): count its entries as plays.
  for (auto& kv : map_) {
    ChartScores& c = kv.second;
    int passes = 0;
    for (const HighScore& s : c.top) passes += s.failed ? 0 : 1;
    c.plays = std::max(c.plays, (int)c.top.size());
    c.clears = std::max(std::min(c.clears, c.plays), passes);
  }
}

std::string HighScoreTable::write() const {
  std::string out =
      "# ddr-wii high scores (tab-separated). Per chart, a chart line, then its top 10:\n"
      "# chart  pack/song  difficulty  description  plays  clears\n"
      "# score  pack/song  difficulty  description  name  percent x100  failed  max combo  "
      "life bar  speed\n";
  char buf[96];
  for (const auto& kv : map_) {
    const ChartScores& c = kv.second;
    std::snprintf(buf, sizeof buf, "\t%d\t%d\n", c.plays, c.clears);
    out += "chart\t" + kv.first + buf;
    for (const HighScore& s : c.top) {
      std::snprintf(buf, sizeof buf, "\t%d\t%d\t%d\t%s\t%.2f\n", s.percent_x100, s.failed ? 1 : 0,
                    s.max_combo, ddr::LifeBarToString(s.life_bar), s.speed);
      out += "score\t" + kv.first + "\t" + (ValidName(s.name) ? s.name : kNoName) + buf;
    }
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
