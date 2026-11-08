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
// Derived from ITGmania v1.3.0 src/NotesLoaderSM.cpp
// (https://github.com/itgmania/itgmania, GPLv3), whose original notice is
// reproduced at the end of this file.
//
// Changes from the original:
//  - Loads into ddr::Song / ddr::Chart instead of Song / Steps; no RageFile,
//    LOG, GAMEMAN or SONGMAN. Warnings go to Song::warnings.
//  - Only gameplay-relevant song tags are kept (titles, artist, files,
//    offset, BPMs, stops/freezes, delays, time signatures, tickcounts,
//    sample start/length, display BPM). BG/FG changes, attacks, keysounds,
//    instrument tracks, edits, the cache and the "-nosongbg-" hack are
//    dropped; their tags are ignored.
//  - Values are double instead of float.
//  - ProcessSpeeds no longer indexes an empty vector for a lone "=".
//  - ProcessBPMsAndStops: when no BPM is set at or before beat 0, the
//    original advanced past the first later BPM before reading it (skipping
//    it, or reading past the end of the list). We read that BPM instead.
//  - The tag lookup is a small if-chain instead of a std::map of function
//    pointers.
//  - ProcessBPMsAndStops, ParseBPMs, ParseStops, ProcessDelays,
//    ProcessTimeSignatures, ProcessTickcounts, ProcessSpeeds, ProcessFakes and
//    LoadFromTokens keep the original logic and comments.

#include "SmLoader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MsdFile.h"
#include "StringUtil.h"

namespace ddr {

namespace {
void Warn(std::vector<std::string>* w, const std::string& msg) {
  if (w) w->push_back(msg);
}
std::string Fmt(const char* fmt, double a, double b = 0) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), fmt, a, b);
  return buf;
}
}  // namespace

