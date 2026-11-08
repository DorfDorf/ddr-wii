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
// Small string helpers used by the chart loaders. Ported from ITGmania v1.3.0
// src/RageUtil.cpp (split, Trim, StringToFloat, StringToInt, HHMMSSToSeconds).
// See StringUtil.cpp for the original notice and the list of changes.

#ifndef DDR_CORE_CHART_STRINGUTIL_H
#define DDR_CORE_CHART_STRINGUTIL_H

#include <string>
#include <vector>

namespace ddr {

// Split sSource on a one-character delimiter. With bIgnoreEmpty, empty pieces
// are dropped (StepMania's default).
void Split(const std::string& sSource, char cDelim,
           std::vector<std::string>& out, bool bIgnoreEmpty = true);

void TrimLeft(std::string& s, const char* chars = "\r\n\t ");
void TrimRight(std::string& s, const char* chars = "\r\n\t ");
void Trim(std::string& s, const char* chars = "\r\n\t ");
std::string Trimmed(std::string s);

void MakeUpper(std::string& s);
bool EqualsNoCase(const std::string& a, const std::string& b);

// strtod-based, but behaves like StepMania's strtof-based version: anything
// that is not finite as a float becomes 0.
double StringToDouble(const std::string& s);
// Like std::stoi without exceptions: leading whitespace, optional sign,
// digits; trailing junk ignored. Returns 0 on failure or overflow.
int StringToInt(const std::string& s);
// "[[HH:]MM:]SS.sss" to seconds.
double HHMMSSToSeconds(const std::string& s);

}  // namespace ddr

#endif
