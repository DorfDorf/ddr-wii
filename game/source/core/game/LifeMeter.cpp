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
// Derived from ITGmania v1.3.0 src/LifeMeterBar.cpp
// (https://github.com/itgmania/itgmania), original notice at the end of this
// file. Values from Simply Love's ITG mode (Scripts/SL_Init.lua) and
// ITGmania's preference defaults (RegenComboAfterMiss 5, MaxRegenComboAfterMiss
// 10, HarshHotLifePenalty on, MercifulDrain off, ProgressiveLifebar 0).
//
// Changes from the original:
//  - Only DrainType_Normal; no NoRecover / SuddenDeath, merciful drain,
//    progressive lifebar, course life difficulty or battery mode.
//  - Life difficulty is fixed at 1 (no scaling of gains and losses).
//  - Settings come from a struct instead of theme metrics and preferences.
//  - Failing (FailType immediate, passmark 0) is decided here instead of in
//    ScreenGameplay: life reaching 0 fails the player for good.
//  - No drawing, messages or Lua.

#include "LifeMeter.h"

#include <algorithm>

namespace game {

void LifeMeter::apply(const JudgeEvent& e) {
  switch (e.kind) {
    case JudgeEvent::kRow: row(e.grade); break;
    case JudgeEvent::kHold: hold(e.hold, e.grade); break;
    case JudgeEvent::kMine: mine(); break;
    default: break;
  }
}

void LifeMeter::row(Grade g) {
  if (g == Grade::None) return;
  double delta = s_.row[static_cast<int>(g) - 1];
  // "make it take a while to get back to 'hot'"
  if (s_.harsh_hot_penalty && hot() && delta < 0) delta = std::min(delta, -0.10);
  change(delta);
}

void LifeMeter::hold(HoldResult r, Grade head) {
  double delta;
  if (head == Grade::Miss)
    delta = s_.missed_hold;   // HNS_Missed
  else if (r == HoldResult::Held)
    delta = s_.held;
  else
    delta = s_.let_go;
  if (s_.harsh_hot_penalty && hot() && r == HoldResult::LetGo && head != Grade::Miss) delta = -0.10;
  change(delta);
}

void LifeMeter::mine() {
  double delta = s_.hit_mine;
  if (s_.harsh_hot_penalty && hot() && delta < 0) delta = std::min(delta, -0.10);
  change(delta);
}

void LifeMeter::change(double delta) {
  // ComboToRegainLife: after a miss, gains wait for a few good rows.
  if (delta >= 0) {
    miss_combo_ = 0;
    combo_to_regain_ = std::max(combo_to_regain_ - 1, 0);
    if (combo_to_regain_ > 0) delta = 0;
  } else {
    miss_combo_++;
    const int regain = std::min(s_.max_regen_combo_after_miss, combo_to_regain_ + s_.regen_combo_after_miss);
    combo_to_regain_ = std::max(combo_to_regain_, regain);
  }
  // If we've already failed, there's no point in letting them fill up the bar again.
  if (failed_) return;
  life_ = std::min(1.0, std::max(0.0, life_ + delta));
  if (life_ <= 0) failed_ = true;
}

}  // namespace game

/*
 * Original notice (ITGmania src/LifeMeterBar.cpp):
 *
 * (c) 2001-2004 Chris Danford
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