namespace sm {

double RowToBeat(std::string line, const int rowsPerBeat) {
  std::string backup = line;
  Trim(line, "r");
  Trim(line, "R");
  if (backup != line) {
    return StringToDouble(line) / rowsPerBeat;
  } else {
    return StringToDouble(line);
  }
}

void ParseBPMs(BeatValueList& out, const std::string& line,
               std::vector<std::string>* warnings, const int rowsPerBeat) {
  std::vector<std::string> arrayBPMChangeExpressions;
  Split(line, ',', arrayBPMChangeExpressions);

  for (unsigned b = 0; b < arrayBPMChangeExpressions.size(); b++) {
    std::vector<std::string> arrayBPMChangeValues;
    Trim(arrayBPMChangeExpressions[b]);
    if (arrayBPMChangeExpressions[b].empty()) {
      continue;
    }
    Split(arrayBPMChangeExpressions[b], '=', arrayBPMChangeValues);
    if (arrayBPMChangeValues.size() != 2) {
      Warn(warnings, "invalid #BPMS value \"" + arrayBPMChangeExpressions[b] +
                         "\" (must have exactly one '='), ignored");
      continue;
    }

    const double fBeat = RowToBeat(arrayBPMChangeValues[0], rowsPerBeat);
    const double fNewBPM = StringToDouble(arrayBPMChangeValues[1]);
    if (fNewBPM == 0) {
      Warn(warnings, "zero BPM ignored");
      continue;
    }

    out.push_back(std::make_pair(fBeat, fNewBPM));
  }
}

void ParseStops(BeatValueList& out, const std::string& line,
                std::vector<std::string>* warnings, const int rowsPerBeat) {
  std::vector<std::string> arrayFreezeExpressions;
  Split(line, ',', arrayFreezeExpressions);

  for (unsigned f = 0; f < arrayFreezeExpressions.size(); f++) {
    std::vector<std::string> arrayFreezeValues;
    Trim(arrayFreezeExpressions[f]);
    if (arrayFreezeExpressions[f].empty()) {
      continue;
    }
    Split(arrayFreezeExpressions[f], '=', arrayFreezeValues);
    if (arrayFreezeValues.size() != 2) {
      Warn(warnings, "invalid #STOPS value \"" + arrayFreezeExpressions[f] +
                         "\" (must have exactly one '='), ignored");
      continue;
    }

    const double fFreezeBeat = RowToBeat(arrayFreezeValues[0], rowsPerBeat);
    const double fFreezeSeconds = StringToDouble(arrayFreezeValues[1]);
    if (fFreezeSeconds == 0) {
      Warn(warnings, "zero-length stop ignored");
      continue;
    }

    out.push_back(std::make_pair(fFreezeBeat, fFreezeSeconds));
  }
}

// Utility function for sorting timing change data
static bool compare_first(const std::pair<double, double>& a,
                          const std::pair<double, double>& b) {
  return a.first < b.first;
}

// Precondition: no BPM change or stop has 0 for its value (change.second).
//     (The ParseBPMs and ParseStops functions make sure of this.)
// Postcondition: all BPM changes, stops, and warps are added to the out
//     parameter, already sorted by beat.
void ProcessBPMsAndStops(TimingData& out, BeatValueList& vBPMs,
                         BeatValueList& vStops,
                         std::vector<std::string>* warnings) {
  BeatValueList::const_iterator ibpm, ibpmend;
  BeatValueList::const_iterator istop, istopend;

  // Current BPM (positive or negative)
  double bpm = 0;
  // Beat at which the previous timing change occurred
  double prevbeat = 0;
  // Start/end of current warp (-1 if not currently warping)
  double warpstart = -1;
  double warpend = -1;
  // BPM prior to current warp, to detect if it has changed
  double prewarpbpm = 0;
  // How far off we have gotten due to negative changes
  double timeofs = 0;

  // Sort BPM changes and stops by beat.  Order matters.
  std::stable_sort(vBPMs.begin(), vBPMs.end(), compare_first);
  std::stable_sort(vStops.begin(), vStops.end(), compare_first);

  // Convert stops that come before beat 0.  All these really do is affect
  // where the arrows are with respect to the music, i.e. the song offset.
  // Positive stops subtract from the offset, and negative add to it.
  istop = vStops.begin();
  istopend = vStops.end();
  for (/* istop */; istop != istopend && istop->first < 0; istop++) {
    out.m_fBeat0OffsetInSeconds -= istop->second;
  }

  // Get rid of BPM changes that come before beat 0.  Positive BPMs before
  // the chart don't really do anything, so we just ignore them.  Negative
  // BPMs cause unpredictable behavior, so ignore them as well and issue a
  // warning.
  ibpm = vBPMs.begin();
  ibpmend = vBPMs.end();
  for (/* ibpm */; ibpm != ibpmend && ibpm->first <= 0; ibpm++) {
    bpm = ibpm->second;
    if (bpm < 0 && ibpm->first < 0) {
      Warn(warnings, "negative BPM prior to beat 0 ignored");
    }
  }

  // It's beat 0.  Do you know where your BPMs are?
  if (bpm == 0) {
    // Nope.  Can we just use the next BPM value?
    if (ibpm == ibpmend) {
      // Nope.
      bpm = 60;
      Warn(warnings, "no valid BPMs; defaulting to 60");
    } else {
      // Yep.  Get the next BPM.
      // (The original increments ibpm before reading it, which skips the
      // first BPM after beat 0 and can read past the end. Read it in place:
      // the loop below will then also apply it at its own beat.)
      bpm = ibpm->second;
      Warn(warnings,
           "no BPM before beat 0; using the value from the next BPM change");
    }
  }
  // We always want to have an initial BPM.  If we start out warping, this
  // BPM will be added later.  If we start with a regular BPM, add it now.
  if (bpm > 0 && bpm <= FAST_BPM_WARP) {
    out.AddSegment(TimingSegment::BPM(BeatToNoteRow(0), bpm));
  }

  // Iterate over all BPMs and stops in tandem
  while (ibpm != ibpmend || istop != istopend) {
    // Get the next change in order, with BPMs taking precedence
    // when they fall on the same beat.
    bool changeIsBpm =
        istop == istopend || (ibpm != ibpmend && ibpm->first <= istop->first);
    const std::pair<double, double>& change = changeIsBpm ? *ibpm : *istop;

    // Calculate the effects of time at the current BPM.  "Infinite"
    // BPMs (SM4 warps) imply that zero time passes, so skip this
    // step in that case.
    if (bpm <= FAST_BPM_WARP) {
      timeofs += (change.first - prevbeat) * 60 / bpm;

      // If we were in a warp and it finished during this
      // timeframe, create the warp segment.
      if (warpstart >= 0 && bpm > 0 && timeofs > 0) {
        // timeofs represents how far past the end we are
        warpend = change.first - (timeofs * bpm / 60);
        out.AddSegment(
            TimingSegment::Warp(BeatToNoteRow(warpstart), warpend - warpstart));

        // If the BPM changed during the warp, put that
        // change at the beginning of the warp.
        if (bpm != prewarpbpm) {
          out.AddSegment(TimingSegment::BPM(BeatToNoteRow(warpstart), bpm));
        }
        // No longer warping
        warpstart = -1;
      }
    }

    // Save the current beat for the next round of calculations
    prevbeat = change.first;

    // Now handle the timing changes themselves
    if (changeIsBpm) {
      // Does this BPM change start a new warp?
      if (warpstart < 0 &&
          (change.second < 0 || change.second > FAST_BPM_WARP)) {
        // Yes.
        warpstart = change.first;
        prewarpbpm = bpm;
        timeofs = 0;
      } else if (warpstart < 0) {
        // No, and we aren't currently warping either.
        // Just a normal BPM change.
        out.AddSegment(
            TimingSegment::BPM(BeatToNoteRow(change.first), change.second));
      }
      bpm = change.second;
      ibpm++;
    } else {
      // Does this stop start a new warp?
      if (warpstart < 0 && change.second < 0) {
        // Yes.
        warpstart = change.first;
        prewarpbpm = bpm;
        timeofs = change.second;
      } else if (warpstart < 0) {
        // No, and we aren't currently warping either.
        // Just a normal stop.
        out.AddSegment(
            TimingSegment::Stop(BeatToNoteRow(change.first), change.second));
      } else {
        // We're warping already.  Stops affect the time
        // offset directly.
        timeofs += change.second;

        // If a stop overcompensates for the time
        // deficit, the warp ends and we stop for the
        // amount it goes over.
        if (change.second > 0 && timeofs > 0) {
          warpend = change.first;
          out.AddSegment(TimingSegment::Warp(BeatToNoteRow(warpstart),
                                             warpend - warpstart));
          out.AddSegment(
              TimingSegment::Stop(BeatToNoteRow(change.first), timeofs));

          // Now, are we still warping because of
          // the BPM value?
          if (bpm < 0 || bpm > FAST_BPM_WARP) {
            // Yep.
            warpstart = change.first;
            // prewarpbpm remains the same
            timeofs = 0;
          } else {
            // Nope, warp is done.  Add any
            // BPM change that happened in
            // the meantime.
            if (bpm != prewarpbpm) {
              out.AddSegment(
                  TimingSegment::BPM(BeatToNoteRow(warpstart), bpm));
            }
            warpstart = -1;
          }
        }
      }
      istop++;
    }
  }

  // If we are still warping, we now have to consider the time remaining
  // after the last timing change.
  if (warpstart >= 0) {
    // Will this warp ever end?
    if (bpm < 0 || bpm > FAST_BPM_WARP) {
      // No, so it ends the entire chart immediately.
      // XXX There must be a less hacky and more accurate way
      // to do this.
      warpend = 99999999.0;
    } else {
      // Yes.  Figure out when it will end.
      warpend = prevbeat - (timeofs * bpm / 60);
    }
    out.AddSegment(
        TimingSegment::Warp(BeatToNoteRow(warpstart), warpend - warpstart));

    // As usual, record any BPM change that happened during the warp
    if (bpm != prewarpbpm) {
      out.AddSegment(TimingSegment::BPM(BeatToNoteRow(warpstart), bpm));
    }
  }
}

void ProcessDelays(TimingData& out, const std::string& line,
                   std::vector<std::string>* warnings, const int rowsPerBeat) {
  std::vector<std::string> arrayDelayExpressions;
  Split(line, ',', arrayDelayExpressions);

  for (unsigned f = 0; f < arrayDelayExpressions.size(); f++) {
    std::vector<std::string> arrayDelayValues;
    Trim(arrayDelayExpressions[f]);
    if (arrayDelayExpressions[f].empty()) {
      continue;
    }
    Split(arrayDelayExpressions[f], '=', arrayDelayValues);
    if (arrayDelayValues.size() != 2) {
      Warn(warnings, "invalid #DELAYS value \"" + arrayDelayExpressions[f] +
                         "\" (must have exactly one '='), ignored");
      continue;
    }
    const double fFreezeBeat = RowToBeat(arrayDelayValues[0], rowsPerBeat);
    const double fFreezeSeconds = StringToDouble(arrayDelayValues[1]);

    if (fFreezeSeconds > 0.0) {
      out.AddSegment(
          TimingSegment::Delay(BeatToNoteRow(fFreezeBeat), fFreezeSeconds));
    } else {
      Warn(warnings, Fmt("invalid delay at beat %f, length %f", fFreezeBeat,
                         fFreezeSeconds));
    }
  }
}

void ProcessTimeSignatures(TimingData& out, const std::string& line,
                           std::vector<std::string>* warnings,
                           const int rowsPerBeat) {
  std::vector<std::string> vs1;
  std::vector<TimingSegment> segments;
  Split(line, ',', vs1);

  for (const std::string& s1 : vs1) {
    std::vector<std::string> vs2;
    Split(s1, '=', vs2);

    if (vs2.size() < 3) {
      Warn(warnings, Fmt("invalid time signature change with %.0f values",
                         static_cast<double>(vs2.size())));
      continue;
    }

    const double fBeat = RowToBeat(vs2[0], rowsPerBeat);
    const int iNumerator = StringToInt(vs2[1]);
    const int iDenominator = StringToInt(vs2[2]);

    if (fBeat < 0) {
      Warn(warnings, Fmt("invalid time signature change with beat %f", fBeat));
      continue;
    }

    if (iNumerator < 1) {
      Warn(warnings, Fmt("invalid time signature numerator at beat %f", fBeat));
      continue;
    }

    if (iDenominator < 1) {
      Warn(warnings,
           Fmt("invalid time signature denominator at beat %f", fBeat));
      continue;
    }
    segments.push_back(TimingSegment::TimeSig(BeatToNoteRow(fBeat), iNumerator,
                                              iDenominator));
  }

  // If there are any time signatures defined, but there isn't one
  // for the very first beat of the song, then add one.
  if (!segments.empty() && segments[0].row > 0) {
    out.AddSegment(TimingSegment::TimeSig(0, 4, 4));
  }

  for (const TimingSegment& segment : segments) {
    out.AddSegment(segment);
  }
}

void ProcessTickcounts(TimingData& out, const std::string& line,
                       std::vector<std::string>* warnings,
                       const int rowsPerBeat) {
  std::vector<std::string> arrayTickcountExpressions;
  Split(line, ',', arrayTickcountExpressions);

  for (unsigned f = 0; f < arrayTickcountExpressions.size(); f++) {
    std::vector<std::string> arrayTickcountValues;
    Trim(arrayTickcountExpressions[f]);
    if (arrayTickcountExpressions[f].empty()) {
      continue;
    }
    Split(arrayTickcountExpressions[f], '=', arrayTickcountValues);
    if (arrayTickcountValues.size() != 2) {
      Warn(warnings, "invalid #TICKCOUNTS value \"" +
                         arrayTickcountExpressions[f] +
                         "\" (must have exactly one '='), ignored");
      continue;
    }

    const double fTickcountBeat =
        RowToBeat(arrayTickcountValues[0], rowsPerBeat);
    int iTicks = std::clamp(StringToInt(arrayTickcountValues[1]), 0,
                            ROWS_PER_BEAT);

    out.AddSegment(
        TimingSegment::Tickcount(BeatToNoteRow(fTickcountBeat), iTicks));
  }
}

void ProcessSpeeds(TimingData& out, const std::string& line,
                   std::vector<std::string>* warnings, const int rowsPerBeat) {
  std::vector<std::string> vs1;
  Split(line, ',', vs1);

  for (const std::string& s1 : vs1) {
    std::vector<std::string> vs2;
    Split(s1, '=', vs2);

    if (vs2.empty()) {
      continue;
    }

    if (vs2[0].empty() && vs2.size() == 2)  // First one always seems to have 2.
    {
      vs2.push_back("0");
    }

    if (vs2.size() == 3)  // use beats by default.
    {
      vs2.push_back("0");
    }

    if (vs2.size() < 4) {
      Warn(warnings, Fmt("speed change with %.0f values",
                         static_cast<double>(vs2.size())));
      continue;
    }

    const double fBeat = RowToBeat(vs2[0], rowsPerBeat);
    const double fRatio = StringToDouble(vs2[1]);
    const double fDelay = StringToDouble(vs2[2]);

    // XXX: ugly...
    int iUnit = StringToInt(vs2[3]);
    int unit = (iUnit == 0) ? 0 /* beats */ : 1 /* seconds */;

    if (fBeat < 0) {
      Warn(warnings, Fmt("speed change with beat %f", fBeat));
      continue;
    }

    if (fDelay < 0) {
      Warn(warnings, Fmt("speed change with beat %f, length %f", fBeat, fDelay));
      continue;
    }

    out.AddSegment(
        TimingSegment::Speed(BeatToNoteRow(fBeat), fRatio, fDelay, unit));
  }
}

void ProcessFakes(TimingData& out, const std::string& line,
                  std::vector<std::string>* warnings, const int rowsPerBeat) {
  std::vector<std::string> arrayFakeExpressions;
  Split(line, ',', arrayFakeExpressions);

  for (unsigned b = 0; b < arrayFakeExpressions.size(); b++) {
    std::vector<std::string> arrayFakeValues;
    Trim(arrayFakeExpressions[b]);
    if (arrayFakeExpressions[b].empty()) {
      continue;
    }
    Split(arrayFakeExpressions[b], '=', arrayFakeValues);
    if (arrayFakeValues.size() != 2) {
      Warn(warnings, "invalid #FAKES value \"" + arrayFakeExpressions[b] +
                         "\" (must have exactly one '='), ignored");
      continue;
    }

    const double fBeat = RowToBeat(arrayFakeValues[0], rowsPerBeat);
    const double fSkippedBeats = StringToDouble(arrayFakeValues[1]);

    if (fSkippedBeats > 0) {
      out.AddSegment(TimingSegment::Fake(BeatToNoteRow(fBeat), fSkippedBeats));
    } else {
      Warn(warnings, Fmt("invalid fake at beat %f, beats to skip %f", fBeat,
                         fSkippedBeats));
    }
  }
}

}  // namespace sm

