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
// Derived from ITGmania v1.3.0 src/MsdFile.cpp
// (https://github.com/itgmania/itgmania, GPLv3), whose original notice is
// reproduced at the end of this file.
//
// Changes from the original:
//  - No RageFile: callers read the file and pass the bytes (ReadFile removed).
//  - The scratch buffer is a std::vector instead of new[]/delete[].
//  - AddParam is a no-op if no value has been started (cannot happen with
//    the original control flow; kept as a guard for fuzzed input).
//  - Namespaced in ddr::. The parsing logic is otherwise unchanged,
//    including the escape handling and the recovery from a missing ';'.

/*
 * The original MSD format is simply:
 *
 * #PARAM0:PARAM1:PARAM2:PARAM3;
 * #NEXTPARAM0:PARAM1:PARAM2:PARAM3;
 *
 * (The first field is typically an identifier, but doesn't have to be.)
 *
 * The semicolon is not optional, though if we hit a # on a new line, eg:
 * #VALUE:PARAM1
 * #VALUE2:PARAM2
 * we'll recover.
 */

#include "MsdFile.h"

namespace ddr {

void MsdFile::AddParam(const char* buf, int len) {
  if (values.empty()) return;
  if (len < 0) len = 0;
  values.back().params.push_back(std::string(buf, static_cast<size_t>(len)));
}

void MsdFile::AddValue() /* (no extra charge) */
{
  values.push_back(value_t());
  values.back().params.reserve(32);
}

void MsdFile::ReadBuf(const char* buf, int len, bool bUnescape) {
  values.reserve(64);

  bool ReadingValue = false;
  int i = 0;
  std::vector<char> processed(static_cast<size_t>(len > 0 ? len : 0) + 1);
  char* cProcessed = processed.data();
  int iProcessedLen = -1;
  while (i < len) {
    if (i + 1 < len && buf[i] == '/' && buf[i + 1] == '/') {
      /* Skip a comment entirely; don't copy the comment to the
       * value/parameter */
      do {
        i++;
      } while (i < len && buf[i] != '\n');

      continue;
    }

    if (ReadingValue && buf[i] == '#') {
      /* Unfortunately, many of these files are missing ;'s.
       * If we get a # when we thought we were inside a value, assume we
       * missed the ;.  Back up and end the value. */
      // Make sure this # is the first non-whitespace character on the line.
      bool FirstChar = true;
      int j = iProcessedLen;
      while (j > 0 && cProcessed[j - 1] != '\r' && cProcessed[j - 1] != '\n') {
        if (cProcessed[j - 1] == ' ' || cProcessed[j - 1] == '\t') {
          --j;
          continue;
        }

        FirstChar = false;
        break;
      }

      if (!FirstChar) {
        /* We're not the first char on a line.  Treat it as if it were a
         * normal character. */
        cProcessed[iProcessedLen++] = buf[i++];
        continue;
      }

      /* Skip newlines and whitespace before adding the value. */
      iProcessedLen = j;
      while (iProcessedLen > 0 && (cProcessed[iProcessedLen - 1] == '\r' ||
                                   cProcessed[iProcessedLen - 1] == '\n' ||
                                   cProcessed[iProcessedLen - 1] == ' ' ||
                                   cProcessed[iProcessedLen - 1] == '\t')) {
        --iProcessedLen;
      }

      AddParam(cProcessed, iProcessedLen);
      iProcessedLen = 0;
      ReadingValue = false;
    }

    /* # starts a new value. */
    if (!ReadingValue && buf[i] == '#') {
      AddValue();
      ReadingValue = true;
    }

    if (!ReadingValue) {
      if (bUnescape && buf[i] == '\\') {
        i += 2;
      } else {
        ++i;
      }
      continue; /* nothing else is meaningful outside of a value */
    }

    /* : and ; end the current param, if any. */
    if (iProcessedLen != -1 && (buf[i] == ':' || buf[i] == ';')) {
      AddParam(cProcessed, iProcessedLen);
    }

    /* # and : begin new params. */
    if (buf[i] == '#' || buf[i] == ':') {
      ++i;
      iProcessedLen = 0;
      continue;
    }

    /* ; ends the current value. */
    if (buf[i] == ';') {
      ReadingValue = false;
      ++i;
      continue;
    }

    /* We've gone through all the control characters.  All that is left is
     * either an escaped character, ie \#, \\, \:, etc., or a regular
     * character. */
    if (buf[i] == '\\' && i < len) {
      // If we're escaping the next character, skip the '\\'
      if (bUnescape) {
        ++i;
      }
      // Otherwise, add the '\\' to cProcessed here, so that
      // whatever character is coming next stays escaped in
      // the resulting value/parameter string
      // (and most importantly, it doesn't get parsed as a control character)
      // on the next iteration
      else {
        cProcessed[iProcessedLen++] = buf[i++];
      }
    }

    if (i < len) {
      cProcessed[iProcessedLen++] = buf[i++];
    }
  }

  /* Add any unterminated value at the very end. */
  if (ReadingValue) {
    AddParam(cProcessed, iProcessedLen);
  }
}

void MsdFile::ReadFromString(const std::string& sString, bool bUnescape) {
  ReadBuf(sString.data(), static_cast<int>(sString.size()), bUnescape);
}

std::string MsdFile::GetParam(unsigned val, unsigned par) const {
  if (val >= GetNumValues() || par >= GetNumParams(val)) {
    return std::string();
  }

  return values[val].params[par];
}

}  // namespace ddr

/*
 * Original notice (ITGmania src/MsdFile.cpp):
 *
 * (c) 2001-2006 Chris Danford, Glenn Maynard
 *
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
