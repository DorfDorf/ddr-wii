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
// Derived from ITGmania v1.3.0 src/NoteDataUtil.cpp
// (LoadFromSMNoteDataString / LoadFromSMNoteDataStringWithPlayer) and
// src/NoteData.cpp (IsHoldNoteAtRow, SetTapNote)
// (https://github.com/itgmania/itgmania, GPLv3), whose original notice is
// reproduced at the end of this file.
//
// Changes from the original:
//  - Output is a flat, sorted std::vector<Note> instead of NoteData.
//  - A line's row is computed with exact integer arithmetic (nearest row,
//    ties to even) instead of float (m + l/size) * 4 * 48. They agree for
//    every ordinary quantisation; for huge measure numbers the float version
//    can be one row off.
//  - The "[n]" keysound index is read with a bounded parser instead of
//    sscanf, which could read past the end of the line.
//  - Measures past MAX_NOTE_ROW are ignored instead of overflowing.
//  - Composite (routine) charts are merged without the player tags.

#include "NoteData.h"

#include <algorithm>
#include <cstring>
#include <map>

namespace ddr {

namespace {

struct TapNote {
  NoteType type = NoteType::Tap;
  int duration = 0;  // holds: rows to the tail; MAX_NOTE_ROW until it is found
  int keysound = -1;
};

using TrackMap = std::map<int, TapNote>;

// Starting just above iRow, search upwards. If we find a hold head, we're
// within a hold. If we find a tap, mine, lift or fake, we're not -- those
// never lie within hold notes. Ignore auto keysounds.
bool IsHoldNoteAtRow(const TrackMap& track, int iRow, int* pHeadRow) {
  auto it = track.lower_bound(iRow);
  while (it != track.begin()) {
    --it;
    const int r = it->first;
    const TapNote& tn = it->second;
    switch (tn.type) {
      case NoteType::HoldHead:
      case NoteType::RollHead:
        if (static_cast<long long>(tn.duration) + r < iRow) return false;
        *pHeadRow = r;
        return true;
      case NoteType::Keysound:
        continue;
      default:
        return false;
    }
  }
  return false;
}

// Row of line l of a measure with `size` lines: nearest of l * 192 / size,
// ties to even (what lrint does).
int RowInMeasure(int l, int size) {
  const long long num = static_cast<long long>(l) * 4 * ROWS_PER_BEAT;
  long long q = num / size;
  const long long r = num % size;
  if (2 * r > size || (2 * r == size && (q & 1))) ++q;
  return static_cast<int>(q);
}

bool IsSpace(char c) { return c == '\r' || c == '\n' || c == '\t' || c == ' '; }

void LoadPlayer(std::vector<TrackMap>& tracks, const std::string& s,
                size_t start, size_t end, int iNumTracks) {
  const int kRowsPerMeasure = 4 * ROWS_PER_BEAT;
  std::vector<std::pair<const char*, const char*>> aMeasureLines;
  int m = 0;
  size_t pos = start;
  while (pos < end) {
    // Split on ',' ignoring empty pieces (so ",,," counts as one measure
    // break, as in StepMania).
    while (pos < end && s[pos] == ',') ++pos;
    if (pos >= end) break;
    size_t mEnd = s.find(',', pos);
    if (mEnd == std::string::npos || mEnd > end) mEnd = end;

    if (m > (MAX_NOTE_ROW / kRowsPerMeasure) - 1) break;

    // Collect the non-empty, trimmed lines of this measure.
    aMeasureLines.clear();
    size_t lpos = pos;
    while (lpos < mEnd) {
      size_t lEnd = s.find('\n', lpos);
      if (lEnd == std::string::npos || lEnd > mEnd) lEnd = mEnd;
      const char* beginLine = s.data() + lpos;
      const char* endLine = s.data() + lEnd;
      while (beginLine < endLine && IsSpace(*beginLine)) ++beginLine;
      while (endLine > beginLine && IsSpace(*(endLine - 1))) --endLine;
      if (beginLine < endLine) aMeasureLines.emplace_back(beginLine, endLine);
      lpos = lEnd + 1;
    }

    const int size = static_cast<int>(aMeasureLines.size());
    for (int l = 0; l < size; l++) {
      const char* p = aMeasureLines[l].first;
      const char* const endLine = aMeasureLines[l].second;
      const int iIndex = m * kRowsPerMeasure + RowInMeasure(l, size);

      int iTrack = 0;
      while (iTrack < iNumTracks && p < endLine) {
        TapNote tn;
        bool empty = false;
        const char ch = *p;

        switch (ch) {
          case '0': empty = true; break;
          case '1': tn.type = NoteType::Tap; break;
          case '2':
          case '4':
            tn.type = ch == '2' ? NoteType::HoldHead : NoteType::RollHead;
            /* Set the hold note to have infinite length. We'll clamp
             * it when we hit the tail. */
            tn.duration = MAX_NOTE_ROW;
            break;
          case '3': {
            // This is the end of a hold. Search for the beginning.
            int iHeadRow;
            if (IsHoldNoteAtRow(tracks[iTrack], iIndex, &iHeadRow)) {
              tracks[iTrack][iHeadRow].duration = iIndex - iHeadRow;
            }
            // (An unmatched 3 is ignored.) This won't write tn, but keep
            // parsing normally anyway.
            empty = true;
            break;
          }
          case 'M': tn.type = NoteType::Mine; break;
          case 'K': tn.type = NoteType::Keysound; break;
          case 'L': tn.type = NoteType::Lift; break;
          case 'F': tn.type = NoteType::Fake; break;
          default:
            /* Invalid data. We don't want to assert, since there might
             * simply be invalid data in an .SM, and we don't want to die
             * due to invalid data. */
            empty = true;
            break;
        }

        p++;

        // look for optional keysound index (e.g. "[123]")
        if (p < endLine && *p == '[') {
          p++;
          const char* q = p;
          bool neg = false;
          if (q < endLine && (*q == '-' || *q == '+')) neg = (*q++ == '-');
          long v = 0;
          bool digits = false;
          while (q < endLine && *q >= '0' && *q <= '9') {
            if (v < 100000000) v = v * 10 + (*q - '0');
            ++q;
            digits = true;
          }
          if (digits && q < endLine && *q == ']') {
            tn.keysound = static_cast<int>(neg ? -v : v);
          }
          // skip past the ']'
          while (p < endLine) {
            if (*(p++) == ']') break;
          }
        }

        if (!empty) {
          tracks[iTrack][iIndex] = tn;  // replaces anything already there
        }

        iTrack++;
      }
    }

    ++m;
    pos = mEnd;
  }

  // Make sure we don't have any hold notes that didn't find a tail.
  for (TrackMap& track : tracks) {
    for (auto it = track.begin(); it != track.end();) {
      const TapNote& tn = it->second;
      if ((tn.type == NoteType::HoldHead || tn.type == NoteType::RollHead) &&
          tn.duration == MAX_NOTE_ROW) {
        it = track.erase(it);
      } else {
        ++it;
      }
    }
  }
}

std::string StripComments(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  size_t pos = 0;
  while (pos < in.size()) {
    size_t c = in.find("//", pos);
    if (c == std::string::npos) {
      out.append(in, pos, std::string::npos);
      break;
    }
    out.append(in, pos, c - pos);
    size_t nl = in.find('\n', c);
    pos = (nl == std::string::npos) ? in.size() : nl + 1;
  }
  return out;
}

}  // namespace

std::vector<Note> ParseNoteData(const std::string& sSMNoteData_, int iNumTracks,
                                bool bComposite) {
  std::vector<Note> out;
  if (iNumTracks <= 0) return out;
  if (iNumTracks > 64) iNumTracks = 64;
  const std::string s = StripComments(sSMNoteData_);

  std::vector<TrackMap> tracks(static_cast<size_t>(iNumTracks));
  if (!bComposite) {
    LoadPlayer(tracks, s, 0, s.size(), iNumTracks);
  } else {
    size_t start = 0;
    for (;;) {
      size_t amp = s.find('&', start);
      size_t end = (amp == std::string::npos) ? s.size() : amp;
      // Each player gets its own hold matching, then the parts are merged
      // (later players win on collisions).
      std::vector<TrackMap> part(static_cast<size_t>(iNumTracks));
      LoadPlayer(part, s, start, end, iNumTracks);
      for (int t = 0; t < iNumTracks; ++t)
        for (auto& kv : part[t]) tracks[t][kv.first] = kv.second;
      if (amp == std::string::npos) break;
      start = amp + 1;
    }
  }

  for (int t = 0; t < iNumTracks; ++t) {
    for (const auto& kv : tracks[t]) {
      Note n;
      n.row = kv.first;
      n.beat = NoteRowToBeat(n.row);
      n.lane = t;
      n.type = kv.second.type;
      n.keysound = kv.second.keysound;
      n.endRow = n.IsHold() ? n.row + kv.second.duration : n.row;
      n.endBeat = NoteRowToBeat(n.endRow);
      out.push_back(n);
    }
  }
  std::sort(out.begin(), out.end(), [](const Note& a, const Note& b) {
    return a.row != b.row ? a.row < b.row : a.lane < b.lane;
  });
  return out;
}

int GuessNumTracks(const std::string& sSMNoteData) {
  const std::string s = StripComments(sSMNoteData);
  size_t pos = 0;
  while (pos < s.size()) {
    size_t e = s.find_first_of("\n,", pos);
    if (e == std::string::npos) e = s.size();
    size_t b = pos;
    while (b < e && IsSpace(s[b])) ++b;
    size_t f = e;
    while (f > b && IsSpace(s[f - 1])) --f;
    if (f > b) {
      // Don't count "[n]" keysound suffixes.
      int n = 0;
      bool inBracket = false;
      for (size_t i = b; i < f; ++i) {
        if (s[i] == '[') inBracket = true;
        else if (s[i] == ']') inBracket = false;
        else if (!inBracket) ++n;
      }
      return n;
    }
    pos = e + 1;
  }
  return 4;
}

}  // namespace ddr

/*
 * Original notice (ITGmania src/NoteDataUtil.cpp and src/NoteData.cpp):
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