// SMLoader::LoadFromTokens
static void LoadFromTokens(std::string sStepsType, std::string sDescription,
                           std::string sDifficulty, std::string sMeter,
                           std::string sNoteData, Chart& out) {
  Trim(sStepsType);
  Trim(sDescription);
  Trim(sDifficulty);
  Trim(sNoteData);

  // backwards compatibility hacks:
  // HACK: We eliminated "ez2-single-hard", but we should still handle it.
  if (sStepsType == "ez2-single-hard") {
    sStepsType = "ez2-single";
  }

  // HACK: "para-single" used to be called just "para"
  if (sStepsType == "para") {
    sStepsType = "para-single";
  }

  out.stepsType = sStepsType;
  out.description = sDescription;
  out.credit = sDescription;     // this is often used for both.
  out.chartName = sDescription;  // yeah, one more for good measure.
  out.difficulty = StringToDifficulty(sDifficulty);

  // Handle hacks that originated back when StepMania didn't have
  // Difficulty_Challenge. (At least v1.64, possibly v3.0 final...)
  if (out.difficulty == Difficulty::Hard) {
    // HACK: SMANIAC used to be Difficulty_Hard with a special description.
    // HACK: CHALLENGE used to be Difficulty_Hard with a special description.
    if (EqualsNoCase(sDescription, "smaniac") ||
        EqualsNoCase(sDescription, "challenge")) {
      out.difficulty = Difficulty::Challenge;
    }
  }

  if (sMeter.empty()) {
    // some simfiles (e.g. X-SPECIALs from Zenius-I-Vanisher) don't
    // have a meter on certain steps. Make the meter 1 in these instances.
    sMeter = "1";
  }
  out.meter = StringToInt(sMeter);

  out.noteData = sNoteData;
}

