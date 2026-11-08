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
// Derived from ITGmania v1.3.0 src/TimingData.cpp and src/TimingSegments.h
// (https://github.com/itgmania/itgmania, GPLv3), whose original notices are
// reproduced at the end of this file.
//
// Changes from the original:
//  - Times, BPS and pauses are double instead of float (doc 06 section 3.2).
//  - The per-type TimingSegment class hierarchy is replaced by one flat
//    value struct; segments are stored by value, not as owned pointers.
//  - BeatToNoteRow clamps to +-MAX_NOTE_ROW instead of overflowing, and row
//    differences/sums are done in 64 bits.
//  - GetSegmentIndexAtRow results of -1 (a row before the first segment) are
//    handled instead of asserting / indexing out of range; GetBPMAtRow
//    returns 60 when there are no BPM segments.
//  - GetDisplayedBeat tolerates an empty scroll list.
//  - No global offset, music rate, GAMESTATE or PREFSMAN: the "NoOffset"
//    variants are the only ones (renamed without the suffix).
//  - Kept: AddSegment, EnsureLookups (lookup tables), FindEvent,
//    GetBeatInternal, GetElapsedTimeInternal, GetDisplayedBeat, IsWarpAtRow,
//    IsFakeAtRow, TidyUpData. Dropped: editor, Lua, scaling, shifting,
//    dumping and speed-percent code.

#include "TimingData.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>

