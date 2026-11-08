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
// Derived from ITGmania v1.3.0 src/NotesLoaderSSC.cpp
// (https://github.com/itgmania/itgmania, GPLv3), whose original notice is
// reproduced at the end of this file.
//
// Changes from the original:
//  - Loads into ddr::Song / ddr::Chart; no RageFile, LOG, GAMEMAN, SONGMAN,
//    cache, edit or Lua support. Warnings go to Song::warnings.
//  - Only gameplay-relevant tags are kept: the song header tags shared with
//    the .sm loader, #VERSION, all song and chart timing tags (#BPMS #STOPS
//    #DELAYS #WARPS #TIMESIGNATURES #TICKCOUNTS #COMBOS #SPEEDS #SCROLLS
//    #FAKES #LABELS #OFFSET), and the chart tags #STEPSTYPE #CHARTNAME
//    #DESCRIPTION #DIFFICULTY #METER #CREDIT #NOTES/#NOTES2. Radar values,
//    tech counts, NPS, GrooveStats hashes, attacks, BG changes etc. are
//    ignored, as are #CHARTSTYLE and the per-chart #DISPLAYBPM/#MUSIC.
//  - Values are double instead of float (the #VERSION comparisons still use
//    float, as the original does).
//  - The tag lookup is an if-chain instead of std::maps of function pointers.
//  - The state machine (song info -> #NOTEDATA -> chart info -> #NOTES) and
//    the split-timing rules are unchanged.

#include "SscLoader.h"

#include "MsdFile.h"
#include "SmLoader.h"
#include "StringUtil.h"