static void SetDisplayBPM(Song& song, const MsdFile::value_t& p) {
  // #DISPLAYBPM:[xxx][xxx:xxx]|[*];
  if (p[1] == "*") {
    song.displayBpmRandom = true;
  } else {
    song.displayBpmSpecified = true;
    song.displayBpmMin = StringToDouble(p[1]);
    song.displayBpmMax = p[2].empty() ? song.displayBpmMin : StringToDouble(p[2]);
  }
}

bool LoadSmFromString(const std::string& text, Song& out) {
  MsdFile msd;
  msd.ReadFromString(text, true);  // unescape

  sm::BeatValueList BPMChanges, Stops;
  std::vector<std::string>* w = &out.warnings;

  for (unsigned i = 0; i < msd.GetNumValues(); i++) {
    const unsigned iNumParams = msd.GetNumParams(i);
    const MsdFile::value_t& sParams = msd.GetValue(i);
    std::string sValueName = sParams[0];
    MakeUpper(sValueName);
    const std::string v1 = sParams[1];

    if (sValueName == "TITLE") out.title = v1;
    else if (sValueName == "SUBTITLE") out.subtitle = v1;
    else if (sValueName == "ARTIST") out.artist = v1;
    else if (sValueName == "TITLETRANSLIT") out.titleTranslit = v1;
    else if (sValueName == "SUBTITLETRANSLIT") out.subtitleTranslit = v1;
    else if (sValueName == "ARTISTTRANSLIT") out.artistTranslit = v1;
    else if (sValueName == "GENRE") out.genre = v1;
    else if (sValueName == "CREDIT") out.credit = v1;
    else if (sValueName == "BANNER") out.banner = v1;
    else if (sValueName == "BACKGROUND") out.background = v1;
    else if (sValueName == "CDTITLE") out.cdtitle = v1;
    else if (sValueName == "MUSIC") out.music = v1;
    else if (sValueName == "OFFSET") out.timing.m_fBeat0OffsetInSeconds = StringToDouble(v1);
    else if (sValueName == "BPMS") {
      BPMChanges.clear();
      sm::ParseBPMs(BPMChanges, v1, w);
    } else if (sValueName == "STOPS" || sValueName == "FREEZES") {
      Stops.clear();
      sm::ParseStops(Stops, v1, w);
    } else if (sValueName == "DELAYS") sm::ProcessDelays(out.timing, v1, w);
    else if (sValueName == "TIMESIGNATURES") sm::ProcessTimeSignatures(out.timing, v1, w);
    else if (sValueName == "TICKCOUNTS") sm::ProcessTickcounts(out.timing, v1, w);
    else if (sValueName == "SAMPLESTART") out.sampleStart = HHMMSSToSeconds(v1);
    else if (sValueName == "SAMPLELENGTH") out.sampleLength = HHMMSSToSeconds(v1);
    else if (sValueName == "DISPLAYBPM") SetDisplayBPM(out, sParams);
    else if (sValueName == "NOTES" || sValueName == "NOTES2") {
      if (iNumParams < 7) {
        out.warnings.push_back("a #NOTES tag has " + std::to_string(iNumParams) +
                               " fields, but should have at least 7");
        continue;
      }
      Chart c;
      LoadFromTokens(sParams[1], sParams[2], sParams[3], sParams[4],
                     sParams[6], c);
      out.charts.push_back(std::move(c));
    }
    // Everything else (BG changes, attacks, keysounds, ...) is ignored.
  }

  // Turn negative time changes into warps
  sm::ProcessBPMsAndStops(out.timing, BPMChanges, Stops, w);

  out.timing.TidyUpData(false);
  out.Finish();
  return !out.charts.empty();
}

bool ReadFileToString(const std::string& path, std::string& out) {
  out.clear();
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  char buf[16384];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  bool ok = !std::ferror(f);
  std::fclose(f);
  return ok;
}

static bool EndsWithNoCase(const std::string& s, const char* suffix) {
  size_t n = std::strlen(suffix);
  return s.size() >= n && EqualsNoCase(s.substr(s.size() - n), suffix);
}

bool LoadSscFromString(const std::string& text, Song& out);  // SscLoader.cpp

bool LoadSongFile(const std::string& path, Song& out) {
  std::string text;
  if (!ReadFileToString(path, text)) return false;
  if (EndsWithNoCase(path, ".sm")) return LoadSmFromString(text, out);
  return LoadSscFromString(text, out);
}

std::string PickSimfile(const std::vector<std::string>& filenames) {
  for (const std::string& f : filenames)
    if (EndsWithNoCase(f, ".ssc")) return f;
  for (const std::string& f : filenames)
    if (EndsWithNoCase(f, ".sm")) return f;
  return std::string();
}

}  // namespace ddr

/*
 * Original notice (ITGmania src/NotesLoaderSM.cpp):
 *
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