namespace ddr {

int BeatToNoteRow(double fBeat) {
  double r = fBeat * ROWS_PER_BEAT;
  if (!(r == r)) return 0;  // NaN
  if (r >= MAX_NOTE_ROW) return MAX_NOTE_ROW;
  if (r <= -MAX_NOTE_ROW) return -MAX_NOTE_ROW;
  return static_cast<int>(std::lrint(r));
}

// Beats between two rows, without int overflow for rows near +-MAX_NOTE_ROW.
static double RowSpanToBeats(int fromRow, int toRow) {
  return static_cast<double>(static_cast<long long>(toRow) - fromRow) /
         ROWS_PER_BEAT;
}

// ---------------------------------------------------------------------------
// TimingSegment (from TimingSegments.h)

static bool AreEqual(double a, double b) {
  // for our purposes, two floats within this level of error are equal
  return std::fabs(a - b) < 1e-6;
}

SegmentEffectType TimingSegment::GetEffectType() const {
  switch (type) {
    case SEGMENT_STOP:
    case SEGMENT_DELAY:
      return SegmentEffectType_Row;
    case SEGMENT_WARP:
    case SEGMENT_FAKE:
      return SegmentEffectType_Range;
    default:
      return SegmentEffectType_Indefinite;
  }
}

bool TimingSegment::IsNotable() const {
  switch (type) {
    case SEGMENT_STOP:
    case SEGMENT_DELAY:
      return value > 0;
    case SEGMENT_WARP:
    case SEGMENT_FAKE:
      return length > 0;
    default:
      return true;  // indefinite segments are always notable
  }
}

bool TimingSegment::SameValue(const TimingSegment& o) const {
  if (type != o.type) return false;
  switch (type) {
    case SEGMENT_BPM:
    case SEGMENT_STOP:
    case SEGMENT_DELAY:
    case SEGMENT_SCROLL:
      return AreEqual(value, o.value);
    case SEGMENT_WARP:
    case SEGMENT_FAKE:
      return length == o.length;
    case SEGMENT_TIME_SIG:
    case SEGMENT_COMBO:
      return num == o.num && den == o.den;
    case SEGMENT_TICKCOUNT:
      return num == o.num;
    case SEGMENT_LABEL:
      return label == o.label;
    case SEGMENT_SPEED:
      return AreEqual(value, o.value) && AreEqual(value2, o.value2) &&
             num == o.num;
    default:
      return false;
  }
}

static TimingSegment MakeSeg(TimingSegmentType t, int row) {
  TimingSegment s;
  s.type = t;
  s.row = row;
  return s;
}
TimingSegment TimingSegment::BPM(int row, double bpm) {
  TimingSegment s = MakeSeg(SEGMENT_BPM, row);
  s.value = bpm / 60.0;
  return s;
}
TimingSegment TimingSegment::Stop(int row, double seconds) {
  TimingSegment s = MakeSeg(SEGMENT_STOP, row);
  s.value = seconds;
  return s;
}
TimingSegment TimingSegment::Delay(int row, double seconds) {
  TimingSegment s = MakeSeg(SEGMENT_DELAY, row);
  s.value = seconds;
  return s;
}
TimingSegment TimingSegment::Warp(int row, double lengthBeats) {
  TimingSegment s = MakeSeg(SEGMENT_WARP, row);
  s.length = BeatToNoteRow(lengthBeats);
  return s;
}
TimingSegment TimingSegment::Fake(int row, double lengthBeats) {
  TimingSegment s = MakeSeg(SEGMENT_FAKE, row);
  s.length = BeatToNoteRow(lengthBeats);
  return s;
}
TimingSegment TimingSegment::TimeSig(int row, int n, int d) {
  TimingSegment s = MakeSeg(SEGMENT_TIME_SIG, row);
  s.num = n;
  s.den = d;
  return s;
}
TimingSegment TimingSegment::Tickcount(int row, int ticks) {
  TimingSegment s = MakeSeg(SEGMENT_TICKCOUNT, row);
  s.num = ticks;
  return s;
}
TimingSegment TimingSegment::Combo(int row, int combo, int miss) {
  TimingSegment s = MakeSeg(SEGMENT_COMBO, row);
  s.num = combo;
  s.den = miss;
  return s;
}
TimingSegment TimingSegment::Label(int row, const std::string& l) {
  TimingSegment s = MakeSeg(SEGMENT_LABEL, row);
  s.label = l;
  return s;
}
TimingSegment TimingSegment::Speed(int row, double ratio, double delay,
                                   int unit) {
  TimingSegment s = MakeSeg(SEGMENT_SPEED, row);
  s.value = ratio;
  s.value2 = delay;
  s.num = unit;
  return s;
}
TimingSegment TimingSegment::Scroll(int row, double ratio) {
  TimingSegment s = MakeSeg(SEGMENT_SCROLL, row);
  s.value = ratio;
  return s;
}

// ---------------------------------------------------------------------------
// TimingData

static const int INVALID_INDEX = -1;

bool TimingData::empty() const {
  for (int t = 0; t < NUM_TimingSegmentType; ++t)
    if (!m_segments[t].empty()) return false;
  return true;
}

void TimingData::Clear() {
  for (int t = 0; t < NUM_TimingSegmentType; ++t) m_segments[t].clear();
  InvalidateLookups();
}

int TimingData::GetSegmentIndexAtRow(TimingSegmentType tst, int iRow) const {
  const std::vector<TimingSegment>& vSegs = m_segments[tst];

  if (vSegs.empty()) {
    return INVALID_INDEX;
  }

  int min = 0, max = static_cast<int>(vSegs.size()) - 1;
  int l = min, r = max;
  while (l <= r) {
    int m = (l + r) / 2;
    if ((m == min || vSegs[m].row <= iRow) &&
        (m == max || iRow < vSegs[m + 1].row)) {
      return m;
    } else if (vSegs[m].row <= iRow) {
      l = m + 1;
    } else {
      r = m - 1;
    }
  }

  // iRow is before the first segment of type tst
  return INVALID_INDEX;
}

double TimingData::GetBPMAtRow(int iRow) const {
  const std::vector<TimingSegment>& bpms = m_segments[SEGMENT_BPM];
  if (bpms.empty()) return 60.0;
  int i = GetSegmentIndexAtRow(SEGMENT_BPM, iRow);
  if (i < 0) i = 0;
  return bpms[i].value * 60.0;
}

// Row-effect segments (stops, delays) only count on their exact row.
static double PauseAtRow(const TimingData& td, TimingSegmentType t, int iRow) {
  const std::vector<TimingSegment>& v = td.GetTimingSegments(t);
  int i = td.GetSegmentIndexAtRow(t, iRow);
  if (i < 0 || v[i].row != iRow) return 0.0;
  return v[i].value;
}
double TimingData::GetStopAtRow(int iRow) const {
  return PauseAtRow(*this, SEGMENT_STOP, iRow);
}
double TimingData::GetDelayAtRow(int iRow) const {
  return PauseAtRow(*this, SEGMENT_DELAY, iRow);
}

bool TimingData::IsWarpAtRow(int iNoteRow) const {
  const std::vector<TimingSegment>& warps = m_segments[SEGMENT_WARP];
  if (warps.empty()) {
    return false;
  }

  int i = GetSegmentIndexAtRow(SEGMENT_WARP, iNoteRow);
  if (i == -1) {
    return false;
  }
  const TimingSegment& s = warps[i];
  if (s.row <= iNoteRow && iNoteRow < static_cast<long long>(s.row) + s.length) {
    // Allow stops inside warps to allow things like stop, warp, stop, warp,
    // stop, and so on.
    if (m_segments[SEGMENT_STOP].empty() && m_segments[SEGMENT_DELAY].empty()) {
      return true;
    }
    if (GetStopAtRow(iNoteRow) != 0.0 || GetDelayAtRow(iNoteRow) != 0.0) {
      return false;
    }
    return true;
  }
  return false;
}

bool TimingData::IsFakeAtRow(int iNoteRow) const {
  const std::vector<TimingSegment>& fakes = m_segments[SEGMENT_FAKE];
  if (fakes.empty()) {
    return false;
  }

  int i = GetSegmentIndexAtRow(SEGMENT_FAKE, iNoteRow);
  if (i == -1) {
    return false;
  }
  const TimingSegment& s = fakes[i];
  return s.row <= iNoteRow && iNoteRow < static_cast<long long>(s.row) + s.length;
}

void TimingData::AddSegment(const TimingSegment& seg) {
  InvalidateLookups();

  std::vector<TimingSegment>& vSegs = m_segments[seg.type];

  // OPTIMIZATION: if this is our first segment, push and return.
  if (vSegs.empty()) {
    vSegs.push_back(seg);
    return;
  }

  int index = GetSegmentIndexAtRow(seg.type, seg.row);
  if (index == INVALID_INDEX) {
    // Before every existing segment of this type (the original asserts
    // here). Only keep it if it means something.
    if (seg.IsNotable()) vSegs.insert(vSegs.begin(), seg);
    return;
  }
  const TimingSegment cur = vSegs[index];

  bool bIsNotable = seg.IsNotable();
  bool bOnSameRow = seg.row == cur.row;

  // ignore changes that are zero and don't overwrite an existing segment
  if (!bIsNotable && !bOnSameRow) {
    return;
  }

  switch (seg.GetEffectType()) {
    case SegmentEffectType_Row:
    case SegmentEffectType_Range: {
      // if we're overwriting a change with a non-notable
      // one, take it to mean deleting the existing segment
      if (bOnSameRow && !bIsNotable) {
        vSegs.erase(vSegs.begin() + index);
        return;
      }

      break;
    }
    case SegmentEffectType_Indefinite: {
      int prevIndex = index;

      // get the segment before last; if we're on the same
      // row, get the segment in effect before 'cur'
      if (bOnSameRow && index > 0) {
        prevIndex = index - 1;
      }
      const TimingSegment prev = vSegs[prevIndex];
      const bool prevIsCur = prevIndex == index;

      // If there is another segment after this one, it might become
      // redundant when this one is inserted.
      // If the next segment is redundant, we want to move its starting row
      // to the row the new segment is being added at instead of erasing it
      // and adding the new segment.
      // If the new segment is also redundant, erase the next segment because
      // that effectively moves it back to the prev segment. -Kyz
      if (static_cast<size_t>(index) < vSegs.size() - 1) {
        TimingSegment& next = vSegs[index + 1];
        if (seg.SameValue(next)) {
          // The segment after this new one is redundant.
          if (seg.SameValue(prev)) {
            // This new segment is redundant.  Erase the next segment and
            // ignore this new one.
            vSegs.erase(vSegs.begin() + index + 1);
            if (!prevIsCur) {
              vSegs.erase(vSegs.begin() + index);
            }
            return;
          } else {
            // Move the next segment's start back to this row.
            next.row = seg.row;
            if (!prevIsCur) {
              vSegs.erase(vSegs.begin() + index);
            }
            return;
          }
        } else {
          // if true, this is redundant segment change
          if (prev.SameValue(seg)) {
            if (!prevIsCur) {
              vSegs.erase(vSegs.begin() + index);
            }
            return;
          }
        }
      } else {
        // if true, this is redundant segment change
        if (prev.SameValue(seg)) {
          if (!prevIsCur) {
            vSegs.erase(vSegs.begin() + index);
          }
          return;
        }
      }
      break;
    }
  }

  // the segment at or before this row is equal to the new one; ignore it
  if (bOnSameRow && cur.SameValue(seg)) {
    return;
  }

  if (bOnSameRow) {
    // replace the existing segment
    vSegs[index] = seg;
  } else {
    // insert a new segment (after any with the same row)
    auto it = std::upper_bound(
        vSegs.begin(), vSegs.end(), seg,
        [](const TimingSegment& a, const TimingSegment& b) {
          return a.row < b.row;
        });
    vSegs.insert(it, seg);
  }
}

enum {
  FOUND_WARP,
  FOUND_WARP_DESTINATION,
  FOUND_BPM_CHANGE,
  FOUND_STOP,
  FOUND_DELAY,
  FOUND_MARKER,
  NOT_FOUND
};

// Find the next timing event after `start`. On ties the checks below win in
// order: warp end, BPM change, delay, the marker (the beat we are looking
// for), stop, warp start. That is what makes a note on a stop come before
// the stop and a note on a delay come after it.
static void FindEvent(int& event_row, int& event_type,
                      TimingData::GetBeatStarts& start, double beat,
                      bool find_marker,
                      const std::vector<TimingSegment>& bpms,
                      const std::vector<TimingSegment>& warps,
                      const std::vector<TimingSegment>& stops,
                      const std::vector<TimingSegment>& delays) {
  if (start.is_warping && BeatToNoteRow(start.warp_destination) < event_row) {
    event_row = BeatToNoteRow(start.warp_destination);
    event_type = FOUND_WARP_DESTINATION;
  }
  if (start.bpm < bpms.size() && bpms[start.bpm].row < event_row) {
    event_row = bpms[start.bpm].row;
    event_type = FOUND_BPM_CHANGE;
  }
  if (start.delay < delays.size() && delays[start.delay].row < event_row) {
    event_row = delays[start.delay].row;
    event_type = FOUND_DELAY;
  }
  if (find_marker && BeatToNoteRow(beat) < event_row) {
    event_row = BeatToNoteRow(beat);
    event_type = FOUND_MARKER;
  }
  if (start.stop < stops.size() && stops[start.stop].row < event_row) {
    event_row = stops[start.stop].row;
    event_type = FOUND_STOP;
  }
  if (start.warp < warps.size() && warps[start.warp].row < event_row) {
    event_row = warps[start.warp].row;
    event_type = FOUND_WARP;
  }
}

void TimingData::InvalidateLookups() {
  m_time_start_lookup.clear();
  m_beat_start_lookup.clear();
}

static const TimingData::GetBeatStarts& LookupResumeStart(
    const std::vector<std::pair<double, TimingData::GetBeatStarts>>& lookup,
    double key) {
  auto it = std::lower_bound(
      lookup.begin(), lookup.end(), key,
      [](const std::pair<double, TimingData::GetBeatStarts>& item, double k) {
        return item.first < k;
      });
  if (it != lookup.begin()) {
    --it;
  }
  return it->second;
}

void TimingData::EnsureLookups() const {
  if (!m_time_start_lookup.empty()) {
    return;
  }

  const std::vector<TimingSegment>& bpms = m_segments[SEGMENT_BPM];
  const std::vector<TimingSegment>& warps = m_segments[SEGMENT_WARP];
  const std::vector<TimingSegment>& stops = m_segments[SEGMENT_STOP];
  const std::vector<TimingSegment>& delays = m_segments[SEGMENT_DELAY];
  size_t event_count = bpms.size() + warps.size() + stops.size() + delays.size();
  m_time_start_lookup.reserve(event_count + 1);
  m_beat_start_lookup.reserve(event_count + 1);

  GetBeatStarts start;
  m_time_start_lookup.push_back(
      lookup_item_t(NoteRowToBeat(start.last_row), start));
  m_beat_start_lookup.push_back(lookup_item_t(start.last_time, start));
  double bps = GetBPMAtRow(start.last_row) / 60.0;
  while (true) {
    int event_row = INT_MAX;
    int event_type = NOT_FOUND;
    FindEvent(event_row, event_type, start, DBL_MAX, false, bpms, warps,
              stops, delays);
    if (event_type == NOT_FOUND) {
      break;
    }
    start.last_time +=
        start.is_warping ? 0 : RowSpanToBeats(start.last_row, event_row) / bps;
    switch (event_type) {
      case FOUND_WARP_DESTINATION:
        start.is_warping = false;
        break;
      case FOUND_BPM_CHANGE:
        bps = bpms[start.bpm].value;
        ++start.bpm;
        break;
      case FOUND_STOP:
        start.last_time += stops[start.stop].value;
        ++start.stop;
        break;
      case FOUND_DELAY:
        start.last_time += delays[start.delay].value;
        ++start.delay;
        break;
      case FOUND_WARP: {
        start.is_warping = true;
        start.warp_begin_row = event_row;
        const TimingSegment& ws = warps[start.warp];
        double warp_sum = NoteRowToBeat(ws.length) + ws.GetBeat();
        if (warp_sum > start.warp_destination) {
          start.warp_destination = warp_sum;
        }
        ++start.warp;
        break;
      }
    }
    start.last_row = event_row;
    m_time_start_lookup.push_back(
        lookup_item_t(NoteRowToBeat(start.last_row), start));
    m_beat_start_lookup.push_back(lookup_item_t(start.last_time, start));
  }
}

void TimingData::GetBeatInternal(GetBeatStarts& start, GetBeatArgs& args) const {
  const std::vector<TimingSegment>& bpms = m_segments[SEGMENT_BPM];
  const std::vector<TimingSegment>& warps = m_segments[SEGMENT_WARP];
  const std::vector<TimingSegment>& stops = m_segments[SEGMENT_STOP];
  const std::vector<TimingSegment>& delays = m_segments[SEGMENT_DELAY];

  double bps = GetBPMAtRow(start.last_row) / 60.0;
  while (true) {
    int event_row = INT_MAX;
    int event_type = NOT_FOUND;
    FindEvent(event_row, event_type, start, 0, false, bpms, warps, stops,
              delays);
    if (event_type == NOT_FOUND) {
      break;
    }
    double time_to_next_event =
        start.is_warping ? 0 : RowSpanToBeats(start.last_row, event_row) / bps;
    double next_event_time = start.last_time + time_to_next_event;
    if (args.elapsed_time < next_event_time) {
      break;
    }
    start.last_time = next_event_time;
    switch (event_type) {
      case FOUND_WARP_DESTINATION:
        start.is_warping = false;
        break;
      case FOUND_BPM_CHANGE:
        bps = bpms[start.bpm].value;
        ++start.bpm;
        break;
      case FOUND_DELAY: {
        const TimingSegment& ss = delays[start.delay];
        time_to_next_event = ss.value;
        next_event_time = start.last_time + time_to_next_event;
        if (args.elapsed_time < next_event_time) {
          args.freeze_out = false;
          args.delay_out = true;
          args.beat = ss.GetBeat();
          args.bps_out = bps;
          return;
        }
        start.last_time = next_event_time;
        ++start.delay;
        break;
      }
      case FOUND_STOP: {
        const TimingSegment& ss = stops[start.stop];
        time_to_next_event = ss.value;
        next_event_time = start.last_time + time_to_next_event;
        if (args.elapsed_time < next_event_time) {
          args.freeze_out = true;
          args.delay_out = false;
          args.beat = ss.GetBeat();
          args.bps_out = bps;
          return;
        }
        start.last_time = next_event_time;
        ++start.stop;
        break;
      }
      case FOUND_WARP: {
        start.is_warping = true;
        const TimingSegment& ws = warps[start.warp];
        double warp_sum = NoteRowToBeat(ws.length) + ws.GetBeat();
        if (warp_sum > start.warp_destination) {
          start.warp_destination = warp_sum;
        }
        args.warp_begin_out = event_row;
        args.warp_dest_out = start.warp_destination;
        ++start.warp;
        break;
      }
    }
    start.last_row = event_row;
  }
  args.beat = NoteRowToBeat(start.last_row) +
              (args.elapsed_time - start.last_time) * bps;
  args.bps_out = bps;
}

void TimingData::GetBeatAndBPSFromElapsedTime(GetBeatArgs& args) const {
  EnsureLookups();
  double offset = m_fBeat0OffsetInSeconds + m_fBeat0GroupOffsetInSeconds;
  args.elapsed_time += offset;
  GetBeatStarts start = LookupResumeStart(m_beat_start_lookup, args.elapsed_time);
  args.warp_begin_out = start.warp_begin_row;
  args.warp_dest_out = start.warp_destination;
  GetBeatInternal(start, args);
  args.elapsed_time -= offset;
}

double TimingData::GetElapsedTimeInternal(GetBeatStarts& start,
                                          double beat) const {
  const std::vector<TimingSegment>& bpms = m_segments[SEGMENT_BPM];
  const std::vector<TimingSegment>& warps = m_segments[SEGMENT_WARP];
  const std::vector<TimingSegment>& stops = m_segments[SEGMENT_STOP];
  const std::vector<TimingSegment>& delays = m_segments[SEGMENT_DELAY];

  double bps = GetBPMAtRow(start.last_row) / 60.0;

  while (true) {
    int event_row = INT_MAX;
    int event_type = NOT_FOUND;
    FindEvent(event_row, event_type, start, beat, true, bpms, warps, stops,
              delays);
    if (event_type == NOT_FOUND) {
      // Only reachable if the marker row is INT_MAX, which BeatToNoteRow
      // never returns; kept as a guard against an endless loop.
      return start.last_time;
    }
    double time_to_next_event =
        start.is_warping ? 0 : RowSpanToBeats(start.last_row, event_row) / bps;
    double next_event_time = start.last_time + time_to_next_event;
    start.last_time = next_event_time;
    switch (event_type) {
      case FOUND_WARP_DESTINATION:
        start.is_warping = false;
        break;
      case FOUND_BPM_CHANGE:
        bps = bpms[start.bpm].value;
        ++start.bpm;
        break;
      case FOUND_STOP:
        time_to_next_event = stops[start.stop].value;
        next_event_time = start.last_time + time_to_next_event;
        start.last_time = next_event_time;
        ++start.stop;
        break;
      case FOUND_DELAY:
        time_to_next_event = delays[start.delay].value;
        next_event_time = start.last_time + time_to_next_event;
        start.last_time = next_event_time;
        ++start.delay;
        break;
      case FOUND_MARKER:
        return start.last_time;
      case FOUND_WARP: {
        start.is_warping = true;
        const TimingSegment& ws = warps[start.warp];
        double warp_sum = NoteRowToBeat(ws.length) + ws.GetBeat();
        if (warp_sum > start.warp_destination) {
          start.warp_destination = warp_sum;
        }
        ++start.warp;
        break;
      }
    }
    start.last_row = event_row;
  }
}

double TimingData::GetElapsedTimeFromBeat(double fBeat) const {
  EnsureLookups();
  GetBeatStarts start = LookupResumeStart(m_time_start_lookup, fBeat);
  GetElapsedTimeInternal(start, fBeat);
  return start.last_time - m_fBeat0OffsetInSeconds -
         m_fBeat0GroupOffsetInSeconds;
}

double TimingData::GetDisplayedBeat(double fBeat) const {
  const std::vector<TimingSegment>& scrolls = m_segments[SEGMENT_SCROLL];
  if (scrolls.empty()) return fBeat;
  double fOutBeat = 0;
  unsigned i;
  for (i = 0; i < scrolls.size() - 1; i++) {
    if (scrolls[i + 1].GetBeat() > fBeat) {
      break;
    }
    fOutBeat += (scrolls[i + 1].GetBeat() - scrolls[i].GetBeat()) *
                scrolls[i].value;
  }
  fOutBeat += (fBeat - scrolls[i].GetBeat()) * scrolls[i].value;
  return fOutBeat;
}

void TimingData::TidyUpData(bool allowEmpty) {
  // Empty TimingData is used to implement steps with no timing of their
  // own.  Don't override this.
  if (allowEmpty && empty()) {
    return;
  }
  InvalidateLookups();

  // If there are no BPM segments, provide a default.
  if (m_segments[SEGMENT_BPM].empty()) {
    AddSegment(TimingSegment::BPM(0, 60));
  }

  // Make sure the first BPM segment starts at beat 0.
  if (m_segments[SEGMENT_BPM][0].row != 0) {
    m_segments[SEGMENT_BPM][0].row = 0;
  }

  // If no time signature specified, assume default time for the whole song.
  if (m_segments[SEGMENT_TIME_SIG].empty()) {
    AddSegment(TimingSegment::TimeSig(0));
  }

  // Likewise, if no tickcount signature is specified, assume 4 ticks
  // per beat for the entire song.
  if (m_segments[SEGMENT_TICKCOUNT].empty()) {
    AddSegment(TimingSegment::Tickcount(0));
  }

  // Have a default combo segment of one just in case.
  if (m_segments[SEGMENT_COMBO].empty()) {
    AddSegment(TimingSegment::Combo(0));
  }

  // Have a default label segment just in case.
  if (m_segments[SEGMENT_LABEL].empty()) {
    AddSegment(TimingSegment::Label(0, "Song Start"));
  }

  // Always be sure there is a starting speed.
  if (m_segments[SEGMENT_SPEED].empty()) {
    AddSegment(TimingSegment::Speed(0));
  }

  // Always be sure there is a starting scrolling factor.
  if (m_segments[SEGMENT_SCROLL].empty()) {
    AddSegment(TimingSegment::Scroll(0));
  }
}

}  // namespace ddr

/*
 * Original notice (ITGmania src/TimingData.cpp):
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
 *
 * Original notice (ITGmania src/TimingSegments.h):
 *
 * @author Jason Felds (c) 2011
 * (Distributed under the same permission notice as above.)
 */
