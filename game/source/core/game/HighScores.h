// SPDX-License-Identifier: GPL-3.0-or-later
// HighScores - letter grades and the machine's best score per chart, kept in
// sd:/ddrwii/scores.txt (doc 06 §7).
//
// Grades: Simply Love's 17 tiers (metrics.ini GradePercentTier01-17), from four stars at
// 100% down to D, and F for a failed play. As in StepMania, the tier is the first one whose
// percentage the score reaches.
//
// One best per chart, whatever the life bar setting or speed. A passed score beats a failed
// one, then the higher % wins. Autoplay scores aren't recorded (the caller's job). There are
// no player names or profiles yet: these are machine bests (a top 10 with names comes later,
// doc 06 §7).
//
// scores.txt is text, one chart per line, tab-separated:
//   pack/song folder, difficulty, description, percent x100, failed, max combo, plays, clears
// Older files had a play mode (Casual/Competitive) in front; those lines are still read, and
// a chart's two modes are merged (the better best, plays and clears added up). Lines that
// don't parse are skipped. It's written whole to a temp file and renamed, like
// config.ini. Original to ddr-wii. Portable C++17, no libogc: tested on the PC.
#pragma once

#include <map>
#include <string>

#include "../config/Config.h"
#include "../songs/SongLibrary.h"

namespace game {

constexpr int kNumGradeTiers = 17;   // tier 1 (four stars) .. 17 (D); 0 = failed (F)
int GradeTier(int percent_x100, bool failed);
const char* GradeTierName(int tier);   // "****", "***", "**", "*", "S+", ... "D", "F"

struct HighScore {
  int percent_x100 = 0;   // 9873 = 98.73% (Score::percentX100)
  bool failed = false;
  int max_combo = 0;
  int tier() const { return GradeTier(percent_x100, failed); }
};

// True when a beats b: passed before failed, then the higher %.
bool Better(const HighScore& a, const HighScore& b);

struct HighScoreEntry {
  HighScore best;
  int plays = 0;    // finished plays (a song stopped with START doesn't count)
  int clears = 0;   // plays that didn't fail
};

// "<pack>/<song>\t<difficulty>\t<description>". The song is named by its last two folders,
// so the key survives moving the songs root.
std::string ChartKey(const ddr::SongEntry& song, const ddr::ChartInfo& chart);

class HighScoreTable {
 public:
  // Counts a finished play. Returns true when it's the chart's new best (or its first).
  bool submit(const std::string& key, const HighScore& score);
  const HighScoreEntry* find(const std::string& key) const;
  size_t size() const { return map_.size(); }

  // Adds the lines of a scores.txt (an entry already there is replaced; old two-mode lines
  // for one chart are merged).
  void parse(const std::string& text);
  std::string write() const;

 private:
  std::map<std::string, HighScoreEntry> map_;
};

// Reads path into out. Returns false (out unchanged) if there is no file.
bool LoadHighScores(const std::string& path, HighScoreTable& out);
// Writes path + ".tmp", then renames it over path (ddr::WriteFileAtomic, util/TextFile.h).
bool SaveHighScores(const std::string& path, const HighScoreTable& table);

}  // namespace game
