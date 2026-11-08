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
// Beat <-> seconds conversion. Port of ITGmania v1.3.0 src/TimingData.h and
// src/TimingSegments.h. See TimingData.cpp for the original notices and the
// list of changes.

#ifndef DDR_CORE_TIMING_TIMINGDATA_H
#define DDR_CORE_TIMING_TIMINGDATA_H

#include <string>
#include <utility>
#include <vector>

namespace ddr {

/** Rows per beat: 1/48 beat = 1/192 measure, as in StepMania. */
constexpr int ROWS_PER_BEAT = 48;
/** Largest row we will ever create (StepMania's MAX_NOTE_ROW). */
constexpr int MAX_NOTE_ROW = 1 << 30;

/** Round a beat to the nearest row (lrint, i.e. ties to even), clamped to
 *  +-MAX_NOTE_ROW so malformed files cannot overflow an int. */
int BeatToNoteRow(double fBeat);
inline double NoteRowToBeat(int iRow) {
  return iRow / static_cast<double>(ROWS_PER_BEAT);
}

enum TimingSegmentType {
  SEGMENT_BPM,
  SEGMENT_STOP,
  SEGMENT_DELAY,
  SEGMENT_TIME_SIG,
  SEGMENT_WARP,
  SEGMENT_LABEL,
  SEGMENT_TICKCOUNT,
  SEGMENT_COMBO,
  SEGMENT_SPEED,
  SEGMENT_SCROLL,
  SEGMENT_FAKE,
  NUM_TimingSegmentType
};

enum SegmentEffectType {
  SegmentEffectType_Row,         // takes effect on a single row
  SegmentEffectType_Range,       // takes effect for a definite amount of rows
  SegmentEffectType_Indefinite,  // takes effect until the next segment
};

/**
 * One timing segment. StepMania uses a class per type; we use one flat value
 * type. Field meaning by type:
 *   BPM:       value = beats per second
 *   STOP/DELAY value = seconds
 *   WARP/FAKE  length = length in rows
 *   TIME_SIG   num/den
 *   TICKCOUNT  num = ticks per beat
 *   COMBO      num = combo, den = miss combo
 *   LABEL      label
 *   SPEED      value = ratio, value2 = delay, num = unit (0 beats, 1 seconds)
 *   SCROLL     value = ratio
 */
struct TimingSegment {
  TimingSegmentType type = SEGMENT_BPM;
  int row = 0;
  double value = 0;
  double value2 = 0;
  int length = 0;
  int num = 0;
  int den = 0;
  std::string label;

  double GetBeat() const { return NoteRowToBeat(row); }
  SegmentEffectType GetEffectType() const;
  bool IsNotable() const;
  /** Same type and same values (the row is not compared). */
  bool SameValue(const TimingSegment& o) const;

  // Constructors mirroring StepMania's segment classes.
  static TimingSegment BPM(int row, double bpm);
  static TimingSegment Stop(int row, double seconds);
  static TimingSegment Delay(int row, double seconds);
  static TimingSegment Warp(int row, double lengthBeats);
  static TimingSegment Fake(int row, double lengthBeats);
  static TimingSegment TimeSig(int row, int num = 4, int den = 4);
  static TimingSegment Tickcount(int row, int ticks = 4);
  static TimingSegment Combo(int row, int combo = 1, int miss = 1);
  static TimingSegment Label(int row, const std::string& label);
  static TimingSegment Speed(int row, double ratio = 1, double delay = 0,
                             int unit = 0);
  static TimingSegment Scroll(int row, double ratio = 1);
};

class TimingData {
 public:
  explicit TimingData(double fOffset = 0) : m_fBeat0OffsetInSeconds(fOffset) {}

  /** The song's #OFFSET (seconds). */
  double m_fBeat0OffsetInSeconds = 0;
  /** The pack's sync offset (pack.ini SyncOffset): 0 or -0.009. */
  double m_fBeat0GroupOffsetInSeconds = 0;

