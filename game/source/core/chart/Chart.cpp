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
// The difficulty name table follows ITGmania v1.3.0 src/Difficulty.cpp
// (OldStyleStringToDifficulty) and the meter fallback follows
// src/Steps.cpp (Steps::TidyUpData); the steps-type column counts follow
// src/GameManager.cpp. ITGmania is GPLv3 (https://github.com/itgmania/itgmania).

#include "Chart.h"

#include <cctype>

#include "NoteData.h"
#include "StringUtil.h"

namespace ddr {

char NoteTypeChar(NoteType t) {
  switch (t) {
    case NoteType::Tap: return '1';
    case NoteType::HoldHead: return '2';
    case NoteType::RollHead: return '4';
    case NoteType::Mine: return 'M';
    case NoteType::Lift: return 'L';
    case NoteType::Fake: return 'F';
    case NoteType::Keysound: return 'K';
  }
  return '?';
}

const char* DifficultyToString(Difficulty d) {
  switch (d) {
    case Difficulty::Beginner: return "Beginner";
    case Difficulty::Easy: return "Easy";
    case Difficulty::Medium: return "Medium";
    case Difficulty::Hard: return "Hard";
    case Difficulty::Challenge: return "Challenge";
    case Difficulty::Edit: return "Edit";
    default: return "";
  }
}

Difficulty StringToDifficulty(const std::string& sIn) {
  std::string s = Trimmed(sIn);
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  struct Entry { const char* name; Difficulty d; };
  static const Entry table[] = {
      {"beginner", Difficulty::Beginner}, {"easy", Difficulty::Easy},
      {"basic", Difficulty::Easy},        {"light", Difficulty::Easy},
      {"medium", Difficulty::Medium},     {"another", Difficulty::Medium},
      {"trick", Difficulty::Medium},      {"standard", Difficulty::Medium},
      {"difficult", Difficulty::Medium},  {"hard", Difficulty::Hard},
      {"ssr", Difficulty::Hard},          {"maniac", Difficulty::Hard},
      {"heavy", Difficulty::Hard},        {"smaniac", Difficulty::Challenge},
      {"challenge", Difficulty::Challenge}, {"expert", Difficulty::Challenge},
      {"oni", Difficulty::Challenge},     {"edit", Difficulty::Edit},
  };
  for (const Entry& e : table)
    if (s == e.name) return e.d;
  return Difficulty::Invalid;
}

int TracksForStepsType(const std::string& st) {
  struct Entry { const char* name; int tracks; };
  static const Entry table[] = {
      {"dance-single", 4},     {"dance-double", 8},  {"dance-couple", 8},
      {"dance-solo", 6},       {"dance-threepanel", 3}, {"dance-routine", 8},
      {"pump-single", 5},      {"pump-halfdouble", 6}, {"pump-double", 10},
      {"pump-couple", 10},     {"pump-routine", 10}, {"kb7-single", 7},
      {"ez2-single", 5},       {"ez2-double", 10},   {"ez2-real", 7},
      {"para-single", 5},      {"ds3ddx-single", 8}, {"beat-single5", 6},
      {"beat-double5", 12},    {"beat-single7", 8},  {"beat-double7", 16},
      {"maniax-single", 4},    {"maniax-double", 8}, {"techno-single4", 4},
      {"techno-single5", 5},   {"techno-single8", 8}, {"techno-double4", 8},
      {"techno-double5", 10},  {"techno-double8", 16}, {"popn-five", 5},
      {"popn-nine", 9},        {"lights-cabinet", 8}, {"kickbox-human", 4},
      {"kickbox-quadarm", 4},  {"kickbox-insect", 6}, {"kickbox-arachnid", 8},
  };
  for (const Entry& e : table)
    if (st == e.name) return e.tracks;
  return -1;
}

static bool IsCompositeStepsType(const std::string& st) {
  return st == "dance-routine" || st == "pump-routine";
}

void Song::RetimeNotes() {
  for (Chart& c : charts) {
    const TimingData& td = TimingFor(c);
    for (Note& n : c.notes) {
      n.time = td.GetElapsedTimeFromBeat(n.beat);
      n.endTime = n.IsHold() ? td.GetElapsedTimeFromBeat(n.endBeat) : n.time;
      n.judgable = td.IsJudgableAtRow(n.row);
    }
  }
}

void Song::Finish() {
  for (Chart& c : charts) {
    // Steps::TidyUpData: fall back to the description, then the meter.
    if (c.difficulty == Difficulty::Invalid)
      c.difficulty = StringToDifficulty(c.description);
    if (c.difficulty == Difficulty::Invalid) {
      if (c.meter == 1) c.difficulty = Difficulty::Beginner;
      else if (c.meter <= 3) c.difficulty = Difficulty::Easy;
      else if (c.meter <= 6) c.difficulty = Difficulty::Medium;
      else c.difficulty = Difficulty::Hard;
    }
    int tracks = TracksForStepsType(c.stepsType);
    if (tracks <= 0) tracks = GuessNumTracks(c.noteData);
    c.numTracks = tracks;
    c.notes = ParseNoteData(c.noteData, tracks, IsCompositeStepsType(c.stepsType));
  }
  RetimeNotes();
}

void Song::SetGroupOffset(double seconds) {
  timing.m_fBeat0GroupOffsetInSeconds = seconds;
  for (Chart& c : charts) c.ownTiming.m_fBeat0GroupOffsetInSeconds = seconds;
  RetimeNotes();
}

}  // namespace ddr
