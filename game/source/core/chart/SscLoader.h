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
// .ssc loader. Port of ITGmania v1.3.0 src/NotesLoaderSSC.{h,cpp}. See
// SscLoader.cpp for the original notice and the list of changes.

#ifndef DDR_CORE_CHART_SSCLOADER_H
#define DDR_CORE_CHART_SSCLOADER_H

#include <string>

#include "Chart.h"

namespace ddr {

// .ssc versions at which the format changed (NotesLoaderSSC.h).
constexpr float VERSION_WARP_SEGMENT = 0.56f;
constexpr float VERSION_SPLIT_TIMING = 0.7f;
constexpr float VERSION_CHART_NAME_TAG = 0.74f;

/** Load a .ssc file held in memory. Returns false if it has no charts.
 *  Per-chart timing tags (split timing, #VERSION >= 0.7) give the chart its
 *  own TimingData; otherwise the chart uses the song's. */
bool LoadSscFromString(const std::string& text, Song& out);

}  // namespace ddr

#endif
