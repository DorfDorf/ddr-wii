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
// MSD tokenizer (#KEY:param:param;). Port of ITGmania v1.3.0 src/MsdFile.h.
// See MsdFile.cpp for the original notice and the list of changes.

#ifndef DDR_CORE_CHART_MSDFILE_H
#define DDR_CORE_CHART_MSDFILE_H

#include <string>
#include <vector>

namespace ddr {

/** Reads the .SSC, .SM, .SMA, .DWI and .MSD family of files. */
class MsdFile {
 public:
  /** One whole "#param:param:param;" value. */
  struct value_t {
    std::vector<std::string> params;
    /** Out-of-range parameters read as empty strings. */
    std::string operator[](unsigned i) const {
      if (i >= params.size()) return std::string();
      return params[i];
    }
  };

  /** Parse a whole file held in memory. */
  void ReadFromString(const std::string& sString, bool bUnescape);
  /** Parse a raw buffer (may contain NULs, need not be terminated). */
  void ReadBuf(const char* buf, int len, bool bUnescape);

  unsigned GetNumValues() const { return static_cast<unsigned>(values.size()); }
  unsigned GetNumParams(unsigned val) const {
    if (val >= GetNumValues()) return 0;
    return static_cast<unsigned>(values[val].params.size());
  }
  const value_t& GetValue(unsigned val) const { return values[val]; }
  std::string GetParam(unsigned val, unsigned par) const;

 private:
  void AddParam(const char* buf, int len);
  void AddValue();

  std::vector<value_t> values;
};

}  // namespace ddr

#endif
