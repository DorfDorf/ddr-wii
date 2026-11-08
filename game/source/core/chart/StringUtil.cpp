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
// Derived from ITGmania v1.3.0 src/RageUtil.cpp
// (https://github.com/itgmania/itgmania, GPLv3), whose original notice is
// reproduced at the end of this file.
//
// Changes from the original:
//  - Only split/Trim/StringToFloat/StringToInt/HHMMSSToSeconds are kept.
//  - StringToFloat parses as double (strtod); values that would not be finite
//    as a float still become 0, as in the original.
//  - StringToInt uses strtol instead of std::stoi, so it never throws (the
//    Wii build may run without exceptions) and logs nothing.

#include "StringUtil.h"

#include <cctype>
#include <cerrno>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ddr {

void Split(const std::string& sSource, char cDelim,
           std::vector<std::string>& out, bool bIgnoreEmpty) {
  // Short-circuit if the source is empty; we want to return an empty vector
  // if the string is empty, even if bIgnoreEmpty is false.
  if (sSource.empty()) return;

  size_t startpos = 0;
  do {
    size_t pos = sSource.find(cDelim, startpos);
    if (pos == std::string::npos) pos = sSource.size();
    if (pos > startpos || !bIgnoreEmpty)
      out.push_back(sSource.substr(startpos, pos - startpos));
    startpos = pos + 1;
  } while (startpos <= sSource.size());
}

void TrimLeft(std::string& s, const char* chars) {
  size_t n = 0;
  while (n < s.size() && s[n] != '\0' && std::strchr(chars, s[n])) n++;
  s.erase(0, n);
}

void TrimRight(std::string& s, const char* chars) {
  size_t n = s.size();
  while (n > 0 && s[n - 1] != '\0' && std::strchr(chars, s[n - 1])) n--;
  s.erase(n);
}

void Trim(std::string& s, const char* chars) {
  TrimRight(s, chars);
  TrimLeft(s, chars);
}

std::string Trimmed(std::string s) {
  Trim(s);
  return s;
}

void MakeUpper(std::string& s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

bool EqualsNoCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::toupper(static_cast<unsigned char>(a[i])) !=
        std::toupper(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

double StringToDouble(const std::string& s) {
  double d = std::strtod(s.c_str(), nullptr);
  if (!std::isfinite(d) || std::fabs(d) > FLT_MAX) d = 0.0;
  return d;
}

int StringToInt(const std::string& s) {
  const char* p = s.c_str();
  char* end = nullptr;
  errno = 0;
  long v = std::strtol(p, &end, 10);
  if (end == p || errno == ERANGE || v > INT_MAX || v < INT_MIN) return 0;
  return static_cast<int>(v);
}

double HHMMSSToSeconds(const std::string& s) {
  std::vector<std::string> bits;
  Split(s, ':', bits, false);
  while (bits.size() < 3) bits.insert(bits.begin(), "0");  // pad missing bits
  double fSeconds = 0;
  fSeconds += StringToInt(bits[0]) * 60.0 * 60.0;
  fSeconds += StringToInt(bits[1]) * 60.0;
  fSeconds += StringToDouble(bits[2]);
  return fSeconds;
}

}  // namespace ddr

/*
 * Original notice (ITGmania src/RageUtil.cpp):
 *
 * Copyright (c) 2001-2005 Chris Danford, Glenn Maynard
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