namespace ddr {

namespace {

void Warn(std::vector<std::string>* w, const std::string& msg) {
  if (w) w->push_back(msg);
}

void ProcessBPMs(TimingData& out, const std::string& sParam,
                 std::vector<std::string>* w) {
  std::vector<std::string> arrayBPMExpressions;
  Split(sParam, ',', arrayBPMExpressions);

  for (unsigned b = 0; b < arrayBPMExpressions.size(); b++) {
    std::vector<std::string> arrayBPMValues;
    Trim(arrayBPMExpressions[b]);
    if (arrayBPMExpressions[b].empty()) {
      continue;
    }
    Split(arrayBPMExpressions[b], '=', arrayBPMValues);
    if (arrayBPMValues.size() != 2) {
      Warn(w, "invalid #BPMS value \"" + arrayBPMExpressions[b] +
                  "\" (must have exactly one '='), ignored");
      continue;
    }

    const double fBeat = StringToDouble(arrayBPMValues[0]);
    const double fNewBPM = StringToDouble(arrayBPMValues[1]);
    if (fBeat >= 0 && fNewBPM > 0) {
      out.AddSegment(TimingSegment::BPM(BeatToNoteRow(fBeat), fNewBPM));
    } else {
      Warn(w, "invalid BPM \"" + arrayBPMExpressions[b] + "\"");
    }
  }
}

void ProcessStops(TimingData& out, const std::string& sParam,
                  std::vector<std::string>* w) {
  std::vector<std::string> arrayStopExpressions;
  Split(sParam, ',', arrayStopExpressions);

  for (unsigned b = 0; b < arrayStopExpressions.size(); b++) {
    std::vector<std::string> arrayStopValues;
    Trim(arrayStopExpressions[b]);
    if (arrayStopExpressions[b].empty()) {
      continue;
    }
    Split(arrayStopExpressions[b], '=', arrayStopValues);
    if (arrayStopValues.size() != 2) {
      Warn(w, "invalid #STOPS value \"" + arrayStopExpressions[b] +
                  "\" (must have exactly one '='), ignored");
      continue;
    }

    const double fBeat = StringToDouble(arrayStopValues[0]);
    const double fNewStop = StringToDouble(arrayStopValues[1]);
    if (fBeat >= 0 && fNewStop > 0) {
      out.AddSegment(TimingSegment::Stop(BeatToNoteRow(fBeat), fNewStop));
    } else {
      Warn(w, "invalid stop \"" + arrayStopExpressions[b] + "\"");
    }
  }
}

void ProcessWarps(TimingData& out, const std::string& sParam,
                  const float fVersion, std::vector<std::string>* w) {
  std::vector<std::string> arrayWarpExpressions;
  Split(sParam, ',', arrayWarpExpressions);

  for (unsigned b = 0; b < arrayWarpExpressions.size(); b++) {
    std::vector<std::string> arrayWarpValues;
    Trim(arrayWarpExpressions[b]);
    if (arrayWarpExpressions[b].empty()) {
      continue;
    }
    Split(arrayWarpExpressions[b], '=', arrayWarpValues);
    if (arrayWarpValues.size() != 2) {
      Warn(w, "invalid #WARPS value \"" + arrayWarpExpressions[b] +
                  "\" (must have exactly one '='), ignored");
      continue;
    }

    const double fBeat = StringToDouble(arrayWarpValues[0]);
    const double fNewBeat = StringToDouble(arrayWarpValues[1]);
    // Early versions were absolute in beats. They should be relative.
    if ((fVersion < VERSION_SPLIT_TIMING && fNewBeat > fBeat)) {
      out.AddSegment(TimingSegment::Warp(BeatToNoteRow(fBeat), fNewBeat - fBeat));
    } else if (fNewBeat > 0) {
      out.AddSegment(TimingSegment::Warp(BeatToNoteRow(fBeat), fNewBeat));
    } else {
      Warn(w, "invalid warp \"" + arrayWarpExpressions[b] + "\"");
    }
  }
}

void ProcessLabels(TimingData& out, const std::string& sParam,
                   std::vector<std::string>* w) {
  std::vector<std::string> arrayLabelExpressions;
  Split(sParam, ',', arrayLabelExpressions);

  for (unsigned b = 0; b < arrayLabelExpressions.size(); b++) {
    std::vector<std::string> arrayLabelValues;
    Trim(arrayLabelExpressions[b]);
    if (arrayLabelExpressions[b].empty()) {
      continue;
    }
    Split(arrayLabelExpressions[b], '=', arrayLabelValues);
    if (arrayLabelValues.size() != 2) {
      Warn(w, "invalid #LABELS value \"" + arrayLabelExpressions[b] +
                  "\" (must have exactly one '='), ignored");
      continue;
    }

    const double fBeat = StringToDouble(arrayLabelValues[0]);
    std::string sLabel = arrayLabelValues[1];
    TrimRight(sLabel);
    if (fBeat >= 0.0) {
      out.AddSegment(TimingSegment::Label(BeatToNoteRow(fBeat), sLabel));
    } else {
      Warn(w, "invalid label \"" + arrayLabelExpressions[b] + "\"");
    }
  }
}

void ProcessCombos(TimingData& out, const std::string& line,
                   std::vector<std::string>* w) {
  std::vector<std::string> arrayComboExpressions;
  Split(line, ',', arrayComboExpressions);

  for (unsigned f = 0; f < arrayComboExpressions.size(); f++) {
    std::vector<std::string> arrayComboValues;
    Split(arrayComboExpressions[f], '=', arrayComboValues);
    size_t size = arrayComboValues.size();
    if (size < 2) {
      Warn(w, "invalid #COMBOS value \"" + arrayComboExpressions[f] +
                  "\" (must have at least one '='), ignored");
      continue;
    }
    const double fComboBeat = StringToDouble(arrayComboValues[0]);
    const int iCombos = StringToInt(arrayComboValues[1]);
    const int iMisses = (size == 2 ? iCombos : StringToInt(arrayComboValues[2]));
    out.AddSegment(
        TimingSegment::Combo(BeatToNoteRow(fComboBeat), iCombos, iMisses));
  }
}

void ProcessScrolls(TimingData& out, const std::string& sParam,
                    std::vector<std::string>* w) {
  std::vector<std::string> vs1;
  Split(sParam, ',', vs1);

  for (const std::string& s1 : vs1) {
    std::vector<std::string> vs2;
    Split(s1, '=', vs2);

    if (vs2.size() < 2) {
      Warn(w, "scroll change with fewer than 2 values");
      continue;
    }

    const double fBeat = StringToDouble(vs2[0]);
    const double fRatio = StringToDouble(vs2[1]);

    if (fBeat < 0) {
      Warn(w, "scroll change with negative beat");
      continue;
    }

    out.AddSegment(TimingSegment::Scroll(BeatToNoteRow(fBeat), fRatio));
  }
}

// One of the timing tags valid both for the song and (split timing) for a
// chart. Returns false if `tag` isn't a timing tag.
bool ProcessTimingTag(const std::string& tag, const std::string& v,
                      TimingData& td, float fVersion,
                      std::vector<std::string>* w) {
  if (tag == "BPMS") ProcessBPMs(td, v, w);
  else if (tag == "STOPS") ProcessStops(td, v, w);
  else if (tag == "DELAYS") sm::ProcessDelays(td, v, w);
  else if (tag == "WARPS") ProcessWarps(td, v, fVersion, w);
  else if (tag == "LABELS") ProcessLabels(td, v, w);
  else if (tag == "TIMESIGNATURES") sm::ProcessTimeSignatures(td, v, w);
  else if (tag == "TICKCOUNTS") sm::ProcessTickcounts(td, v, w);
  else if (tag == "COMBOS") ProcessCombos(td, v, w);
  else if (tag == "SPEEDS") sm::ProcessSpeeds(td, v, w);
  else if (tag == "SCROLLS") ProcessScrolls(td, v, w);
  else if (tag == "FAKES") sm::ProcessFakes(td, v, w);
  else return false;
  return true;
}

void SetDisplayBPM(Song& song, const MsdFile::value_t& p) {
  // #DISPLAYBPM:[xxx][xxx:xxx]|[*];
  if (p[1] == "*") {
    song.displayBpmRandom = true;
  } else {
    song.displayBpmSpecified = true;
    song.displayBpmMin = StringToDouble(p[1]);
    song.displayBpmMax = p[2].empty() ? song.displayBpmMin : StringToDouble(p[2]);
  }
}

enum SSCLoadingStates { GETTING_SONG_INFO, GETTING_STEP_INFO };

}  // namespace

bool LoadSscFromString(const std::string& text, Song& out) {
  MsdFile msd;
  msd.ReadFromString(text, true);

  int state = GETTING_SONG_INFO;
  const unsigned values = msd.GetNumValues();
  Chart pending;
  TimingData stepsTiming;
  bool has_own_timing = false;
  std::vector<std::string>* w = &out.warnings;
  // Song::m_fVersion is a float in StepMania; the comparisons use float.
  auto version = [&out]() { return static_cast<float>(out.version); };

  for (unsigned i = 0; i < values; i++) {
    const MsdFile::value_t& sParams = msd.GetValue(i);
    std::string sValueName = sParams[0];
    MakeUpper(sValueName);
    const std::string v = sParams[1];

    switch (state) {
      case GETTING_SONG_INFO: {
        if (sValueName == "VERSION") out.version = StringToDouble(v);
        else if (sValueName == "TITLE") out.title = v;
        else if (sValueName == "SUBTITLE") out.subtitle = v;
        else if (sValueName == "ARTIST") out.artist = v;
        else if (sValueName == "TITLETRANSLIT") out.titleTranslit = v;
        else if (sValueName == "SUBTITLETRANSLIT") out.subtitleTranslit = v;
        else if (sValueName == "ARTISTTRANSLIT") out.artistTranslit = v;
        else if (sValueName == "GENRE") out.genre = v;
        else if (sValueName == "CREDIT") out.credit = v;
        else if (sValueName == "BANNER") out.banner = v;
        else if (sValueName == "BACKGROUND") out.background = v;
        else if (sValueName == "CDTITLE") out.cdtitle = v;
        else if (sValueName == "MUSIC") out.music = v;
        else if (sValueName == "SAMPLESTART") out.sampleStart = HHMMSSToSeconds(v);
        else if (sValueName == "SAMPLELENGTH") out.sampleLength = HHMMSSToSeconds(v);
        else if (sValueName == "DISPLAYBPM") SetDisplayBPM(out, sParams);
        else if (sValueName == "OFFSET") out.timing.m_fBeat0OffsetInSeconds = StringToDouble(v);
        else if (ProcessTimingTag(sValueName, v, out.timing, version(), w)) {
        }
        // This tag will get us to the next section.
        else if (sValueName == "NOTEDATA") {
          state = GETTING_STEP_INFO;
          pending = Chart();
          stepsTiming = TimingData(out.timing.m_fBeat0OffsetInSeconds);
          has_own_timing = false;
        } else {
          // Silently ignore unrecognized tags, as was done before. -Kyz
        }
        break;
      }
      case GETTING_STEP_INFO: {
        const bool split = version() >= VERSION_SPLIT_TIMING;
        if (sValueName == "VERSION") {
          out.version = StringToDouble(v);
        } else if (sValueName == "CHARTNAME") {
          pending.chartName = Trimmed(v);
        } else if (sValueName == "STEPSTYPE") {
          pending.stepsType = v;
        } else if (sValueName == "DESCRIPTION") {
          std::string name = Trimmed(v);
          if (version() < VERSION_CHART_NAME_TAG) {
            pending.chartName = name;
          } else {
            pending.description = name;
          }
        } else if (sValueName == "DIFFICULTY") {
          pending.difficulty = StringToDifficulty(v);
        } else if (sValueName == "METER") {
          pending.meter = StringToInt(v);
        } else if (sValueName == "CREDIT") {
          pending.credit = v;
        } else if (sValueName == "OFFSET") {
          if (split) {
            stepsTiming.m_fBeat0OffsetInSeconds = StringToDouble(v);
            has_own_timing = true;
          }
        } else if (sValueName == "BPMS" || sValueName == "STOPS" ||
                   sValueName == "DELAYS" || sValueName == "TIMESIGNATURES" ||
                   sValueName == "TICKCOUNTS" || sValueName == "COMBOS" ||
                   sValueName == "WARPS" || sValueName == "SPEEDS" ||
                   sValueName == "SCROLLS" || sValueName == "FAKES" ||
                   sValueName == "LABELS") {
          if (split) {
            ProcessTimingTag(sValueName, v, stepsTiming, version(), w);
            has_own_timing = true;
          }
        } else if (sValueName == "NOTES" || sValueName == "NOTES2" ||
                   sValueName == "STEPFILENAME") {
          state = GETTING_SONG_INFO;
          if (has_own_timing) {
            pending.hasOwnTiming = true;
            pending.ownTiming = stepsTiming;
          }
          has_own_timing = false;
          if (sValueName != "STEPFILENAME") pending.noteData = v;
          out.charts.push_back(std::move(pending));
          pending = Chart();
        } else {
          // Silently ignore unrecognized tags, as was done before. -Kyz
        }
        break;
      }
    }
  }

  // Song::TidyUpData
  out.timing.TidyUpData(false);
  for (Chart& c : out.charts) {
    c.ownTiming.TidyUpData(true);
    if (c.ownTiming.empty()) c.hasOwnTiming = false;
  }
  out.Finish();
  return !out.charts.empty();
}

}  // namespace ddr

/*
 * Original notice (ITGmania src/NotesLoaderSSC.cpp):
 *
 * (c) 2011 Jason Felds
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
