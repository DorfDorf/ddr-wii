// SPDX-License-Identifier: GPL-3.0-or-later
// Judge - grades one player's steps against their chart, and keeps their score
// (doc 06 §3.4 and §7). One Judge per player (doc 06 §11.1).
//
// Times are song seconds on the chart's clock (ddr::Note::time). Turning a LaneEvent's
// timestamp into song seconds, global offset included, is the caller's job.
//
// Rules:
//   - A press takes the closest unjudged note in its lane within the Boo window.
//   - Early-hit rule (the idea from ITGmania v1.3.0 Player.cpp, re-implemented): an early
//     hit worse than Great on a one-note row doesn't use up the note. It is remembered, a
//     better re-hit still counts, and if the note passes unhit it keeps the early grade
//     instead of a Miss.
//   - Jumps: notes are graded one by one, but combo and score count the row once, with
//     its worst grade, when every note in it is judged.
//   - Holds (rolls too, for now): the head is a tap. Once it's hit the hold is OK as long
//     as the lane is never released for longer than hold_ok before the tail.
//   - Only taps and hold/roll heads that are judgable (not in warps or #FAKES) are judged.
//     Mines, lifts, fakes and keysounds are ignored for now.
// Portable C++17, no libogc: tested on the PC (game/tests).
#pragma once

#include <cstdint>
#include <vector>

#include "../chart/Chart.h"

namespace game {

enum class Grade : uint8_t { None, Marvelous, Perfect, Great, Good, Boo, Miss };
constexpr int kNumGrades = 7;
const char* GradeName(Grade g);
// True when a is a worse grade than b (None counts as best).
inline bool Worse(Grade a, Grade b) { return static_cast<int>(a) > static_cast<int>(b); }

enum class HoldResult : uint8_t { None, Held, LetGo };

// ± seconds from the note (StepMania defaults, doc 06 §3.4).
struct Windows {
  double marvelous = 0.0225;
  double perfect = 0.045;
  double great = 0.090;
  double good = 0.135;
  double boo = 0.180;
  double hold_ok = 0.250;   // longest release allowed during a hold
};

// The grade for an error of abs_seconds, or None when it's outside the Boo window.
Grade GradeForError(double abs_seconds, const Windows& w);

struct Score {
  int rows[kNumGrades] = {};   // judged rows by their worst grade (a jump counts once)
  int holds_held = 0;
  int holds_let_go = 0;
  int combo = 0;
  int max_combo = 0;
  int held_misses = 0;         // notes missed while their lane was held (stuck/dead panel?)
  int dance_points = 0;
  int possible_points = 0;     // for the whole chart, set at load
  int hits = 0;                // notes hit (misses left out), for the timing stats
  double offset_sum = 0;       // seconds, negative = early
  double offset_sq_sum = 0;

  // Dance points as a percentage of the whole chart (doc 06 §7).
  double percent() const;
  double meanOffset() const { return hits ? offset_sum / hits : 0; }
  double offsetStdDev() const;
};

// What happened, for the screen: per-note grades for the ms error display, early hits,
// finished rows for the judgement text and combo, and hold results.
struct JudgeEvent {
  enum Kind : uint8_t { kNote, kEarly, kRow, kHold };
  Kind kind;
  Grade grade;         // kNote/kEarly: the note's grade; kRow: the row's grade
  HoldResult hold;     // kHold
  int lane;            // kNote/kEarly/kHold; -1 for kRow
  int row;             // chart row (48 per beat)
  double offset;       // kNote/kEarly: hit time - note time, seconds (negative = early)
};

class Judge {
 public:
  static constexpr int kMaxLanes = 16;

  struct Options {
    Windows windows;
    bool early_hit_rule = true;
  };

  // State of one judged note.
  struct NoteState {
    double time = 0, end_time = 0;
    int row = 0;
    int lane = 0;
    int row_index = 0;           // into the row table
    bool hold = false;
    Grade grade = Grade::None;   // final grade, None until judged
    double offset = 0;
    Grade early = Grade::None;   // remembered early hit (early-hit rule)
    double early_offset = 0;
    HoldResult hold_result = HoldResult::None;
  };

  // Takes the judgable notes of a chart (ddr::Chart::notes) with lanes < `lanes`.
  void load(const std::vector<ddr::Note>& notes, int lanes, const Options& opt);
  void load(const std::vector<ddr::Note>& notes, int lanes) { load(notes, lanes, Options()); }

  // Feed presses and releases in time order. Each first brings the judge up to t.
  void press(int lane, double t);
  void release(int lane, double t);
  // Call every frame with the current song time: misses, hold results.
  void update(double t);

  bool finished() const { return rows_done_ == rows_.size() && holds_done_ == holds_total_; }
  const Score& score() const { return score_; }
  const std::vector<NoteState>& notes() const { return notes_; }
  const Options& options() const { return opt_; }
  bool laneDown(int lane) const { return lane >= 0 && lane < lanes_ && down_[lane]; }

  // Events since the caller last cleared them.
  std::vector<JudgeEvent>& events() { return events_; }

 private:
  struct Row {
    int first = 0, count = 0, pending = 0;
    Grade worst = Grade::None;
  };
  struct ActiveHold {
    int note;
    double released_at;   // < 0 while the lane is held
  };

  void judgeNote(int i, Grade g, double offset, double t);
  void finishRow(int r);
  void finishHold(int i, HoldResult h);

  Options opt_;
  int lanes_ = 0;
  std::vector<NoteState> notes_;
  std::vector<Row> rows_;
  std::vector<int> lane_notes_[kMaxLanes];   // note indices per lane, in time order
  size_t lane_next_[kMaxLanes] = {};         // first maybe-unjudged entry per lane
  bool down_[kMaxLanes] = {};
  std::vector<ActiveHold> active_;
  size_t row_next_ = 0;                      // first row not yet done
  size_t rows_done_ = 0;
  size_t holds_total_ = 0, holds_done_ = 0;
  Score score_;
  std::vector<JudgeEvent> events_;
};

// Autoplay: a press at every judged note's time, held to the tail for holds and briefly
// for taps. Sorted by time, releases before presses at the same moment. Feeding these to
// a Judge must give 100% Marvelous (doc 05 M4c).
struct AutoStep {
  double time;
  int lane;
  bool pressed;
};
std::vector<AutoStep> MakeAutoplay(const Judge& judge);

}  // namespace game
