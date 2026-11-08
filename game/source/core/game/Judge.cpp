// SPDX-License-Identifier: GPL-3.0-or-later
#include "Judge.h"

#include <algorithm>
#include <cmath>

namespace game {
namespace {

// Dance points (doc 06 §7): Marvelous/Perfect full, Great half, the rest nothing; a held
// hold full.
constexpr int kRowPoints[kNumGrades] = {0, 2, 2, 1, 0, 0, 0};
constexpr int kMaxRowPoints = 2;
constexpr int kHoldPoints = 2;

bool IsJudged(const ddr::Note& n) {
  return n.judgable && (n.type == ddr::NoteType::Tap || n.IsHold());
}

}  // namespace

const char* GradeName(Grade g) {
  switch (g) {
    case Grade::Marvelous: return "Marvelous";
    case Grade::Perfect: return "Perfect";
    case Grade::Great: return "Great";
    case Grade::Good: return "Good";
    case Grade::Boo: return "Boo";
    case Grade::Miss: return "Miss";
    default: return "";
  }
}

Grade GradeForError(double e, const Windows& w) {
  if (e <= w.marvelous) return Grade::Marvelous;
  if (e <= w.perfect) return Grade::Perfect;
  if (e <= w.great) return Grade::Great;
  if (e <= w.good) return Grade::Good;
  if (e <= w.boo) return Grade::Boo;
  return Grade::None;
}

double Score::percent() const {
  return possible_points > 0 ? 100.0 * std::max(dance_points, 0) / possible_points : 0;
}

double Score::offsetStdDev() const {
  if (hits < 2) return 0;
  const double m = meanOffset();
  return std::sqrt(std::max(offset_sq_sum / hits - m * m, 0.0));
}

void Judge::load(const std::vector<ddr::Note>& notes, int lanes, const Options& opt) {
  *this = Judge();
  opt_ = opt;
  lanes_ = std::min(std::max(lanes, 0), kMaxLanes);
  for (const ddr::Note& n : notes) {
    if (n.judgable && n.type == ddr::NoteType::Mine && n.lane >= 0 && n.lane < lanes_) {
      MineState m;
      m.time = n.time;
      m.row = n.row;
      m.lane = n.lane;
      mines_.push_back(m);
      continue;
    }
    if (!IsJudged(n) || n.lane < 0 || n.lane >= lanes_) continue;
    NoteState s;
    s.time = n.time;
    s.end_time = n.IsHold() ? n.endTime : n.time;
    s.row = n.row;
    s.end_row = n.IsHold() ? n.endRow : n.row;
    s.lane = n.lane;
    s.hold = n.IsHold();
    s.roll = n.type == ddr::NoteType::RollHead;
    if (rows_.empty() || notes_.back().row != n.row) {
      Row r;
      r.first = static_cast<int>(notes_.size());
      rows_.push_back(r);
    }
    s.row_index = static_cast<int>(rows_.size() - 1);
    rows_.back().count++;
    rows_.back().pending++;
    lane_notes_[n.lane].push_back(static_cast<int>(notes_.size()));
    if (s.hold) holds_total_++;
    notes_.push_back(s);
  }
  std::stable_sort(mines_.begin(), mines_.end(),
                   [](const MineState& a, const MineState& b) { return a.time < b.time; });
  score_.possible_points =
      static_cast<int>(rows_.size()) * kMaxRowPoints + static_cast<int>(holds_total_) * kHoldPoints;
}

void Judge::press(int lane, double t) {
  update(t);
  if (lane < 0 || lane >= lanes_) return;
  down_[lane] = true;
  down_since_[lane] = t;

  // The closest pending mine in the lane within the mine window, if any.
  int mine = -1;
  double mine_err = 0;
  for (size_t k = mine_next_; k < mines_.size() && mines_[k].time <= t + opt_.windows.mine; k++) {
    const MineState& m = mines_[k];
    const double err = t - m.time;
    if (m.state != MineState::kPending || m.lane != lane || std::fabs(err) > opt_.windows.mine) continue;
    if (mine < 0 || std::fabs(err) < std::fabs(mine_err)) {
      mine = static_cast<int>(k);
      mine_err = err;
    }
  }

  // Pressing again during a hold in this lane grabs it back; during a roll it's the re-press.
  for (ActiveHold& h : active_) {
    if (notes_[h.note].lane == lane) {
      h.released_at = notes_[h.note].roll ? t : -1;
      if (mine >= 0) hitMine(mine);
      return;
    }
  }

  // The closest unjudged note in the lane within the Boo window (the earlier on a tie).
  const std::vector<int>& list = lane_notes_[lane];
  size_t& next = lane_next_[lane];
  while (next < list.size() && notes_[list[next]].grade != Grade::None) next++;
  int best = -1;
  double best_err = 0;
  for (size_t k = next; k < list.size(); k++) {
    const NoteState& n = notes_[list[k]];
    if (n.time > t + opt_.windows.boo) break;
    if (n.grade != Grade::None) continue;
    const double err = t - n.time;
    if (std::fabs(err) > opt_.windows.boo) continue;
    if (best < 0 || std::fabs(err) < std::fabs(best_err)) {
      best = list[k];
      best_err = err;
    }
  }
  // As in StepMania, a step takes whichever is closer, the note or the mine.
  if (mine >= 0 && (best < 0 || std::fabs(mine_err) < std::fabs(best_err))) {
    hitMine(mine);
    return;
  }
  if (best < 0) return;   // a stray press

  NoteState& n = notes_[best];
  const Grade g = GradeForError(std::fabs(best_err), opt_.windows);
  const bool bad = Worse(g, Grade::Great);
  if (opt_.early_hit_rule && bad && best_err < 0 && rows_[n.row_index].count == 1) {
    // Early and bad: remember the first such hit, keep the note for a better one.
    if (n.early == Grade::None) {
      n.early = g;
      n.early_offset = best_err;
      events_.push_back({JudgeEvent::kEarly, g, HoldResult::None, n.lane, n.row, best_err});
    }
    return;
  }
  // After an early hit, only a re-hit of Great or better replaces it.
  if (n.early != Grade::None && bad) return;
  judgeNote(best, g, best_err, t);
}

void Judge::release(int lane, double t) {
  update(t);
  if (lane < 0 || lane >= lanes_) return;
  down_[lane] = false;
  for (ActiveHold& h : active_)   // rolls track presses, not releases
    if (notes_[h.note].lane == lane && !notes_[h.note].roll && h.released_at < 0) h.released_at = t;
}

void Judge::update(double t) {
  // Mines up to t: hit if the lane is down as they pass (down_ is exact here: press and
  // release update to their own time first), avoided once past the mine window.
  for (size_t k = mine_next_; k < mines_.size() && mines_[k].time <= t; k++) {
    MineState& m = mines_[k];
    if (m.state != MineState::kPending) continue;
    if (down_[m.lane] && down_since_[m.lane] <= m.time) {   // already down when it passed
      hitMine(static_cast<int>(k));
    } else if (t > m.time + opt_.windows.mine) {
      m.state = MineState::kAvoided;
      score_.mines_avoided++;
      mines_done_++;
    }
  }
  while (mine_next_ < mines_.size() && mines_[mine_next_].state != MineState::kPending) mine_next_++;

  // Rows whose notes are past the Boo window: unhit notes get their early grade or Miss.
  while (row_next_ < rows_.size()) {
    const Row& r = rows_[row_next_];
    if (r.pending == 0) {
      row_next_++;
      continue;
    }
    const double pass = notes_[r.first].time + opt_.windows.boo;
    if (pass >= t) break;
    for (int i = r.first; i < r.first + r.count; i++) {
      NoteState& n = notes_[i];
      if (n.grade != Grade::None) continue;
      if (n.early != Grade::None) {
        judgeNote(i, n.early, n.early_offset, pass);
      } else {
        if (down_[n.lane]) score_.held_misses++;
        judgeNote(i, Grade::Miss, 0, pass);
      }
    }
    row_next_++;
  }

  // Holds: let go if released for longer than hold_ok before the tail, else OK at the tail.
  // Rolls: released_at is the last press; let go if the next one is later than roll_ok.
  for (size_t k = 0; k < active_.size();) {
    const ActiveHold h = active_[k];
    const NoteState& n = notes_[h.note];
    const double drop = h.released_at + (n.roll ? opt_.windows.roll_ok : opt_.windows.hold_ok);
    HoldResult res = HoldResult::None;
    if (h.released_at >= 0 && drop < n.end_time && t >= drop)
      res = HoldResult::LetGo;
    else if (t >= n.end_time)
      res = HoldResult::Held;
    if (res == HoldResult::None) {
      k++;
      continue;
    }
    active_.erase(active_.begin() + static_cast<long>(k));
    finishHold(h.note, res);
  }
}

void Judge::judgeNote(int i, Grade g, double offset, double t) {
  NoteState& n = notes_[i];
  n.grade = g;
  n.offset = offset;
  if (g != Grade::Miss) {
    score_.hits++;
    score_.offset_sum += offset;
    score_.offset_sq_sum += offset * offset;
  }
  events_.push_back({JudgeEvent::kNote, g, HoldResult::None, n.lane, n.row, offset});

  if (n.hold) {
    if (g == Grade::Miss)
      finishHold(i, HoldResult::LetGo);
    else
      active_.push_back({i, n.roll ? t : (down_[n.lane] ? -1.0 : t)});
  }

  Row& r = rows_[n.row_index];
  if (Worse(g, r.worst)) r.worst = g;
  if (--r.pending == 0) finishRow(n.row_index);
}

void Judge::finishRow(int ri) {
  const Row& r = rows_[ri];
  score_.rows[static_cast<int>(r.worst)]++;
  score_.dance_points += kRowPoints[static_cast<int>(r.worst)];
  if (Worse(r.worst, Grade::Great)) {
    score_.combo = 0;   // Good breaks combo too (DDR rule, doc 06 §3.4)
  } else {
    score_.combo++;
    score_.max_combo = std::max(score_.max_combo, score_.combo);
  }
  rows_done_++;
  events_.push_back({JudgeEvent::kRow, r.worst, HoldResult::None, -1, notes_[r.first].row, 0});
}

void Judge::finishHold(int i, HoldResult h) {
  NoteState& n = notes_[i];
  n.hold_result = h;
  if (h == HoldResult::Held) {
    score_.holds_held++;
    score_.dance_points += kHoldPoints;
  } else {
    score_.holds_let_go++;
  }
  holds_done_++;
  events_.push_back({JudgeEvent::kHold, n.grade, h, n.lane, n.row, 0});
}

void Judge::hitMine(int k) {
  MineState& m = mines_[k];
  m.state = MineState::kHit;
  score_.mines_hit++;
  mines_done_++;
  events_.push_back({JudgeEvent::kMine, Grade::None, HoldResult::None, m.lane, m.row, 0});
}

std::vector<AutoStep> MakeAutoplay(const Judge& judge) {
  const std::vector<Judge::NoteState>& notes = judge.notes();
  const std::vector<Judge::MineState>& mines = judge.mines();
  const double mine_w = judge.options().windows.mine;
  std::vector<AutoStep> steps;
  steps.reserve(notes.size() * 2);
  // Per note, the time its lane must be free again: the lane's next note, or a mine's
  // window. Walk backwards keeping the next note time and next mine time per lane.
  std::vector<double> next(notes.size(), 1e300);
  std::vector<double> lane_next(Judge::kMaxLanes, 1e300);
  for (size_t k = notes.size(); k-- > 0;) {
    next[k] = lane_next[notes[k].lane];
    lane_next[notes[k].lane] = notes[k].time;
  }
  for (size_t k = 0; k < notes.size(); k++) {
    const Judge::NoteState& n = notes[k];
    auto it = std::upper_bound(mines.begin(), mines.end(), n.time,
                               [](double t, const Judge::MineState& m) { return t < m.time; });
    for (; it != mines.end() && it->time - mine_w < next[k]; ++it)
      if (it->lane == n.lane) {
        next[k] = it->time - mine_w;
        break;
      }
  }
  for (size_t k = 0; k < notes.size(); k++) {
    const Judge::NoteState& n = notes[k];
    steps.push_back({n.time, n.lane, true});
    if (n.roll) {   // tap through the roll every 0.1 s
      double t = n.time;
      while (t + 0.1 < n.end_time) {
        steps.push_back({t + 0.05, n.lane, false});
        t += 0.1;
        steps.push_back({t, n.lane, true});
      }
    }
    const double up = n.hold ? n.end_time : n.time + std::max(0.001, std::min(0.05, (next[k] - n.time) / 2));
    steps.push_back({up, n.lane, false});
  }
  std::stable_sort(steps.begin(), steps.end(), [](const AutoStep& a, const AutoStep& b) {
    if (a.time != b.time) return a.time < b.time;
    return !a.pressed && b.pressed;
  });
  return steps;
}

}  // namespace game
