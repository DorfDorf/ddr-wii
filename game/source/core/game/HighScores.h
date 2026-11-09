// SPDX-License-Identifier: GPL-3.0-or-later
// HighScores - letter grades and the machine's top 10 per chart, kept in
// sd:/ddrwii/scores.txt (doc 06 §7).
//
// Grades: Simply Love's 17 tiers (metrics.ini GradePercentTier01-17), from four stars at
// 100% down to D, and F for a failed play. As in StepMania, the tier is the first one whose
// percentage the score reaches.
//
// One top-10 list per chart (song and difficulty), whatever the speed or life bar setting, as
// in ITGmania. Each entry keeps a three-letter name, %, failed, max combo, the life bar setting
// and the speed. A passed score beats a failed one, then the higher % wins; a new score goes
// above an equal older one (StepMania's HighScoreList::AddHighScore does the same). Plays and
// clears are counted per chart. Autoplay scores aren't recorded (the caller's job).
//
// scores.txt is text, tab-separated. Per chart, a "chart" line, then its top 10 best first:
//   chart  pack/song  difficulty  description  plays  clears
//   score  pack/song  difficulty  description  name  percent x100  failed  max combo  life bar  speed
// The pack/song field always holds a '/', so it can't be mistaken for the "chart" or "score"
// tag. Two older formats are still read, each line becoming one entry named "---" with no
// speed (0) and its plays and clears added to the chart's:
//   pack/song  difficulty  description  percent x100  failed  max combo  plays  clears
//     (one best per chart, 2026-11-09 slices 1-2; life bar On, as the setting wasn't kept)
//   Casual|Competitive  <the same 8 fields>
//     (the play mode, M5 step 4; Casual had no bar (Off), Competitive had it On)
// Lines that don't parse are skipped. It's written whole to a temp file and renamed, like
// config.ini. Original to ddr-wii. Portable C++17, no libogc: tested on the PC.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../config/Config.h"
#include "../songs/SongLibrary.h"

namespace game {

constexpr int kNumGradeTiers = 17;   // tier 1 (four stars) .. 17 (D); 0 = failed (F)
int GradeTier(int percent_x100, bool failed);
const char* GradeTierName(int tier);   // "****", "***", "**", "*", "S+", ... "D", "F"

constexpr const char* kNoName = "---";   // entries from before names existed

// True for a name the game can enter: three letters A-Z.
bool ValidName(const std::string& name);

struct HighScore {
  int percent_x100 = 0;   // 9873 = 98.73% (Score::percentX100)
  bool failed = false;
  int max_combo = 0;
  ddr::LifeBar life_bar = ddr::LifeBar::On;
  double speed = 0;               // x-mod; 0 = not known (old lines)
  std::string name = kNoName;     // three letters, or kNoName
  uint32_t id = 0;                // set by HighScoreTable::submit, not saved; 0 = from the file
  int tier() const { return GradeTier(percent_x100, failed); }
};

// True when a beats b: passed before failed, then the higher %.
bool Better(const HighScore& a, const HighScore& b);

struct ChartScores {
  std::vector<HighScore> top;   // best first, at most HighScoreTable::kTopN
  int plays = 0;    // finished plays (a song quit from the pause menu doesn't count)
  int clears = 0;   // plays that didn't fail
  const HighScore* best() const { return top.empty() ? nullptr : &top[0]; }
};

// "<pack>/<song>\t<difficulty>\t<description>". The song is named by its last two folders,
// so the key survives moving the songs root.
std::string ChartKey(const ddr::SongEntry& song, const ddr::ChartInfo& chart);

class HighScoreTable {
 public:
  static constexpr int kTopN = 10;

  // Counts a finished play and adds it to the chart's top 10 if it makes it. Returns the
  // entry's id (score.id is ignored), or 0 when it didn't make the list.
  uint32_t submit(const std::string& key, HighScore score);
  // The entry's place in its chart's list, 0 = the best; -1 if it isn't there (any more).
  int rank(const std::string& key, uint32_t id) const;
  // Renames an entry submitted this session. False if it isn't in the list.
  bool setName(const std::string& key, uint32_t id, const std::string& name);

  const ChartScores* find(const std::string& key) const;
  size_t size() const { return map_.size(); }

  // Adds the lines of a scores.txt to the table (entries are merged into each chart's list,
  // plays and clears added up).
  void parse(const std::string& text);
  std::string write() const;

 private:
  void insert(ChartScores& c, HighScore score, bool wins_ties);   // keeps the order and the cut
  std::map<std::string, ChartScores> map_;
  uint32_t next_id_ = 1;
};

// Reads path into out. Returns false (out unchanged) if there is no file.
bool LoadHighScores(const std::string& path, HighScoreTable& out);
// Writes path + ".tmp", then renames it over path (ddr::WriteFileAtomic, util/TextFile.h).
bool SaveHighScores(const std::string& path, const HighScoreTable& table);

}  // namespace game
