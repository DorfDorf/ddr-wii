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
// LifeMeter - ITG's life bar, for a player with the life bar On or On, no fail (doc 06 §3.4;
// M5 step 4, no fail in target menu flow slice 2). Port of ITGmania v1.3.0
// src/LifeMeterBar.cpp (normal drain only) with the values Simply Love sets for its ITG mode.
// See LifeMeter.cpp for the notice and what changed.

#ifndef DDR_CORE_GAME_LIFEMETER_H
#define DDR_CORE_GAME_LIFEMETER_H

#include "Judge.h"

namespace game {

struct LifeSettings {
  double initial = 0.5;
  // Change per row grade, Marvelous..Boo then Miss (Simply Love ITG: W1..W5, Miss).
  double row[6] = {0.008, 0.008, 0.004, 0.000, -0.050, -0.100};
  double held = 0.008;
  double let_go = -0.080;
  double missed_hold = 0.0;     // the head was missed: the Miss row already counted
  double hit_mine = -0.050;
  bool harsh_hot_penalty = true;  // a loss at full life costs at least 10%
  int regen_combo_after_miss = 5;   // gains need this many good rows after a miss...
  int max_regen_combo_after_miss = 10;   // ...stacking up to this many
  double hot = 1.0;
  double danger = 0.2;
};

class LifeMeter {
 public:
  // can_fail false is ITG's FailType Off ("On, no fail"): the bar never fails the player and
  // keeps moving after it reaches 0.
  explicit LifeMeter(bool can_fail = true, const LifeSettings& s = LifeSettings())
      : s_(s), life_(s.initial), can_fail_(can_fail) {}

  // Feed every JudgeEvent: rows, holds and mines change life; notes are ignored.
  void apply(const JudgeEvent& e);
  void row(Grade g);
  void hold(HoldResult r, Grade head);
  void mine();

  double life() const { return life_; }      // 0..1
  bool failed() const { return failed_; }    // reached 0 while it can fail; stays failed
  bool empty() const { return life_ <= 0; }  // ITG's IsFailing (passmark 0), even with no fail
  bool hot() const { return life_ >= s_.hot; }
  bool danger() const { return life_ < s_.danger; }

 private:
  void change(double delta);

  LifeSettings s_;
  double life_;
  int miss_combo_ = 0;
  int combo_to_regain_ = 0;
  bool can_fail_ = true;
  bool failed_ = false;
};

}  // namespace game

#endif
