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
// .sm loader. Port of ITGmania v1.3.0 src/NotesLoaderSM.{h,cpp}. See
// SmLoader.cpp for the original notice and the list of changes.

#ifndef DDR_CORE_CHART_SMLOADER_H
#define DDR_CORE_CHART_SMLOADER_H

#include <string>
#include <utility>
#include <vector>

#include "Chart.h"
#include "../util/TextFile.h"  // ReadFileToString, which used to live here

namespace ddr {

/** Load a .sm file held in memory. Returns false if it has no charts. */
bool LoadSmFromString(const std::string& text, Song& out);

/** Load a .sm or .ssc file from disk (chosen by extension, .ssc if unsure).
 *  Returns false if the file can't be read or has no charts. */
bool LoadSongFile(const std::string& path, Song& out);

/** Of the files in a song folder, the one StepMania would load: .ssc before
 *  .sm. Returns "" if there is neither. */
std::string PickSimfile(const std::vector<std::string>& filenames);


// Timing tag helpers shared by the .sm and .ssc loaders (SMLoader members in
// StepMania). Problems are appended to *warnings.
namespace sm {

/** Largest BPM that is still a real BPM; above it StepMania treats a BPM
 *  as an SM4-style warp. */
constexpr double FAST_BPM_WARP = 9999999.0;

using BeatValueList = std::vector<std::pair<double, double>>;

double RowToBeat(std::string line, int rowsPerBeat = -1);
void ParseBPMs(BeatValueList& out, const std::string& line,
               std::vector<std::string>* warnings, int rowsPerBeat = -1);
void ParseStops(BeatValueList& out, const std::string& line,
                std::vector<std::string>* warnings, int rowsPerBeat = -1);
/** Turn .sm BPMs and stops (possibly negative) into BPM, stop and warp
 *  segments. Negative BPMs and stops become warps; stops before beat 0 move
 *  the offset. */
void ProcessBPMsAndStops(TimingData& out, BeatValueList& vBPMs,
                         BeatValueList& vStops,
                         std::vector<std::string>* warnings);
void ProcessDelays(TimingData& out, const std::string& line,
                   std::vector<std::string>* warnings, int rowsPerBeat = -1);
void ProcessTimeSignatures(TimingData& out, const std::string& line,
                           std::vector<std::string>* warnings,
                           int rowsPerBeat = -1);
void ProcessTickcounts(TimingData& out, const std::string& line,
                       std::vector<std::string>* warnings, int rowsPerBeat = -1);
void ProcessSpeeds(TimingData& out, const std::string& line,
                   std::vector<std::string>* warnings, int rowsPerBeat = -1);
void ProcessFakes(TimingData& out, const std::string& line,
                  std::vector<std::string>* warnings, int rowsPerBeat = -1);

}  // namespace sm

}  // namespace ddr

#endif