  /** Add a segment, keeping each list sorted and free of redundant entries
   *  (StepMania's AddSegment rules). */
  void AddSegment(const TimingSegment& seg);
  const std::vector<TimingSegment>& GetTimingSegments(TimingSegmentType t) const {
    return m_segments[t];
  }
  bool empty() const;
  void Clear();

  /** Fill in defaults: a 60 BPM segment if there is none, the first BPM moved
   *  to beat 0, and default time signature/tickcount/combo/label/speed/scroll.
   *  With allowEmpty, an empty TimingData (a chart without its own timing)
   *  is left empty. */
  void TidyUpData(bool allowEmpty);

  double GetBPMAtRow(int iRow) const;
  double GetBPMAtBeat(double fBeat) const { return GetBPMAtRow(BeatToNoteRow(fBeat)); }
  double GetStopAtRow(int iRow) const;
  double GetDelayAtRow(int iRow) const;
  bool IsWarpAtRow(int iRow) const;
  bool IsFakeAtRow(int iRow) const;
  /** A note on this row counts for scoring (not in a warp, not in a fake). */
  bool IsJudgableAtRow(int iRow) const { return !IsWarpAtRow(iRow) && !IsFakeAtRow(iRow); }
  /** Index of the segment of type t in effect at iRow, or -1. */
  int GetSegmentIndexAtRow(TimingSegmentType t, int iRow) const;

  struct GetBeatArgs {
    double elapsed_time = 0;
    double beat = 0;
    double bps_out = 0;
    double warp_dest_out = 0;
    int warp_begin_out = -1;
    bool freeze_out = false;
    bool delay_out = false;
  };

  /**
   * Seconds of music at which `fBeat` is reached, with the song offset and
   * the pack offset applied (StepMania's GetElapsedTimeFromBeatNoOffset: the
   * "NoOffset" means no *global* offset; that one is the caller's job).
   * A note on a stop is hit before the stop; a note on a delay after it.
   */
  double GetElapsedTimeFromBeat(double fBeat) const;
  /** Inverse of GetElapsedTimeFromBeat. During a stop/delay the beat stays on
   *  the stop's beat and freeze_out/delay_out is set. */
  void GetBeatAndBPSFromElapsedTime(GetBeatArgs& args) const;
  double GetBeatFromElapsedTime(double fElapsedTime) const {
    GetBeatArgs args;
    args.elapsed_time = fElapsedTime;
    GetBeatAndBPSFromElapsedTime(args);
    return args.beat;
  }
  /** Beat used for drawing, after #SCROLLS. */
  double GetDisplayedBeat(double fBeat) const;

  // Names used in docs/06-technical-design.md.
  double secondsAtBeat(double beat) const { return GetElapsedTimeFromBeat(beat); }
  double beatAtSeconds(double sec) const { return GetBeatFromElapsedTime(sec); }

  struct GetBeatStarts {
    unsigned bpm = 0;
    unsigned warp = 0;
    unsigned stop = 0;
    unsigned delay = 0;
    int last_row = 0;
    double last_time = 0;
    double warp_destination = 0;
    bool is_warping = false;
    // Row of the most recently entered warp.
    int warp_begin_row = -1;
  };

 private:
  void InvalidateLookups();
  void EnsureLookups() const;
  void GetBeatInternal(GetBeatStarts& start, GetBeatArgs& args) const;
  double GetElapsedTimeInternal(GetBeatStarts& start, double beat) const;

  std::vector<TimingSegment> m_segments[NUM_TimingSegmentType];

  // Resume points for the walks above, so lookups don't start at beat 0.
  using lookup_item_t = std::pair<double, GetBeatStarts>;
  mutable std::vector<lookup_item_t> m_beat_start_lookup;  // keyed by time
  mutable std::vector<lookup_item_t> m_time_start_lookup;  // keyed by beat
};

}  // namespace ddr

#endif
