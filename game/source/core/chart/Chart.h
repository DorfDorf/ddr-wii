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
// Song / chart data model (docs/06-technical-design.md section 6). Loaded by
// SmLoader / SscLoader. Original to ddr-wii.

#ifndef DDR_CORE_CHART_CHART_H
#define DDR_CORE_CHART_CHART_H

#include <string>
#include <vector>

#include "../timing/TimingData.h"

namespace ddr {

enum class NoteType {
  Tap,       // '1'
  HoldHead,  // '2' (with its '3' tail folded into endRow)
  RollHead,  // '4' (with its '3' tail folded into endRow)
  Mine,      // 'M'
  Lift,      // 'L'
  Fake,      // 'F'
  Keysound,  // 'K' (auto keysound)
};

/** The .sm character for a note type ('1', '2', ...). */
char NoteTypeChar(NoteType t);

struct Note {
  int row = 0;        // 48 rows per beat
  double beat = 0;    // row / 48
  double time = 0;    // seconds, TimingData::GetElapsedTimeFromBeat(beat)
  int lane = 0;       // column / track
  NoteType type = NoteType::Tap;
  int endRow = 0;     // holds/rolls: the tail's row; others: == row
  double endBeat = 0;
  double endTime = 0;
  int keysound = -1;  // "[n]" keysound index, -1 if none
  bool judgable = true;  // false inside warps and #FAKES ranges

  bool IsHold() const { return type == NoteType::HoldHead || type == NoteType::RollHead; }
};

enum class Difficulty { Beginner, Easy, Medium, Hard, Challenge, Edit, Invalid };
const char* DifficultyToString(Difficulty d);
/** "Easy", "basic", "light", "Expert", "smaniac", ... (StepMania's
 *  OldStyleStringToDifficulty, used by both loaders). */
Difficulty StringToDifficulty(const std::string& s);

struct Chart {
  std::string stepsType;    // "dance-single", ...
  std::string description;
  std::string chartName;    // .ssc #CHARTNAME
  std::string credit;
  Difficulty difficulty = Difficulty::Beginner;
  int meter = 1;
  int numTracks = 4;
  std::string noteData;     // raw #NOTES text (trimmed)

  /** True when the chart has its own .ssc timing (split timing). */
  bool hasOwnTiming = false;
  /** The chart's own timing; only meaningful when hasOwnTiming. */
  TimingData ownTiming;

  /** Parsed notes, sorted by row then lane (filled by Song::Finish). */
  std::vector<Note> notes;
};

struct Song {
  std::string title, subtitle, artist;
  std::string titleTranslit, subtitleTranslit, artistTranslit;
  std::string genre, credit, banner, background, music, cdtitle;
  double sampleStart = 0;
  double sampleLength = 0;
  bool displayBpmRandom = false;
  bool displayBpmSpecified = false;
  double displayBpmMin = 0, displayBpmMax = 0;
  double version = 0;  // .ssc #VERSION

  TimingData timing;   // song timing (and the fallback for every chart)
  std::vector<Chart> charts;
  std::vector<std::string> warnings;  // problems found while loading

  /** The timing that applies to a chart: its own, or the song's. */
  const TimingData& TimingFor(const Chart& c) const {
    return c.hasOwnTiming ? c.ownTiming : timing;
  }

  /** Apply a pack sync offset (pack.ini SyncOffset) to the song timing and
   *  every chart's timing, then recompute note times. */
  void SetGroupOffset(double seconds);

  /** Parse every chart's note data and compute note times. Called by the
   *  loaders; call again after changing timing. */
  void Finish();
  /** Recompute note times only (after a timing/offset change). */
  void RetimeNotes();
};

/** Number of columns for a steps type (dance-single 4, dance-double 8, ...).
 *  Unknown types get 4. */
int TracksForStepsType(const std::string& stepsType);

}  // namespace ddr

#endif
