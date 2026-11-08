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
// #NOTES text -> notes. Port of ITGmania v1.3.0
// NoteDataUtil::LoadFromSMNoteDataString. See NoteData.cpp for the original
// notice and the list of changes.

#ifndef DDR_CORE_CHART_NOTEDATA_H
#define DDR_CORE_CHART_NOTEDATA_H

#include <string>
#include <vector>

#include "Chart.h"

namespace ddr {

/**
 * Parse StepMania note data into notes sorted by (row, lane). Only row, beat,
 * lane, type, endRow/endBeat and keysound are filled; times are the caller's
 * job. Holds/rolls without a tail are dropped and tails without a head are
 * ignored, as in StepMania. With bComposite (routine charts) the text is
 * split on '&' and every player's notes are merged.
 */
std::vector<Note> ParseNoteData(const std::string& sSMNoteData, int iNumTracks,
                                bool bComposite = false);

/** Width of the first non-empty note row (used for unknown steps types). */
int GuessNumTracks(const std::string& sSMNoteData);

}  // namespace ddr

#endif
